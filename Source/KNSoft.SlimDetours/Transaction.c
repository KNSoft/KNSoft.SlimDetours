/*
 * KNSoft.SlimDetours (https://github.com/KNSoft/KNSoft.SlimDetours) Transaction APIs
 * Copyright (c) KNSoft.org (https://github.com/KNSoft). All rights reserved.
 * Licensed under the MIT license.
 *
 * Source base on Microsoft Detours:
 *
 * Microsoft Research Detours Package, Version 4.0.1
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT license.
 */

#include "SlimDetours.inl"

static _Interlocked_operand_ HANDLE volatile s_nPendingThreadId = NULL; // Thread owning pending transaction.
static PHANDLE s_phSuspendedThreads = NULL;
static ULONG s_ulSuspendedThreadCount = 0;
static PDETOUR_OPERATION s_pPendingOperations = NULL;

static
PVOID
detour_copy_target_instruction(
    _In_opt_ PVOID pDst,
    _In_ PVOID pSrc,
    _Out_opt_ PVOID* ppTarget,
    _Out_opt_ LONG* plExtra,
    _In_ BOOL fTargetArm64Ec)
{
#if defined(_M_ARM64EC)
    return fTargetArm64Ec ?
        detour_copy_instruction_arm64(pDst, pSrc, ppTarget, plExtra) :
        detour_copy_instruction(pDst, pSrc, ppTarget, plExtra);
#elif defined(_M_ARM64)
    UNREFERENCED_PARAMETER(fTargetArm64Ec);
    return detour_copy_instruction_arm64(pDst, pSrc, ppTarget, plExtra);
#else
    UNREFERENCED_PARAMETER(fTargetArm64Ec);
    return detour_copy_instruction(pDst, pSrc, ppTarget, plExtra);
#endif
}

static
BOOL
detour_does_target_code_end_function(
    _In_ PBYTE pbCode,
    _In_ BOOL fTargetArm64Ec)
{
#if defined(_M_ARM64EC)
    return fTargetArm64Ec ? detour_does_code_end_function_arm64(pbCode) : detour_does_code_end_function(pbCode);
#elif defined(_M_ARM64)
    UNREFERENCED_PARAMETER(fTargetArm64Ec);
    return detour_does_code_end_function_arm64(pbCode);
#else
    UNREFERENCED_PARAMETER(fTargetArm64Ec);
    return detour_does_code_end_function(pbCode);
#endif
}

static
ULONG
detour_is_target_code_filler(
    _In_ PBYTE pbCode,
    _In_ BOOL fTargetArm64Ec)
{
#if defined(_M_ARM64EC)
    return fTargetArm64Ec ? detour_is_code_filler_arm64(pbCode) : detour_is_code_filler(pbCode);
#elif defined(_M_ARM64)
    UNREFERENCED_PARAMETER(fTargetArm64Ec);
    return detour_is_code_filler_arm64(pbCode);
#else
    UNREFERENCED_PARAMETER(fTargetArm64Ec);
    return detour_is_code_filler(pbCode);
#endif
}

HRESULT
NTAPI
SlimDetoursTransactionBeginEx(
    _In_ PCDETOUR_TRANSACTION_OPTIONS pOptions)
{
    NTSTATUS Status;

    // Make sure only one thread can start a transaction.
    if (_InterlockedCompareExchangePointer(&s_nPendingThreadId, NtCurrentThreadId(), NULL) != NULL)
    {
        return HRESULT_FROM_NT(STATUS_TRANSACTIONAL_CONFLICT);
    }

    // Initialize memory management
    detour_memory_init();

    // Make sure the trampoline pages are writable.
    Status = detour_writable_trampoline_regions();
    if (!NT_SUCCESS(Status))
    {
        goto fail;
    }

    if (pOptions->fSuspendThreads)
    {
        Status = detour_thread_suspend(&s_phSuspendedThreads, &s_ulSuspendedThreadCount);
        if (!NT_SUCCESS(Status))
        {
            detour_runnable_trampoline_regions();
            goto fail;
        }
    } else
    {
        s_phSuspendedThreads = NULL;
        s_ulSuspendedThreadCount = 0;
    }

    s_pPendingOperations = NULL;
    return HRESULT_FROM_NT(STATUS_SUCCESS);

fail:
#ifdef _MSC_VER
#pragma warning(disable: __WARNING_INTERLOCKED_ACCESS)
#endif
    s_nPendingThreadId = NULL;
#ifdef _MSC_VER
#pragma warning(default: __WARNING_INTERLOCKED_ACCESS)
#endif
    return HRESULT_FROM_NT(Status);
}

