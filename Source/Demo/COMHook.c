/*
 * This demo performs a COM hook (IStream::Read), get procedure address from virtual table and set inline hook.
 * Similar to "commem" sample in the original detours.
 *
 * Run "Demo.exe -Run COMHook".
 * 
 * This demo may crash on ARM64 host, see opening issues:
 *   https://github.com/microsoft/Detours/issues/292
 *   https://github.com/microsoft/Detours/issues/355
 * it may be related to ARM64 emulators, there’s nothing we can do.
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

TEST_FUNC(COMHook)
{
    /* FIXME: Skip incompatible cases, see issues mentioned in the head */
    if (GET_NT_VERSION() >= NT_VERSION_WIN10 &&
        SharedUserData->NativeProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 &&
        NtReadTeb(WowTebOffset) != 0)
    {
        TEST_SKIP("FIXME: COM Hook has issues in non-native environments on ARM64");
        return;
    }

    HRESULT hr;

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
    hr = SlimDetoursInlineHook(TRUE, (PVOID*)&g_pfnIStream_Read, &Hooked_IStream_Read);
    if (FAILED(hr))
    {
        TEST_FAIL("SlimDetoursInlineHook failed with 0x%18lX", hr);
        goto _Exit_1;
    }
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
    hr = SlimDetoursInlineHook(FALSE, (PVOID*)&g_pfnIStream_Read, &Hooked_IStream_Read);
    if (FAILED(hr))
    {
        TEST_FAIL("SlimDetoursInlineHook failed with 0x%18lX", hr);
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
