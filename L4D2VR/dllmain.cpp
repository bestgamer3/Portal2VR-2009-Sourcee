// Portal 2 VR loader entry point. Work is moved off DllMain's loader lock.
#include <Windows.h>
#include <Shellapi.h>
#include <iostream>
#include <cstdio>
#include "game.h"
#include "hooks.h"
#include "vr.h"
#include "sdk.h"

namespace
{
    HANDLE g_LoaderLog = INVALID_HANDLE_VALUE;

    void LoaderLog(const char* message)
    {
        if (!message)
            return;

        OutputDebugStringA(message);
        OutputDebugStringA("\r\n");

        if (g_LoaderLog != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(g_LoaderLog, message, static_cast<DWORD>(lstrlenA(message)), &written, nullptr);
            WriteFile(g_LoaderLog, "\r\n", 2, &written, nullptr);
            FlushFileBuffers(g_LoaderLog);
        }
    }

    void OpenLoaderLog()
    {
        char tempPath[MAX_PATH]{};
        const DWORD pathLength = GetTempPathA(MAX_PATH, tempPath);
        if (pathLength == 0 || pathLength >= MAX_PATH)
        {
            OutputDebugStringA("Portal2VR: GetTempPathA failed; loader trace will use debugger output only.\r\n");
            return;
        }

        char logPath[MAX_PATH]{};
        if (sprintf_s(logPath, "%sPortal2VR_2009_loader.log", tempPath) < 0)
            return;

        g_LoaderLog = CreateFileA(logPath, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_LoaderLog == INVALID_HANDLE_VALUE)
        {
            OutputDebugStringA("Portal2VR: could not create the temporary loader log.\r\n");
            return;
        }

        LoaderLog("============================================================");
        LoaderLog("Portal 2 VR loader thread entered.");
        LoaderLog(logPath);
    }

