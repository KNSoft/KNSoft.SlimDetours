#pragma once

#include <SdkDdkVer.h>

#include "SlimDetours.NDK.inl"
#include "SlimDetours.h"

#include <suppress.h>

#if _DEBUG
#define DETOUR_TRACE DbgPrint
#define DETOUR_BREAK() __debugbreak()
#else
#define DETOUR_TRACE(Format, ...)
#define DETOUR_BREAK()
#endif

EXTERN_C_START

/* Basic structures */

typedef struct _DETOUR_ALIGN
{
#if defined(_M_ARM64) || defined(_M_ARM64EC)
    // ARM64 obTarget can reach 12 bytes, while obTrampoline can reach 72 bytes.
    BYTE obTarget;
    BYTE obTrampoline;
#else
    BYTE obTarget : 3;
    BYTE obTrampoline : 5;
#endif
} DETOUR_ALIGN, *PDETOUR_ALIGN;

#if defined(_M_ARM64) || defined(_M_ARM64EC)
_STATIC_ASSERT(sizeof(DETOUR_ALIGN) == 2);
#else
_STATIC_ASSERT(sizeof(DETOUR_ALIGN) == 1);
#endif

typedef struct _DETOUR_TRAMPOLINE
{
    // An X64 instruction can be 15 bytes long.
    // In practice 11 seems to be the limit.
    // 
    // An ARM64 instruction is 4 bytes long.
    //
    // The overwrite is always composed of 3 instructions (12 bytes) which perform an indirect jump
    // using _DETOUR_TRAMPOLINE::pbDetour as the address holding the target location.
    //
    // Copied instructions can expand.
    //
    // The scheme using MovImmediate can cause an instruction
    // to grow as much as 6 times.
    // That would be Bcc or Tbz with a large address space:
    //   4 instructions to form immediate
    //   inverted tbz/bcc
    //   br
    //
    // An expansion of 4 is not uncommon -- bl/blr and small address space:
    //   3 instructions to form immediate
    //   br or brl
    //
    // A theoretical maximum for rbCode is therefore 4*4*6 + 16 = 112 (another 16 for jmp to pbRemain).
    //
    // With literals, the maximum expansion is 5, including the literals: 4*4*5 + 16 = 96.
    //
    // The number is rounded up to 128. m_rbScratchDst should match this.
    //
#if defined(_M_ARM64EC)
    BYTE            rbCode[128];        // target code + jmp to pbRemain.
#elif defined(_M_IX86) || defined(_M_X64)
    BYTE            rbCode[30];         // target code + jmp to pbRemain.
#elif defined(_M_ARM64)
    BYTE            rbCode[128];        // target code + jmp to pbRemain.
#endif
    BYTE            cbCode;             // size of moved target code.
#if defined(_M_ARM64EC)
    BYTE            cbCodeBreak[3];     // padding to make debugging easier.
#elif defined(_M_IX86) || defined(_M_X64)
    BYTE            cbCodeBreak;        // padding to make debugging easier.
#elif defined(_M_ARM64)
    BYTE            cbCodeBreak[3];     // padding to make debugging easier.
#endif
#if defined(_M_ARM64EC)
    BYTE            rbRestore[30];      // original target code.
#elif defined(_M_IX86)
    BYTE            rbRestore[22];      // original target code.
#elif defined(_M_X64)
    BYTE            rbRestore[30];      // original target code.
#elif defined(_M_ARM64)
    BYTE            rbRestore[24];      // original target code.
#endif
    BYTE            cbRestore;          // size of original target code.
#if defined(_M_ARM64) || defined(_M_ARM64EC)
    BYTE            cbRestoreBreak[3];  // padding to make debugging easier.
#elif defined(_M_IX86) || defined(_M_X64)
    BYTE            cbRestoreBreak;     // padding to make debugging easier.
#endif
    DETOUR_ALIGN    rAlign[8];          // instruction alignment array.
    PBYTE           pbRemain;           // first instruction after moved code. [free list]
    PBYTE           pbDetour;           // first instruction of detour function.
#if defined(_M_IX86) || defined(_M_X64)
    BYTE            rbCodeIn[8];        // jmp [pbDetour]
#endif
} DETOUR_TRAMPOLINE, *PDETOUR_TRAMPOLINE;

