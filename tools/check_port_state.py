#!/usr/bin/env python3
"""Static consistency checks for the Portal 2 July 2009 diagnostic port."""
from __future__ import annotations
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Hook creation and enable registration must stay in sync.
source = (ROOT / "L4D2VR" / "hooks.cpp").read_text(encoding="utf-8", errors="replace")
source_no_comments = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
source_no_comments = re.sub(r"//[^\n]*", "", source_no_comments)
created = set(re.findall(r"\b(hk\w+)\.createHook\s*\(", source_no_comments))
enabled = set(re.findall(r"\bENABLE_HOOK\s*\(\s*(hk\w+)\s*\)", source_no_comments))
if created != enabled:
    raise SystemExit(f"Hook mismatch: created-but-not-enabled={sorted(created-enabled)}; enabled-but-not-created={sorted(enabled-created)}")

# Confirm 37 named targets are guarded (exclude the CHECK_REQUIRED_OFFSET macro parameter).
offsets = (ROOT / "L4D2VR" / "offsets.h").read_text(encoding="utf-8", errors="replace")
block = offsets.split("std::string GetRequiredOffsetFailures() const", 1)[1].split("#undef CHECK_REQUIRED_OFFSET", 1)[0]
required = [name for name in re.findall(r"CHECK_REQUIRED_OFFSET\((\w+)\)", block) if name != "name"]
if len(required) != 37 or len(set(required)) != 37:
    raise SystemExit(f"Expected 37 unique guarded targets, found {len(required)} entries / {len(set(required))} unique")

# Check the two new RTTI-derived function candidates remain on the supplied 2009 RVAs.
if not re.search(r'Offset\s+RenderView\s*=\s*\{\s*"client\.dll"\s*,\s*0x181120', offsets):
    raise SystemExit("2009 CViewRender::RenderView candidate was changed unexpectedly")
if not re.search(r'Offset\s+CreateMove\s*=\s*\{\s*"client\.dll"\s*,\s*0x95970', offsets):
    raise SystemExit("2009 ClientModePortalNormal::CreateMove candidate was changed unexpectedly")

# Both ABI gates must remain closed in this development build.
profile = (ROOT / "L4D2VR" / "portal2_2009_profile.h").read_text(encoding="utf-8", errors="replace")
if not re.search(r"PORTAL2_2009_INTERFACE_VTABLES_VERIFIED\s*=\s*false", profile):
    raise SystemExit("Interface vtable verification gate is not closed")
if not re.search(r"PORTAL2_2009_FUNCTION_HOOK_ABIS_VERIFIED\s*=\s*false", profile):
    raise SystemExit("Function hook ABI verification gate is not closed")

# Diagnostics must return before VR/OpenVR or gameplay-hook setup.
game_cpp = (ROOT / "L4D2VR" / "game.cpp").read_text(encoding="utf-8", errors="replace")
if "if (m_DiagnosticsOnly)" not in game_cpp or "Diagnostics complete." not in game_cpp:
    raise SystemExit("Read-only diagnostics path is missing")
diag_end = game_cpp.find("Diagnostics complete.")
vr_init = game_cpp.find("m_VR = new VR(this")
if diag_end < 0 or vr_init < 0 or diag_end > vr_init:
    raise SystemExit("Diagnostics path no longer returns before VR initialization")
# RenderView is a client.dll CViewRender function, not engine.dll VEngineRenderView013 slot 6.
if "RuntimeAuditVerifyFunctionTarget" not in game_cpp or "RuntimeAuditVerifyInterfaceSlot(" in game_cpp:
    raise SystemExit("RenderView target validation is comparing unrelated engine/client interfaces")
if not re.search(r'RuntimeAuditVerifyFunctionTarget\([\s\S]*?"client\.dll"', game_cpp):
    raise SystemExit("RenderView is not verified as an executable client.dll function")
# The per-frame OpenVR update must be called from the live render detour.
if not re.search(r"void __fastcall Hooks::dRenderView[\s\S]*?m_VR->Update\(\);", source):
    raise SystemExit("VR::Update() is not called from the RenderView detour")
# The custom usercmd tail must be consumed even when a player cannot be mapped.
read_body = source.split("int Hooks::dReadUsercmd", 1)[1].split("void Hooks::dAdjustEngineViewport", 1)[0]
if "const int marker = buf->ReadChar();" not in read_body or "buf->ReadBitVec3Coord(controllerPosition);" not in read_body:
    raise SystemExit("ReadUsercmd no longer consumes the custom controller payload safely")
vr_cpp = (ROOT / "L4D2VR" / "vr.cpp").read_text(encoding="utf-8", errors="replace")
if "const bool gameStateChanged = !m_HasLastInGameState || inGame != m_LastInGameState;" not in vr_cpp:
    raise SystemExit("VR render-target invalidation is not limited to menu/game transitions")

loader = (ROOT / "L4D2VR" / "dllmain.cpp").read_text(encoding="utf-8", errors="replace")
if 'L"-vrdiagnostics"' not in loader or "!insecureEnabled && !diagnosticsOnly" not in loader:
    raise SystemExit("-vrdiagnostics is not correctly exempted from the -insecure requirement")

# The runtime audit header must be compiled as part of the project.
project = (ROOT / "L4D2VR" / "l4d2vr.vcxproj").read_text(encoding="utf-8", errors="replace")
filters = (ROOT / "L4D2VR" / "l4d2vr.vcxproj.filters").read_text(encoding="utf-8", errors="replace")
if "runtime_audit.h" not in project or "runtime_audit.h" not in filters:
    raise SystemExit("runtime_audit.h is missing from the Visual Studio project or filters")

for path in (ROOT / "L4D2VR").glob("*.vcxproj*"):
    ET.parse(path)

print(f"PASS: {len(created)} active hooks are all registered for enable.")
print(f"PASS: {len(required)} required targets are fail-closed before VR hook initialization.")
print("PASS: both RTTI-derived client targets are pinned to the audited 2009 RVAs.")
print("PASS: both ABI verification gates remain false.")
print("PASS: diagnostics return before VR_Init/hooks and can run without -insecure.")
print("PASS: runtime audit header and Visual Studio XML are present/valid.")
print("PASS: RenderView updates OpenVR, ReadUsercmd consumes controller payloads, and render targets invalidate only on state transitions.")
