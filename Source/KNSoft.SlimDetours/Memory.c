/*
 * KNSoft.SlimDetours (https://github.com/KNSoft/KNSoft.SlimDetours) Memory Management
 * Copyright (c) KNSoft.org (https://github.com/KNSoft). All rights reserved.
 * Licensed under the MIT license.
 */

#include "SlimDetours.inl"

/*
 * Region reserved for system DLLs
 *
 * System reserved a region to make each system DLL was relocated only once
 * when loaded into every process as far as possible. Avoid using this region for trampoline.
 *
 * The region is [0x50000000 ... 0x78000000) (640MB) on 32-bit Windows;
 * and [0x00007FF7FFFF0000 ... 0x00007FFFFFFF0000) (32GB) on 64-bit Windows, which is too large to avoid.
 * In this case, avoiding 1GB range starting at Ntdll.dll makes sense.
 * If ASLR is disabled on NT6.0 or NT6.1, we reserve the top 1GB or 640MB region.
 *
 * The original Microsoft Detours always assumes and reserves [0x70000000 ... 0x80000000] (256MB) for system DLLs,
 * this should be used on NT5.
 *
 * Use MI_ASLR_* provided by KNSoft.NDK instead of hard-coded.
 */

#define SYSTEM_RESERVED_REGION_HIGHEST ((ULONG_PTR)MI_ASLR_HIGHEST_SYSTEM_RANGE_ADDRESS)
#define SYSTEM_RESERVED_REGION_SIZE (MI_ASLR_BITMAP_SIZE * (ULONG_PTR)CHAR_BIT * MM_ALLOCATION_GRANULARITY)
#define SYSTEM_RESERVED_REGION_LOWEST (SYSTEM_RESERVED_REGION_HIGHEST - SYSTEM_RESERVED_REGION_SIZE + 1)

static ULONG_PTR s_ulSystemRegionLowUpperBound = 0;
static ULONG_PTR s_ulSystemRegionLowLowerBound = 0;

#if defined(_WIN64)
_STATIC_ASSERT(SYSTEM_RESERVED_REGION_HIGHEST + 1 == 0x00007FFFFFFF0000ULL);
_STATIC_ASSERT(SYSTEM_RESERVED_REGION_SIZE == _32GB);
_STATIC_ASSERT(SYSTEM_RESERVED_REGION_LOWEST == 0x00007FF7FFFF0000ULL);

static const UNICODE_STRING g_usNtdll = RTL_CONSTANT_STRING(L"ntdll.dll");
static ULONG_PTR s_ulSystemRegionHighLowerBound = MAXULONG_PTR;
#else
_STATIC_ASSERT(SYSTEM_RESERVED_REGION_HIGHEST + 1 == 0x78000000UL);
_STATIC_ASSERT(SYSTEM_RESERVED_REGION_SIZE == _640MB);
_STATIC_ASSERT(SYSTEM_RESERVED_REGION_LOWEST == 0x50000000UL);
#endif

static const UNICODE_STRING g_MmMoveImages = RTL_CONSTANT_STRING(L"MoveImages");
static const UNICODE_STRING g_MmKeyPath = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management");
static const OBJECT_ATTRIBUTES g_MmMoveImagesKey = RTL_CONSTANT_OBJECT_ATTRIBUTES(&g_MmKeyPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE);

/*
 * System memory information with defaults
 *
 * Result from NtQuerySystemInformation(SystemBasicInformation, ...) is better,
 * and those default values are enough to work properly.
 */
static SYSTEM_BASIC_INFORMATION g_sbi = {
    .PageSize = PAGE_SIZE,
    .AllocationGranularity = MM_ALLOCATION_GRANULARITY,
    .MinimumUserModeAddress = (ULONG_PTR)MM_LOWEST_USER_ADDRESS,
#if defined(_WIN64)
    .MaximumUserModeAddress = 0x00007FFFFFFEFFFF,
#else
    .MaximumUserModeAddress = 0x7FFEFFFF,
#endif
};

/*
 * ARM64EC Support
 */

#if defined(_M_ARM64EC)

static CONST ANSI_STRING g_asRtlIsEcCode = RTL_CONSTANT_STRING("RtlIsEcCode");
static typeof(&RtlIsEcCode) g_pfnRtlIsEcCode = NULL;

