[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    # Optional: the game's bin directory (the folder that contains engine.dll).
    # When supplied, the script deploys d3d9.dll + openvr_api.dll there and puts
    # the VR runtime files in the parent game directory's VR folder.
    [string]$GameBin = '',

    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Test-DependencyFiles {
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

    return @($required | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) })
}

function Test-ProjectSourceFiles {
    [xml]$projectXml = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'L4D2VR/l4d2vr.vcxproj') -Raw
    $namespace = New-Object System.Xml.XmlNamespaceManager($projectXml.NameTable)
    $namespace.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $nodes = $projectXml.SelectNodes('//m:ClCompile | //m:ClInclude | //m:ResourceCompile | //m:None | //m:Text', $namespace)
    $missing = New-Object System.Collections.Generic.List[string]
    $projectDirectory = Join-Path $PSScriptRoot 'L4D2VR'

    foreach ($node in $nodes) {
        $include = [string]$node.GetAttribute('Include')
        if ([string]::IsNullOrWhiteSpace($include) -or $include.Contains('$(')) { continue }
        $candidate = [System.IO.Path]::GetFullPath((Join-Path $projectDirectory $include))
        if (!(Test-Path -LiteralPath $candidate -PathType Leaf)) {
            $missing.Add($include)
        }
    }

    return @($missing)
}

function Copy-DirectoryContents {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$DestinationDirectory
    )

    if (!(Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
        throw "Required runtime directory not found: $SourceDirectory"
    }
    New-Item -ItemType Directory -Path $DestinationDirectory -Force | Out-Null
    $children = Join-Path $SourceDirectory '*'
    Copy-Item -Path $children -Destination $DestinationDirectory -Recurse -Force
}

$missingDependencies = Test-DependencyFiles
if ($missingDependencies.Count -gt 0) {
    Write-Host 'One or more source/build dependencies are missing:' -ForegroundColor Yellow
    $missingDependencies | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
    & (Join-Path $PSScriptRoot 'bootstrap-dependencies.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "Dependency bootstrap failed with exit code $LASTEXITCODE."
    }
    $missingDependencies = Test-DependencyFiles
    if ($missingDependencies.Count -gt 0) {
        throw "Dependencies are still missing after bootstrap: $($missingDependencies -join ', ')"
    }
}

# Apply the local DXVK safety patch after dependencies are available and before compilation.
$dxvkPatchScript = Join-Path $PSScriptRoot 'apply-dxvk-vr-safety-patch.ps1'
if (!(Test-Path -LiteralPath $dxvkPatchScript -PathType Leaf)) {
    throw "Required DXVK safety patch script not found: $dxvkPatchScript"
}
& $dxvkPatchScript

$missingProjectFiles = Test-ProjectSourceFiles
if ($missingProjectFiles.Count -gt 0) {
    $preview = ($missingProjectFiles | Select-Object -First 20) -join ', '
    throw "The Visual Studio project references missing files: $preview"
}

$programFilesX86 = ${env:ProgramFiles(x86)}
if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
    $programFilesX86 = $env:ProgramFiles
}
$vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Visual Studio Installer/vswhere.exe was not found. Install Visual Studio 2026 with Desktop development with C++ and the MSVC v145 x86/x64 tools.'
}

