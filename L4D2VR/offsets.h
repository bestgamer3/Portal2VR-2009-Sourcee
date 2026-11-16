#pragma once
// Portal 2 July 2009 profile. RVAs below are only set to 2009 candidates when
// their byte signatures and surrounding x86 instructions were checked against
// the supplied PE32 modules. Unverified hooks intentionally remain unresolved.
#include "sigscanner.h"
#include "game.h"
#include <sstream>
#include <string>
#include <utility>


struct Offset
{
    std::string moduleName;
    int requestedOffset = 0;
    int offset = 0;
    uintptr_t address = 0;
    std::string signature;
    int sigOffset = 0;
    bool valid = false;
    std::string failure;

    Offset(std::string moduleName, int currentOffset, std::string signature, int sigOffset = 0)
    {
        this->moduleName = std::move(moduleName);
        this->requestedOffset = currentOffset;
        this->signature = std::move(signature);
        this->sigOffset = sigOffset;

        int resolvedOffset = 0;
        if (!SigScanner::VerifyOffset(this->moduleName, currentOffset, this->signature,
                                      sigOffset, resolvedOffset, failure))
        {
            return;
        }

        HMODULE module = GetModuleHandleA(this->moduleName.c_str());
        if (!module)
        {
            failure = "module unloaded during resolution";
            return;
        }

        offset = resolvedOffset;
        address = reinterpret_cast<uintptr_t>(module) + static_cast<uintptr_t>(resolvedOffset);
        valid = address != 0;
        if (!valid)
            failure = "resolved address is null";
    }
};

class Offsets
{
public:
    Offset GetFullScreenTexture =        { "client.dll", 0x1A83F0, "A1 ? ? ? ? 85 C0 75 53 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? 6A 00 6A 01 68 ? ? ? ? 68 ? ? ? ? FF D2 50 B9 ? ? ? ? E8 ? ? ? ? 80 3D ? ? ? ? ? 75 1C 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? 68 ? ? ? ? C6 05 ? ? ? ? ? FF D2 A1 ? ? ? ? C3" };
    // RTTI-derived 2009 target: CViewRender primary vtable slot 6.
    // The function ends with ret 0x10, matching two view structs and two ints.
    Offset RenderView =                  { "client.dll", 0x181120, "81 EC BC 01 00 00 53 55 8B E9 6A 00 8D 8D FC 03 00 00 E8 ? ? ? ? 8B 0D ? ? ? ? 8B 01 8B 90 00 02 00 00 FF D2" };
    Offset g_pClientMode =               { "client.dll", 0x28A600, "8B 0D ? ? ? ? 8B", 2 };
    Offset CalcViewModelView =           { "client.dll", 0x27D750, "55 8B EC 83 EC 34 53 8B D9 80 BB" };
    // RTTI-derived 2009 target: ClientModePortalNormal primary vtable slot 22.
    // The function ends with ret 0x8, matching (float, CUserCmd*).
    Offset CreateMove =                  { "client.dll", 0x95970, "6A FF E8 ? ? ? ? 83 C4 04 85 C0 74 1C 8B 10 8B C8 8B 82 60 03 00 00 FF D0 85 C0 74 0C 8B 10 8B 92 24 05 00 00 8B C8 FF E2 C2 08 00" };

    //Offset WriteUsercmdDeltaToBuffer =   { "client.dll", 0x134790, "55 8B EC 83 EC 60 0F 57 C0 8B 55 0C" }; //
    Offset WriteUsercmd =                { "client.dll", 0x1C2060, "55 8B EC A1 ? ? ? ? 83 78 30 00 53 8B 5D 0C 56 57" };
    Offset g_pppInput =                  { "client.dll", 0xD12A0, "8B 0D ? ? ? ? 8B 01 8B 50 68 FF E2", 2 };
    /*Offset AdjustEngineViewport =        { "client.dll", 0x41AD10, "55 8B EC 8B 0D ? ? ? ? 85 C9 74 17" };
    Offset IsSplitScreen =               { "client.dll", 0x1B2A60, "33 C0 83 3D ? ? ? ? ? 0F 9D C0" };*/
    Offset PrePushRenderTarget =         { "client.dll", 0xA8C80, "55 8B EC 8B C1 56 8B 75 08 8B 0E 89 08 8B 56 04 89" };

