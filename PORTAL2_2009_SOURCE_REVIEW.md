# Source review notes — 16 November 2026

This review is based on the user-supplied `L4D2VR.zip` source and the supplied July 2009 PE32 binaries. Changes are source-level safety/diagnostic improvements, not proof of a playable port.

## Important runtime path finding

A source-wide call-site search found that `VR::Update()` was defined but never called. That method performs the once-per-frame OpenVR compositor submission (`SubmitVRTextures`), pose acquisition (`WaitGetPoses` inside `UpdatePosesAndActions`), tracking updates, and controller/menu input processing. The review adds a call from `Hooks::dRenderView` once per intercepted render entry, after a ready-state check and before left/right eye renders. Runtime frame order still needs validation in the target game.

## Packet parsing fix

`dWriteUsercmd` appends a `-2` marker plus controller position/angles when VR is enabled. Previously `dReadUsercmd` returned immediately when the server player index was outside the local 24-entry VR-player array. That left the appended bits unread. The review now reads the marker and, when present, consumes the controller payload into temporary values; it stores them only when the player index is valid. This preserves the `bf_read` cursor for subsequent commands.

## Additional safeguards

- `dRenderView` forwards directly to the original function until `Game` and `VR` initialization have completed.
- Per-player array accesses use a common non-negative, size-checked predicate.
- `dProcessUsercmds` saves/restores the prior current-player index around the original call.
- `dWeapon_ShootPosition` checks that the original function returned a valid pointer before writing through it.
- OpenVR projection values are checked for finite, non-zero values before division.
- Overlay interface acquisition and overlay creation are checked.
- The action manifest path, individual action handles, and action-set handle are checked; failure aborts VR initialization before detours are installed.
- Application manifest registration reports interface/API errors without dereferencing a null interface.
- The unnecessary material render-context call previously used only by commented-out code was removed.
- `CViewSetup::STR()` returns an owned string and uses a type-correct format string.

## Safety gate

`PORTAL2_2009_INTERFACE_VTABLES_VERIFIED` and `PORTAL2_2009_FUNCTION_HOOK_ABIS_VERIFIED` remain `false`. The 37-target gate has 5 exact static candidates and 32 unresolved targets. No attempt was made to enable hooks by guessing addresses.

## Validation

The package passes:
- `tools/check_hook_registration.py`
- `tools/check_build_layout.py`
- `tools/check_port_state.py`
- Visual Studio project/filter XML parsing
- PE32 executable-section signature scan against the supplied binaries
- MSVC RTTI/vtable static analysis

A Windows/MSVC rebuild and live SteamVR test have not been run for this source review revision.
