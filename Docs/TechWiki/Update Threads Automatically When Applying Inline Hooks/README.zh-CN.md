| [English (en-US)](./README.md) | **简体中文 (zh-CN)** |
| --- | --- |

&nbsp;

# 应用内联钩子时自动更新线程

## 内联挂钩时更新线程的必要性

内联挂钩需要修改函数开头的指令实现跳转，为应对有线程正好运行在要修改的指令上的可能，需要更新处于此状态的线程避免其在修改指令时执行非法的新老共存的指令。

## 其它挂钩库中的实现

### Detours

[Detours](https://github.com/microsoft/Detours)提供了[`DetourUpdateThread`](https://github.com/microsoft/Detours/wiki/DetourUpdateThread)函数更新线程，但需要由调用方传入需要进行更新线程的句柄：
```C
LONG WINAPI DetourUpdateThread(_In_ HANDLE hThread);
```
也就是说，需要由调用方遍历进程中除自己以外的所有线程并传入给此函数，用起来比较复杂且不方便。

[Detours](https://github.com/microsoft/Detours)更新线程非常精细，它通过使用[`GetThreadContext`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadcontext)与[`SetThreadContext`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadcontext)准确地调整线程上下文中的PC（程序计数器）到正确位置，实现参考[Detours/src/detours.cpp于4b8c659f · microsoft/Detours](https://github.com/microsoft/Detours/blob/4b8c659f549b0ab21cf649377c7a84eb708f5e68/src/detours.cpp#L1840-L1906)。

但[Detours](https://github.com/microsoft/Detours)对线程的更新在x64下仍有遗漏的地方，参考我为此提交的[PR #344: Improve thread program counter adjustment](https://github.com/microsoft/Detours/pull/344)。

> [!TIP]
> 虽然它的官方示例“[Using Detours](https://github.com/microsoft/Detours/wiki/Using-Detours)”中有`DetourUpdateThread(GetCurrentThread())`这样的代码，但这用法无意义且无效，应使用其更新进程中除当前线程外的所有线程，详见[`DetourUpdateThread`](https://github.com/microsoft/Detours/wiki/DetourUpdateThread)。但即便以正确的方式更新线程，也会带来一个新的风险，见[🔗 技术Wiki：更新线程时避免堆死锁](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Avoid%20Deadlocking%20on%20The%20Heap%20When%20Updating%20Threads/README.zh-CN.md)。

### MinHook

[MinHook](https://github.com/TsudaKageyu/minhook)做得比较好，它在挂钩（和脱钩）时调用[CreateToolhelp32Snapshot](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot)获取其它线程并自动更新它们，然后像[Detours](https://github.com/microsoft/Detours)一样准确地更新线程上下文中的PC（程序计数器）。

### mhook

[mhook](https://github.com/martona/mhook)在挂钩（和脱钩）时调用[NtQuerySystemInformation](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation)获取其它线程并自动更新它们。但更新线程的方式相对笨拙，若线程正好位于要修改指令的区域则等待100毫秒，最多尝试3次，实现参考[mhook/mhook-lib/mhook.cpp于e58a58ca · martona/mhook](https://github.com/martona/mhook/blob/e58a58ca31dbe14f202b9b26315bff9f7a32598c/mhook-lib/mhook.cpp#L557-L631)：
```C
while (GetThreadContext(hThread, &ctx))
{
    ...
    if (nTries < 3)
    {
        // oops - we should try to get the instruction pointer out of here. 
        ODPRINTF((L"mhooks: SuspendOneThread: suspended thread %d - IP is at %p - IS COLLIDING WITH CODE", dwThreadId, pIp));
        ResumeThread(hThread);
        Sleep(100);
        SuspendThread(hThread);
        nTries++;
    }
    ...
}
```

## SlimDetours的实现

[SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours)获取其它线程的方式有两个，以NT5为目标时同样调用[NtQuerySystemInformation](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation)，而以NT6+为目标（默认）时则采用`NtGetNextThread`以大幅提升性能和正确性保障。

线程的更新沿袭了[Detours](https://github.com/microsoft/Detours)并进行了一些修正和改进。

要点：
1. 调用`NtGetNextThread`以遍历当前进程所有线程
2. 调用`NtSuspendThread`挂起除当前线程外的所有线程
3. 修改指令实现内联挂钩
4. 更新被成功挂起的线程
5. 调用`NtResumeThread`恢复挂起的线程

完整实现参考[KNSoft.SlimDetours/Source/KNSoft.SlimDetours/Thread.c于main · KNSoft/KNSoft.SlimDetours](../../../Source/KNSoft.SlimDetours/Thread.c)。

<br>
<hr>

本作品采用 [知识共享署名-非商业性使用-相同方式共享 4.0 国际许可协议 (CC BY-NC-SA 4.0)](http://creativecommons.org/licenses/by-nc-sa/4.0/) 进行许可。  
<br>
**[Ratin](https://github.com/RatinCN) &lt;[<ratin@knsoft.org>](mailto:ratin@knsoft.org)&gt;**  
*中国国家认证系统架构设计师*  
*[ReactOS](https://github.com/reactos/reactos)贡献者*
