| **English (en-US)** | [简体中文 (zh-CN)](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/README.zh-CN.md) |
| --- | --- |

<br>

# KNSoft.SlimDetours

[![NuGet Downloads](https://img.shields.io/nuget/dt/KNSoft.SlimDetours)](https://www.nuget.org/packages/KNSoft.SlimDetours) [![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/KNSoft/KNSoft.SlimDetours/msbuild.yml)](https://github.com/KNSoft/KNSoft.SlimDetours/actions/workflows/msbuild.yml) ![PR Welcome](https://img.shields.io/badge/PR-welcome-0688CB.svg) [![GitHub License](https://img.shields.io/github/license/KNSoft/KNSoft.SlimDetours)](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/LICENSE)

[SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) is an improved Windows API hooking library base on [Microsoft Detours](https://github.com/microsoft/Detours).

## Feature

Compared to the original [Detours](https://github.com/microsoft/Detours), the advantages are:

- New feature
  - **Support delay hook (set hooks automatically when target DLL loaded)** [🔗 TechWiki: Implement Delay Hook](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Implement%20Delay%20Hook/README.md)
  - **Automatically update threads when set hooks** [🔗 TechWiki: Update Threads Automatically When Applying Inline Hooks](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Update%20Threads%20Automatically%20When%20Applying%20Inline%20Hooks/README.md)
- Improved
  - **Avoid deadlocks when updating threads** [🔗 TechWiki: Avoid Deadlocking on The Heap When Updating Threads](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Avoid%20Deadlocking%20on%20The%20Heap%20When%20Updating%20Threads/README.md)
  - Avoid occupying system reserved memory region [🔗 TechWiki: Avoid Occupying System Reserved Region When Allocating Trampoline](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Avoid%20Occupying%20System%20Reserved%20Region%20When%20Allocating%20Trampoline/README.md)
  - Other bug fixes and code improvements
- Lite
  - **Depends on `Ntdll.dll` only**
  - Retain API hooking functions only
  - Remove support for ARM (ARM32), IA64, WinXP, GNUC
  - Smaller binary size
- Out-of-the-box
  - NuGet package is released

## Usage

[![NuGet Downloads](https://img.shields.io/nuget/dt/KNSoft.SlimDetours)](https://www.nuget.org/packages/KNSoft.SlimDetours)

### TL;DR

KNSoft.SlimDetours package contains both of [SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) and the original [Microsoft Detours](https://github.com/microsoft/Detours).

Include header [SlimDetours.h](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/SlimDetours/SlimDetours.h) for KNSoft.SlimDetours, or header [detours.h](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/Detours/src/detours.h) for the original [Microsoft Detours](https://github.com/microsoft/Detours), then link compiled library `KNSoft.SlimDetours.lib`.

NuGet package [KNSoft.SlimDetours](https://www.nuget.org/packages/KNSoft.SlimDetours) is out-of-the-box, install to project and the compiled library will be linked automatically.

```C
#include <KNSoft/SlimDetours/SlimDetours.h> // KNSoft.SlimDetours
#include <KNSoft/SlimDetours/detours.h>     // Microsoft Detours
```

If your project configuration name is neither "Release" nor "Debug", [MSBuild sheet](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/KNSoft.SlimDetours.targets) in NuGet package cannot link compiled library automatically, link manually is required, for example:
```C
#pragma comment(lib, "Debug/KNSoft.SlimDetours.lib")
```

Usage is similiar to the original [Microsoft Detours](https://github.com/microsoft/Detours), but:

- Function name begin with `"SlimDetours"`
- Most of return values are [`HRESULT`](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/0642cb2f-2075-4469-918c-4441e69c548a) that wraps [`NTSTATUS`](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/87fba13e-bf06-450e-83b1-9241dc81e781) by [`HRESULT_FROM_NT`](https://learn.microsoft.com/en-us/windows/win32/api/winerror/nf-winerror-hresult_from_nt) macro, use macros like [`SUCCEEDED`](https://learn.microsoft.com/en-us/windows/win32/api/winerror/nf-winerror-succeeded) to check them.
- [Threads are updated automatically](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Update%20Threads%20Automatically%20When%20Applying%20Inline%20Hooks/README.md), [`DetourUpdateThread`](https://github.com/microsoft/Detours/wiki/DetourUpdateThread) has been omitted.
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
If you feel the above Detours-style API calls complicated, [SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) provides some APIs in [Wrapper.c](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/SlimDetours/Wrapper.c), which can do the job with just one line, such as:
```C
SlimDetoursSetHook((PVOID*)&g_pfnXxx, Hooked_Xxx);
```

### Delay Hook

"Delay Hook" will set hooks automatically when target DLL loaded, supported on NT6+.

For example, call `SlimDetoursDelayAttach` to hook `a.dll!FuncXxx` automatically when `a.dll` loaded:
```C
SlimDetoursDelayAttach((PVOID*)&g_pfnFuncXxx,
                       Hooked_FuncXxx,
                       L"a.dll",
                       L"FuncXxx",
                       NULL,
                       NULL);
```
Demo: [DelayHook.c](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/Demo/DelayHook.c)

## Compatibility

Project building: support for the latest MSVC generation tools and SDKs is mainly considered, and it is generally more widely backward compatible. GCC compatible and can be built with [ReactOS](https://github.com/reactos/reactos).

Artifact integration: widely compatible with MSVC generation tools (support for VS2015 is known), and different compilation configurations (e.g., `/MD`, `/MT`).

Runtime environment: NT5 or above OS, x86/x64/ARM64 platform.

> [!CAUTION]
> In beta stage, should be used with caution.

## License

[![GitHub License](https://img.shields.io/github/license/KNSoft/KNSoft.SlimDetours)](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/LICENSE)

KNSoft.SlimDetours is licensed under the [MIT](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/LICENSE) license.

Source is based on [Microsoft Detours](https://github.com/microsoft/Detours) which is licensed under the [MIT](https://github.com/microsoft/Detours/blob/main/LICENSE) license.

Also uses [KNSoft.NDK](https://github.com/KNSoft/KNSoft.NDK) to access low-level Windows NT APIs and its Unit Test Framework.