static CONST ANSI_STRING g_asNtAllocateVirtualMemoryEx = RTL_CONSTANT_STRING("NtAllocateVirtualMemoryEx");
static typeof(&NtAllocateVirtualMemoryEx) g_pfnNtAllocateVirtualMemoryEx = NULL;

#define DETOUR_MEMORY_HEAP_INITIALIZING ((HANDLE)(LONG_PTR)-1)
#define DETOUR_MEMORY_HEAP_UNINITIALIZING ((HANDLE)(LONG_PTR)-2)
static _Interlocked_operand_ HANDLE volatile _detour_memory_heap = NULL;

#else

static HANDLE _detour_memory_heap = NULL;

#endif

#if defined(_WIN64)
static
_Success_(return != NULL)
PVOID
detour_memory_get_ntdll(VOID)
{
    /* Get the first initialized entry */
    PLDR_DATA_TABLE_ENTRY Entry = CONTAINING_RECORD(NtCurrentPeb()->Ldr->InInitializationOrderModuleList.Flink,
                                                    LDR_DATA_TABLE_ENTRY,
                                                    InInitializationOrderLinks);

    /* May be replaced by honey pot by very few tamper security softwares */
    if (RtlEqualUnicodeString(&Entry->BaseDllName, (PUNICODE_STRING)&g_usNtdll, TRUE))
    {
        return Entry->DllBase;
    }

    /* Fallback to LdrGetDllHandleEx */
    PVOID NtdllBase;
    return NT_SUCCESS(LdrGetDllHandleEx(LDR_GET_DLL_HANDLE_EX_UNCHANGED_REFCOUNT,
                                        NULL,
                                        NULL,
                                        (PUNICODE_STRING)&g_usNtdll,
                                        &NtdllBase)) ? NtdllBase : NULL;
}
#endif

/*
 * For NT6.0 and NT6.1 only, ASLR can be turned off by MmMoveImages registry option,
 * this function returns FALSE if the option was set to 0 explicitly.
 */
static
BOOL
detour_memory_is_aslr_enabled(VOID)
{
    NTSTATUS Status;
    HANDLE hKey;
    ULONG Length;
    DEFINE_ANYSIZE_STRUCT(stRegValue, KEY_VALUE_PARTIAL_INFORMATION, UCHAR, sizeof(DWORD));

    Status = NtOpenKey(&hKey, KEY_QUERY_VALUE, (POBJECT_ATTRIBUTES)&g_MmMoveImagesKey);
    if (!NT_SUCCESS(Status))
    {
        return TRUE;
    }
    Status = NtQueryValueKey(hKey,
                             (PUNICODE_STRING)&g_MmMoveImages,
                             KeyValuePartialInformation,
                             &stRegValue,
                             sizeof(stRegValue),
                             &Length);
    NtClose(hKey);
    return (!NT_SUCCESS(Status) ||
            Length != sizeof(stRegValue) ||
            stRegValue.BaseType.Type != REG_DWORD ||
            *(PDWORD)stRegValue.BaseType.Data != 0);
}

