/*
 * This demo shows delay hook, msidle.dll!GetIdleMinutes(#8) will be hooked automatically when msidle.dll loaded.
 * Implemented by DLL Notification mechanism, introduced in NT6.
 *
 * Run "Demo.exe -Run DelayHook".
 *
 * See also https://github.com/KNSoft/KNSoft.SlimDetours/tree/main/Docs/TechWiki/Implement%20Delay%20Hook
 */

#include "Demo.h"

#if _WIN32_WINNT >= _WIN32_WINNT_WIN6

#pragma comment(lib, "KNSoft.NDK.WinAPI.lib") // For Ldr(Register/Unregister)DllNotification imports

static HRESULT g_hrDelayAttach = E_FAIL;
static typeof(&GetIdleMinutes) g_pfnOrgGetIdleMinutes, g_pfnGetIdleMinutes = NULL;
static CONST UNICODE_STRING g_usMsidle = RTL_CONSTANT_STRING(L"msidle.dll");
static LONG volatile g_lGetIdleMinutesCount = 0;
static DWORD g_dwGetIdleMinutesRet = 0;

static
DWORD
WINAPI
Hooked_GetIdleMinutes(
    DWORD dwReserved)
{
    _InterlockedIncrement(&g_lGetIdleMinutesCount);
    g_dwGetIdleMinutesRet = g_pfnGetIdleMinutes(dwReserved);
    UnitTest_FormatMessage("GetIdleMinutes returns %lu\n", g_dwGetIdleMinutesRet);
    return g_dwGetIdleMinutesRet;
}

static
_Function_class_(LDR_DLL_NOTIFICATION_FUNCTION)
VOID
CALLBACK
DllLoadCallback(
    _In_ ULONG NotificationReason,
    _In_ PCLDR_DLL_NOTIFICATION_DATA NotificationData,
    _In_opt_ PVOID Context)
{
    NTSTATUS Status;

    if (NotificationReason != LDR_DLL_NOTIFICATION_REASON_LOADED ||
        RtlCompareUnicodeString((PUNICODE_STRING)NotificationData->Loaded.BaseDllName,
                                (PUNICODE_STRING)&g_usMsidle,
                                TRUE) != 0)
    {
        return;
    }

    Status = LdrGetProcedureAddress(NotificationData->Loaded.DllBase, NULL, 8, (PVOID*)&g_pfnGetIdleMinutes);
    if (NT_SUCCESS(Status))
    {
        g_pfnOrgGetIdleMinutes = g_pfnGetIdleMinutes;
        g_hrDelayAttach = SlimDetoursInlineHook(TRUE, (PVOID*)&g_pfnGetIdleMinutes, Hooked_GetIdleMinutes);
    } else
    {
        UnitTest_FormatMessage("LdrGetProcedureAddress failed with 0x%08lX in DllLoadCallback\n", Status);
        g_hrDelayAttach = HRESULT_FROM_NT(Status);
    }
}

TEST_FUNC(DelayHook)
{
    NTSTATUS Status;
    PVOID Cookie;
    PVOID hMsidle;
    DWORD dwRet;

    /* Make sure msidle.dll is not loaded yet */
    Status = LdrGetDllHandle(NULL, NULL, (PUNICODE_STRING)&g_usMsidle, &hMsidle);
    if (NT_SUCCESS(Status))
    {
        TEST_SKIP("msidle.dll is loaded, test cannot continue");
        return;
    } else if (Status != STATUS_DLL_NOT_FOUND)
    {
        TEST_SKIP("LdrGetDllHandle failed with 0x%08lX", Status);
        return;
    }

    /* Register DLL load callback */
    Status = LdrRegisterDllNotification(0, DllLoadCallback, NULL, &Cookie);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("LdrRegisterDllNotification failed with 0x%08lX", Status);
        return;
    }

    /* Load msidle.dll now */
    Status = LdrLoadDll(NULL, NULL, (PUNICODE_STRING)&g_usMsidle, &hMsidle);
    if (!NT_SUCCESS(Status))
    {
        TEST_SKIP("LdrLoadDll failed with 0x%08lX", Status);
        goto _Exit;
    }

    /* Delay attach callback should be called and GetIdleMinutes is hooked successfully */
    TEST_OK(SUCCEEDED(g_hrDelayAttach));
    dwRet = g_pfnOrgGetIdleMinutes(0);
    TEST_OK(dwRet == g_dwGetIdleMinutesRet);
    TEST_OK(g_lGetIdleMinutesCount == 1);

    LdrUnloadDll(hMsidle);
_Exit:
    LdrUnregisterDllNotification(Cookie);
}

#endif /* _WIN32_WINNT >= _WIN32_WINNT_WIN6 */