HRESULT
NTAPI
SlimDetoursTransactionAbort(VOID)
{
    PVOID pMem;
    SIZE_T sMem;
    DWORD dwOld;
    BOOL freed = FALSE;

    if (s_nPendingThreadId != NtCurrentThreadId())
    {
        return HRESULT_FROM_NT(STATUS_TRANSACTIONAL_CONFLICT);
    }

    // Restore all of the page permissions.
    for (PDETOUR_OPERATION o = s_pPendingOperations; o != NULL;)
    {
        // We don't care if this fails, because the code is still accessible.
        pMem = o->pbTarget;
        sMem = o->pTrampoline->cbRestore;
        NtProtectVirtualMemory(NtCurrentProcess(), &pMem, &sMem, o->dwPerm, &dwOld);
        if (o->fIsAdd)
        {
            detour_free_trampoline(o->pTrampoline);
            o->pTrampoline = NULL;
            freed = TRUE;
        }

        PDETOUR_OPERATION n = o->pNext;
        detour_memory_free(o);
        o = n;
    }
    s_pPendingOperations = NULL;
    if (freed)
    {
        detour_free_unused_trampoline_regions();
    }

    // Make sure the trampoline pages are no longer writable.
    detour_runnable_trampoline_regions();

    // Resume any suspended threads.
    if (s_phSuspendedThreads != NULL)
    {
        detour_thread_resume(s_phSuspendedThreads, s_ulSuspendedThreadCount);
        s_phSuspendedThreads = NULL;
        s_ulSuspendedThreadCount = 0;
    }

    s_nPendingThreadId = NULL;
    return HRESULT_FROM_NT(STATUS_SUCCESS);
}