// ARM64EC APIs can initialize memory outside a transaction, so gate concurrent initialization.
VOID
detour_memory_init(VOID)
{
    HANDLE Heap;

#if defined(_M_ARM64EC)
    while ((Heap = _InterlockedCompareExchangePointer(&_detour_memory_heap,
                                                      DETOUR_MEMORY_HEAP_INITIALIZING,
                                                      NULL)) != NULL)
    {
        while (Heap == DETOUR_MEMORY_HEAP_INITIALIZING || Heap == DETOUR_MEMORY_HEAP_UNINITIALIZING)
        {
            NtYieldExecution();
            Heap = _InterlockedCompareExchangePointer(&_detour_memory_heap, NULL, NULL);
        }

        if (Heap != NULL)
        {
            return;
        }
    }
#else
    if (_detour_memory_heap != NULL)
    {
        return;
    }
#endif

#if defined(_WIN64)
    PVOID NtdllBase = detour_memory_get_ntdll();
#endif

    /* Initialize memory management information */
    NtQuerySystemInformation(SystemBasicInformation, &g_sbi, sizeof(g_sbi), NULL);

    if (SharedUserData->NtMajorVersion >= 6)
    {
        if (SharedUserData->NtMajorVersion != 6 ||
            SharedUserData->NtMinorVersion > 1 ||
            detour_memory_is_aslr_enabled())
        {
#if defined(_WIN64)
            /* 1GB after Ntdll.dll */

            /*
             * Ntdll.dll is expected to be loaded in the system reserved region.
             * If that's not the case, e.g. due to future changes in Windows or
             * EDR tampering, we fall back to the non-ASLR reserved region.
             *
             * Currently, SYSTEM_RESERVED_REGION_HIGHEST is 0x00007FFFFFFEFFFFULL on 64-bit Windows,
             * which is the maximum user mode address, so here we compare with SYSTEM_RESERVED_REGION_LOWEST only.
             */
            if ((ULONG_PTR)NtdllBase >= SYSTEM_RESERVED_REGION_LOWEST)
            {
                /*
                 * Note: The Ntdll.dll region isn't excluded, but it's already
                 * occupied by ntdll, so that shouldn't be a problem.
                 */
                s_ulSystemRegionLowUpperBound = (ULONG_PTR)NtdllBase - 1;

                s_ulSystemRegionLowLowerBound = s_ulSystemRegionLowUpperBound - _1GB + 1;
                if (s_ulSystemRegionLowLowerBound < SYSTEM_RESERVED_REGION_LOWEST)
                {
                    s_ulSystemRegionHighLowerBound = s_ulSystemRegionLowLowerBound + SYSTEM_RESERVED_REGION_SIZE;
                    s_ulSystemRegionLowLowerBound = SYSTEM_RESERVED_REGION_LOWEST;
                }
            } else
            {
                /* Reserve a region in the top as a fallback */
                s_ulSystemRegionLowUpperBound = g_sbi.MaximumUserModeAddress;
                s_ulSystemRegionLowLowerBound = s_ulSystemRegionLowUpperBound - _1GB + 1;
            }
#else
            s_ulSystemRegionLowUpperBound = SYSTEM_RESERVED_REGION_HIGHEST;
            s_ulSystemRegionLowLowerBound = SYSTEM_RESERVED_REGION_LOWEST;
#endif
        } else
        {
            /* Reserve a region in the top */
            s_ulSystemRegionLowUpperBound = g_sbi.MaximumUserModeAddress;
            s_ulSystemRegionLowLowerBound = s_ulSystemRegionLowUpperBound -
#if defined(_WIN64)
                _1GB
#else
                _640MB
#endif
                + 1;
        }
#if defined(_M_ARM64EC)
        LdrGetProcedureAddress(NtdllBase,
                               (PANSI_STRING)&g_asRtlIsEcCode,
                               0,
                               (PVOID*)&g_pfnRtlIsEcCode);
        LdrGetProcedureAddress(NtdllBase,
                               (PANSI_STRING)&g_asNtAllocateVirtualMemoryEx,
                               0,
                               (PVOID*)&g_pfnNtAllocateVirtualMemoryEx);
#endif
    } else
    {
        /*
         * This is the original Detours behavior, for NT5 only.
         * ntdll.dll, kernel32.dll, user32.dll in this range even if on 64-bit process
         */
        s_ulSystemRegionLowUpperBound = 0x80000000;
        s_ulSystemRegionLowLowerBound = 0x70000000;
    }

    /* Initialize private heap */
    Heap = RtlCreateHeap(HEAP_NO_SERIALIZE | HEAP_GROWABLE, NULL, 0, 0, NULL, NULL);
    if (Heap == NULL)
    {
        DETOUR_TRACE("RtlCreateHeap failed, fallback to use process default heap\n");
        Heap = RtlProcessHeap();
    }
#if defined(_M_ARM64EC)
    _InterlockedExchangePointer(&_detour_memory_heap, Heap);
#else
    _detour_memory_heap = Heap;
#endif
}

_Must_inspect_result_
_Ret_maybenull_
_Post_writable_byte_size_(Size)
PVOID
detour_memory_alloc(
    _In_ SIZE_T Size)
{
    return RtlAllocateHeap(_detour_memory_heap, 0, Size);
}

