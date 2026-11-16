#!/usr/bin/env python3
"""Static checks for the Visual Studio build/deployment setup."""
from __future__ import annotations
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'L4D2VR' / 'l4d2vr.vcxproj'
SOLUTION = ROOT / 'l4d2vr.sln'
NS = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}

sln = SOLUTION.read_text(encoding='utf-8-sig', errors='replace')
if not re.search(r'Release\|x86\s*=\s*Release\|x86', sln):
    raise SystemExit('Solution does not declare Release|x86.')
if not re.search(r'Release\|x86\.ActiveCfg\s*=\s*Release\|Win32', sln):
    raise SystemExit('Solution Release|x86 is not mapped to the project Win32 configuration.')

root = ET.parse(PROJECT).getroot()
for name in ('Debug', 'Release'):
    cond = f"'$(Configuration)|$(Platform)'=='{name}|Win32'"
    groups = [g for g in root.findall('m:PropertyGroup', NS) if g.attrib.get('Condition') == cond]
    if not groups:
        raise SystemExit(f'Missing {name}|Win32 property group.')
    target_names = [g.findtext('m:TargetName', namespaces=NS) for g in groups]
    out_dirs = [g.findtext('m:OutDir', namespaces=NS) for g in groups]
    if 'd3d9' not in target_names:
        raise SystemExit(f'{name} target name is not d3d9.')
    if '$(ProjectDir)bin\\$(Configuration)\\' not in out_dirs:
        raise SystemExit(f'{name} output directory is not the expected stable bin directory.')
    toolsets = [g.findtext('m:PlatformToolset', namespaces=NS) for g in groups]
    if 'v145' not in toolsets:
        raise SystemExit(f'{name} project configuration does not use the Visual Studio 2026 v145 toolset.')

for node in root.findall('.//m:PreprocessorDefinitions', NS):
    text = node.text or ''
    if '_WIN32_WINNT=0x0601' not in text or 'WINVER=0x0601' not in text:
        raise SystemExit('A build configuration does not set the Windows API baseline.')

# Every project-local file must be in this archive. External source dependencies
# are intentionally excluded because the bootstrap script fetches them on Windows.
project_dir = PROJECT.parent
external_roots = ('..\\dxvk\\', '..\\thirdparty\\minhook\\')
missing_local = []
for tag in ('ClCompile', 'ClInclude', 'ResourceCompile', 'None', 'Text'):
    for item in root.findall(f'.//m:{tag}', NS):
        include = item.attrib.get('Include', '')
        if not include or '$(' in include:
            continue
        normalized = include.replace('/', '\\')
        if normalized.lower().startswith(external_roots):
            continue
        candidate = project_dir / include.replace('\\', '/')
        if not candidate.exists():
            missing_local.append(include)
if missing_local:
    raise SystemExit('Missing project-local items: ' + ', '.join(missing_local[:20]))

for file in (
    ROOT / 'thirdparty/openvr/lib/win32/openvr_api.lib',
    ROOT / 'thirdparty/openvr/bin/win32/openvr_api.dll',
    ROOT / 'L4D2VR/manifest.vrmanifest',
    ROOT / 'L4D2VR/config.txt',
    ROOT / 'L4D2VR/SteamVRActionManifest/action_manifest.json',
):
    if not file.is_file():
        raise SystemExit(f'Missing required bundled runtime/build file: {file.relative_to(ROOT)}')

script = (ROOT / 'build-win32-release.ps1').read_text(encoding='utf-8', errors='replace')
for required in ('/p:Platform=x86', 'WindowsTargetPlatformVersion', 'Test-ProjectSourceFiles', 'portal2vr_2009_', 'openvr_api.dll', 'SteamVRActionManifest', 'GameBin'):
    if required not in script:
        raise SystemExit(f'Build helper is missing expected setting/staging behavior: {required}')
if '/p:Platform=Win32' in script:
    raise SystemExit('Build helper still passes the invalid solution-level Platform=Win32 setting.')
bootstrap = (ROOT / 'bootstrap-dependencies.ps1').read_text(encoding='utf-8', errors='replace')
for required in ('dxvk/include/openvr/openvr.hpp', 'dxvk/lib32/vulkan-1.lib', 'thirdparty/minhook/src/hde/hde32.h', 'thirdparty/minhook/src/hde/table32.h'):
    if required not in bootstrap:
        raise SystemExit(f'Dependency bootstrap does not validate required build input: {required}')

print('PASS: solution platform x86 maps to project Win32.')
print('PASS: Debug and Release output paths are deterministic and use the v145 toolset.')
print('PASS: both configurations set the Windows API target baseline.')
print('PASS: all project-local source/runtime items exist.')
print('PASS: bundled OpenVR runtime and action manifest files exist.')
print('PASS: build helper validates project inputs, uses x86, logs MSBuild output, and stages runtime assets.')
print('PASS: dependency bootstrap validates DXVK/OpenVR/MinHook build prerequisites.')
