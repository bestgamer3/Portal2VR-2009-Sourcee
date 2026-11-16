$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Invoke-GitClone([string]$Url, [string]$Path, [string]$Ref = '') {
    if (Test-Path -LiteralPath $Path) {
        $existing = Get-ChildItem -Force -LiteralPath $Path -ErrorAction SilentlyContinue
        if (($existing | Measure-Object).Count -gt 0) {
            if (Test-Path -LiteralPath (Join-Path $Path '.git')) {
                Write-Host "Updating dependency: $Path"
                if ($Ref) {
                    git -C $Path fetch --depth 1 origin $Ref
                    if ($LASTEXITCODE -ne 0) { throw "git fetch failed for $Url ref '$Ref'." }
                    git -C $Path checkout -B $Ref FETCH_HEAD
                    if ($LASTEXITCODE -ne 0) { throw "git checkout failed for $Url ref '$Ref'." }
                }
                return
            }
            # Keep manually supplied dependencies; the validation below reports gaps.
            Write-Host "Keeping existing non-git dependency folder: $Path"
            return
        }
        Remove-Item -Force -Recurse -LiteralPath $Path
    }

    $parent = Split-Path -Parent $Path
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    if ($Ref) {
        git clone --depth 1 --branch $Ref $Url $Path
    } else {
        git clone --depth 1 $Url $Path
    }
    if ($LASTEXITCODE -ne 0) { throw "git clone failed for $Url ref '$Ref'. Check internet access and try again." }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git for Windows is required. Install Git, reopen PowerShell, and rerun this script.'
}

# Keep the DXVK fork/branch recorded by the original project's .gitmodules;
# this is the branch that supplies the D3D9 VR extensions used by this project.
Invoke-GitClone 'https://github.com/fholger/dxvk_l4d2vr.git' 'dxvk' 'msaa'
# Pin MinHook to a known stable release rather than tracking a moving master branch.
Invoke-GitClone 'https://github.com/TsudaKageyu/MinHook.git' 'thirdparty/minhook' 'v1.3.4'

$required = @(
    'dxvk/src/d3d9/d3d9.def',
    'dxvk/src/d3d9/d3d9_vr.h',
    'dxvk/src/d3d9/d3d9_vr.cpp',
    'dxvk/include/openvr/openvr.hpp',
    'dxvk/lib32/vulkan-1.lib',
    'thirdparty/minhook/include/MinHook.h',
    'thirdparty/minhook/src/buffer.c',
    'thirdparty/minhook/src/hook.c',
    'thirdparty/minhook/src/trampoline.c',
    'thirdparty/minhook/src/hde/hde32.c',
    'thirdparty/minhook/src/hde/hde32.h',
    'thirdparty/minhook/src/hde/table32.h',
    'thirdparty/openvr/include/openvr.h',
    'thirdparty/openvr/lib/win32/openvr_api.lib',
    'thirdparty/openvr/bin/win32/openvr_api.dll'
)
$missing = @($required | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Host "MISSING: $_" -ForegroundColor Red }
    throw 'Dependency validation failed. The checked-out DXVK branch or MinHook release does not contain the files required by this project.'
}

Write-Host 'All dependency files required by the Visual Studio project are present.' -ForegroundColor Green
Write-Host 'Open l4d2vr.sln and select Release | x86 (the project maps x86 to Win32).'