    LONG WINAPI LogLoaderException(EXCEPTION_POINTERS* exceptionInfo)
    {
        char message[256]{};
        const DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord
            ? exceptionInfo->ExceptionRecord->ExceptionCode : 0;
        const void* address = exceptionInfo && exceptionInfo->ExceptionRecord
            ? exceptionInfo->ExceptionRecord->ExceptionAddress : nullptr;
        sprintf_s(message, "Structured exception in Portal 2 VR startup: code=0x%08lX address=%p",
            static_cast<unsigned long>(code), address);
        LoaderLog(message);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    bool RequiredSourceModulesLoaded()
    {
        static const char* names[] = {
            "client.dll", "engine.dll", "materialsystem.dll", "server.dll",
            "vgui2.dll", "vguimatsurface.dll"
        };
        for (const char* name : names)
        {
            if (!GetModuleHandleA(name))
                return false;
        }
        return true;
    }

    void LogSourceModuleWaitStatus()
    {
        const bool client = GetModuleHandleA("client.dll") != nullptr;
        const bool engine = GetModuleHandleA("engine.dll") != nullptr;
        const bool material = GetModuleHandleA("materialsystem.dll") != nullptr;
        const bool server = GetModuleHandleA("server.dll") != nullptr;
        const bool vgui2 = GetModuleHandleA("vgui2.dll") != nullptr;
        const bool surface = GetModuleHandleA("vguimatsurface.dll") != nullptr;
        char message[512]{};
        sprintf_s(message,
            "Background module wait: client=%s engine=%s materialsystem=%s server=%s vgui2=%s vguimatsurface=%s",
            client ? "loaded" : "missing",
            engine ? "loaded" : "missing",
            material ? "loaded" : "missing",
            server ? "loaded" : "missing",
            vgui2 ? "loaded" : "missing",
            surface ? "loaded" : "missing");
        LoaderLog(message);
    }
}

// Keep the SEH boundary in a function that has no C++ objects requiring unwinding.
static void ConstructGameForDiagnostics(bool diagnosticsOnly)
{
    LoaderLog("ConstructGameForDiagnostics entered.");
    try
    {
        g_Game = new Game(diagnosticsOnly);
    }
    catch (...)
    {
        LoaderLog("C++ exception caught while constructing Game.");
        return;
    }
    LoaderLog("new Game completed.");
}

static void ConstructGameWithSeh(bool diagnosticsOnly)
{
    LoaderLog("ConstructGameWithSeh entered.");
    __try
    {
        ConstructGameForDiagnostics(diagnosticsOnly);
    }
    __except (LogLoaderException(GetExceptionInformation()))
    {
        LoaderLog("Game constructor terminated by a structured exception.");
    }
}

DWORD WINAPI InitPortal2VR2009(LPVOID parameter)
{
    OpenLoaderLog();
    LoaderLog("BUILD_ID: p2vr-diag-module-path-20261116-a");

    char loadedModulePath[MAX_PATH]{};
    if (parameter && GetModuleFileNameA(static_cast<HMODULE>(parameter), loadedModulePath, MAX_PATH))
    {
        LoaderLog("Loaded Portal2VR proxy DLL:");
        LoaderLog(loadedModulePath);
    }
    else
    {
        LoaderLog("Could not resolve the loaded Portal2VR proxy DLL path.");
    }

#ifdef _DEBUG
    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
#endif

    char executablePath[MAX_PATH]{};
    char currentDirectory[MAX_PATH]{};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    GetCurrentDirectoryA(MAX_PATH, currentDirectory);
    LoaderLog("Executable:");
    LoaderLog(executablePath);
    LoaderLog("Working directory:");
    LoaderLog(currentDirectory);
    LoaderLog("Command line:");
    LoaderLog(GetCommandLineA());

    LoaderLog("Calling CommandLineToArgvW.");
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    LoaderLog(arguments ? "CommandLineToArgvW succeeded." : "CommandLineToArgvW failed.");
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

    LoaderLog(diagnosticsOnly ? "-vrdiagnostics was detected." : "-vrdiagnostics was NOT detected.");
    LoaderLog(insecureEnabled ? "-insecure was detected." : "-insecure was NOT detected.");

    // Diagnostic mode can run without -insecure because it never installs hooks.
    // Regular VR initialization continues to require -insecure.
    if (!insecureEnabled && !diagnosticsOnly)
    {
        LoaderLog("No startup flag requested; loader thread is exiting normally.");
        if (g_LoaderLog != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_LoaderLog);
            g_LoaderLog = INVALID_HANDLE_VALUE;
        }
        return 0;
    }

    // This loader thread is independent of the game's main thread. Wait here for
    // the Source game DLLs to finish loading; never block inside Game::Game and
    // never try to LoadLibrary the client/server modules ourselves.
    LoaderLog("Waiting asynchronously for all required Source modules (maximum 180 seconds).");
    LogSourceModuleWaitStatus();
    const ULONGLONG moduleDeadline = GetTickCount64() + 180000ULL;
    ULONGLONG nextModuleReport = GetTickCount64() + 1000ULL;
    while (!RequiredSourceModulesLoaded() && GetTickCount64() < moduleDeadline)
    {
        Sleep(250);
        if (GetTickCount64() >= nextModuleReport)
        {
            LogSourceModuleWaitStatus();
            nextModuleReport = GetTickCount64() + 1000ULL;
        }
    }

    if (!RequiredSourceModulesLoaded())
    {
        LoaderLog("Timed out waiting for Source modules. Game startup was left untouched; no interfaces, VR, or hooks were initialized.");
        if (g_LoaderLog != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_LoaderLog);
            g_LoaderLog = INVALID_HANDLE_VALUE;
        }
        return 0;
    }

    LoaderLog("All required Source modules are loaded.");
    LoaderLog("Calling ConstructGameWithSeh now.");
    ConstructGameWithSeh(diagnosticsOnly);
    LoaderLog("Returned from ConstructGameWithSeh.");

    if (g_LoaderLog != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_LoaderLog);
        g_LoaderLog = INVALID_HANDLE_VALUE;
    }
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
        else
            OutputDebugStringA("Portal2VR: CreateThread failed in DllMain.\r\n");
    }
    return TRUE;
}
