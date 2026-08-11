| **English (en-US)** | [简体中文 (zh-CN)](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/README.zh-CN.md) |
| --- | --- |

&nbsp;

# KNSoft.SlimDetours

[![NuGet Downloads](https://img.shields.io/nuget/dt/KNSoft.SlimDetours)](https://www.nuget.org/packages/KNSoft.SlimDetours) [![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/KNSoft/KNSoft.SlimDetours/Build_Publish.yml)](https://github.com/KNSoft/KNSoft.SlimDetours/actions/workflows/Build_Publish.yml) ![PR Welcome](https://img.shields.io/badge/PR-welcome-0688CB.svg) [![GitHub License](https://img.shields.io/github/license/KNSoft/KNSoft.SlimDetours)](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/LICENSE)

[SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) is an improved Windows API hooking library based on [Microsoft Detours](https://github.com/microsoft/Detours).

Compared with the original [Detours](https://github.com/microsoft/Detours), it has the following advantages:

- Improved
  - **Automatically updates threads when applying hooks** [🔗 TechWiki: Update Threads Automatically When Applying Inline Hooks](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Update%20Threads%20Automatically%20When%20Applying%20Inline%20Hooks/README.md)
  - **Avoid heap deadlocks when updating threads** [🔗 TechWiki: Avoid Heap Deadlocks When Updating Threads](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Avoid%20Heap%20Deadlocks%20When%20Updating%20Threads/README.md)
  - **ARM64EC compatibility** [🔗 TechWiki: Implement ARM64EC Compatibility](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Implement%20ARM64EC%20Compatibility/README.md)
  - Avoid occupying system-reserved memory regions [🔗 TechWiki: Avoid Occupying System Reserved Region When Allocating Trampoline](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Docs/TechWiki/Avoid%20Occupying%20System%20Reserved%20Region%20When%20Allocating%20Trampoline/README.md)
  - Other bug fixes and code improvements
- Lite
  - **Depends on `Ntdll.dll` only**
  - Retains only API hooking functions
  - Removes support for ARM (ARM32) and IA64
  - Smaller binary size

See also the [Todo List](https://github.com/KNSoft/KNSoft.SlimDetours/milestones?with_issues=no).

## Projects using SlimDetours

- [ExplorerPatcher](https://github.com/valinet/ExplorerPatcher)
- [Windhawk](https://github.com/ramensoftware/windhawk)
- ...

## Usage

[![NuGet Downloads](https://img.shields.io/nuget/dt/KNSoft.SlimDetours)](https://www.nuget.org/packages/KNSoft.SlimDetours)

### TL;DR

The [KNSoft.SlimDetours package](https://www.nuget.org/packages/KNSoft.SlimDetours) is ready to use out of the box. It contains both [KNSoft.SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours) and the latest [Microsoft Detours](https://github.com/microsoft/Detours), and includes the corresponding headers ([SlimDetours.h](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/KNSoft.SlimDetours/SlimDetours.h) or [detours.h](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/Microsoft.Detours/src/detours.h)) and compiled static libraries.

```C
/* KNSoft.SlimDetours */
#include <KNSoft/SlimDetours/SlimDetours.h>
#pragma comment(lib, "KNSoft.SlimDetours.lib")

/* Microsoft Detours */
#include <KNSoft/SlimDetours/detours.h>
#pragma comment(lib, "Microsoft.Detours.lib")
```

If your project configuration name contains neither "Release" nor "Debug", the [MSBuild property sheet](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/KNSoft.SlimDetours.targets) in the NuGet package cannot automatically determine which final library-path directory ("Release" or "Debug") should be used, so you need to add it manually. For example:
```C
#if DBG
#pragma comment(lib, "Debug/KNSoft.SlimDetours.lib")
#else
#pragma comment(lib, "Release/KNSoft.SlimDetours.lib")
#endif
```

The usage has been simplified, e.g. the hook only needs one line:
```C
HRESULT hr = SlimDetoursInlineHook(TRUE, (PVOID*)&g_pfnXxx, Hooked_Xxx);  // Hook
...
hr = SlimDetoursInlineHook(FALSE, (PVOID*)&g_pfnXxx, Hooked_Xxx); // Unhook
```
For a more simplified API, see [InlineHook.c](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/KNSoft.SlimDetours/InlineHook.c).

### Details

The original [Microsoft Detours](https://github.com/microsoft/Detours)-style functions are also retained, but with a few differences:

- Function names begin with `"SlimDetours"`
- Most return values are [`HRESULT`](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/0642cb2f-2075-4469-918c-4441e69c548a) values that wrap [`NTSTATUS`](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/87fba13e-bf06-450e-83b1-9241dc81e781) through the [`HRESULT_FROM_NT`](https://learn.microsoft.com/en-us/windows/win32/api/winerror/nf-winerror-hresult_from_nt) macro; use macros such as [`SUCCEEDED`](https://learn.microsoft.com/en-us/windows/win32/api/winerror/nf-winerror-succeeded) to check them.
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

## Compatibility

Project building: support for the latest MSVC build tools and SDKs is the main focus. The code in this project remains backward-compatible with MSVC build tools and GCC, but actual compatibility also depends on the NDK it uses; see [SlimDetours.NDK.inl](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/Source/KNSoft.SlimDetours/SlimDetours.NDK.inl). It can also be built together with [ReactOS](https://github.com/reactos/reactos). The default minimum target platform is NT6; specify the `_WIN32_WINNT` macro at compile time to build binaries that target lower NT versions.

Artifact integration: broadly compatible with MSVC build tools (VS2015 is known to be supported) and different compilation configurations (e.g., `/MD`, `/MT`).

Runtime environment: NT5 or later, x86/x64/ARM64/ARM64EC target platforms.

> [!CAUTION]
> This project is in beta and should be used with caution. Some APIs may change frequently; please keep an eye on the release notes.

## License

[![GitHub License](https://img.shields.io/github/license/KNSoft/KNSoft.SlimDetours)](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/LICENSE)

KNSoft.SlimDetours is licensed under the [MIT](https://github.com/KNSoft/KNSoft.SlimDetours/blob/main/LICENSE) license.

Source is based on [Microsoft Detours](https://github.com/microsoft/Detours) which is licensed under the [MIT](https://github.com/microsoft/Detours/blob/main/LICENSE) license.

It also uses [KNSoft.NDK](https://github.com/KNSoft/KNSoft.NDK) to access low-level Windows NT APIs and its unit test framework.