    Offset ReadUserCmd =                 { "server.dll", 0x205100, "55 8B EC 53 8B 5D 10 56 57 8B 7D 0C 53" };
    Offset ProcessUsercmds =             { "server.dll", 0x170300, "55 8B EC B8 ? ? ? ? E8 ? ? ? ? 0F 57 C0 53 56 57 B9 ? ? ? ? 8D 85 ? ? ? ? 33 DB" }; //?
    Offset CBaseEntity_entindex =        { "server.dll", 0x1A150, // verified 2009 RVA: returns the entity index from the handle serial
         "8B 41 1C 85 C0 75 01 C3 8B 0D ? ? ? ? 2B 41 58 C1 F8 04 C3 CC"};
    Offset EyePosition =                 { "server.dll", 0xF40E0, "55 8B EC 56 8B F1 8B 86 ? ? ? ? C1 E8 0B A8 01 74 05 E8 ? ? ? ? 8B 45 08 F3" };

    /*Offset GetRenderTarget =             { "materialsystem.dll", 0x2CD30, "83 79 4C 00" };
    Offset Viewport =                    { "materialsystem.dll", 0x2E010, "55 8B EC 8B 45 0C 53 8B 5D" };
    Offset GetViewport =                 { "materialsystem.dll", 0x2CAF0, "55 8B EC 8B 41 4C 8B 49 40 8D 04 C0 83 7C 81 ? ?" };*/
    Offset PushRenderTargetAndViewport = { "materialsystem.dll", 0x2D5F0, "55 8B EC 83 EC 24 8B 45 08 8B 55 10 89" };
    Offset PopRenderTargetAndViewport =  { "materialsystem.dll", 0x2CE80, "56 8B F1 83 7E 4C 00" };

