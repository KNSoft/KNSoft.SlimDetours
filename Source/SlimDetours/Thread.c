/*
 * KNSoft.SlimDetours (https://github.com/KNSoft/KNSoft.SlimDetours) Thread management
 * Copyright (c) KNSoft.org (https://github.com/KNSoft). All rights reserved.
 * Licensed under the MIT license.
 */

#include "SlimDetours.inl"

NTSTATUS
detour_thread_suspend(
    _Outptr_result_maybenull_ PHANDLE* SuspendedHandles,
    _Out_ PULONG SuspendedHandleCount)
{
    NTSTATUS Status;
    ULONG i, ThreadCount, SuspendedCount;
    PSYSTEM_PROCESS_INFORMATION pSPI, pCurrentSPI;
    PSYSTEM_THREAD_INFORMATION pSTI;
    PHANDLE Buffer;
    HANDLE ThreadHandle, CurrentPID, CurrentTID;
    OBJECT_ATTRIBUTES ObjectAttributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(NULL, 0);

    /* Get system process and thread information */
    i = _1MB;
_Try_alloc:
    pSPI = (PSYSTEM_PROCESS_INFORMATION)detour_memory_alloc(i);
    if (pSPI == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = NtQuerySystemInformation(SystemProcessInformation, pSPI, i, &i);
    if (!NT_SUCCESS(Status))
    {
        detour_memory_free(pSPI);
        if (Status == STATUS_INFO_LENGTH_MISMATCH)
        {
            goto _Try_alloc;
        }
        return Status;
    }

    /* Find current process and threads */
    CurrentPID = (HANDLE)(ULONG_PTR)NtCurrentProcessId();
    pCurrentSPI = pSPI;
    while (pCurrentSPI->UniqueProcessId != CurrentPID)
    {
        if (pCurrentSPI->NextEntryOffset == 0)
        {
            Status = STATUS_NOT_FOUND;
            goto _Exit;
        }
        pCurrentSPI = (PSYSTEM_PROCESS_INFORMATION)Add2Ptr(pCurrentSPI, pCurrentSPI->NextEntryOffset);
    }
    pSTI = (PSYSTEM_THREAD_INFORMATION)Add2Ptr(pCurrentSPI, sizeof(*pCurrentSPI));

    /* Skip if no other threads */
    ThreadCount = pCurrentSPI->NumberOfThreads - 1;
    if (ThreadCount == 0)
    {
        *SuspendedHandles = NULL;
        *SuspendedHandleCount = 0;
        Status = STATUS_SUCCESS;
        goto _Exit;
    }

    /* Create handle array */
    Buffer = (PHANDLE)detour_memory_alloc(ThreadCount * sizeof(HANDLE));
    if (Buffer == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto _Exit;
    }

    /* Suspend threads */
    SuspendedCount = 0;
    CurrentTID = (HANDLE)(ULONG_PTR)NtCurrentThreadId();
    for (i = 0; i < pCurrentSPI->NumberOfThreads; i++)
    {
        if (pSTI[i].ClientId.UniqueThread == CurrentTID ||
            !NT_SUCCESS(NtOpenThread(&ThreadHandle,
                                     THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                                     &ObjectAttributes,
                                     &pSTI[i].ClientId)))
        {
            continue;
        }
        if (NT_SUCCESS(NtSuspendThread(ThreadHandle, NULL)))
        {
            _Analysis_assume_(SuspendedCount < ThreadCount);
            Buffer[SuspendedCount++] = ThreadHandle;
        } else
        {
            NtClose(ThreadHandle);
        }
    }

    /* Return suspended thread handles */
    if (SuspendedCount == 0)
    {
        detour_memory_free(Buffer);
        *SuspendedHandles = NULL;
    } else
    {
        *SuspendedHandles = Buffer;
    }
    *SuspendedHandleCount = SuspendedCount;
    Status = STATUS_SUCCESS;

_Exit:
    detour_memory_free(pSPI);
    return Status;
}

VOID
detour_thread_resume(
    _In_reads_(SuspendedHandleCount) _Frees_ptr_ PHANDLE SuspendedHandles,
    _In_ ULONG SuspendedHandleCount)
{
    ULONG i;

    for (i = 0; i < SuspendedHandleCount; i++)
    {
        NtResumeThread(SuspendedHandles[i], NULL);
        NtClose(SuspendedHandles[i]);
    }
    detour_memory_free(SuspendedHandles);
}

NTSTATUS
detour_thread_update(
    _In_ HANDLE ThreadHandle,
    _In_ PDETOUR_OPERATION PendingOperations)
{
    NTSTATUS Status;
    PDETOUR_OPERATION o;
    CONTEXT cxt;
    BOOL bUpdateContext;

    cxt.ContextFlags = CONTEXT_CONTROL;
    Status = NtGetContextThread(ThreadHandle, &cxt);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (o = PendingOperations; o != NULL; o = o->pNext)
    {
        bUpdateContext = FALSE;
        if (o->fIsRemove)
        {
            if (cxt.CONTEXT_PC >= (ULONG_PTR)o->pTrampoline->rbCode &&
                cxt.CONTEXT_PC < ((ULONG_PTR)o->pTrampoline->rbCode + RTL_FIELD_SIZE(DETOUR_TRAMPOLINE, rbCode)))
            {
                cxt.CONTEXT_PC = (ULONG_PTR)o->pbTarget +
                    detour_align_from_trampoline(o->pTrampoline, (BYTE)(cxt.CONTEXT_PC - (ULONG_PTR)o->pTrampoline));
                bUpdateContext = TRUE;
            }
#if defined(_X86_) || defined(_AMD64_)
            else if (cxt.CONTEXT_PC == (ULONG_PTR)o->pTrampoline->rbCodeIn)
            {
                cxt.CONTEXT_PC = (ULONG_PTR)o->pbTarget;
                bUpdateContext = TRUE;
            }
#endif
        } else
        {
            if (cxt.CONTEXT_PC >= (ULONG_PTR)o->pbTarget &&
                cxt.CONTEXT_PC < ((ULONG_PTR)o->pbTarget + o->pTrampoline->cbRestore))
            {
                cxt.CONTEXT_PC = (ULONG_PTR)o->pTrampoline +
                    detour_align_from_target(o->pTrampoline, (BYTE)(cxt.CONTEXT_PC - (ULONG_PTR)o->pbTarget));
                bUpdateContext = TRUE;
            }
        }
        if (bUpdateContext)
        {
            Status = NtSetContextThread(ThreadHandle, &cxt);
            break;
        }
    }

    return Status;
}
