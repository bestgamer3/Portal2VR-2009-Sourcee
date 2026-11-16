# Portal 2 2009 source build-log fix

The supplied Visual Studio build log contains one blocking compiler error:

`hooks.cpp`: cannot convert `const QAngle` to `QAngle&` in `Hooks::dEyeAngles`.

Cause: the `vrPlayer` local was bound as `const auto&`, making `vrPlayer.controllerAngle` a const reference, while the detoured `EyeAngles` function intentionally matches the original `QAngle&` return type.

Fix applied: changed only the `vrPlayer` local inside `Hooks::dEyeAngles` to `auto&`. Other read-only uses elsewhere remain const. This keeps the function's return type and detour ABI unchanged and returns the mutable `controllerAngle` member from the mutable `m_PlayersVRInfo` entry.

The remaining messages in the supplied log are warnings. The log ends before a final MSBuild success/failure summary, so this patch is not evidence that the complete project now compiles; rebuild in Visual Studio to confirm.
