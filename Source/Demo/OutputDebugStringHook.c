/*
 * This demo shows kernel32!OutputDebugStringA can be an architecture-specific stub before reaching
 * the actual implementation in kernelbase.dll.
 *
 * Run "Demo.exe -Run OutputDebugStringHook -Engine=MSDetours" will fail this test,
 *   because the original Microsoft Detours hooks only the kernel32 stub.
 * Run "Demo.exe -Run OutputDebugStringHook -Engine=SlimDetours" will pass this test.
 */

#include "Demo.h"

#if defined(_M_X64) || defined(_M_ARM64)

typedef
VOID
WINAPI
FN_OutputDebugStringA(
    _In_opt_ LPCSTR lpOutputString);

static FN_OutputDebugStringA* g_pfnOutputDebugStringA = NULL;
static LONG volatile g_lOutputDebugStringACount = 0;
static CONST UNICODE_STRING g_usKernel32 = RTL_CONSTANT_STRING(L"kernel32.dll");
static CONST UNICODE_STRING g_usKernelBase = RTL_CONSTANT_STRING(L"kernelbase.dll");
static ANSI_STRING g_asOutputDebugStringA = RTL_CONSTANT_STRING("OutputDebugStringA");

static
VOID
WINAPI
Hooked_OutputDebugStringA(
    _In_opt_ LPCSTR lpOutputString)
{
    _InterlockedIncrement(&g_lOutputDebugStringACount);
    g_pfnOutputDebugStringA(lpOutputString);
}

#if defined(_M_X64)

static
_Success_(return != FALSE)
BOOL
DecodeKernel32OutputDebugStringAStub(
    _In_ PBYTE pbCode,
    _Out_ PVOID * ppTarget)
{
    if ((pbCode[0] & 0xf0) != 0x40 || pbCode[1] != 0xff || pbCode[2] != 0x25)
    {
        return FALSE;
    }

    *ppTarget = *(UNALIGNED PVOID*)(pbCode + 7 + *(UNALIGNED INT32*) & pbCode[3]);
    return TRUE;
}

#elif defined(_M_ARM64)

static
ULONG
FetchArm64Opcode(
    _In_ PBYTE pbCode)
{
    return *(UNALIGNED ULONG*)pbCode;
}

static
INT64
SignExtend(
    _In_ UINT64 Value,
    _In_ UINT Bits)
{
    UINT64 const Sign = 1ui64 << (Bits - 1);
    return (INT64)((Value & Sign) ? (Value | (~0ui64 << Bits)) : Value);
}

static
_Success_(return != FALSE)
BOOL
DecodeArm64ImportThunk(
    _In_ PBYTE pbCode,
    _Out_ PVOID * ppTarget)
{
    ULONG const Opcode = FetchArm64Opcode(pbCode);
    ULONG const Opcode2 = FetchArm64Opcode(pbCode + 4);
    ULONG const Opcode3 = FetchArm64Opcode(pbCode + 8);
    UINT64 pageLow2;
    UINT64 pageHigh19;
    INT64 page;
    UINT64 offset;
    PBYTE pbTarget;

    if ((Opcode & 0x9f00001f) != 0x90000010 ||
        (Opcode2 & 0xffe003ff) != 0xf9400210 ||
        Opcode3 != 0xd61f0200)
    {
        return FALSE;
    }

    pageLow2 = (Opcode >> 29) & 3;
    pageHigh19 = (Opcode >> 5) & ~(~0ui64 << 19);
    page = SignExtend((pageHigh19 << 2) | pageLow2, 21) << 12;
    offset = ((Opcode2 >> 10) & ~(~0ui64 << 12)) << 3;
    pbTarget = (PBYTE)((ULONG_PTR)pbCode & ~(ULONG_PTR)0xfff) + page + offset;

    *ppTarget = *(UNALIGNED PVOID*)pbTarget;
    return TRUE;
}

