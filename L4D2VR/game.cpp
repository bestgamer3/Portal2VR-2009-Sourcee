#include "game.h"
#include <Windows.h>
#include <fstream>
#include <string>
#include "sdk.h"
#include "vr.h"
#include "hooks.h"
#include "offsets.h"
#include "sigscanner.h"
#include "portal2_2009_profile.h"
#include "runtime_audit.h"
#include <vector>
#include <ostream>

namespace
{
    void GameCtorTrace(const char* message)
    {
        OutputDebugStringA(message ? message : "(null)");
        OutputDebugStringA("\r\n");

        char tempPath[MAX_PATH]{};
        const DWORD length = GetTempPathA(MAX_PATH, tempPath);
        if (length == 0 || length >= MAX_PATH)
            return;

        char logPath[MAX_PATH]{};
        if (sprintf_s(logPath, "%sPortal2VR_2009_game_ctor.log", tempPath) < 0)
            return;

        HANDLE file = CreateFileA(logPath, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        DWORD written = 0;
        WriteFile(file, message ? message : "(null)",
            static_cast<DWORD>(lstrlenA(message ? message : "(null)")), &written, nullptr);
        WriteFile(file, "\r\n", 2, &written, nullptr);
        CloseHandle(file);
    }

    void WriteOffsetStatus(std::ostream& out, const char* name, const Offset& offset)
    {
        out << "  " << name << " [" << offset.moduleName << "] candidateRVA=0x"
            << std::hex << offset.requestedOffset << std::dec << ": ";
        if (!offset.valid)
        {
            out << "UNRESOLVED (" << offset.failure << ")\n";
            return;
        }

        out << "RESOLVED RVA=0x" << std::hex << offset.offset
            << " address=0x" << offset.address << std::dec << "\n";
    }

    bool AllRequiredModulesLoaded()
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

    void LogRequiredModuleStatus(const char* phase)
    {
        const HMODULE client = GetModuleHandleA("client.dll");
        const HMODULE engine = GetModuleHandleA("engine.dll");
        const HMODULE materialSystem = GetModuleHandleA("materialsystem.dll");
        const HMODULE server = GetModuleHandleA("server.dll");
        const HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
        const HMODULE vguiSurface = GetModuleHandleA("vguimatsurface.dll");

        char message[768]{};
        sprintf_s(message,
            "Module status (%s): client.dll=%s@%p; engine.dll=%s@%p; "
            "materialsystem.dll=%s@%p; server.dll=%s@%p; "
            "vgui2.dll=%s@%p; vguimatsurface.dll=%s@%p",
            phase ? phase : "unknown",
            client ? "loaded" : "MISSING", client,
            engine ? "loaded" : "MISSING", engine,
            materialSystem ? "loaded" : "MISSING", materialSystem,
            server ? "loaded" : "MISSING", server,
            vgui2 ? "loaded" : "MISSING", vgui2,
            vguiSurface ? "loaded" : "MISSING", vguiSurface);
        GameCtorTrace(message);
    }
}

Game::Game(bool diagnosticsOnly)
    : m_DiagnosticsOnly(diagnosticsOnly)
{
    GameCtorTrace("Game::Game entered.");
    // Some original SDK helper wrappers read g_Game during initialization.
    // Publish the partially constructed object before any VR/helper setup.
    g_Game = this;
    GameCtorTrace("Game::Game assigned g_Game.");

    std::ofstream initLog("portal2vr_2009_init.log", std::ios::out | std::ios::trunc);
    GameCtorTrace(initLog ? "Game::Game opened portal2vr_2009_init.log." :
        "Game::Game FAILED to open portal2vr_2009_init.log.");
    if (!initLog)
    {
        errorMsg("Could not create portal2vr_2009_init.log in the game working directory.");
        return;
    }

    initLog << "Portal 2 VR - July 2009 compatibility runtime audit\n";
    initLog << "Mode: " << (m_DiagnosticsOnly ? "diagnostics only" : "VR initialization") << "\n";
    initLog << "No detours are installed by the diagnostics-only mode.\n\n";
    initLog.flush();
    GameCtorTrace("Game::Game wrote and flushed initial audit header.");

    // Do not wait inside the game's early startup window. The Source client/server
    // DLLs may be loaded later (for example, when the Portal 2 game DLL is activated).
    // Returning here is intentionally fail-closed: no factory calls, signature scans,
    // VR initialization, or hooks are attempted until all required modules are present.
    LogRequiredModuleStatus("at constructor startup");
    if (!AllRequiredModulesLoaded())
    {
        initLog << "Initialization deferred safely: one or more required Source modules "
                   "were not loaded when the proxy startup thread ran.\n";
        initLog << "No wait loop, interface factory calls, signature scans, VR initialization, "
                   "or hooks were attempted.\n";
        initLog.flush();
        initLog.close();
        GameCtorTrace("Game::Game returning safely because required Source modules are not ready.");
        return;
    }
    GameCtorTrace("Game::Game all required Source modules are already loaded.");

    m_BaseClient = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
    m_BaseEngine = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine.dll"));
    m_BaseMaterialSystem = reinterpret_cast<uintptr_t>(GetModuleHandleA("materialsystem.dll"));
    m_BaseServer = reinterpret_cast<uintptr_t>(GetModuleHandleA("server.dll"));
    m_BaseVgui2 = reinterpret_cast<uintptr_t>(GetModuleHandleA("vgui2.dll"));
    GameCtorTrace("Game::Game cached Source module base addresses.");

    GameCtorTrace("Game::Game collecting loaded module audit.");
    const std::vector<RuntimeAuditModule> modules = RuntimeAuditCollectModules();
    GameCtorTrace("Game::Game RuntimeAuditCollectModules returned.");
    GameCtorTrace("Game::Game writing loaded module audit.");
    RuntimeAuditDumpModuleTable(initLog, modules);
    initLog.flush();
    GameCtorTrace("Game::Game loaded module audit written.");

    // Interface names were found in the supplied target images. Runtime pointer and
    // vtable inspection below is the first actual factory/vtable evidence.
    GameCtorTrace("Querying VClientEntityList003.");
    m_ClientEntityList = static_cast<IClientEntityList*>(GetInterface("client.dll", "VClientEntityList003"));
    GameCtorTrace("VClientEntityList003 query returned.");
    GameCtorTrace("Querying EngineTraceClient003.");
    m_EngineTrace = static_cast<IEngineTrace*>(GetInterface("engine.dll", "EngineTraceClient003"));
    GameCtorTrace("EngineTraceClient003 query returned.");
    GameCtorTrace("Querying VEngineClient013.");
    m_EngineClient = static_cast<IEngineClient*>(GetInterface("engine.dll", "VEngineClient013"));
    GameCtorTrace("VEngineClient013 query returned.");
    GameCtorTrace("Querying VMaterialSystem080.");
    m_MaterialSystem = static_cast<IMaterialSystem*>(GetInterface("materialsystem.dll", "VMaterialSystem080"));
    GameCtorTrace("VMaterialSystem080 query returned.");
    GameCtorTrace("Querying VEngineRenderView013 (client).");
    m_ClientViewRender = static_cast<IViewRender*>(GetInterface("client.dll", "VEngineRenderView013"));
    GameCtorTrace("VEngineRenderView013 (client) query returned.");
    GameCtorTrace("Querying VEngineRenderView013 (engine).");
    m_EngineViewRender = static_cast<IViewRender*>(GetInterface("engine.dll", "VEngineRenderView013"));
    GameCtorTrace("VEngineRenderView013 (engine) query returned.");
    GameCtorTrace("Querying VModelInfoClient004.");
    m_ModelInfo = static_cast<IModelInfo*>(GetInterface("engine.dll", "VModelInfoClient004"));
    GameCtorTrace("VModelInfoClient004 query returned.");
    GameCtorTrace("Querying VEngineModel016.");
    m_ModelRender = static_cast<IModelRender*>(GetInterface("engine.dll", "VEngineModel016"));
    GameCtorTrace("VEngineModel016 query returned.");
    GameCtorTrace("Querying VGUI_InputInternal001.");
    m_VguiInput = static_cast<IInput*>(GetInterface("vgui2.dll", "VGUI_InputInternal001"));
    GameCtorTrace("VGUI_InputInternal001 query returned.");
    GameCtorTrace("Querying VGUI_Surface031.");
    m_VguiSurface = static_cast<ISurface*>(GetInterface("vguimatsurface.dll", "VGUI_Surface031"));
    GameCtorTrace("VGUI_Surface031 query returned.");
    GameCtorTrace("Game::Game Source interface factory queries completed.");

    initLog << "\nCreateInterface factory results:\n";
    initLog << "  VClientEntityList003: " << (m_ClientEntityList ? "created" : "not created") << "\n";
    initLog << "  EngineTraceClient003: " << (m_EngineTrace ? "created" : "not created") << "\n";
    initLog << "  VEngineClient013: " << (m_EngineClient ? "created" : "not created") << "\n";
    initLog << "  VMaterialSystem080: " << (m_MaterialSystem ? "created" : "not created") << "\n";
    initLog << "  VEngineRenderView013 (client): " << (m_ClientViewRender ? "created" : "not created") << "\n";
    initLog << "  VEngineRenderView013 (engine): " << (m_EngineViewRender ? "created" : "not created") << "\n";
    initLog << "  VModelInfoClient004: " << (m_ModelInfo ? "created" : "not created") << "\n";
    initLog << "  VEngineModel016: " << (m_ModelRender ? "created" : "not created") << "\n";
    initLog << "  VGUI_InputInternal001: " << (m_VguiInput ? "created" : "not created") << "\n";
    initLog << "  VGUI_Surface031: " << (m_VguiSurface ? "created" : "not created") << "\n";

    if (m_DiagnosticsOnly)
    {
        GameCtorTrace("Game::Game entering interface/vtable audit.");
        RuntimeAuditDumpInterface(initLog, "VClientEntityList003", m_ClientEntityList, 32, modules);
        RuntimeAuditDumpInterface(initLog, "EngineTraceClient003", m_EngineTrace, 16, modules);
        RuntimeAuditDumpInterface(initLog, "VEngineClient013", m_EngineClient, 160, modules);
        RuntimeAuditDumpInterface(initLog, "VMaterialSystem080", m_MaterialSystem, 180, modules);
        RuntimeAuditDumpInterface(initLog, "VEngineRenderView013 (client)", m_ClientViewRender, 64, modules);
        RuntimeAuditDumpInterface(initLog, "VEngineRenderView013 (engine)", m_EngineViewRender, 64, modules);
        RuntimeAuditDumpInterface(initLog, "VModelInfoClient004", m_ModelInfo, 64, modules);
        RuntimeAuditDumpInterface(initLog, "VEngineModel016", m_ModelRender, 96, modules);
        RuntimeAuditDumpInterface(initLog, "VGUI_InputInternal001", m_VguiInput, 128, modules);
        RuntimeAuditDumpInterface(initLog, "VGUI_Surface031", m_VguiSurface, 180, modules);

        initLog << "\nSignature scan skipped in this safe diagnostics pass.\n";
        initLog << "The hook verification gates remain disabled; no Offsets object, hooks, or VR session was created.\n";
        initLog << "\nDiagnostics complete. VR_Init was not called and no hooks were installed.\n";
        initLog.flush();
        initLog.close();
        GameCtorTrace("Game::Game safe diagnostics complete; returning without signature scanning or VR_Init.");
        return;
    }

    initLog.flush();
    GameCtorTrace("Game::Game wrote interface factory results.");

    // The July 2009 client.dll does not expose VEngineRenderView013. The engine
    // factory does expose it, and source review shows the client-side pointer is
    // not used by the current VR implementation. Treat it as optional.
    if (!m_ClientViewRender)
    {
        initLog << "  Note: VEngineRenderView013 from client.dll is unavailable; "
                   "the engine.dll instance was created and remains the required render interface.\n";
        GameCtorTrace("Optional client VEngineRenderView013 is unavailable; continuing.");
    }

    if (!m_ClientEntityList || !m_EngineTrace || !m_EngineClient || !m_MaterialSystem ||
        !m_EngineViewRender || !m_ModelInfo || !m_ModelRender ||
        !m_VguiInput || !m_VguiSurface)
    {
        initLog << "\nInitialization stopped: one or more required Source interfaces were not created.\n";
        initLog.flush();
        initLog.close();
        GameCtorTrace("Game::Game stopping because a required Source interface was not created.");
        errorMsg("A required Source interface could not be created.\nSee portal2vr_2009_init.log.");
        return;
    }

    // Do not run signature scanning or attempt VR hooks until the 2009 interfaces
    // and hook ABIs are explicitly verified. This is intentionally fail-closed.
    if (!PORTAL2_2009_INTERFACE_VTABLES_VERIFIED || !PORTAL2_2009_FUNCTION_HOOK_ABIS_VERIFIED)
    {
        initLog << "\nInitialization stopped safely: the 2009 vtable/calling-convention verification gates are disabled.\n";
        initLog << "Signature scanning, VR initialization, and hooks were skipped.\n";
        initLog.flush();
        initLog.close();
        GameCtorTrace("Game::Game stopped safely before signature scanning: verification gates are disabled.");
        return;
    }

    GameCtorTrace("Game::Game constructing Offsets for verified hook setup.");
    m_Offsets = new Offsets();
    GameCtorTrace("Game::Game Offsets construction returned.");
    const std::string unresolved = m_Offsets->GetRequiredOffsetFailures();
    if (!unresolved.empty())
    {
        initLog << "\nInitialization stopped: required 2009 function signatures remain unresolved.\n";
        initLog << unresolved;
        initLog << "\nNo VR hooks were installed.\n";
        initLog.close();
        errorMsg("The 2009 build's function hooks are not fully ported yet.\n"
                 "No hooks were installed. See portal2vr_2009_init.log.");
        return;
    }

    if (!PORTAL2_2009_INTERFACE_VTABLES_VERIFIED || !PORTAL2_2009_FUNCTION_HOOK_ABIS_VERIFIED)
    {
        initLog << "\nInitialization stopped: the 2009 vtable/calling-convention verification gates are disabled.\n";
        initLog << "No hooks were installed.\n";
        initLog.close();
        errorMsg("The 2009 interface vtables and function-hook ABIs are not fully verified yet.\n"
                 "No hooks were installed. See portal2vr_2009_init.log.");
        return;
    }

    m_VR = new VR(this);
    if (!m_VR || !m_VR->m_IsInitialized)
    {
        initLog << "\nInitialization stopped: OpenVR initialization failed.\n";
        initLog.close();
        errorMsg("OpenVR initialization did not complete. No game hooks were installed.");
        return;
    }

    m_Hooks = new Hooks(this);
    if (!m_Hooks || !m_Hooks->IsReady())
    {
        initLog << "\nInitialization stopped: the complete hook set could not be created/enabled.\n";
        initLog.close();
        errorMsg("Portal 2 VR could not install its complete hook set.\nSee portal2vr_2009_init.log.");
        return;
    }

    m_Initialized = true;
    initLog << "All required signatures resolved and hooks initialized.\n";
    initLog.close();
}

void* Game::GetInterface(const char* dllname, const char* interfacename)
{
    HMODULE module = GetModuleHandleA(dllname);
    if (!module)
        return nullptr;

    auto createInterface = reinterpret_cast<tCreateInterface>(
        GetProcAddress(module, "CreateInterface"));
    if (!createInterface)
        return nullptr;

    int returnCode = -1;
    void* createdInterface = createInterface(interfacename, &returnCode);
    return returnCode == 0 ? createdInterface : nullptr;
}

void Game::errorMsg(const char* msg)
{
    MessageBoxA(nullptr, msg, "Portal 2 VR (2009 compatibility port)", MB_ICONERROR | MB_OK);
}

CBaseEntity* Game::GetClientEntity(int entityIndex)
{
    return m_ClientEntityList ? static_cast<CBaseEntity*>(m_ClientEntityList->GetClientEntity(entityIndex)) : nullptr;
}

char* Game::getNetworkName(uintptr_t* entity)
{
    if (!entity)
        return nullptr;

    uintptr_t* IClientNetworkableVtable = reinterpret_cast<uintptr_t*>(*(entity + 0x8));
    if (!IClientNetworkableVtable)
        return nullptr;
    uintptr_t* GetClientClassPtr = reinterpret_cast<uintptr_t*>(*(IClientNetworkableVtable + 0x8));
    if (!GetClientClassPtr)
        return nullptr;
    uintptr_t* ClientClassPtr = reinterpret_cast<uintptr_t*>(*(GetClientClassPtr + 0x1));
    if (!ClientClassPtr)
        return nullptr;
    return reinterpret_cast<char*>(*(ClientClassPtr + 0x8));
}

void Game::ClientCmd(const char* szCmdString)
{
    if (m_EngineClient && szCmdString)
        m_EngineClient->ClientCmd(szCmdString);
}

void Game::ClientCmd_Unrestricted(const char* szCmdString)
{
    if (m_EngineClient && szCmdString)
        m_EngineClient->ClientCmd_Unrestricted(szCmdString);
}