#if defined(_M_ARM64EC)
_STATIC_ASSERT(sizeof(DETOUR_TRAMPOLINE) == 208);
#elif defined(_M_IX86)
_STATIC_ASSERT(sizeof(DETOUR_TRAMPOLINE) == 80);
#elif defined(_M_X64)
_STATIC_ASSERT(sizeof(DETOUR_TRAMPOLINE) == 96);
#elif defined(_M_ARM64)
_STATIC_ASSERT(sizeof(DETOUR_TRAMPOLINE) == 192);
#endif

typedef struct _DETOUR_OPERATION DETOUR_OPERATION, *PDETOUR_OPERATION;

struct _DETOUR_OPERATION
{
    PDETOUR_OPERATION pNext;
    BOOL fIsAdd : 1;
    BOOL fIsRemove : 1;
#if defined(_M_ARM64EC)
    BOOL fTargetArm64Ec : 1;
#endif
    PBYTE* ppbPointer;
    PBYTE pbTarget;
    PDETOUR_TRAMPOLINE pTrampoline;
    ULONG dwPerm;
};

/* Memory management */

VOID
detour_memory_init(VOID);

_Must_inspect_result_
_Ret_maybenull_
_Post_writable_byte_size_(Size)
PVOID
detour_memory_alloc(
    _In_ SIZE_T Size);

_Must_inspect_result_
_Ret_maybenull_
_Post_writable_byte_size_(Size)
PVOID
detour_memory_realloc(
    _Frees_ptr_opt_ PVOID BaseAddress,
    _In_ SIZE_T Size);

BOOL
detour_memory_free(
    _Frees_ptr_opt_ _Post_invalid_ PVOID BaseAddress);

BOOL
detour_memory_uninitialize(VOID);

BOOL
detour_memory_is_system_reserved(
    _In_ PVOID Address);

_Ret_notnull_
PVOID
detour_memory_2gb_below(
    _In_ PVOID Address);

_Ret_notnull_
PVOID
detour_memory_2gb_above(
    _In_ PVOID Address);

#if defined(_M_ARM64EC)

BOOL
detour_is_ec_code(
    _In_ PVOID Address);

#else

FORCEINLINE
BOOL
detour_is_ec_code(
    _In_ PVOID Address)
{
    return FALSE;
}

#endif

NTSTATUS
detour_alloc_region(
    _Inout_ PVOID* ppBaseAddress,
    _Inout_ PSIZE_T pRegionSize,
    _In_ BOOL fEcCode);

/* Instruction Utility */

enum
{
#if defined(_M_IX86) || defined(_M_X64)
    SIZE_OF_JMP = 5,
#if defined(_M_ARM64EC)
    SIZE_OF_JMP_ARM64 = 12
#endif
#elif defined(_M_ARM64)
    SIZE_OF_JMP = 12
#endif
};

#if defined(_M_IX86) || defined(_M_X64)

_Ret_notnull_
PBYTE
detour_gen_jmp_immediate(
    _In_ PBYTE pbCode,
    _In_ PBYTE pbJmpVal);

BOOL
detour_is_jmp_immediate_to(
    _In_ PBYTE pbCode,
    _In_ PBYTE pbJmpVal);

_Ret_notnull_
PBYTE
detour_gen_jmp_indirect(
    _In_ PBYTE pbCode,
    _In_ PBYTE* ppbJmpVal);

BOOL
detour_is_jmp_indirect_to(
    _In_ PBYTE pbCode,
    _In_ PBYTE* ppbJmpVal);

#endif

#if defined(_M_ARM64) || defined(_M_ARM64EC)

_Ret_notnull_
PBYTE
detour_gen_jmp_immediate_arm64(
    _In_ PBYTE pbCode,
    _In_opt_ PBYTE* ppPool,
    _In_ PBYTE pbJmpVal);

