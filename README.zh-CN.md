| [English (en-US)](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/README.md) | **简体中文 (zh-CN)** |
| --- | --- |

&nbsp;

# KNSoft.SlimDetours

[![NuGet Downloads](https://img.shields.io/nuget/dt/KNSoft.SlimDetours)](https://www.nuget.org/packages/KNSoft.SlimDetours) [![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/KNSoft/KNSoft.SlimDetours/msbuild.yml)](https://github.com/KNSoft/KNSoft.SlimDetours/actions/workflows/msbuild.yml) ![PR Welcome](https://img.shields.io/badge/PR-welcome-0688CB.svg) [![GitHub License](https://img.shields.io/github/license/KNSoft/KNSoft.SlimDetours)](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/LICENSE)

[SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours)是一个基于[Microsoft Detours](https://github.com/microsoft/Detours)改进而来的Windows API挂钩库。

相比于原版[Detours](https://github.com/microsoft/Detours)，有以下优势：

- 经改进
  - **挂钩时自动更新线程** [🔗 技术Wiki：应用内联钩子时自动更新线程](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Update%20Threads%20Automatically%20When%20Applying%20Inline%20Hooks/README.zh-CN.md)
  - **更新线程时避免堆死锁** [🔗 技术Wiki：更新线程时避免堆死锁](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Avoid%20Deadlocking%20on%20The%20Heap%20When%20Updating%20Threads/README.zh-CN.md)
  - 避免占用系统保留的内存区域 [🔗 技术Wiki：分配Trampoline时避免占用系统保留区域](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Avoid%20Occupying%20System%20Reserved%20Region%20When%20Allocating%20Trampoline/README.zh-CN.md)
  - 其它Bug修复与代码改进
- 轻量
  - **仅依赖`Ntdll.dll`**
  - 仅保留API挂钩函数
  - 移除对ARM (ARM32)、IA64的支持
  - 更小的二进制体积

  以及此处的[待办列表](https://github.com/KNSoft/KNSoft.SlimDetours/milestones?with_issues=no)。

## 用法

[![NuGet Downloads](https://img.shields.io/nuget/dt/KNSoft.SlimDetours)](https://www.nuget.org/packages/KNSoft.SlimDetours)

### 提要

[KNSoft.SlimDetours NuGet包](https://www.nuget.org/packages/KNSoft.SlimDetours)是开箱即用的，同时含有[SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours)与最新的[Microsoft Detours](https://github.com/microsoft/Detours)，包含对应的头文件（[SlimDetours.h](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/KNSoft.SlimDetours/SlimDetours.h) or [detours.h](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/Microsoft.Detours/src/detours.h)）和编译出的静态库以使用它们。

```C
/* KNSoft.SlimDetours */
#include <KNSoft/SlimDetours/SlimDetours.h>
#pragma comment(lib, "KNSoft.SlimDetours.lib")

/* Microsoft Detours */
#include <KNSoft/SlimDetours/detours.h>
#pragma comment(lib, "Microsoft.Detours.lib")
```

如果你项目的配置名称既不包含“Release”也不包含“Debug”，NuGet包中的[MSBuild表单](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/KNSoft.SlimDetours.targets)无法自动确定库路径的最后一层目录名（"Release"或"Debug"），需要手动加上它，例如：
```C
#if DBG
#pragma comment(lib, "Debug/KNSoft.SlimDetours.lib")
#else
#pragma comment(lib, "Release/KNSoft.SlimDetours.lib")
#endif
```

用法已进行了简化，例如挂钩仅需一行：
```C
SlimDetoursInlineHook(TRUE, (PVOID*)&g_pfnXxx, Hooked_Xxx);  // 挂钩
...
SlimDetoursInlineHook(FALSE, (PVOID*)&g_pfnXxx, Hooked_Xxx); // 脱钩
```
更多简化的API参考[InlineHook.c](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/KNSoft.SlimDetours/InlineHook.c)。

### 详细说明

原版[Microsoft Detours](https://github.com/microsoft/Detours)风格的函数也有保留，但有少许不同：

- 函数名以`"SlimDetours"`开头
- 大多数返回值是用[`HRESULT_FROM_NT`](https://learn.microsoft.com/en-us/windows/win32/api/winerror/nf-winerror-hresult_from_nt)宏包装[`NTSTATUS`](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/87fba13e-bf06-450e-83b1-9241dc81e781)而来的[`HRESULT`](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/0642cb2f-2075-4469-918c-4441e69c548a)，使用类似[`SUCCEEDED`](https://learn.microsoft.com/en-us/windows/win32/api/winerror/nf-winerror-succeeded) 的宏检查它们。
- [线程会被自动更新](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Update%20Threads%20Automatically%20When%20Applying%20Inline%20Hooks/README.zh-CN.md)，[`DetourUpdateThread`](https://github.com/microsoft/Detours/wiki/DetourUpdateThread)已被省去。
```C
hr = SlimDetoursTransactionBegin();
if (FAILED(hr))
{
    return hr;
}
hr = SlimDetoursAttach((PVOID*)&g_pfnXxx, Hooked_Xxx);
if (FAILED(hr))
{
    SlimDetoursTransactionAbort();
    return hr;
}
return SlimDetoursTransactionCommit();
```

## 兼容性

项目构建：主要考虑对最新MSVC生成工具和SDK的支持。本项目代码能向下兼容MSVC生成工具与GCC，但具体还要看其依赖的NDK，参考[SlimDetours.NDK.inl](./Source/KNSoft.SlimDetours/SlimDetours.NDK.inl)。支持随[ReactOS](https://github.com/reactos/reactos)一同构建。SlimDetours默认最低目标平台为NT6，编译时指定`_WIN32_WINNT`宏可以构建支持更低NT版本的二进制。

制品集成：广泛地兼容MSVC生成工具（已知支持VS2015），以及不同编译配置（如`/MD`、`/MT`）。

运行环境：NT5及以上操作系统，x86/x64/ARM64/ARM64EC目标平台。

> [!CAUTION]
> 处于beta阶段，应慎重使用。有些API可能频繁调整，请留意发行说明。

## 协议

[![GitHub License](https://img.shields.io/github/license/KNSoft/KNSoft.SlimDetours)](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/LICENSE)

KNSoft.SlimDetours根据[MIT](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/LICENSE)协议进行许可。

源码基于[Microsoft Detours](https://github.com/microsoft/Detours)，其根据[MIT](https://github.com/microsoft/Detours/blob/main/LICENSE)协议进行许可。

同时使用了[KNSoft.NDK](https://github.com/KNSoft/KNSoft.NDK)以访问底层Windows NT API及其中的单元测试框架。