HRESULT
NTAPI
SlimDetoursTransactionCommit(VOID)
{
    PVOID pMem;
    SIZE_T sMem;
    DWORD dwOld;

    // Common variables.
    PDETOUR_OPERATION o, n;
    PBYTE pbCode;
    BOOL freed = FALSE;
    ULONG i;

    if (s_nPendingThreadId != NtCurrentThreadId())
    {
        return HRESULT_FROM_NT(STATUS_TRANSACTIONAL_CONFLICT);
    }

    if (s_pPendingOperations == NULL)
    {
        goto _exit;
    }

    // Insert or remove each of the detours.
    o = s_pPendingOperations;
    do
    {
        if (o->fIsRemove)
        {
            // Check if the jmps still points where we expect, otherwise someone might have hooked us.
            BOOL hookIsStillThere;
#if defined(_M_ARM64EC)
            if (o->fTargetArm64Ec)
            {
                hookIsStillThere = detour_is_jmp_indirect_to_arm64(o->pbTarget, (ULONG64*)&(o->pTrampoline->pbDetour));
            } else
            {
                hookIsStillThere =
                    detour_is_jmp_immediate_to(o->pbTarget, o->pTrampoline->rbCodeIn) &&
                    detour_is_jmp_indirect_to(o->pTrampoline->rbCodeIn, &o->pTrampoline->pbDetour);
            }
#elif defined(_M_IX86) || defined(_M_X64)
            hookIsStillThere =
                detour_is_jmp_immediate_to(o->pbTarget, o->pTrampoline->rbCodeIn) &&
                detour_is_jmp_indirect_to(o->pTrampoline->rbCodeIn, &o->pTrampoline->pbDetour);
#elif defined(_M_ARM64)
            hookIsStillThere =
                detour_is_jmp_indirect_to_arm64(o->pbTarget, (ULONG64*)&(o->pTrampoline->pbDetour));
#endif

            if (hookIsStillThere)
            {
                RtlCopyMemory(o->pbTarget, o->pTrampoline->rbRestore, o->pTrampoline->cbRestore);
                NtFlushInstructionCache(NtCurrentProcess(), o->pbTarget, o->pTrampoline->cbRestore);
            } else
            {
                // Don't remove in this case, put in bypass mode and leak trampoline.
                o->fIsRemove = FALSE;
                o->pTrampoline->pbDetour = o->pTrampoline->rbCode;
                DETOUR_TRACE("detours: Leaked hook on pbTarget=%p due to external hooking\n", o->pbTarget);
            }

            *o->ppbPointer = o->pbTarget;
        } else if (o->fIsAdd)
        {
            DETOUR_TRACE("detours: pbTramp =%p, pbRemain=%p, pbDetour=%p, cbRestore=%u\n",
                         o->pTrampoline,
                         o->pTrampoline->pbRemain,
                         o->pTrampoline->pbDetour,
                         o->pTrampoline->cbRestore);

            DETOUR_TRACE("detours: pbTarget=%p: "
                         "%02x %02x %02x %02x "
                         "%02x %02x %02x %02x "
                         "%02x %02x %02x %02x [before]\n",
                         o->pbTarget,
                         o->pbTarget[0], o->pbTarget[1], o->pbTarget[2], o->pbTarget[3],
                         o->pbTarget[4], o->pbTarget[5], o->pbTarget[6], o->pbTarget[7],
                         o->pbTarget[8], o->pbTarget[9], o->pbTarget[10], o->pbTarget[11]);

#if defined(_M_ARM64EC)
            if (o->fTargetArm64Ec)
            {
                pbCode = detour_gen_jmp_indirect_arm64(o->pbTarget, (ULONG64*)&(o->pTrampoline->pbDetour));
                pbCode = detour_gen_brk_arm64(pbCode, o->pTrampoline->pbRemain);
            } else
            {
                pbCode = detour_gen_jmp_indirect(o->pTrampoline->rbCodeIn, &o->pTrampoline->pbDetour);
                NtFlushInstructionCache(NtCurrentProcess(),
                                        o->pTrampoline->rbCodeIn,
                                        pbCode - o->pTrampoline->rbCodeIn);
                pbCode = detour_gen_jmp_immediate(o->pbTarget, o->pTrampoline->rbCodeIn);
                pbCode = detour_gen_brk(pbCode, o->pTrampoline->pbRemain);
            }
#elif defined(_M_IX86) || defined(_M_X64)
            pbCode = detour_gen_jmp_indirect(o->pTrampoline->rbCodeIn, &o->pTrampoline->pbDetour);
            NtFlushInstructionCache(NtCurrentProcess(), o->pTrampoline->rbCodeIn, pbCode - o->pTrampoline->rbCodeIn);
            pbCode = detour_gen_jmp_immediate(o->pbTarget, o->pTrampoline->rbCodeIn);
            pbCode = detour_gen_brk(pbCode, o->pTrampoline->pbRemain);
#elif defined(_M_ARM64)
            pbCode = detour_gen_jmp_indirect_arm64(o->pbTarget, (ULONG64*)&(o->pTrampoline->pbDetour));
            pbCode = detour_gen_brk_arm64(pbCode, o->pTrampoline->pbRemain);
#endif
            NtFlushInstructionCache(NtCurrentProcess(), o->pbTarget, pbCode - o->pbTarget);
            *o->ppbPointer = o->pTrampoline->rbCode;
            UNREFERENCED_PARAMETER(pbCode);

            DETOUR_TRACE("detours: pbTarget=%p: "
                         "%02x %02x %02x %02x "
                         "%02x %02x %02x %02x "
                         "%02x %02x %02x %02x [after]\n",
                         o->pbTarget,
                         o->pbTarget[0], o->pbTarget[1], o->pbTarget[2], o->pbTarget[3],
                         o->pbTarget[4], o->pbTarget[5], o->pbTarget[6], o->pbTarget[7],
                         o->pbTarget[8], o->pbTarget[9], o->pbTarget[10], o->pbTarget[11]);

            DETOUR_TRACE("detours: pbTramp =%p: "
                         "%02x %02x %02x %02x "
                         "%02x %02x %02x %02x "
                         "%02x %02x %02x %02x\n",
                         o->pTrampoline,
                         o->pTrampoline->rbCode[0], o->pTrampoline->rbCode[1],
                         o->pTrampoline->rbCode[2], o->pTrampoline->rbCode[3],
                         o->pTrampoline->rbCode[4], o->pTrampoline->rbCode[5],
                         o->pTrampoline->rbCode[6], o->pTrampoline->rbCode[7],
                         o->pTrampoline->rbCode[8], o->pTrampoline->rbCode[9],
                         o->pTrampoline->rbCode[10], o->pTrampoline->rbCode[11]);
        }

        o = o->pNext;
    } while (o != NULL);

    // Update any suspended threads.
    for (i = 0; i < s_ulSuspendedThreadCount; i++)
    {
        detour_thread_update(s_phSuspendedThreads[i], s_pPendingOperations);
    }

    // Restore all of the page permissions and free any trampoline regions that are now unused.
    for (o = s_pPendingOperations; o != NULL;)
    {
        // We don't care if this fails, because the code is still accessible.
        pMem = o->pbTarget;
        sMem = o->pTrampoline->cbRestore;
        NtProtectVirtualMemory(NtCurrentProcess(), &pMem, &sMem, o->dwPerm, &dwOld);
        if (o->fIsRemove)
        {
            detour_free_trampoline(o->pTrampoline);
            o->pTrampoline = NULL;
            freed = TRUE;
        }

        n = o->pNext;
        detour_memory_free(o);
        o = n;
    }
    s_pPendingOperations = NULL;
    if (freed)
    {
        detour_free_unused_trampoline_regions();
    }

_exit:
    // Make sure the trampoline pages are no longer writable.
    detour_runnable_trampoline_regions();

    // Resume any suspended threads.
    if (s_phSuspendedThreads != NULL)
    {
        detour_thread_resume(s_phSuspendedThreads, s_ulSuspendedThreadCount);
        s_phSuspendedThreads = NULL;
        s_ulSuspendedThreadCount = 0;
    }

    s_nPendingThreadId = NULL;
    return HRESULT_FROM_NT(STATUS_SUCCESS);
}