_Ret_notnull_
PBYTE
detour_gen_jmp_indirect_arm64(
    _In_ PBYTE pbCode,
    _In_ PULONG64 pbJmpVal);

BOOL
detour_is_jmp_indirect_to_arm64(
    _In_ PBYTE pbCode,
    _In_ PULONG64 pbJmpVal);

_Ret_notnull_
PBYTE
detour_gen_brk_arm64(
    _In_ PBYTE pbCode,
    _In_ PBYTE pbLimit);

_Ret_notnull_
PBYTE
detour_skip_jmp_arm64(
    _In_ PBYTE pbCode);

#if defined(_M_ARM64EC)

_Ret_notnull_
PBYTE
detour_skip_jmp_arm64ec(
    _In_ PBYTE pbCode,
    _Out_opt_ PBOOL pfArm64Ec);

#endif

VOID
detour_find_jmp_bounds_arm64(
    _In_ PBYTE pbCode,
    _Outptr_ PVOID* ppLower,
    _Outptr_ PVOID* ppUpper);

BOOL
detour_does_code_end_function_arm64(
    _In_ PBYTE pbCode);

ULONG
detour_is_code_filler_arm64(
    _In_ PBYTE pbCode);

PVOID
NTAPI
detour_copy_instruction_arm64(
    _In_opt_ PVOID pDst,
    _In_ PVOID pSrc,
    _Out_opt_ PVOID* ppTarget,
    _Out_opt_ LONG* plExtra);

#endif

#if defined(_M_IX86) || defined(_M_X64)

PVOID
NTAPI
detour_copy_instruction(
    _In_opt_ PVOID pDst,
    _In_ PVOID pSrc,
    _Out_opt_ PVOID* ppTarget,
    _Out_opt_ LONG* plExtra);

_Ret_notnull_
PBYTE
detour_gen_brk(
    _In_ PBYTE pbCode,
    _In_ PBYTE pbLimit);

_Ret_notnull_
PBYTE
detour_skip_jmp(
    _In_ PBYTE pbCode);

VOID
detour_find_jmp_bounds(
    _In_ PBYTE pbCode,
    _Outptr_ PVOID* ppLower,
    _Outptr_ PVOID* ppUpper);

BOOL
detour_does_code_end_function(
    _In_ PBYTE pbCode);

ULONG
detour_is_code_filler(
    _In_ PBYTE pbCode);

#endif

/* Thread management */

NTSTATUS
detour_thread_suspend(
    _Outptr_result_maybenull_ PHANDLE* SuspendedHandles,
    _Out_ PULONG SuspendedHandleCount);

VOID
detour_thread_resume(
    _In_reads_(SuspendedHandleCount) _Frees_ptr_ PHANDLE SuspendedHandles,
    _In_ ULONG SuspendedHandleCount);

NTSTATUS
detour_thread_update(
    _In_ HANDLE ThreadHandle,
    _In_ PDETOUR_OPERATION PendingOperations);

/* Trampoline management */

NTSTATUS
detour_writable_trampoline_regions(VOID);

VOID
detour_runnable_trampoline_regions(VOID);

_Ret_maybenull_
PDETOUR_TRAMPOLINE
detour_alloc_trampoline(
    _In_ PBYTE pbTarget,
    _In_ BOOL fEcCode);

VOID
detour_free_trampoline(
    _In_ PDETOUR_TRAMPOLINE pTrampoline);

VOID detour_free_unused_trampoline_regions(VOID);

VOID
detour_free_trampoline_region_if_unused(
    _In_ PDETOUR_TRAMPOLINE pTrampoline);

BYTE
detour_align_from_trampoline(
    _In_ PDETOUR_TRAMPOLINE pTrampoline,
    BYTE obTrampoline);

BYTE
detour_align_from_target(
    _In_ PDETOUR_TRAMPOLINE pTrampoline,
    BYTE obTarget);

EXTERN_C_END