static
_Success_(return != FALSE)
BOOL
DecodeKernel32OutputDebugStringAStub(
    _In_ PBYTE pbCode,
    _Out_ PVOID * ppTarget)
{
    ULONG const Opcode = FetchArm64Opcode(pbCode);
    INT64 branchOffset;

    if ((Opcode & 0xfc000000) != 0x14000000)
    {
        return FALSE;
    }

    // B <imm26>
    branchOffset = SignExtend((Opcode & 0x03ffffffULL) << 2, 28);
    return DecodeArm64ImportThunk(pbCode + branchOffset, ppTarget);
}

#endif

static
HRESULT
SetOutputDebugStringAHook(
    _In_ DEMO_ENGINE_TYPE EngineType,
    _In_ BOOL Enable)
{
    HRESULT hr;

    hr = HookTransactionBegin(EngineType);
    if (FAILED(hr))
    {
        return hr;
    }
    hr = HookAttach(EngineType, Enable, (PVOID*)&g_pfnOutputDebugStringA, Hooked_OutputDebugStringA);
    if (SUCCEEDED(hr))
    {
        hr = HookTransactionCommit(EngineType);
    } else
    {
        HookTransactionAbort(EngineType);
    }
    return hr;
}

TEST_FUNC(OutputDebugStringHook)
{
    NTSTATUS Status;
    HRESULT hr;
    DEMO_ENGINE_TYPE EngineType;
    PVOID hKernel32, hKernelBase;
    FN_OutputDebugStringA *pfnKernel32OutputDebugStringA, *pfnKernelBaseOutputDebugStringA;
    PVOID pStubTarget;

    if (FAILED(GetEngineTypeFromArgs(TEST_PARAMETER_ARGC, TEST_PARAMETER_ARGV, &EngineType)))
    {
        TEST_SKIP("Invalid engine type");
        return;
    }

    Status = LdrGetDllHandle(NULL, NULL, (PUNICODE_STRING)&g_usKernel32, &hKernel32);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("LdrGetDllHandle for kernel32.dll failed with 0x%08lX", Status);
        return;
    }
    Status = LdrGetDllHandle(NULL, NULL, (PUNICODE_STRING)&g_usKernelBase, &hKernelBase);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("LdrGetDllHandle for kernelbase.dll failed with 0x%08lX", Status);
        return;
    }

    Status = LdrGetProcedureAddress(hKernel32,
                                    &g_asOutputDebugStringA,
                                    0,
                                    (PVOID*)&pfnKernel32OutputDebugStringA);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("LdrGetProcedureAddress for kernel32!OutputDebugStringA failed with 0x%08lX", Status);
        return;
    }
    Status = LdrGetProcedureAddress(hKernelBase,
                                    &g_asOutputDebugStringA,
                                    0,
                                    (PVOID*)&pfnKernelBaseOutputDebugStringA);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("LdrGetProcedureAddress for kernelbase!OutputDebugStringA failed with 0x%08lX", Status);
        return;
    }
    if (!DecodeKernel32OutputDebugStringAStub((PBYTE)pfnKernel32OutputDebugStringA, &pStubTarget))
    {
        TEST_SKIP("kernel32!OutputDebugStringA is not the expected stub");
        return;
    }

    if (pStubTarget != (PVOID)pfnKernelBaseOutputDebugStringA)
    {
        TEST_SKIP("kernel32!OutputDebugStringA stub does not target kernelbase!OutputDebugStringA");
        return;
    }

    g_lOutputDebugStringACount = 0;
    g_pfnOutputDebugStringA = pfnKernel32OutputDebugStringA;

    hr = SetOutputDebugStringAHook(EngineType, TRUE);
    if (FAILED(hr))
    {
        TEST_FAIL("Hook OutputDebugStringA failed with 0x%08lX", hr);
        return;
    }

    pfnKernelBaseOutputDebugStringA("OutputDebugStringHook: kernelbase");
    pfnKernel32OutputDebugStringA("OutputDebugStringHook: kernel32");
    TEST_OK(g_lOutputDebugStringACount == 2);

    hr = SetOutputDebugStringAHook(EngineType, FALSE);
    if (FAILED(hr))
    {
        TEST_FAIL("Unhook OutputDebugStringA failed with 0x%08lX", hr);
    }
}

#endif
