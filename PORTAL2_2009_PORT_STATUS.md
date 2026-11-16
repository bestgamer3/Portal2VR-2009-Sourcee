# Portal 2 VR — July 2009 port status

## Current state

The uploaded current `L4D2VR` source has been reviewed and updated. The Visual Studio project targets **v145 / Win32** for the user's Visual Studio 2026 installation. Static project checks pass. The user's earlier log demonstrated that the previous source revision compiled successfully; **this newer source review package has not yet been compiled by MSVC**.

**This is not playable VR yet.** The source intentionally blocks normal VR startup until the target functions and interfaces are verified. Both constants in `L4D2VR/portal2_2009_profile.h` remain `false`:

- `PORTAL2_2009_INTERFACE_VTABLES_VERIFIED`
- `PORTAL2_2009_FUNCTION_HOOK_ABIS_VERIFIED`

## Source changes in this review

- Connected `VR::Update()` to the active `RenderView` detour. The previous source defined compositor submission, `WaitGetPoses`, tracking, and action updates, but a source-wide call-site audit found no invocation of `VR::Update()`.
- Added a render-hook readiness guard so the original renderer is used until `Game` and `VR` initialization are complete.
- Added bounds-safe handling for per-player VR state in server command, shoot-position, portal trace, and eye-angle detours.
- Changed `dReadUsercmd` to always consume a recognized custom controller payload, even when its entity index is out of range; it now discards the parsed payload rather than leaving bits unread.
- Restored the previous `m_CurrentUsercmdID` after the original `ProcessUsercmds` call so the temporary player association does not leak beyond its scope.
- Added null checks for the original shoot-position return pointer.
- Added SteamVR projection, overlay, action-manifest, action-handle, and application-manifest error checks.
- Changed render-target invalidation to occur only on menu/game state transitions, preventing per-frame target recreation now that `VR::Update()` is called from the renderer.
- Removed an unused render-context access from overlay setup.
- `CViewSetup::STR()` returns an owned `std::string` and uses correct format specifiers, avoiding the previous dangling pointer and format mismatch.
- Diagnostics now print all 37 required target statuses with candidate RVA, module, resolved address, and failure reason.

## Verified static candidates

| Target | Module | RVA | Evidence |
|---|---|---:|---|
| `RenderView` | `client.dll` | `0x181120` | `CViewRender` primary vtable slot 6; unique signature; `ret 0x10` |
| `CreateMove` | `client.dll` | `0x95970` | `ClientModePortalNormal` primary vtable slot 22; unique signature; `ret 0x8` |
| `CBaseEntity_entindex` | `server.dll` | `0x1A150` | `CPortal_Player` primary vtable slot 46; unique signature |
| `CHudCrosshair_ShouldDraw` | `client.dll` | `0xE7BA0` | Unique signature in executable section |
| `GetViewModelFOV` | `client.dll` | `0x964C0` | `ClientModePortalNormal` primary vtable slot 34; unique signature |

**32 of the 37 required offsets remain unresolved.** The 5 static candidates do not equal 5 fully validated hooks; full ABI/object-layout validation remains incomplete.

## Binary identity

The supplied game files are PE32/i386 modules with these timestamps (UTC):

| File | Timestamp |
|---|---|
| `hl2.exe` | 2009-07-16 01:25:13 |
| `Client.dll` | 2009-07-28 22:11:58 |
| `engine.dll` | 2009-07-28 22:06:37 |
| `Server.dll` | 2009-07-29 00:24:36 |
| `MaterialSystem.dll` | 2009-07-28 22:11:32 |
| `vgui2.dll` | 2009-07-28 01:16:23 |
| `vguimatsurface.dll` | 2009-07-28 01:16:23 |

## Static checks run on this package

- `python tools/check_hook_registration.py` — 28 created hooks, 28 enable registrations.
- `python tools/check_build_layout.py` — passed.
- `python tools/check_port_state.py` — passed; 37 targets remain fail-closed and both safety gates remain false.
- Visual Studio project/filter XML parse — passed.
- PE32 signature scan — 5 unique exact matches among the 37 required targets.
- MSVC RTTI/vtable scan — identifies `RenderView`, `CreateMove`, `GetViewModelFOV`, and the entity-index vtable candidate.

These are static checks only. No live hook has been executed against the 2009 executable in this environment.

## Next validation step

Rebuild this source package with VS2026 / v145 and run the target executable with `-vrdiagnostics`. Send `portal2vr_2009_init.log`. The log will show which interface factories actually return objects in the running 2009 process, their read-only vtable entries, and the 37 function-signature status results. That runtime evidence is needed before additional targets can be promoted.
