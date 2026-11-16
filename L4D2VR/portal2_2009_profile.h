#pragma once

// Keep these disabled until the corresponding binary reverse-engineering work
// has been completed. The current port has not validated the whole 2009 vtable
// layout or the calling convention/semantics of every detoured function.
constexpr bool PORTAL2_2009_INTERFACE_VTABLES_VERIFIED = false;
constexpr bool PORTAL2_2009_FUNCTION_HOOK_ABIS_VERIFIED = false;
