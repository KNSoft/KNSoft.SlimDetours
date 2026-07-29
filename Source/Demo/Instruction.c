/*
 * This demo compiled with platform specified assembly source to hook functions with customized instructions.
 * 
 * Run "Demo.exe -Run Instruction".
 */

#include "Demo.h"
#include "Instruction.inl"

typedef
_Function_class_(FN_INSTRUCTION)
ULONG_PTR
_cdecl
FN_INSTRUCTION(VOID);

#if defined(_M_X64) && !defined(_M_ARM64EC)

EXTERN_C FN_INSTRUCTION SimpleInstructionFunc1X64;

#elif defined(_M_IX86)

EXTERN_C FN_INSTRUCTION SimpleInstructionFunc1X86;

#endif

static
_Function_class_(FN_INSTRUCTION)
ULONG_PTR
_cdecl
Hooked_InstructionFunc(VOID)
{
    return (ULONG_PTR)PRESET_RETURN_VALUE * 2;
}

static FN_INSTRUCTION* g_apfnInstructionFunctions[] = {
#if defined(_M_X64) && !defined(_M_ARM64EC)
    &SimpleInstructionFunc1X64,
#elif defined(_M_IX86)
    &SimpleInstructionFunc1X86,
#endif
    NULL
};

#if defined(_M_ARM64EC)

