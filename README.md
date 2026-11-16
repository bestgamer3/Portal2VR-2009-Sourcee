# Portal 2 VR — July 2009 source review / development build

**Target:** the supplied July 2009 Portal 2 PE32/i386 build, using SteamVR/OpenVR and Oculus Rift CV1.  
**Toolchain:** Visual Studio 2026 with MSVC v145, Windows 10/11 SDK, x86 target.  
**Status:** integrated OpenVR + D3D9 menu-overlay bring-up. The Portal 2 main-menu backbuffer is routed to a SteamVR overlay through DXVK `PresentEx`. The July 2009 `RenderView` stereo detour and gameplay/controller hooks remain disabled until their x86 ABIs are verified. This is not yet a complete playable VR mod.

The latest source package includes the full `L4D2VR` project, OpenVR headers/import library/runtime DLL, PowerShell dependency/build helpers, static audit tools, and the source review notes. DXVK's custom D3D9-VR branch and MinHook source are bootstrapped by the script.

## Build

1. Extract this ZIP to a writable folder.
2. Install Visual Studio 2026 with **Desktop development with C++**, MSVC **v145 x86/x64 tools**, and a Windows 10/11 SDK. Install Git for Windows.
3. Open PowerShell in the extracted folder and run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build-win32-release.ps1
```

The script checks/fetches DXVK and MinHook dependencies, validates project-listed source files, locates MSBuild and an installed Windows SDK, builds **Release | x86**, saves `portal2vr_2009_release_build.log`, and stages OpenVR plus the VR runtime assets beside the output.

Expected output:

```text
L4D2VR\bin\Release\d3d9.dll
```

For Debug:

```powershell
.\build-win32-release.ps1 -Configuration Debug
```

For a clean rebuild:

```powershell
.\build-win32-release.ps1 -Rebuild
```

The solution configuration is `x86`, mapped to project `Win32`. Do not pass `Platform=Win32` to MSBuild at the solution level.

## Runtime diagnostics and VR menu bring-up

The source supports `-vrdiagnostics`. With the D3D9 proxy deployed in the correct game `bin` directory and the runtime `VR` folder in the game working directory, launch the 2009 game with `-vrdiagnostics`. The module waits for the Source DLLs, reports `CreateInterface` factory results, performs read-only vtable pointer inspection, and logs the 37 target-signature checks in:

```text
portal2vr_2009_init.log
```

A normal `-insecure` launch now initializes OpenVR as a scene application before checking the gameplay-hook ABI gate. It also initializes the D3D9/Vulkan bridge and creates the main-menu overlay without installing a Source detour. DXVK's `PresentEx` path refreshes the current backbuffer before Present, waits for the D3D9/Vulkan work to complete, and calls `VR::Update()` once per presented frame. The menu image is therefore independent of the unverified `RenderView` detour.

The `RenderView` detour still contains the two-eye render path, but it remains disabled while `PORTAL2_2009_FUNCTION_HOOK_ABIS_VERIFIED` is `false`. In this branch, the expected first milestone is the Portal 2 main menu as a floating SteamVR overlay in the Rift; stereoscopic gameplay and controllers are not enabled yet.

## What changed in this source review

- Moved the single per-frame `VR::Update()` call to DXVK `PresentEx`, so it runs for the main menu even when the Source `RenderView` detour is disabled. The stereo hook only marks the rendered eye pair ready for the following Present.
- Added a startup-ready guard to the render detour, player-index bounds checks, null checks for player/entity and shoot-position results, and restoration of the active user-command player index after the original network processing call.
- Corrected user-command parsing so the custom controller payload is consumed when its marker is present even when the received entity index cannot be mapped into the local 24-player table. This prevents leaving unread custom bytes in the bit buffer.
- Added validation for SteamVR projection values, the overlay interface/creation result, action-manifest path and action handles, and application-manifest registration.
- Invalidated render targets only when transitioning between menu and gameplay, avoiding repeated texture allocation from the new per-frame update call.
- Removed an unused material render-context call from VR overlay setup.
- Initialized OpenVR and the D3D9 menu-overlay bridge before the fail-closed gameplay-hook ABI gate. This allows SteamVR to recognize the scene app and display the actual game backbuffer without installing any Source detour.
- Kept the original fail-closed verification gates. No unverified 2009 function hook is force-enabled.
- Updated the DXVK helper patch to capture the current menu backbuffer before swapchain Present, refresh the OpenVR texture metadata, null-check the Present bridge, and release the temporary backbuffer COM reference.
- Kept the `CViewSetup::STR()` helper safe: it returns an owned `std::string` and uses matching format specifiers.

## Current 2009 binary evidence

Five of the 37 required function/global targets currently have unique static candidates in the supplied binaries:

| Target | Module | RVA | Evidence |
|---|---|---:|---|
| `RenderView` | `client.dll` | `0x181120` | `CViewRender` primary RTTI vtable slot 6; unique byte signature; ends with `ret 0x10` |
| `CreateMove` | `client.dll` | `0x95970` | `ClientModePortalNormal` primary RTTI vtable slot 22; unique byte signature; ends with `ret 0x8` |
| `CBaseEntity_entindex` | `server.dll` | `0x1A150` | `CPortal_Player` primary RTTI vtable slot 46 and unique signature |
| `CHudCrosshair_ShouldDraw` | `client.dll` | `0xE7BA0` | Unique executable-section signature |
| `GetViewModelFOV` | `client.dll` | `0x964C0` | `ClientModePortalNormal` primary RTTI vtable slot 34 and unique signature |

The other 32 target candidates remain unresolved. A unique byte match or RTTI slot does not alone validate a function's full x86 ABI, object layout, side effects, or semantic equivalence. The two gates in `L4D2VR/portal2_2009_profile.h` therefore remain `false`.

## Not yet verified / not playable

- Remaining 32 function/global targets and all detour calling conventions.
- Runtime interface method order and SDK structure compatibility.
- Actual stereo projection and eye texture flow in the 2009 renderer.
- Head/controller tracking in live gameplay.
- Portal traversal, portal-gun aim, object grab/throw, HUD, menus, save/load, and shutdown.
- End-to-end Oculus Rift CV1 + SteamVR gameplay.

The prior user-provided build log showed that an earlier source revision compiled to `d3d9.dll`. This package contains additional source changes and must be rebuilt in Visual Studio before its updated DLL is used. I cannot run the Windows/MSVC or SteamVR runtime in this environment.

## If the build fails

Send the full `portal2vr_2009_release_build.log` and the first compiler/linker error block. Warnings alone do not necessarily mean the build failed; check whether MSBuild ends with the `l4d2vr.vcxproj -> ...\d3d9.dll` line.
