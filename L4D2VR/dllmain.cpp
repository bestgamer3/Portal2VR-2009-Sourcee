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
}

// Keep the SEH boundary in a function that has no C++ objects requiring unwinding.
static void ConstructGameForDiagnostics(bool diagnosticsOnly)
{
    g_Game = new Game(diagnosticsOnly);
}

static void ConstructGameWithSeh(bool diagnosticsOnly)
{
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
    (void)parameter;
    OpenLoaderLog();

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

    LoaderLog("About to construct Game; diagnostics log should be created in the working directory.");
    ConstructGameWithSeh(diagnosticsOnly);
    LoaderLog("Game constructor returned.");

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
