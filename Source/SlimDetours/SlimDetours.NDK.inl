/*
 * Adapt to NDKs to access low-level Windows NT APIs
 * 
 * SlimDetours uses KNSoft.NDK by default, and also support other NDKs.
 * 
 * KNSoft.NDK
 *   Used when macro `_USE_KNSOFT_NDK` is defined, this is the default behavior on offical project.
 * 
 * ReactOS NDK
 *   Used when macro `__REACTOS__` is defined, can be built with ReactOS.
 * 
 * Other NDKs
 *   Include other NDKs (e.g. phnt) header before SlimDetours, they should provide what we need.
 */

#pragma once

#if defined(_USE_KNSOFT_NDK)

#include <KNSoft/NDK/NDK.h>

#elif defined(__REACTOS__)

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

#define NTOS_MODE_USER
#include <ndk/exfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/mmfuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>

#endif

/* Add KNSoft.NDK specific stuff */
#ifndef _USE_KNSOFT_NDK 

#define PAGE_SIZE 0x1000
#define MM_ALLOCATION_GRANULARITY 0x10000

#if defined(_X86_)
#define CONTEXT_PC Eip
#elif defined(_AMD64_)
#define CONTEXT_PC Rip
#elif defined(_ARM64_)
#define CONTEXT_PC Pc
#endif

#define Add2Ptr(P,I) ((PVOID)((PUCHAR)(P) + (I)))
#define PtrOffset(B,O) ((ULONG)((ULONG_PTR)(O) - (ULONG_PTR)(B)))

#define KB_TO_BYTES(x) ((x) * 1024UL)
#define MB_TO_KB(x) ((x) * 1024UL)
#define MB_TO_BYTES(x) (KB_TO_BYTES(MB_TO_KB(x)))
#define GB_TO_MB(x) ((x) * 1024UL)
#define GB_TO_BYTES(x) (MB_TO_BYTES(GB_TO_MB(x)))

#define MM_LOWEST_USER_ADDRESS ((PVOID)0x10000)

#if defined(_WIN64)

/* [0x00007FF7FFFF0000 ... 0x00007FFFFFFF0000], 32G */
#define MI_ASLR_BITMAP_SIZE 0x10000
#define MI_ASLR_HIGHEST_SYSTEM_RANGE_ADDRESS ((PVOID)0x00007FFFFFFF0000ULL)

#else

/* [0x50000000 ... 0x78000000], 640M */
#define MI_ASLR_BITMAP_SIZE 0x500
#define MI_ASLR_HIGHEST_SYSTEM_RANGE_ADDRESS ((PVOID)0x78000000UL)

#endif

#define NtCurrentProcessId() ((HANDLE)ReadTeb(ClientId.UniqueProcess))
#define NtCurrentThreadId() ((HANDLE)ReadTeb(ClientId.UniqueThread))
#define NtGetProcessHeap() (NtCurrentPeb()->ProcessHeap)
#define NtGetNtdllBase() (CONTAINING_RECORD(NtCurrentPeb()->Ldr->InInitializationOrderModuleList.Flink, LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks)->DllBase)

#endif
