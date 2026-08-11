| **English (en-US)** | [简体中文 (zh-CN)](./README.zh-CN.md) |
| --- | --- |

&nbsp;

# Implement Delay Hook

## What is "delay hook" and what benefits does it bring?

The usual way to hook a function in a DLL is to first load the corresponding DLL into the process address space and locate the function address (for example, by using [`LoadLibraryW`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw) + [`GetProcAddress`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress)).

For hooks designed for a specific program, the target function will usually be called sooner or later, and the DLL is also required by the process, so loading the corresponding DLL early is generally acceptable. But hooks injected into different processes (especially global hooks) do not know whether each process requires this DLL, so they usually still load the DLL into every process address space and hook the function, even if the process itself does not need that DLL.

Imagine a global hook with many dependencies that tries to hook functions in various system DLLs. It would bring all involved DLLs into every process for loading and initialization, which is extremely expensive.

"Delay hook" is a good solution to this problem: set the hook immediately if the target DLL is already loaded; otherwise, set the hook when the target DLL is loaded into the process.

## Technical solution

Obviously, the key to implementing "delay hook" is to receive DLL load notifications as early as possible. The "[DLL Load Notification](https://learn.microsoft.com/en-us/windows/win32/devnotes/dll-load-notification)" mechanism was introduced in NT6, and it is exactly what we need.

See the [LdrRegisterDllNotification](https://learn.microsoft.com/en-us/windows/win32/devnotes/ldrregisterdllnotification) function. DLL load (and unload) notifications are sent to the callback registered by this function. At that point, the memory mapped from the DLL is available, so we can set the hook.

Although Microsoft Learn notes that the related APIs may be changed or removed, their usage has not changed. Since NT6.1, only the held lock changed from `LdrpLoaderLock` to the dedicated `LdrpDllNotificationLock`. In any case, keep the callback as simple as possible.

> [!TIP]
> If you want to understand the internal implementation of "[DLL Load Notification](https://learn.microsoft.com/en-us/windows/win32/devnotes/dll-load-notification)" on Windows, see [ReactOS PR #6795](https://github.com/reactos/reactos/pull/6795), which I contributed to [ReactOS](https://github.com/reactos/reactos). Do not refer to [the WINE implementation](https://gitlab.winehq.org/wine/wine/-/commit/4c13e1765f559b322d8c071b2e23add914981db7), as it contains mistakes as of this writing. For example, its `LdrUnregisterDllNotification` removes the node without checking whether it is in the list.

[Demo: DelayHook](../../../Source/Demo/DelayHook.c) demonstrates how to use this mechanism to hook functions in a DLL while the DLL is being loaded.

<br>
<hr>

This work is licensed under [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](http://creativecommons.org/licenses/by-nc-sa/4.0/).  
<br>
**[Ratin](https://github.com/RatinCN) &lt;[ratin@knsoft.org](mailto:ratin@knsoft.org)&gt;**  
*China national certified senior system architect*  
*[ReactOS](https://github.com/reactos/reactos) contributor*
