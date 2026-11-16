// Portal 2 VR loader entry point. Work is moved off DllMain's loader lock.
#include <Windows.h>
#include <Shellapi.h>
#include <iostream>
#include "game.h"
#include "hooks.h"
#include "vr.h"
#include "sdk.h"

DWORD WINAPI InitPortal2VR2009(LPVOID parameter)
{
    (void)parameter;

#ifdef _DEBUG
    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
#endif

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    bool insecureEnabled = false;
    bool diagnosticsOnly = false;
    if (arguments)
    {
        for (int i = 0; i < argumentCount; ++i)
        {
            if (lstrcmpiW(arguments[i], L"-insecure") == 0)
                insecureEnabled = true;
            else if (lstrcmpiW(arguments[i], L"-vrdiagnostics") == 0)
                diagnosticsOnly = true;
        }
        LocalFree(arguments);
    }

    // Diagnostic mode can run without -insecure because it never installs hooks.
    // Regular VR initialization continues to require -insecure.
    if (!insecureEnabled && !diagnosticsOnly)
        return 0;

    g_Game = new Game(diagnosticsOnly);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        HANDLE initThread = CreateThread(nullptr, 0, InitPortal2VR2009, hModule, 0, nullptr);
        if (initThread)
            CloseHandle(initThread);
    }
    return TRUE;
}
