#!/usr/bin/env python3
"""Static sanity checks for the standalone OpenVR/Vulkan smoke-test project."""
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "tools" / "OpenVRVulkanSmokeTest" / "OpenVRVulkanSmokeTest.vcxproj"
SOURCE = ROOT / "tools" / "OpenVRVulkanSmokeTest" / "main.cpp"
BUILD = ROOT / "build-openvr-vulkan-smoketest.ps1"
BOOTSTRAP = ROOT / "bootstrap-dependencies.ps1"

ET.parse(PROJECT)
source = SOURCE.read_text(encoding="utf-8")
project = PROJECT.read_text(encoding="utf-8")
build = BUILD.read_text(encoding="utf-8")
bootstrap = BOOTSTRAP.read_text(encoding="utf-8")

checks = {
    "OpenVR scene app is initialized": "VRApplication_Scene" in source,
    "Runtime instance extensions are queried": "GetVulkanInstanceExtensionsRequired" in source,
    "Runtime device extensions are queried": "GetVulkanDeviceExtensionsRequired" in source,
    "HMD output GPU is queried": "GetOutputDevice" in source,
    "Vulkan eye images include transfer-src usage": "VK_IMAGE_USAGE_TRANSFER_SRC_BIT" in source,
    "Vulkan eye images include sampled usage": "VK_IMAGE_USAGE_SAMPLED_BIT" in source,
    "Submitted image layout is transfer-src optimal": "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL" in source,
    "Both eye textures are submitted": "vr::Eye_Left" in source and "vr::Eye_Right" in source,
    "Log writes are flushed": "log_.flush()" in source,
    "Project targets Win32 with v145": "Release|Win32" in project and "<PlatformToolset>v145</PlatformToolset>" in project,
    "Project links OpenVR and Vulkan import libraries": "openvr_api.lib" in project and "vulkan-1.lib" in project,
    "Build script validates the Vulkan-Headers submodule": "Test-VulkanHeaders" in build,
    "Bootstrap initializes Vulkan-Headers submodule": "submodule update --init" in bootstrap,
}

for label, passed in checks.items():
    print(("PASS" if passed else "FAIL") + ": " + label)

if not all(checks.values()):
    raise SystemExit(1)
print("PASS: standalone smoke-test project static checks.")
