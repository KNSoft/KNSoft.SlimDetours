/*
 * This demo chains three hooks on user32.dll!EqualRect within a single transaction, and removes
 * them within a single transaction. The detour attached last is called first.
 *
 * Run "Demo.exe -Run ChainedHook".
 */

#include "Demo.h"

#define CHAINED_HOOK_COUNT 3

/* Covers the longest patch SlimDetours writes, the bytes past it are the same in both snapshots. */
#define CHAINED_HOOK_CODE_SIZE 32

static typeof(&EqualRect) g_pfnChainedEqualRect[CHAINED_HOOK_COUNT] = { NULL };
static LONG volatile g_lChainedCallCount = 0;
static LONG g_lChainedCallOrder[CHAINED_HOOK_COUNT] = { 0 };

static
BOOL
WINAPI
Hooked_ChainedEqualRect1(
    _In_ CONST RECT *lprc1,
    _In_ CONST RECT *lprc2)
{
    g_lChainedCallOrder[0] = _InterlockedIncrement(&g_lChainedCallCount);
    return g_pfnChainedEqualRect[0](lprc1, lprc2);
}

static
BOOL
WINAPI
Hooked_ChainedEqualRect2(
    _In_ CONST RECT *lprc1,
    _In_ CONST RECT *lprc2)
{
    g_lChainedCallOrder[1] = _InterlockedIncrement(&g_lChainedCallCount);
    return g_pfnChainedEqualRect[1](lprc1, lprc2);
}

static
BOOL
WINAPI
Hooked_ChainedEqualRect3(
    _In_ CONST RECT *lprc1,
    _In_ CONST RECT *lprc2)
{
    g_lChainedCallOrder[2] = _InterlockedIncrement(&g_lChainedCallCount);
    return g_pfnChainedEqualRect[2](lprc1, lprc2);
}

static CONST PVOID g_pChainedDetours[CHAINED_HOOK_COUNT] = {
    (PVOID)Hooked_ChainedEqualRect1,
    (PVOID)Hooked_ChainedEqualRect2,
    (PVOID)Hooked_ChainedEqualRect3
};

static
NTSTATUS
GetPageProtect(
    _In_ PVOID Address,
    _Out_ PULONG Protect)
{
    NTSTATUS Status;
    MEMORY_BASIC_INFORMATION mbi;

    Status = NtQueryVirtualMemory(NtCurrentProcess(),
                                  Address,
                                  MemoryBasicInformation,
                                  &mbi,
                                  sizeof(mbi),
                                  NULL);
    if (NT_SUCCESS(Status))
    {
        *Protect = mbi.Protect;
    }
    return Status;
}

TEST_FUNC(ChainedHook)
{
    NTSTATUS Status;
    HRESULT hr;
    RECT rc = { 0 };
    PVOID pCode;
    BYTE OriginalCode[CHAINED_HOOK_CODE_SIZE];
    ULONG OriginalProtect, Protect;
    ULONG i;

    Status = LoadEqualRect();
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("Load user32.dll!EqualRect failed with 0x%08lX\n", Status);
        return;
    }

    pCode = SlimDetoursCodeFromPointer(g_pfnEqualRect);
    RtlCopyMemory(OriginalCode, pCode, sizeof(OriginalCode));
    Status = GetPageProtect(pCode, &OriginalProtect);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("Query page protection failed with 0x%08lX\n", Status);
        return;
    }

    hr = SlimDetoursTransactionBegin();
    if (FAILED(hr))
    {
        TEST_FAIL("SlimDetoursTransactionBegin failed with 0x%08lX\n", hr);
        return;
    }
    for (i = 0; i < CHAINED_HOOK_COUNT; i++)
    {
        g_pfnChainedEqualRect[i] = g_pfnEqualRect;
        hr = SlimDetoursAttach((PVOID*)&g_pfnChainedEqualRect[i], g_pChainedDetours[i]);
        if (FAILED(hr))
        {
            SlimDetoursTransactionAbort();
            TEST_FAIL("SlimDetoursAttach #%lu failed with 0x%08lX\n", i, hr);
            return;
        }
    }
    hr = SlimDetoursTransactionCommit();
    if (FAILED(hr))
    {
        TEST_FAIL("SlimDetoursTransactionCommit failed with 0x%08lX\n", hr);
        return;
    }

    TEST_OK(g_pfnEqualRect(&rc, &rc) != FALSE);
    TEST_OK(g_lChainedCallCount == CHAINED_HOOK_COUNT);
    TEST_OK(g_lChainedCallOrder[2] == 1);
    TEST_OK(g_lChainedCallOrder[1] == 2);
    TEST_OK(g_lChainedCallOrder[0] == 3);

    /* The page keeps the protection it had before the transaction. */
    TEST_OK(NT_SUCCESS(GetPageProtect(pCode, &Protect)) && Protect == OriginalProtect);

    /* Detaching the innermost hook first makes the removal take several passes to unwind. */
    hr = SlimDetoursTransactionBegin();
    if (FAILED(hr))
    {
        TEST_FAIL("SlimDetoursTransactionBegin failed with 0x%08lX\n", hr);
        return;
    }
    for (i = 0; i < CHAINED_HOOK_COUNT; i++)
    {
        hr = SlimDetoursDetach((PVOID*)&g_pfnChainedEqualRect[i], g_pChainedDetours[i]);
        if (FAILED(hr))
        {
            SlimDetoursTransactionAbort();
            TEST_FAIL("SlimDetoursDetach #%lu failed with 0x%08lX\n", i, hr);
            return;
        }
    }
    hr = SlimDetoursTransactionCommit();
    if (FAILED(hr))
    {
        TEST_FAIL("SlimDetoursTransactionCommit failed with 0x%08lX\n", hr);
        return;
    }

    TEST_OK(RtlEqualMemory(pCode, OriginalCode, sizeof(OriginalCode)));
    TEST_OK(NT_SUCCESS(GetPageProtect(pCode, &Protect)) && Protect == OriginalProtect);

    g_lChainedCallCount = 0;
    TEST_OK(g_pfnEqualRect(&rc, &rc) != FALSE);
    TEST_OK(g_lChainedCallCount == 0);
}