$vsPath = & $vswhere -latest -products '*' `
    -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ([string]::IsNullOrWhiteSpace($vsPath)) {
    throw 'Visual Studio with MSBuild and the x86/x64 C++ toolset was not found.'
}
$msbuild = Join-Path $vsPath 'MSBuild/Current/Bin/MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw "MSBuild not found at $msbuild"
}

# Select an installed Windows 10/11 SDK instead of hard-coding 10.0, which is
# not an installed SDK version on many systems.
$windowsKitsLib = Join-Path $programFilesX86 'Windows Kits/10/Lib'
$sdkVersion = $null
if (-not [string]::IsNullOrWhiteSpace($env:WindowsSDKVersion)) {
    $candidate = $env:WindowsSDKVersion.TrimEnd([char]'\', [char]'/')
    if (Test-Path -LiteralPath (Join-Path $windowsKitsLib "$candidate/um/x86/kernel32.lib")) {
        $sdkVersion = $candidate
    }
}
if (-not $sdkVersion -and (Test-Path -LiteralPath $windowsKitsLib -PathType Container)) {
    $sdkVersion = Get-ChildItem -LiteralPath $windowsKitsLib -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um/x86/kernel32.lib') } |
        Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
        Select-Object -First 1 -ExpandProperty Name
}
if (-not $sdkVersion) {
    throw 'No Windows 10/11 SDK x86 libraries were found. Install a Windows 10/11 SDK through Visual Studio Installer.'
}
$windowsSdkInclude = Join-Path $programFilesX86 "Windows Kits/10/Include/$sdkVersion/um/Windows.h"
if (!(Test-Path -LiteralPath $windowsSdkInclude -PathType Leaf)) {
    throw "The Windows SDK headers for $sdkVersion were not found. Reinstall that SDK component."
}

$solution = Join-Path $PSScriptRoot 'l4d2vr.sln'
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$buildLog = Join-Path $PSScriptRoot "portal2vr_2009_$($Configuration.ToLowerInvariant())_build.log"
Write-Host "Building Portal 2 VR port: $Configuration | x86 (solution maps this to project Win32)"
Write-Host "Windows SDK: $sdkVersion"
Write-Host "Build log: $buildLog"
& $msbuild $solution /m "/t:$target" "/p:Configuration=$Configuration" /p:Platform=x86 "/p:WindowsTargetPlatformVersion=$sdkVersion" /p:PreferredToolArchitecture=x64 /nologo /v:minimal 2>&1 |
    Tee-Object -FilePath $buildLog
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE. Send the build log: $buildLog"
}

$projectDir = Join-Path $PSScriptRoot 'L4D2VR'
$outputDir = Join-Path $projectDir "bin/$Configuration"
$d3d9Dll = Join-Path $outputDir 'd3d9.dll'
if (!(Test-Path -LiteralPath $d3d9Dll -PathType Leaf)) {
    throw "MSBuild reported success, but the expected output was not found: $d3d9Dll"
}

# The game reads these files relative to its working directory. Stage a complete
# runtime folder beside the built DLL and copy it into the actual game root below
# when -GameBin was supplied.
$openVrDll = Join-Path $PSScriptRoot 'thirdparty/openvr/bin/win32/openvr_api.dll'
Copy-Item -LiteralPath $openVrDll -Destination (Join-Path $outputDir 'openvr_api.dll') -Force
$vrOutput = Join-Path $outputDir 'VR'
New-Item -ItemType Directory -Path $vrOutput -Force | Out-Null
foreach ($file in @('manifest.vrmanifest', 'config.txt', 'portal2vr_capsule_main.png', 'portal2vr_portrait_main.png')) {
    Copy-Item -LiteralPath (Join-Path $projectDir $file) -Destination $vrOutput -Force
}
Copy-DirectoryContents -SourceDirectory (Join-Path $projectDir 'SteamVRActionManifest') -DestinationDirectory (Join-Path $vrOutput 'SteamVRActionManifest')

Write-Host ''
Write-Host 'Build succeeded.' -ForegroundColor Green
Write-Host "DLL: $d3d9Dll"
Write-Host "OpenVR runtime DLL and VR manifests staged under: $outputDir"
Write-Host 'This build initializes OpenVR and routes the Portal 2 main-menu backbuffer to a SteamVR overlay.' -ForegroundColor Green
Write-Host 'The unverified RenderView stereo detour and controller/gameplay hooks remain disabled for crash safety.' -ForegroundColor Yellow

if (-not [string]::IsNullOrWhiteSpace($GameBin)) {
    $resolvedGameBin = (Resolve-Path -LiteralPath $GameBin -ErrorAction Stop).Path
    if (!(Test-Path -LiteralPath (Join-Path $resolvedGameBin 'engine.dll') -PathType Leaf)) {
        Write-Warning "No engine.dll found in '$resolvedGameBin'. Deploying there anyway because you explicitly selected this folder."
    }

    foreach ($file in @('d3d9.dll', 'openvr_api.dll')) {
        $source = Join-Path $outputDir $file
        $destination = Join-Path $resolvedGameBin $file
        if (Test-Path -LiteralPath $destination -PathType Leaf) {
            $backup = "$destination.pre-portal2vr.bak"
            if (!(Test-Path -LiteralPath $backup -PathType Leaf)) {
                Copy-Item -LiteralPath $destination -Destination $backup
                Write-Host "Backed up existing $file to $backup"
            }
        }
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }

    $gameRoot = Split-Path -Parent $resolvedGameBin
    $gameVr = Join-Path $gameRoot 'VR'
    New-Item -ItemType Directory -Path $gameVr -Force | Out-Null
    foreach ($file in @('manifest.vrmanifest', 'portal2vr_capsule_main.png', 'portal2vr_portrait_main.png')) {
        Copy-Item -LiteralPath (Join-Path $vrOutput $file) -Destination $gameVr -Force
    }
    Copy-DirectoryContents -SourceDirectory (Join-Path $vrOutput 'SteamVRActionManifest') -DestinationDirectory (Join-Path $gameVr 'SteamVRActionManifest')
    $gameConfig = Join-Path $gameVr 'config.txt'
    if (!(Test-Path -LiteralPath $gameConfig -PathType Leaf)) {
        Copy-Item -LiteralPath (Join-Path $vrOutput 'config.txt') -Destination $gameConfig
    }

    Write-Host "Deployed d3d9.dll and openvr_api.dll to: $resolvedGameBin"
    Write-Host "Deployed VR manifest/actions/config to: $gameVr"
}