HRESULT
NTAPI
SlimDetoursAttach(
    _Inout_ PVOID* ppPointer,
    _In_ PVOID pDetour)
{
    NTSTATUS Status;
    PVOID pMem;
    SIZE_T sMem;
    DWORD dwOld;
    BOOL fTargetArm64Ec;
#if defined(_M_ARM64EC)
    BOOL fDetourArm64Ec;
#endif

    if (s_nPendingThreadId != NtCurrentThreadId())
    {
        return HRESULT_FROM_NT(STATUS_TRANSACTIONAL_CONFLICT);
    }

    PBYTE pbTarget = (PBYTE)*ppPointer;
    PDETOUR_TRAMPOLINE pTrampoline = NULL;
    PDETOUR_OPERATION o = NULL;
#if defined(_M_ARM64EC)
    pbTarget = detour_skip_jmp_arm64ec(pbTarget, &fTargetArm64Ec);
    pDetour = detour_skip_jmp_arm64ec((PBYTE)pDetour, &fDetourArm64Ec);
    if (fTargetArm64Ec && !fDetourArm64Ec)
    {
        Status = STATUS_NOT_SUPPORTED;
        DETOUR_BREAK();
        goto fail;
    }
#elif defined(_M_X64)
    if (detour_is_ec_code(pbTarget))
    {
        Status = STATUS_NOT_SUPPORTED;
        goto fail;
    }
    pbTarget = detour_skip_jmp(pbTarget);
    if (detour_is_ec_code(pbTarget))
    {
        Status = STATUS_NOT_SUPPORTED;
        goto fail;
    }
    fTargetArm64Ec = FALSE;
    pDetour = detour_skip_jmp((PBYTE)pDetour);
#elif defined(_M_ARM64)
    fTargetArm64Ec = FALSE;
    pbTarget = detour_skip_jmp_arm64(pbTarget);
    pDetour = detour_skip_jmp_arm64((PBYTE)pDetour);
#else
    fTargetArm64Ec = FALSE;
    pbTarget = (PBYTE)detour_skip_jmp(pbTarget);
    pDetour = detour_skip_jmp((PBYTE)pDetour);
#endif

    // Don't follow a jump if its destination is the target function.
    // This happens when the detour does nothing other than call the target.
    if (pDetour == (PVOID)pbTarget)
    {
        Status = STATUS_INVALID_PARAMETER;
        DETOUR_BREAK();
        goto fail;
    }

    o = detour_memory_alloc(sizeof(DETOUR_OPERATION));
    if (o == NULL)
    {
        Status = STATUS_NO_MEMORY;
fail:
        DETOUR_BREAK();
        if (pTrampoline != NULL)
        {
            detour_free_trampoline(pTrampoline);
            detour_free_trampoline_region_if_unused(pTrampoline);
            pTrampoline = NULL;
        }
        if (o != NULL)
        {
            detour_memory_free(o);
        }
        return HRESULT_FROM_NT(Status);
    }

    pTrampoline = detour_alloc_trampoline(pbTarget, fTargetArm64Ec);
    if (pTrampoline == NULL)
    {
        Status = STATUS_NO_MEMORY;
        DETOUR_BREAK();
        goto fail;
    }

    DETOUR_TRACE("detours: pbTramp=%p, pDetour=%p\n", pTrampoline, pDetour);

    RtlZeroMemory(pTrampoline->rAlign, sizeof(pTrampoline->rAlign));

    // Determine the number of movable target instructions.
    PBYTE pbSrc = pbTarget;
    PBYTE pbTrampoline = pTrampoline->rbCode;
    PBYTE pbPool = pbTrampoline + sizeof(pTrampoline->rbCode);
    ULONG cbTarget = 0;
    ULONG cbJump =
#if defined(_M_ARM64EC)
        fTargetArm64Ec ? SIZE_OF_JMP_ARM64 :
#endif
        SIZE_OF_JMP;
    ULONG nAlign = 0;

    while (cbTarget < cbJump)
    {
        PBYTE pbOp = pbSrc;
        LONG lExtra = 0;

        DETOUR_TRACE(" detour_copy_target_instruction(%p,%p)\n", pbTrampoline, pbSrc);
        pbSrc = (PBYTE)detour_copy_target_instruction(pbTrampoline, pbSrc, NULL, &lExtra, fTargetArm64Ec);
        DETOUR_TRACE(" detour_copy_target_instruction() = %p (%d bytes)\n", pbSrc, (int)(pbSrc - pbOp));
        pbTrampoline += (pbSrc - pbOp) + lExtra;
        cbTarget = PtrOffset(pbTarget, pbSrc);
        pTrampoline->rAlign[nAlign].obTarget = (BYTE)cbTarget;
        pTrampoline->rAlign[nAlign].obTrampoline = (BYTE)(pbTrampoline - pTrampoline->rbCode);
        nAlign++;

        if (nAlign >= ARRAYSIZE(pTrampoline->rAlign))
        {
            break;
        }

        if (detour_does_target_code_end_function(pbOp, fTargetArm64Ec))
        {
            break;
        }
    }

    // Consume, but don't duplicate padding if it is needed and available.
    while (cbTarget < cbJump)
    {
        LONG cFiller = detour_is_target_code_filler(pbSrc, fTargetArm64Ec);
        if (cFiller == 0)
        {
            break;
        }

        pbSrc += cFiller;
        cbTarget = PtrOffset(pbTarget, pbSrc);
    }

#if _DEBUG
    {
        DETOUR_TRACE(" detours: rAlign [");
        LONG n = 0;
        for (n = 0; n < ARRAYSIZE(pTrampoline->rAlign); n++)
        {
            if (pTrampoline->rAlign[n].obTarget == 0 && pTrampoline->rAlign[n].obTrampoline == 0)
            {
                break;
            }
            DETOUR_TRACE(" %u/%u", pTrampoline->rAlign[n].obTarget, pTrampoline->rAlign[n].obTrampoline);

        }
        DETOUR_TRACE(" ]\n");
    }
#endif

    if (cbTarget < cbJump || nAlign > ARRAYSIZE(pTrampoline->rAlign))
    {
        // Too few instructions.
        Status = STATUS_INVALID_BLOCK_LENGTH;
        DETOUR_BREAK();
        goto fail;
    }

    if (pbTrampoline > pbPool)
    {
        __debugbreak();
    }

    pTrampoline->cbCode = (BYTE)(pbTrampoline - pTrampoline->rbCode);
    pTrampoline->cbRestore = (BYTE)cbTarget;
    RtlCopyMemory(pTrampoline->rbRestore, pbTarget, cbTarget);

    if (cbTarget > sizeof(pTrampoline->rbCode) - cbJump)
    {
        // Too many instructions.
        Status = STATUS_INVALID_HANDLE;
        DETOUR_BREAK();
        goto fail;
    }

    pTrampoline->pbRemain = pbTarget + cbTarget;
    pTrampoline->pbDetour = (PBYTE)pDetour;

    pbTrampoline = pTrampoline->rbCode + pTrampoline->cbCode;
#if defined(_M_ARM64EC)
    if (fTargetArm64Ec)
    {
        pbTrampoline = detour_gen_jmp_immediate_arm64(pbTrampoline, &pbPool, pTrampoline->pbRemain);
        pbTrampoline = detour_gen_brk_arm64(pbTrampoline, pbPool);
    } else
    {
        pbTrampoline = detour_gen_jmp_indirect(pbTrampoline, &pTrampoline->pbRemain);
        pbTrampoline = detour_gen_brk(pbTrampoline, pbPool);
    }
#elif defined(_M_X64)
    pbTrampoline = detour_gen_jmp_indirect(pbTrampoline, &pTrampoline->pbRemain);
    pbTrampoline = detour_gen_brk(pbTrampoline, pbPool);
#elif defined(_M_IX86)
    pbTrampoline = detour_gen_jmp_immediate(pbTrampoline, pTrampoline->pbRemain);
    pbTrampoline = detour_gen_brk(pbTrampoline, pbPool);
#elif defined(_M_ARM64)
    pbTrampoline = detour_gen_jmp_immediate_arm64(pbTrampoline, &pbPool, pTrampoline->pbRemain);
    pbTrampoline = detour_gen_brk_arm64(pbTrampoline, pbPool);
#endif
    UNREFERENCED_PARAMETER(pbTrampoline);

    pMem = pbTarget;
    sMem = cbTarget;
    Status = NtProtectVirtualMemory(NtCurrentProcess(), &pMem, &sMem, PAGE_EXECUTE_READWRITE, &dwOld);
    if (!NT_SUCCESS(Status))
    {
        DETOUR_BREAK();
        goto fail;
    }

    DETOUR_TRACE("detours: pbTarget=%p: "
                 "%02x %02x %02x %02x "
                 "%02x %02x %02x %02x "
                 "%02x %02x %02x %02x\n",
                 pbTarget,
                 pbTarget[0], pbTarget[1], pbTarget[2], pbTarget[3],
                 pbTarget[4], pbTarget[5], pbTarget[6], pbTarget[7],
                 pbTarget[8], pbTarget[9], pbTarget[10], pbTarget[11]);
    DETOUR_TRACE("detours: pbTramp =%p: "
                 "%02x %02x %02x %02x "
                 "%02x %02x %02x %02x "
                 "%02x %02x %02x %02x\n",
                 pTrampoline,
                 pTrampoline->rbCode[0], pTrampoline->rbCode[1],
                 pTrampoline->rbCode[2], pTrampoline->rbCode[3],
                 pTrampoline->rbCode[4], pTrampoline->rbCode[5],
                 pTrampoline->rbCode[6], pTrampoline->rbCode[7],
                 pTrampoline->rbCode[8], pTrampoline->rbCode[9],
                 pTrampoline->rbCode[10], pTrampoline->rbCode[11]);

    o->fIsAdd = TRUE;
    o->fIsRemove = FALSE;
#if defined(_M_ARM64EC)
    o->fTargetArm64Ec = fTargetArm64Ec;
#endif
    o->ppbPointer = (PBYTE*)ppPointer;
    o->pTrampoline = pTrampoline;
    o->pbTarget = pbTarget;
    o->dwPerm = dwOld;
    o->pNext = s_pPendingOperations;
    s_pPendingOperations = o;

    return HRESULT_FROM_NT(STATUS_SUCCESS);
}

