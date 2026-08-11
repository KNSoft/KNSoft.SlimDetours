| **English (en-US)** | [简体中文 (zh-CN)](./README.zh-CN.md) |
| --- | --- |

&nbsp;

# Avoid Occupying System Reserved Region When Allocating Trampoline

## Windows reserved region for system DLLs

When allocating trampolines, hooking libraries usually prefer to find available memory space near the hook target function. When hooking system APIs, this can easily occupy the area used by system DLLs, causing a system DLL that should have been loaded there to be loaded elsewhere and perform additional relocation. It can even cause a `STATUS_ILLEGAL_DLL_RELOCATION (0xC0000269)` exception (for example, when kernel32.dll or user32.dll is affected).

Windows has supported ASLR since NT6 and explicitly reserves a region for system DLLs in user mode. This lets the same system DLL be mapped at the same location in the reserved region across different processes, so relocation information can be reused after the first load and relocation does not need to run again on subsequent loads.

This mechanism is described in detail in the "Image randomization" section of Chapter 5, "Memory management", in "Windows Internals 7th Part 1", so it will not be repeated here. The exact reserved regions I obtained by referring to the book and analyzing `ntoskrnl.exe!MiInitializeRelocations` are:

32-bit process：[0x50000000 ... 0x78000000), a total of 640MB  
64-bit process：[0x00007FF7FFFF0000 ... 0x00007FFFFFFF0000), a total of 32GB

Even without ASLR, it is still worth reserving a certain amount of space from the top of the address space.

## Other hooking libraries' practices

As Microsoft's official hooking library, [Detours](https://github.com/microsoft/Detours) accounts for the fact that the system reserved region cannot be used for trampolines. It hardcodes the [0x70000000 ... 0x80000000] address range to avoid:
```C
//////////////////////////////////////////////////////////////////////////////
//
// Region reserved for system DLLs, which cannot be used for trampolines.
//
static PVOID    s_pSystemRegionLowerBound   = (PVOID)(ULONG_PTR)0x70000000;
static PVOID    s_pSystemRegionUpperBound   = (PVOID)(ULONG_PTR)0x80000000;
```

This range only applies to NT5, ntdll.dll, kernel32.dll, and user32.dll remain within this range in 64-bit NT5.

[jdu2600](https://github.com/jdu2600) is also aware of this issue and opened an unofficial PR, [microsoft/Detours PR #307](https://github.com/microsoft/Detours/pull/307), for [Detours](https://github.com/microsoft/Detours) to update this range for recent Windows versions.

[MinHook](https://github.com/TsudaKageyu/minhook) and [mhook](https://github.com/martona/mhook) are both well-known Windows API hooking libraries, but unfortunately they don't seem to take this issue into account.

## SlimDetours implementation

For 32-bit processes, ASLR reserves only a 640MB range, which can be avoided directly. For 64-bit processes, things are more complicated: ASLR reserves a 32GB range, which is too large to avoid completely. Considering the ASLR layout rules and the location requirements of trampolines, it makes sense to treat the 1GB range after `Ntdll.dll` as a reserved range to avoid. This consideration is consistent with the PR mentioned above. Note that this range may be split into two blocks, as in the following layout:

| Address | Load Order | System DLL Name | Size |
| :---: | :---: | :---: | :---: |
| （Top）<br>0x00007FFFFFFF0000<br>...<br>0x00007FFFFF9E0000 | #3 | B.dll | 6,208KB |
| 0x00007FFFFF9E0000<br>...<br>0x00007FFFFF820000 | #4 | C.dll | 1,792KB |
| ... | ... | ... |
| 0x00007FF800690000<br>...<br>0x00007FF800480000 | #1 | Ntdll.dll | 2,112KB |
| 0x00007FF800480000<br>...<br>0x00007FF7FFFF0000<br>（Bottom） | #2 | A.dll | 4,672KB |

`Ntdll.dll` is randomly loaded by ASLR to a lower memory address in the reserved range. When subsequent DLL placement reaches the bottom, it wraps to the top of the reserved range and continues there. In this case, the "1GB range after `Ntdll.dll`" is two discontinuous regions.

[SlimDetours](https://github.com/KNSoft/KNSoft.SlimDetours)' implementation details and avoidance range differ from the PR above, with more careful consideration for different NT versions. For example, in NT6.0 and NT6.1, ASLR can be disabled by the `MoveImages` value under the `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management` registry key. It also calls `NtQuerySystemInformation` to obtain a more accurate user address space range than a hardcoded value, which helps constrain trampoline placement. See [KNSoft.SlimDetours/Source/KNSoft.SlimDetours/Memory.c at main · KNSoft/KNSoft.SlimDetours](../../../Source/KNSoft.SlimDetours/Memory.c).

<br>
<hr>

This work is licensed under [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](http://creativecommons.org/licenses/by-nc-sa/4.0/).  
<br>
**[Ratin](https://github.com/RatinCN) &lt;[ratin@knsoft.org](mailto:ratin@knsoft.org)&gt;**  
*China national certified senior system architect*  
*[ReactOS](https://github.com/reactos/reactos) contributor*