_Must_inspect_result_
_Ret_maybenull_
_Post_writable_byte_size_(Size)
PVOID
detour_memory_realloc(
    _Frees_ptr_opt_ PVOID BaseAddress,
    _In_ SIZE_T Size)
{
    return RtlReAllocateHeap(_detour_memory_heap, 0, BaseAddress, Size);
}

BOOL
detour_memory_free(
    _Frees_ptr_opt_ _Post_invalid_ PVOID BaseAddress)
{
    return RtlFreeHeap(_detour_memory_heap, 0, BaseAddress);
}

BOOL
detour_memory_uninitialize(VOID)
{
#if defined(_M_ARM64EC)
    HANDLE Heap = _InterlockedExchangePointer(&_detour_memory_heap, DETOUR_MEMORY_HEAP_UNINITIALIZING);
    if (Heap == RtlProcessHeap())
    {
        Heap = NULL;
    } else if (Heap != NULL)
    {
        Heap = RtlDestroyHeap(Heap);
    }
    _InterlockedExchangePointer(&_detour_memory_heap, Heap);
    return Heap == NULL;
#else
    if (_detour_memory_heap != NULL && _detour_memory_heap != RtlProcessHeap())
    {
        _detour_memory_heap = RtlDestroyHeap(_detour_memory_heap);
        return _detour_memory_heap == NULL;
    }

    return TRUE;
#endif
}

BOOL
detour_memory_is_system_reserved(
    _In_ PVOID Address)
{
    return
        ((ULONG_PTR)Address >= s_ulSystemRegionLowLowerBound && (ULONG_PTR)Address <= s_ulSystemRegionLowUpperBound)
#if defined(_WIN64)
        || ((ULONG_PTR)Address >= s_ulSystemRegionHighLowerBound &&
            (ULONG_PTR)Address <= SYSTEM_RESERVED_REGION_HIGHEST)
#endif
        ;
}

_Ret_notnull_
PVOID
detour_memory_2gb_below(
    _In_ PVOID Address)
{
    return (ULONG_PTR)Address > g_sbi.MinimumUserModeAddress + _2GB ?
        (PBYTE)Address - (_2GB - _512KB) :
        (PVOID)(ULONG_PTR)(g_sbi.MinimumUserModeAddress + _512KB);
}

_Ret_notnull_
PVOID
detour_memory_2gb_above(
    _In_ PVOID Address)
{
    return (
#if !defined(_WIN64)
        g_sbi.MaximumUserModeAddress >= _2GB &&
#endif
        (ULONG_PTR)Address <= g_sbi.MaximumUserModeAddress - _2GB) ?
        (PBYTE)Address + (_2GB - _512KB) :
        (PVOID)(ULONG_PTR)(g_sbi.MaximumUserModeAddress - _512KB);
}

#if defined(_M_ARM64EC)

BOOL
detour_is_ec_code(
    _In_ PVOID Address)
{
    return g_pfnRtlIsEcCode != NULL && g_pfnRtlIsEcCode((DWORD64)Address);
}

#endif

NTSTATUS
detour_alloc_region(
    _Inout_ PVOID* ppBaseAddress,
    _Inout_ PSIZE_T pRegionSize,
    _In_ BOOL fEcCode)
{
#if defined(_M_ARM64EC)
    if (fEcCode)
    {
        MEM_EXTENDED_PARAMETER Parameter = { 0 };

        if (g_pfnNtAllocateVirtualMemoryEx == NULL)
        {
            return STATUS_NOT_SUPPORTED;
        }

        Parameter.Type = MemExtendedParameterAttributeFlags;
        Parameter.ULong64 = MEM_EXTENDED_PARAMETER_EC_CODE;
        return g_pfnNtAllocateVirtualMemoryEx(NtCurrentProcess(),
                                              ppBaseAddress,
                                              pRegionSize,
                                              MEM_COMMIT | MEM_RESERVE,
                                              PAGE_EXECUTE_READWRITE,
                                              &Parameter,
                                              1);
    }
#else
    UNREFERENCED_PARAMETER(fEcCode);
#endif

    return NtAllocateVirtualMemory(NtCurrentProcess(),
                                   ppBaseAddress,
                                   0,
                                   pRegionSize,
                                   MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
}