HRESULT
NTAPI
SlimDetoursDetach(
    _Inout_ PVOID* ppPointer,
    _In_ PVOID pDetour)
{
    NTSTATUS Status;
    PVOID pMem;
    SIZE_T sMem;
    DWORD dwOld;
#if defined(_M_ARM64EC)
    BOOL fTargetArm64Ec, fDetourArm64Ec;
#endif

    if (s_nPendingThreadId != NtCurrentThreadId())
    {
        return HRESULT_FROM_NT(STATUS_TRANSACTIONAL_CONFLICT);
    }

    PDETOUR_OPERATION o = detour_memory_alloc(sizeof(DETOUR_OPERATION));
    if (o == NULL)
    {
        Status = STATUS_NO_MEMORY;
fail:
        DETOUR_BREAK();
        if (o != NULL)
        {
            detour_memory_free(o);
        }
        return HRESULT_FROM_NT(Status);
    }

    PDETOUR_TRAMPOLINE pTrampoline = (PDETOUR_TRAMPOLINE)*ppPointer;
#if defined(_M_ARM64EC)
    fTargetArm64Ec = detour_is_ec_code(pTrampoline->rbCode);
    pDetour = detour_skip_jmp_arm64ec((PBYTE)pDetour, &fDetourArm64Ec);
    if (fTargetArm64Ec && !fDetourArm64Ec)
    {
        Status = STATUS_NOT_SUPPORTED;
        DETOUR_BREAK();
        goto fail;
    }
#elif defined(_M_ARM64)
    pDetour = detour_skip_jmp_arm64((PBYTE)pDetour);
#else
    pDetour = detour_skip_jmp((PBYTE)pDetour);
#endif

    ////////////////////////////////////// Verify that Trampoline is in place.
    //
    LONG cbTarget = pTrampoline->cbRestore;
    PBYTE pbTarget = pTrampoline->pbRemain - cbTarget;
    if (cbTarget == 0 || cbTarget > sizeof(pTrampoline->rbCode) || pTrampoline->pbDetour != pDetour)
    {
        Status = STATUS_INVALID_BLOCK_LENGTH;
        DETOUR_BREAK();
        goto fail;
    }

    pMem = pbTarget;
    sMem = cbTarget;
    Status = NtProtectVirtualMemory(NtCurrentProcess(), &pMem, &sMem, PAGE_EXECUTE_READWRITE, &dwOld);
    if (!NT_SUCCESS(Status))
    {
        DETOUR_BREAK();
        goto fail;
    }

    o->fIsAdd = FALSE;
    o->fIsRemove = TRUE;
#if defined(_M_ARM64EC)
    o->fTargetArm64Ec = fTargetArm64Ec;
#endif
    o->ppbPointer = (PBYTE*)ppPointer;
    o->pTrampoline = pTrampoline;
    o->pbTarget = pbTarget;
    o->dwPerm = dwOld;
    o->pNext = s_pPendingOperations;
    s_pPendingOperations = o;

    return HRESULT_FROM_NT(STATUS_SUCCESS);
}

HRESULT
NTAPI
SlimDetoursUninitialize(VOID)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!detour_memory_uninitialize())
    {
        Status = STATUS_INVALID_HANDLE;
    }

    return HRESULT_FROM_NT(Status);
}