    //Offset TraceFirePortalClient =       { "client.dll", 0x3E0980, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F1 6A" };
    // Firing Portals
    Offset TraceFirePortalServer =       { "server.dll", 0x400D50, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F1 6A" };
    Offset CWeaponPortalgun_FirePortal = { "server.dll", 0x401370, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F9 89 7D EC E8 ? ? ? ?" };

    //53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F1 6A 00 56 8D 4D C0 89 75 F8 E8
    //Offset DrawModelExecute =            { "engine.dll", 0xE05E0, "55 8B EC 81 EC ? ? ? ? A1 ? ? ? ? 33 C5 89 45 FC 8B 45 10 56 8B 75 08 57 8B" }; //
    Offset VGui_Paint =                  { "engine.dll", 0x115CE0, "55 8B EC E8 ? ? ? ? 8B 10 8B C8 8B 52 38" };

    Offset PlayerPortalled = { "client.dll", 0x27C9D0, "55 8B EC 83 EC 78 53 56 8B D9 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? 57 33 FF 57 FF D2" };

    // Ingame UI
    Offset DrawSelf = { "client.dll", 0x12CC90, "55 8B EC 56 8B F1 80 BE ? ? ? ? ? 0F 84 ? ? ? ? 8B 0D" };
    Offset ClipTransform = { "client.dll", 0x1DD130, "55 8B EC 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? FF D2 8B 4D" };
    /*Offset VGui_GetHudBounds = { "client.dll", 0x1CC550, "55 8B EC 51 56 8B 75 08 8B CE" };
    Offset VGui_GetPanelBounds = { "client.dll", 0x1CC350, "55 8B EC 8B 45 08 8B C8 83 E1 1F BA ? ? ? ?" };

    Offset VGUI_UpdateScreenSpaceBounds = { "client.dll", 0x1CC8C0, "55 8B EC 83 EC 14 8B 45 0C 8B 4D 10 53 8B 5D 18 56 A3 ? ? ? ? 33 C0" };
    Offset VGui_GetTrueScreenSize = { "client.dll", 0x1CBCF0, "55 8B EC 8B 45 08 8B 0D ? ? ? ? 8B 55 0C 89 08 A1 ? ? ? ? 89 02 5D C3" };*/

    Offset VGui_GetClientDLLRootPanel = { "client.dll", 0x26EDF0, "8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? FF D2 8B 04 85 ? ? ? ? 8B 48 04" };
    Offset g_pFullscreenRootPanel = { "client.dll", 0x26EE20, "A1 ? ? ? ? C3", 2 };

    // Pointer laser
    Offset CreatePingPointer = { "client.dll", 0x280660, "55 8B EC 83 EC 14 53 56 8B F1 8B 8E ? ? ? ? 57 85 C9 74 30" };
    //Offset ClientThink = { "client.dll", 0x27EA30, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ?" };
    Offset GetPortalPlayer = { "client.dll", 0x8DCA0, "55 8B EC 8B 45 08 83 F8 FF 75 10 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? FF D2" };
    Offset PrecacheParticleSystem = { "server.dll", 0x16DF40, "55 8B EC 8B 0D ? ? ? ? 8B 55 08 8B 01 8B 40 20 6A 00 6A FF" };
    Offset Precache = { "server.dll", 0x35A2C0, "E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ?" };
    //Offset GetActivePortalWeapon = { "client.dll", 0x2A8910, "8B 89 ? ? ? ? 83 F9 FF 74 1F 8B 15 ? ? ? ?" };

    Offset SetControlPoint = { "client.dll", 0x17BD30, "55 8B EC 53 56 8B 75 0C 57 8B F9 BB ? ? ? ? 84 9F ? ? ? ?" };
    Offset SetDrawOnlyForSplitScreenUser = { "client.dll", 0x17B9E0, "55 8B EC 8B 45 08 53 8B D9 3B 83 ? ? ? ? 74 55" };
    Offset StopEmission = { "client.dll", 0x17B6A0, "55 8B EC 53 8B 5D 08 57 8B F9 F6 87 ? ? ? ? ? 74 7F" };

    // Aim related
    Offset CHudCrosshair_ShouldDraw = { "client.dll", 0xE7BA0, // verified 2009 RVA: matches the early crosshair visibility test
         "57 8B F9 80 BF ? ? ? ? ? 74 04 32 C0 5F C3" };

    // VR Eyes
    Offset UTIL_Portal_FirstAlongRay = { "server.dll", 0x377200, "55 8B EC 8B 0D ? ? ? ? 85 C9 74 19 A1 ? ? ? ?" };
    Offset UTIL_IntersectRayWithPortal = { "server.dll", 0x376730, "55 8B EC 83 EC 48 56 8B 75 0C 85 F6 0F 84 ? ? ? ?" };
    Offset UTIL_Portal_AngleTransform = { "server.dll", 0x375CA0, "55 8B EC 8B 45 08 8B 4D 0C 83 EC 0C 50 51 8D 55 F4" };

    /*Offset GetScreenSize = { "vguimatsurface.dll", 0xB8C0, "55 8B EC 83 EC 08 80 B9 ? ? ? ? ? 74 1C" };
    Offset GetHudSize = { "client.dll", 0x1CBCD0, "55 8B EC 8B 55 0C 8B 0D ? ? ? ? 8B 01 8B 80 ? ? ? ? 52 8B 55 08 52 FF D0 5D C3" };

    Offset SetSizeC = { "client.dll", 0x63FB70, "55 8B EC 8B 41 04 8B 50 04 8B 45 0C 56 8B 35 ? ? ? ?" };
    Offset SetSizeE = { "engine.dll", 0x298620, "55 8B EC 8B 41 04 8B 50 04 8B 45 0C 56 8B 35 ? ? ? ?" };
    Offset SetSizeV = { "vguimatsurface.dll", 0x4B6D0, "55 8B EC 8B 41 04 8B 50 04 8B 45 0C 56 8B 35 ? ? ? ?" };

    //Offset SetBoundsC = { "client.dll", 0x63FBF0, "55 8B EC 8B 55 0C 53 56 8B F1 8B 46 04 8B 48 04 8B 45 08 57 8B 3D ? ? ? ?" };
    Offset SetBoundsE = { "engine.dll", 0x2986A0, "55 8B EC 8B 55 0C 53 56 8B F1 8B 46 04 8B 48 04 8B 45 08 57 8B 3D ? ? ? ? 8B 1F 8D 4C 31 04 52 8B 11 50 8B 02 FF D0 8B 53 08 50 8B CF FF D2" };*/
   

    /*Offset Push2DView = { "engine.dll", 0xDF980, "55 8B EC 51 53 8B D9 8B 83 ? ? ? ? 56 8D B3 ? ? ? ? 57 89 5D FC 3B 46 04 7C 09" };
    Offset Render = { "client.dll", 0x1D6800, "55 8B EC 81 EC ? ? ? ? 53 56 57 8B F9 8B 0D ? ? ? ? 89 7D F4 FF 15 ? ? ? ?" };
    Offset GetClipRect = { "vguimatsurface.dll", 0x4C700, "55 8B EC 8B 81 ? ? ? ? 8B 50 04 8B 45 14 56 8B 35 ? ? ? ? 57 8B 3E 8D 8C 0A ? ? ? ? 8B 55 10 50" };
    //Offset GetWeaponCrosshairScale = {}
    Offset GetModeHeight = { "engine.dll", 0x1F9F10, "8B 81 ? ? ? ? C3" };*/

    //Grababbles
    //Offset Weapon_ShootPosition =        { "client.dll", 0x2A8A60, "55 8B EC 8B 01 8B 90 ? ? ? ? 56 8B 75 08 56 FF D2 8B C6 5E 5D C2 04 00" };
    Offset Weapon_ShootPosition = { "server.dll", 0x1033C0, "55 8B EC 8B 01 8B 90 ? ? ? ? 56 8B 75 08 56 FF D2 8B C6 5E 5D C2 04 00" };
    Offset ComputeError = { "server.dll", 0x3C8140, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 8B F1 8B 86 ? ? ? ? 57 83 F8 FF 74 2A" };
    Offset UpdateObject = { "server.dll", 0x3CA010, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F9 8B 87 ? ? ? ? 89 BD" };
    Offset UpdateObjectVM = { "server.dll", 0x3CBB10, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F9 8B 87 ? ? ? ? 83 F8" };
    Offset RotateObject = { "server.dll", 0x3C7890, "55 8B EC 0F 57 C0 F3 0F 10 4D ? 81 EC ? ? ? ? 0F 2E C8 9F 57 8B F9 F6 C4 44 7A 12" };
    Offset EyeAngles = { "server.dll", 0x103A50, "55 8B EC 8B 81 ? ? ? ? 83 EC 60 56 57 8B 3D ? ? ? ? 83 F8 FF 74 1D" };

    // For Portal gun VFX (do we really need all three??)
    Offset MatrixBuildPerspectiveX = { "engine.dll", 0x2737E0, "55 8B EC 83 EC 08 F2 0F 10 45 ? F2 0F 59 05 ? ? ? ?" };
    Offset GetFOV = { "client.dll", 0x2772B0, "55 8B EC 51 56 8B F1 E8 ? ? ? ? D9 5D FC 8B 06 8B 90 ? ? ? ? 8B CE FF D2" };
    Offset GetDefaultFOV = { "client.dll", 0x279020, "A1 ? ? ? ? F3 0F 2C 40 ? C3" };
    Offset GetViewModelFOV = { "client.dll", 0x964C0, // verified 2009 RVA: loads the viewmodel FOV float and returns it
         "A1 ? ? ? ? D9 40 2C C3" };

    // Multiplayer
    Offset GetOwner = { "server.dll", 0xD7550, "8B 81 ? ? ? ? 83 F8 FF 74 23 8B 15 ? ? ? ?" };
    //Offset GetActiveWeapon = { "server.dll", 0xD3FD0, "8B 89 ? ? ? ? 83 F9 FF 74 1F 8B 15 ? ? ? ?" };

    bool IsReadyForHooks() const
    {
        return GetRequiredOffsetFailures().empty();
    }

    std::string GetRequiredOffsetFailures() const
    {
        std::ostringstream report;
#define CHECK_REQUIRED_OFFSET(name) \
        do { if (!(name).valid) report << #name << " [" << (name).moduleName \
            << "]: " << ((name).failure.empty() ? "unresolved" : (name).failure) << "\n"; } while (0)
        CHECK_REQUIRED_OFFSET(RenderView);
        CHECK_REQUIRED_OFFSET(CalcViewModelView);
        CHECK_REQUIRED_OFFSET(CreateMove);
        CHECK_REQUIRED_OFFSET(WriteUsercmd);
        CHECK_REQUIRED_OFFSET(PrePushRenderTarget);
        CHECK_REQUIRED_OFFSET(ReadUserCmd);
        CHECK_REQUIRED_OFFSET(ProcessUsercmds);
        CHECK_REQUIRED_OFFSET(CBaseEntity_entindex);
        CHECK_REQUIRED_OFFSET(EyePosition);
        CHECK_REQUIRED_OFFSET(PushRenderTargetAndViewport);
        CHECK_REQUIRED_OFFSET(PopRenderTargetAndViewport);
        CHECK_REQUIRED_OFFSET(TraceFirePortalServer);
        CHECK_REQUIRED_OFFSET(CWeaponPortalgun_FirePortal);
        CHECK_REQUIRED_OFFSET(VGui_Paint);
        CHECK_REQUIRED_OFFSET(PlayerPortalled);
        CHECK_REQUIRED_OFFSET(DrawSelf);
        CHECK_REQUIRED_OFFSET(ClipTransform);
        CHECK_REQUIRED_OFFSET(CreatePingPointer);
        CHECK_REQUIRED_OFFSET(GetPortalPlayer);
        CHECK_REQUIRED_OFFSET(PrecacheParticleSystem);
        CHECK_REQUIRED_OFFSET(Precache);
        CHECK_REQUIRED_OFFSET(SetDrawOnlyForSplitScreenUser);
        CHECK_REQUIRED_OFFSET(CHudCrosshair_ShouldDraw);
        CHECK_REQUIRED_OFFSET(SetControlPoint);
        CHECK_REQUIRED_OFFSET(StopEmission);
        CHECK_REQUIRED_OFFSET(UTIL_Portal_FirstAlongRay);
        CHECK_REQUIRED_OFFSET(UTIL_IntersectRayWithPortal);
        CHECK_REQUIRED_OFFSET(Weapon_ShootPosition);
        CHECK_REQUIRED_OFFSET(ComputeError);
        CHECK_REQUIRED_OFFSET(UpdateObject);
        CHECK_REQUIRED_OFFSET(UpdateObjectVM);
        CHECK_REQUIRED_OFFSET(RotateObject);
        CHECK_REQUIRED_OFFSET(EyeAngles);
        CHECK_REQUIRED_OFFSET(GetFOV);
        CHECK_REQUIRED_OFFSET(GetDefaultFOV);
        CHECK_REQUIRED_OFFSET(GetViewModelFOV);
        CHECK_REQUIRED_OFFSET(GetOwner);
#undef CHECK_REQUIRED_OFFSET
        return report.str();
    }

};