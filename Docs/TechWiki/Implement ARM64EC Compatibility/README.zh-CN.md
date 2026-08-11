| [English (en-US)](./README.md) | **简体中文 (zh-CN)** |
| --- | --- |

&nbsp;

# 实现ARM64EC兼容性

## ARM64EC兼容问题与其它库的兼容性

[Windows on Arm (WoA)](https://learn.microsoft.com/en-us/windows/arm/overview) 通过 [ARM64EC ABI](https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi) 实现了x64指令与ARM64指令的互操作性，使x64与ARM64EC进程可以同时加载这两种二进制模块：x64模块指令在模拟环境中运行，ARM64EC模块则用ARM64指令原生运行。

ARM64EC 提供 [FFS](https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi#fast-forward-sequences) (快进序列，x64函数) 来兼容强依赖于体系结构的内联挂钩场景，但目标地址不经过 [FFS](https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi#fast-forward-sequences) 直接指向ARM64EC代码时（如一些系统ARM64EC模块提供的COM接口虚表项），目前常见挂钩库在x64/ARM64EC构建下仍将目标指令当作x64处理，写入x64跳转并会在执行时触发 `STATUS_ILLEGAL_INSTRUCTION` 异常，见 Detours Issue: [#292](https://github.com/microsoft/Detours/issues/292) [#355](https://github.com/microsoft/Detours/issues/355)。

## SlimDetours的实现

完整实现参考 [KNSoft.SlimDetours #30](https://github.com/KNSoft/KNSoft.SlimDetours/pull/30)，以下是其实现要点。

### 检测目标代码类型

[`RtlIsEcCode`](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtliseccode)用于检测目标代码是否为ARM64仿真兼容：
- 虽然官方文档写它由 `kernel32.lib` 导入，但它实际实现位于 `ntdll.dll` 中的同名导出函数。
- 虽然在Windows SDK中它的声明被 `#if defined(_M_ARM64EC)` 守卫，但x64构建下的二进制仍可调用它，ARM64与x64 Win11中 `ntdll.dll` 均导出了此函数。
- 它的内部读取了 `PEB::EcCodeBitMap`，实现可参考 [KNSoft.NDK: [NT:RTL] Implement `_Inline_RtlIsEcCode`](https://github.com/KNSoft/KNSoft.NDK/commit/d31d310347040e5f4e69a74e48b71f4be332c33b)。

### 处理两种指令集

为使ARM64EC构建同时处理x64与ARM64代码：
- 指令处理函数默认与编译目标平台匹配；处理ARM64指令的函数增加 `_arm64` 后缀，如`detour_skip_jmp_arm64`、`detour_copy_instruction_arm64`、`detour_gen_jmp_immediate_arm64`等，并在ARM64EC构建下参与编译。
- 增加 `detour_skip_jmp_arm64ec`，按目标代码类型选择 `detour_skip_jmp` 或`detour_skip_jmp_arm64`。
- `SlimDetoursCopyInstruction` 与 `SlimDetoursCodeFromPointer` 则在事务外自行检测代码类型。

### Trampoline的兼容

ARM64EC构建下，Trampoline要同时兼容x64和ARM64EC架构：
- ARM64EC Trampoline由`NtAllocateVirtualMemoryEx`带`MEM_EXTENDED_PARAMETER_EC_CODE`参数分配，x64 Trampoline沿用普通可执行内存。
- 目标补丁与Trampoline的跳回代码按代码类型生成：ARM64EC目标使用ARM64间接跳转，x64目标沿用x64跳转序列。

### x64构建为何无法为ARM64EC目标函数生成通用的转接桥？

x64 Hook模块挂钩ARM64EC目标时，调用链需要两次ABI转换：
- ARM64EC目标跳转到x64 Detour，需要Exit Thunk。
- x64 Detour通过Trampoline调用原函数，需要Entry Thunk。

[Entry/Exit Thunks](https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi#entry-and-exit-thunks)的参数与返回值转换取决于函数签名；x64编译器不会生成ARM64EC Thunk，我们也无法仅凭函数地址生成它们。

ARM64EC Hook模块挂钩x64目标时同样存在两次ABI转换，但签名来自Hook代码：编译器根据ARM64EC Detour的签名生成Entry Thunk，并在通过有类型的原函数指针调用x64 Trampoline时生成Exit Thunk。挂钩ARM64EC目标时，目标、Detour与Trampoline均使用ARM64EC ABI，无需转换。

故x64构建Hook前要检测原始和解析后的目标地址，遇到ARM64EC代码即返回`HRESULT_FROM_NT(STATUS_NOT_SUPPORTED)`。挂钩此类目标应使用ARM64EC构建的Hook模块。若Windows未来使这些目标入口指向 [FFS](https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi#fast-forward-sequences)，x64 Hook模块才能更好地兼容这类目标。

## 兼容性验证

以下组合均使用[COMHook](../../../Source/Demo/COMHook.c)测试逻辑在ARM64真机上挂钩`IStream::Read`。进程架构由EXE决定，Hook模块架构单独列出。

| 进程 | Hook模块 | 目标代码 | Microsoft Detours | SlimDetours |
| --- | --- | --- | --- | --- |
| x64 | x64 | ARM64EC | **崩溃**（`STATUS_ILLEGAL_INSTRUCTION`） | **报错**（`STATUS_NOT_SUPPORTED`） |
| x64 | ARM64EC | ARM64EC | **崩溃**（`STATUS_ILLEGAL_INSTRUCTION`） | 通过 |
| ARM64EC | x64 | ARM64EC | **崩溃**（`STATUS_ILLEGAL_INSTRUCTION`） | **报错**（`STATUS_NOT_SUPPORTED`） |
| ARM64EC | ARM64EC | ARM64EC | **崩溃**（`STATUS_ILLEGAL_INSTRUCTION`） | 通过 |

## 参考资料

- [Understanding Arm64EC ABI and assembly code](https://learn.microsoft.com/en-us/windows/arm/arm64ec-abi)

<br>
<hr>

本作品采用 [知识共享署名-非商业性使用-相同方式共享 4.0 国际许可协议 (CC BY-NC-SA 4.0)](http://creativecommons.org/licenses/by-nc-sa/4.0/) 进行许可。  
<br>
**[Ratin](https://github.com/RatinCN) &lt;[ratin@knsoft.org](mailto:ratin@knsoft.org)&gt;**  
*中国国家认证系统架构设计师*  
*[ReactOS](https://github.com/reactos/reactos)贡献者*