_Must_inspect_result_
static
NTSTATUS
AllocateInstructionTestCode(
    _Outptr_result_bytebuffer_(PAGE_SIZE) PVOID* ppCode,
    _In_ BOOL fEcCode)
{
    SIZE_T RegionSize = PAGE_SIZE;

    *ppCode = NULL;
    if (fEcCode)
    {
        MEM_EXTENDED_PARAMETER Parameter = { 0 };

        Parameter.Type = MemExtendedParameterAttributeFlags;
        Parameter.ULong64 = MEM_EXTENDED_PARAMETER_EC_CODE;
        return NtAllocateVirtualMemoryEx(NtCurrentProcess(),
                                         ppCode,
                                         &RegionSize,
                                         MEM_COMMIT | MEM_RESERVE,
                                         PAGE_EXECUTE_READWRITE,
                                         &Parameter,
                                         1);
    }

    return NtAllocateVirtualMemory(NtCurrentProcess(),
                                   ppCode,
                                   0,
                                   &RegionSize,
                                   MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
}

_Must_inspect_result_
static
NTSTATUS
FreeInstructionTestCode(
    _Frees_ptr_opt_ PVOID pCode)
{
    if (pCode == NULL)
    {
        return STATUS_SUCCESS;
    }

    SIZE_T RegionSize = 0;
    return NtFreeVirtualMemory(NtCurrentProcess(), &pCode, &RegionSize, MEM_RELEASE);
}

#endif

TEST_FUNC(Instruction)
{
    HRESULT hr;
    FN_INSTRUCTION *pfn1, *pfn2;

#if defined(_M_ARM64EC)
    PVOID pEcCode = NULL;
    PVOID pX64Code = NULL;
    NTSTATUS Status = AllocateInstructionTestCode(&pEcCode, TRUE);

    TEST_OK(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        for (ULONG i = 0; i < 4; i++)
        {
            ((PULONG)pEcCode)[i] = 0x8b0300eb; // add x11, x7, x3
        }
        NtFlushInstructionCache(NtCurrentProcess(), pEcCode, sizeof(ULONG) * 4);

        TEST_OK(RtlIsEcCode((ULONG64)pEcCode));
        TEST_OK(SlimDetoursCodeFromPointer(pEcCode) == pEcCode);
        TEST_OK(SlimDetoursCopyInstruction(NULL, pEcCode, NULL, NULL) == (PBYTE)pEcCode + sizeof(ULONG));
    }

    Status = AllocateInstructionTestCode(&pX64Code, FALSE);
    TEST_OK(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        PBYTE pbX64Code = pX64Code;

        RtlFillMemory(pbX64Code, PAGE_SIZE, 0xcc);
        pbX64Code[16] = 0xeb;
        pbX64Code[17] = 0x00;
        pbX64Code[18] = 0x90;
        pbX64Code[19] = 0x90;

        pbX64Code[32] = 0x48;
        pbX64Code[33] = 0xb8;
        *(UNALIGNED PULONG64)&pbX64Code[34] = PRESET_RETURN_VALUE;
        pbX64Code[42] = 0xc3;
        NtFlushInstructionCache(NtCurrentProcess(), pX64Code, 43);

        TEST_OK(!RtlIsEcCode((ULONG64)pX64Code));
        TEST_OK(SlimDetoursCodeFromPointer(pbX64Code + 16) == pbX64Code + 18);
        TEST_OK(SlimDetoursCopyInstruction(NULL, pbX64Code + 16, NULL, NULL) == pbX64Code + 18);

        pfn2 = pfn1 = (FN_INSTRUCTION*)(pbX64Code + 32);
        TEST_OK(pfn1() == (ULONG_PTR)PRESET_RETURN_VALUE);
        hr = SlimDetoursInlineHook(TRUE, (PVOID*)&pfn1, Hooked_InstructionFunc);
        TEST_OK(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
        {
            TEST_OK(pfn1() == (ULONG_PTR)PRESET_RETURN_VALUE);
            TEST_OK(pfn2() == (ULONG_PTR)PRESET_RETURN_VALUE * 2);

            hr = SlimDetoursInlineHook(FALSE, (PVOID*)&pfn1, Hooked_InstructionFunc);
            TEST_OK(SUCCEEDED(hr));
            if (SUCCEEDED(hr))
            {
                TEST_OK(pfn2() == (ULONG_PTR)PRESET_RETURN_VALUE);
            }
        }
    }

    if (pEcCode != NULL && pX64Code != NULL)
    {
        PBYTE pbX64Code = pX64Code;
        LONG64 BranchOffset =
            (LONG64)(ULONG_PTR)pEcCode -
            (LONG64)(ULONG_PTR)(pbX64Code + 7);

#if !defined(_DEBUG)
        PVOID pTarget = pEcCode;
        hr = SlimDetoursInlineHook(TRUE, &pTarget, pbX64Code + 32);
        TEST_OK(hr == HRESULT_FROM_NT(STATUS_NOT_SUPPORTED));
#endif

        if (BranchOffset == (LONG64)(LONG)BranchOffset)
        {
            pbX64Code[0] = 0xeb;
            pbX64Code[1] = 0x00;
            pbX64Code[2] = 0xe9;
            *(UNALIGNED PLONG)&pbX64Code[3] = (LONG)BranchOffset;
            NtFlushInstructionCache(NtCurrentProcess(), pbX64Code, 7);

            TEST_OK(SlimDetoursCodeFromPointer(pbX64Code) == pEcCode);

            pfn1 = (FN_INSTRUCTION*)pbX64Code;
            hr = SlimDetoursInlineHook(TRUE, (PVOID*)&pfn1, Hooked_InstructionFunc);
            TEST_OK(SUCCEEDED(hr));
            if (SUCCEEDED(hr))
            {
                TEST_OK(RtlIsEcCode((ULONG64)pfn1));
                hr = SlimDetoursInlineHook(FALSE, (PVOID*)&pfn1, Hooked_InstructionFunc);
                TEST_OK(SUCCEEDED(hr));
            }
        } else
        {
            TEST_SKIP("X64 and ARM64EC test code are not within relative branch range");
        }

        for (ULONG InstructionOffset = sizeof(ULONG);
             InstructionOffset <= sizeof(ULONG) * 2;
             InstructionOffset += sizeof(ULONG))
        {
            HANDLE hThread = CreateThread(
                NULL,
                0,
                (LPTHREAD_START_ROUTINE)pEcCode,
                NULL,
                CREATE_SUSPENDED,
                NULL);
            if (hThread != NULL)
            {
                CONTEXT Context = { 0 };

                Context.ContextFlags = CONTEXT_CONTROL;
                Status = NtGetContextThread(hThread, &Context);
                TEST_OK(NT_SUCCESS(Status));
                if (NT_SUCCESS(Status))
                {
                    Context.CONTEXT_PC = (ULONG_PTR)pEcCode + InstructionOffset;
                    Status = NtSetContextThread(hThread, &Context);
                    TEST_OK(NT_SUCCESS(Status));
                }

                if (NT_SUCCESS(Status))
                {
                    pfn1 = (FN_INSTRUCTION*)pEcCode;
                    hr = SlimDetoursInlineHook(TRUE, (PVOID*)&pfn1, Hooked_InstructionFunc);
                    TEST_OK(SUCCEEDED(hr));
                    if (SUCCEEDED(hr))
                    {
                        Context.ContextFlags = CONTEXT_CONTROL;
                        Status = NtGetContextThread(hThread, &Context);
                        TEST_OK(NT_SUCCESS(Status));
                        if (NT_SUCCESS(Status))
                        {
                            TEST_OK(Context.CONTEXT_PC == (ULONG_PTR)pfn1 + InstructionOffset);
                        }

                        hr = SlimDetoursInlineHook(FALSE, (PVOID*)&pfn1, Hooked_InstructionFunc);
                        TEST_OK(SUCCEEDED(hr));
                        if (SUCCEEDED(hr))
                        {
                            Context.ContextFlags = CONTEXT_CONTROL;
                            Status = NtGetContextThread(hThread, &Context);
                            TEST_OK(NT_SUCCESS(Status));
                            if (NT_SUCCESS(Status))
                            {
                                TEST_OK(Context.CONTEXT_PC == (ULONG_PTR)pEcCode + InstructionOffset);
                            }
                        }
                    }
                }

                NtTerminateThread(hThread, STATUS_SUCCESS);
                NtClose(hThread);
            } else
            {
                TEST_SKIP("Failed to create suspended instruction test thread");
            }
        }
    }

    TEST_OK(NT_SUCCESS(FreeInstructionTestCode(pX64Code)));
    TEST_OK(NT_SUCCESS(FreeInstructionTestCode(pEcCode)));
#endif

    for (ULONG i = 0; i < ARRAYSIZE(g_apfnInstructionFunctions) && g_apfnInstructionFunctions[i] != NULL; i++)
    {
        pfn2 = pfn1 = g_apfnInstructionFunctions[i];
        if (pfn1() != (ULONG_PTR)PRESET_RETURN_VALUE)
        {
            TEST_SKIP("Instruction Function #%lu did not return the preset value", i);
            continue;
        }
        hr = SlimDetoursInlineHook(TRUE, (PVOID*)&pfn1, Hooked_InstructionFunc);
        TEST_OK(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
        {
            TEST_OK(pfn1() == (ULONG_PTR)PRESET_RETURN_VALUE);
            TEST_OK(pfn2() == (ULONG_PTR)PRESET_RETURN_VALUE * 2);
        }
        hr = SlimDetoursInlineHook(FALSE, (PVOID*)&pfn1, Hooked_InstructionFunc);
        TEST_OK(SUCCEEDED(hr));
        if (SUCCEEDED(hr))
        {
            TEST_OK(pfn2() == (ULONG_PTR)PRESET_RETURN_VALUE);
        }
    }
}
