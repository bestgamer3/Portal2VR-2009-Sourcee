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
}

Game::Game(bool diagnosticsOnly, bool theaterOnly)
    : m_DiagnosticsOnly(diagnosticsOnly), m_TheaterOnly(theaterOnly)
{
    // Some original SDK helper wrappers read g_Game during initialization.
    // Publish the partially constructed object before any VR/helper setup.
    g_Game = this;

    std::ofstream initLog("portal2vr_2009_init.log", std::ios::out | std::ios::trunc);
    if (!initLog)
    {
        errorMsg("Could not create portal2vr_2009_init.log in the game working directory.");
        return;
    }

    initLog << "Portal 2 VR - July 2009 compatibility runtime audit\n";
    initLog << "Mode: " << (m_DiagnosticsOnly ? "diagnostics only" : "VR initialization") << "\n";
    initLog << "No detours are installed by the diagnostics-only mode.\n\n";

    // Use one global deadline. The previous per-module wait could take twelve minutes.
    const ULONGLONG deadline = GetTickCount64() + 180000ULL;
    while (!AllRequiredModulesLoaded() && GetTickCount64() < deadline)
        Sleep(250);

    if (!AllRequiredModulesLoaded())
    {
        initLog << "Initialization stopped: at least one required Source module did not load within 180 seconds.\n";
        initLog.close();
        if (!m_DiagnosticsOnly)
            errorMsg("Timed out waiting for required Portal 2 modules.\nSee portal2vr_2009_init.log.");
        return;
    }

    m_BaseClient = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
    m_BaseEngine = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine.dll"));
    m_BaseMaterialSystem = reinterpret_cast<uintptr_t>(GetModuleHandleA("materialsystem.dll"));
    m_BaseServer = reinterpret_cast<uintptr_t>(GetModuleHandleA("server.dll"));
    m_BaseVgui2 = reinterpret_cast<uintptr_t>(GetModuleHandleA("vgui2.dll"));

    const std::vector<RuntimeAuditModule> modules = RuntimeAuditCollectModules();
    RuntimeAuditDumpModuleTable(initLog, modules);

    // Interface names were found in the supplied target images. Runtime pointer and
    // vtable inspection below is the first actual factory/vtable evidence.
    m_ClientEntityList = static_cast<IClientEntityList*>(GetInterface("client.dll", "VClientEntityList003"));
    m_EngineTrace = static_cast<IEngineTrace*>(GetInterface("engine.dll", "EngineTraceClient003"));
    m_EngineClient = static_cast<IEngineClient*>(GetInterface("engine.dll", "VEngineClient013"));
    m_MaterialSystem = static_cast<IMaterialSystem*>(GetInterface("materialsystem.dll", "VMaterialSystem080"));
    m_ClientViewRender = static_cast<IViewRender*>(GetInterface("client.dll", "VEngineRenderView013"));
    m_EngineViewRender = static_cast<IViewRender*>(GetInterface("engine.dll", "VEngineRenderView013"));
    m_ModelInfo = static_cast<IModelInfo*>(GetInterface("engine.dll", "VModelInfoClient004"));
    m_ModelRender = static_cast<IModelRender*>(GetInterface("engine.dll", "VEngineModel016"));
    m_VguiInput = static_cast<IInput*>(GetInterface("vgui2.dll", "VGUI_InputInternal001"));
    m_VguiSurface = static_cast<ISurface*>(GetInterface("vguimatsurface.dll", "VGUI_Surface031"));

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

        if (m_TheaterOnly)
    {
        initLog << "\\nStarting safe SteamVR theater mode.\\n";
        initLog << "No Source detours or gameplay hooks will be installed.\\n";

        m_VR = new VR(this);
        if (!m_VR || !m_VR->m_IsInitialized)
        {
            initLog << "\\nTheater-mode OpenVR initialization failed.\\n";
            initLog.close();
            errorMsg("SteamVR theater mode could not initialize.\\nSee portal2vr_2009_init.log.");
            return;
        }

        m_Initialized = true;
        initLog << "\\nSteamVR theater mode initialized. The desktop game image will be shown on a head-locked VR overlay.\\n";
        initLog << "This mode does not enable stereoscopic 6DOF gameplay or motion-controller gameplay hooks.\\n";
        initLog.close();
        return;
    }

    m_Offsets = new Offsets();
        initLog << "\nRequired hook target signature results (all 37 targets):\n";
        WriteOffsetStatus(initLog, "RenderView", m_Offsets->RenderView);
        WriteOffsetStatus(initLog, "CalcViewModelView", m_Offsets->CalcViewModelView);
        WriteOffsetStatus(initLog, "CreateMove", m_Offsets->CreateMove);
        WriteOffsetStatus(initLog, "WriteUsercmd", m_Offsets->WriteUsercmd);
        WriteOffsetStatus(initLog, "PrePushRenderTarget", m_Offsets->PrePushRenderTarget);
        WriteOffsetStatus(initLog, "ReadUserCmd", m_Offsets->ReadUserCmd);
        WriteOffsetStatus(initLog, "ProcessUsercmds", m_Offsets->ProcessUsercmds);
        WriteOffsetStatus(initLog, "CBaseEntity_entindex", m_Offsets->CBaseEntity_entindex);
        WriteOffsetStatus(initLog, "EyePosition", m_Offsets->EyePosition);
        WriteOffsetStatus(initLog, "PushRenderTargetAndViewport", m_Offsets->PushRenderTargetAndViewport);
        WriteOffsetStatus(initLog, "PopRenderTargetAndViewport", m_Offsets->PopRenderTargetAndViewport);
        WriteOffsetStatus(initLog, "TraceFirePortalServer", m_Offsets->TraceFirePortalServer);
        WriteOffsetStatus(initLog, "CWeaponPortalgun_FirePortal", m_Offsets->CWeaponPortalgun_FirePortal);
        WriteOffsetStatus(initLog, "VGui_Paint", m_Offsets->VGui_Paint);
        WriteOffsetStatus(initLog, "PlayerPortalled", m_Offsets->PlayerPortalled);
        WriteOffsetStatus(initLog, "DrawSelf", m_Offsets->DrawSelf);
        WriteOffsetStatus(initLog, "ClipTransform", m_Offsets->ClipTransform);
        WriteOffsetStatus(initLog, "CreatePingPointer", m_Offsets->CreatePingPointer);
        WriteOffsetStatus(initLog, "GetPortalPlayer", m_Offsets->GetPortalPlayer);
        WriteOffsetStatus(initLog, "PrecacheParticleSystem", m_Offsets->PrecacheParticleSystem);
        WriteOffsetStatus(initLog, "Precache", m_Offsets->Precache);
        WriteOffsetStatus(initLog, "SetDrawOnlyForSplitScreenUser", m_Offsets->SetDrawOnlyForSplitScreenUser);
        WriteOffsetStatus(initLog, "CHudCrosshair_ShouldDraw", m_Offsets->CHudCrosshair_ShouldDraw);
        WriteOffsetStatus(initLog, "SetControlPoint", m_Offsets->SetControlPoint);
        WriteOffsetStatus(initLog, "StopEmission", m_Offsets->StopEmission);
        WriteOffsetStatus(initLog, "UTIL_Portal_FirstAlongRay", m_Offsets->UTIL_Portal_FirstAlongRay);
        WriteOffsetStatus(initLog, "UTIL_IntersectRayWithPortal", m_Offsets->UTIL_IntersectRayWithPortal);
        WriteOffsetStatus(initLog, "Weapon_ShootPosition", m_Offsets->Weapon_ShootPosition);
        WriteOffsetStatus(initLog, "ComputeError", m_Offsets->ComputeError);
        WriteOffsetStatus(initLog, "UpdateObject", m_Offsets->UpdateObject);
        WriteOffsetStatus(initLog, "UpdateObjectVM", m_Offsets->UpdateObjectVM);
        WriteOffsetStatus(initLog, "RotateObject", m_Offsets->RotateObject);
        WriteOffsetStatus(initLog, "EyeAngles", m_Offsets->EyeAngles);
        WriteOffsetStatus(initLog, "GetFOV", m_Offsets->GetFOV);
        WriteOffsetStatus(initLog, "GetDefaultFOV", m_Offsets->GetDefaultFOV);
        WriteOffsetStatus(initLog, "GetViewModelFOV", m_Offsets->GetViewModelFOV);
        WriteOffsetStatus(initLog, "GetOwner", m_Offsets->GetOwner);
        initLog << "\n";

        initLog << "\nRequired hook targets still unresolved:\n";
        const std::string unresolved = m_Offsets->GetRequiredOffsetFailures();
        initLog << (unresolved.empty() ? "  none\n" : unresolved);
        initLog << "\nDiagnostics complete. VR_Init was not called and no hooks were installed.\n";
        initLog.close();
        return;
    }

    if (!m_ClientEntityList || !m_EngineTrace || !m_EngineClient || !m_MaterialSystem ||
        !m_ClientViewRender || !m_EngineViewRender || !m_ModelInfo || !m_ModelRender ||
        !m_VguiInput || !m_VguiSurface)
    {
        initLog << "\nInitialization stopped: one or more required interfaces were not created.\n";
        initLog.close();
        errorMsg("One or more required Source interfaces could not be created.\nSee portal2vr_2009_init.log.");
        return;
    }

    m_Offsets = new Offsets();
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
