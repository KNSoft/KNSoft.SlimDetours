/*
 * This demo performs a COM hook (IStream::Read) and compares Microsoft Detours with KNSoft.SlimDetours
 * compatibility on ARM64.
 *
 * On ARM64EC, run "Demo.exe -Run COMHook -Engine=MSDetours" may crash when hooking ARM64EC code.
 * Run "Demo.exe -Run COMHook -Engine=SlimDetours" will pass the same test.
 * 
 * See also:
 *   https://github.com/microsoft/Detours/issues/292
 *   https://github.com/microsoft/Detours/issues/355
 */

#include "Demo.h"

#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static IStream* g_pStream = NULL;
static FIELD_TYPE(IStream, lpVtbl->Read) g_pfnIStream_Read = NULL;
static LONG volatile g_lCount = 0;
static ULONG g_ulValue = 123, g_ulRead = 0;

static
HRESULT
STDMETHODCALLTYPE
Hooked_IStream_Read(
    IStream* This,
    _Out_writes_bytes_to_(cb, *pcbRead) void* pv,
    _In_ ULONG cb,
    _Out_opt_ ULONG* pcbRead)
{
    if (This == g_pStream &&
        pv == &g_ulValue &&
        cb == sizeof(g_ulValue) &&
        pcbRead == &g_ulRead)
    {
        _InterlockedIncrement(&g_lCount);
    }
    return g_pfnIStream_Read(This, pv, cb, pcbRead);
}

static
HRESULT
SetIStreamReadHook(
    _In_ DEMO_ENGINE_TYPE EngineType,
    _In_ BOOL Enable)
{
    HRESULT hr;

    hr = HookTransactionBegin(EngineType);
    if (FAILED(hr))
    {
        return hr;
    }
    hr = HookAttach(EngineType, Enable, (PVOID*)&g_pfnIStream_Read, Hooked_IStream_Read);
    if (SUCCEEDED(hr))
    {
        hr = HookTransactionCommit(EngineType);
    } else
    {
        HookTransactionAbort(EngineType);
    }
    return hr;
}

TEST_FUNC(COMHook)
{
    HRESULT hr;
    DEMO_ENGINE_TYPE EngineType;

    if (FAILED(GetEngineTypeFromArgs(TEST_PARAMETER_ARGC, TEST_PARAMETER_ARGV, &EngineType)))
    {
        TEST_SKIP("Invalid engine type");
        return;
    }

    /* Initialize IStream */
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        TEST_SKIP("CoInitializeEx failed with 0x%18lX", hr);
        return;
    }
    g_pStream = SHCreateMemStream((const BYTE*)&g_ulValue, sizeof(g_ulValue));
    if (g_pStream == NULL)
    {
        TEST_SKIP("SHCreateMemStream failed");
        goto _Exit_0;
    }

    /* Hook IStream::Read and call it */
    g_pfnIStream_Read = g_pStream->lpVtbl->Read;
#if defined(_M_ARM64EC)
    if (!RtlIsEcCode((ULONG64)g_pfnIStream_Read))
    {
        TEST_SKIP("IStream::Read is not ARM64EC code");
        goto _Exit_1;
    }
#endif
    hr = SetIStreamReadHook(EngineType, TRUE);
#if defined(_M_X64) && !defined(_M_ARM64EC)
    if (EngineType == EngineSlimDetours && FAILED(hr))
    {
        TEST_OK(hr == HRESULT_FROM_NT(STATUS_NOT_SUPPORTED));
        goto _Exit_1;
    }
#endif
    if (FAILED(hr))
    {
        TEST_FAIL("Hook IStream::Read failed with 0x%18lX", hr);
        goto _Exit_1;
    }
#if defined(_M_ARM64EC)
    TEST_OK(RtlIsEcCode((ULONG64)g_pfnIStream_Read));
#endif
    g_ulValue = 0xDEADBEEF;
    hr = g_pStream->lpVtbl->Read(g_pStream, &g_ulValue, sizeof(g_ulValue), &g_ulRead);
    if (FAILED(hr))
    {
        TEST_FAIL("IStream::Read failed with 0x%18lX", hr);
        goto _Exit_1;
    }

    /* Verify results */
    TEST_OK(g_ulValue == 123);
    TEST_OK(g_ulRead == sizeof(g_ulValue));
    TEST_OK(g_lCount == 1);

    /* Unhook IStream::Read and call it */
    hr = SetIStreamReadHook(EngineType, FALSE);
    if (FAILED(hr))
    {
        TEST_FAIL("Unhook IStream::Read failed with 0x%18lX", hr);
        goto _Exit_1;
    }
    hr = g_pStream->lpVtbl->Read(g_pStream, &g_ulValue, sizeof(g_ulValue), &g_ulRead);
    if (FAILED(hr))
    {
        TEST_FAIL("IStream::Read failed with 0x%18lX", hr);
    }
    TEST_OK(g_lCount == 1);

_Exit_1:
    g_pStream->lpVtbl->Release(g_pStream);
_Exit_0:
    CoUninitialize();
}
