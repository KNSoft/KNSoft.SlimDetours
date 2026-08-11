| **English (en-US)** | [简体中文 (zh-CN)](./README.zh-CN.md) |
| --- | --- |


&nbsp;

# Avoid Heap Deadlocks When Updating Threads

## Why can Detours deadlock when updating threads?

The original [Detours](https://github.com/microsoft/Detours) uses the CRT heap (via `new/delete`). If thread updating suspends another thread that also uses this heap while it is holding the heap lock, [Detours](https://github.com/microsoft/Detours) will deadlock when it accesses the heap again.

[Raymond Chen](https://devblogs.microsoft.com/oldnewthing/author/oldnewthing) discusses the same CRT heap deadlock scenario caused by suspending threads in his [blog "The Old New Thing"](https://devblogs.microsoft.com/oldnewthing/) article ["Are there alternatives to _lock and _unlock in Visual Studio 2015?"](https://devblogs.microsoft.com/oldnewthing/20170125-00/?p=95255). The article also mentions [Detours](https://github.com/microsoft/Detours), so the original text is quoted here without further elaboration:
> Furthermore, you would be best served to take the heap lock (Heap­Lock) before suspending the thread, because the Detours library will allocate memory during thread suspension.

## Demo of Detours Deadlock

[SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) provides [Demo: DeadLock](../../../Source/Demo/DeadLock.c) to demonstrate how a deadlock occurs in [Detours](https://github.com/microsoft/Detours) and how [SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) resolves it.

One of the threads (`HeapUserThread`) keeps calling `malloc/free` (equivalent to `new/delete`):
```C
 while (!g_bStop)
 {
     p = malloc(4);
     if (p != NULL)
     {
         free(p);
     }
 }
```

Another thread (`SetHookThread`) repeatedly hooks and unhooks using [Detours](https://github.com/microsoft/Detours) or [SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours):
```C
while (!g_bStop)
{
    hr = HookTransactionBegin(g_eEngineType);
    if (FAILED(hr))
    {
        break;
    }
    if (g_eEngineType == EngineMicrosoftDetours)
    {
        hr = HRESULT_FROM_WIN32(DetourUpdateThread((HANDLE)lpThreadParameter));
        if (FAILED(hr))
        {
            break;
        }
    }
    hr = HookAttach(g_eEngineType, EnableHook, (PVOID*)&g_pfnEqualRect, Hooked_EqualRect);
    if (FAILED(hr))
    {
        HookTransactionAbort(g_eEngineType);
        break;
    }
    hr = HookTransactionCommit(g_eEngineType);
    if (FAILED(hr))
    {
        break;
    }

    EnableHook = !EnableHook;
}
```
> [!NOTE]
> [SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) updates threads automatically (see [🔗 TechWiki: Update Threads Automatically When Applying Inline Hooks](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Update%20Threads%20Automatically%20When%20Applying%20Inline%20Hooks/README.md)), so there is no such function as [`DetourUpdateThread`](https://github.com/microsoft/Detours/wiki/DetourUpdateThread).

Run these two threads at the same time for 10 seconds, then send a stop signal (`g_bStop = TRUE;`) and wait for another 10 seconds. If the wait times out, a deadlock has most likely occurred, a breakpoint will be triggered, and you can inspect the call stacks of these two threads in the debugger to confirm it. For example, if you run this demo with [Detours](https://github.com/microsoft/Detours) by specifying `"Demo.exe -Run DeadLock -Engine=MSDetours"`, the following call stacks show a heap deadlock:
```C
Worker Thread	Demo.exe!HeapUserThread	Demo.exe!heap_alloc_dbg_internal
    [External Code]
    Demo.exe!heap_alloc_dbg_internal(const unsigned __int64 size, const int block_use, const char * const file_name, const int line_number) Line 359
    Demo.exe!heap_alloc_dbg(const unsigned __int64 size, const int block_use, const char * const file_name, const int line_number) Line 450
    Demo.exe!_malloc_dbg(unsigned __int64 size, int block_use, const char * file_name, int line_number) Line 496
    Demo.exe!malloc(unsigned __int64 size) Line 27
    Demo.exe!HeapUserThread(void * lpThreadParameter) Line 29
    [External Code]

Worker Thread	Demo.exe!SetHookThread	Demo.exe!__acrt_lock
    [External Code]
    Demo.exe!__acrt_lock(__acrt_lock_id _Lock) Line 55
    Demo.exe!heap_alloc_dbg_internal(const unsigned __int64 size, const int block_use, const char * const file_name, const int line_number) Line 309
    Demo.exe!heap_alloc_dbg(const unsigned __int64 size, const int block_use, const char * const file_name, const int line_number) Line 450
    Demo.exe!_malloc_dbg(unsigned __int64 size, int block_use, const char * file_name, int line_number) Line 496
    Demo.exe!malloc(unsigned __int64 size) Line 27
    [External Code]
    Demo.exe!DetourDetach(void * * ppPointer, void * pDetour) Line 2392
    Demo.exe!HookAttach(_DEMO_ENGINE_TYPE EngineType, int Enable, void * * ppPointer, void * pDetour) Line 140
    Demo.exe!SetHookThread(void * lpThreadParameter) Line 65
    [External Code]
```
Running this demo with [SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) by specifying `"Demo.exe -Run DeadLock -Engine=SlimDetours"` will pass successfully.

## How did other hooking libraries avoid this problem?

[mhook](https://github.com/martona/mhook) uses [`Virtual­Alloc`](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc) to allocate memory pages instead of using [`Heap­Alloc`](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc) to allocate heap memory. This is one solution mentioned at the end of the article above.

Both [MinHook](https://github.com/TsudaKageyu/minhook) and [SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) create a private heap for internal use, which avoids this problem and saves memory:
```C
_detour_memory_heap = RtlCreateHeap(HEAP_NO_SERIALIZE | HEAP_GROWABLE, NULL, 0, 0, NULL, NULL);
```
> [!NOTE]
> [Detours](https://github.com/microsoft/Detours) already has a transaction mechanism, so this heap does not need serialized access.

[MinHook](https://github.com/TsudaKageyu/minhook) creates this heap in its initialization function `MH_Initialize`, while [SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) creates it via one-time initialization in the first memory allocation function that is called. Therefore, [SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) has no separate initialization function and does not need one.

<br>
<hr>

This work is licensed under [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](http://creativecommons.org/licenses/by-nc-sa/4.0/).  
<br>
**[Ratin](https://github.com/RatinCN) &lt;[ratin@knsoft.org](mailto:ratin@knsoft.org)&gt;**  
*China national certified senior system architect*  
*[ReactOS](https://github.com/reactos/reactos) contributor*
