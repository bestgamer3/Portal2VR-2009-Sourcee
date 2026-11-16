[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Test-VulkanHeaders {
    foreach ($candidate in @('dxvk/include/vulkan/vulkan.h', 'dxvk/include/vulkan/include/vulkan/vulkan.h')) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $true }
    }
    return $false
}

$required = @(
    'dxvk/lib32/vulkan-1.lib',
    'thirdparty/openvr/include/openvr.h',
    'thirdparty/openvr/lib/win32/openvr_api.lib',
    'thirdparty/openvr/bin/win32/openvr_api.dll'
)
$missing = @($required | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) })
if (-not (Test-VulkanHeaders)) { $missing += 'DXVK Vulkan-Headers' }
if ($missing.Count -gt 0) {
    Write-Host 'Bootstrapping missing DXVK/OpenVR dependencies:' -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "  $_" }
    & (Join-Path $PSScriptRoot 'bootstrap-dependencies.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "Dependency bootstrap failed with exit code $LASTEXITCODE."
    }
    $missing = @($required | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) })
    if (-not (Test-VulkanHeaders)) { $missing += 'DXVK Vulkan-Headers' }
    if ($missing.Count -gt 0) {
        throw "Required files are still missing: $($missing -join ', ')"
    }
}

$programFilesX86 = ${env:ProgramFiles(x86)}
if ([string]::IsNullOrWhiteSpace($programFilesX86)) { $programFilesX86 = $env:ProgramFiles }
$vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'vswhere.exe not found. Install Visual Studio 2026 with Desktop development with C++ and MSVC v145 x86/x64 tools.'
}
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($vsPath)) {
    throw 'Visual Studio with MSBuild and x86/x64 C++ tools was not found.'
}
$msbuild = Join-Path $vsPath 'MSBuild/Current/Bin/MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw "MSBuild not found: $msbuild" }

$windowsKitsLib = Join-Path $programFilesX86 'Windows Kits/10/Lib'
$sdkVersion = $null
if (-not [string]::IsNullOrWhiteSpace($env:WindowsSDKVersion)) {
    $candidate = $env:WindowsSDKVersion.TrimEnd([char]'\', [char]'/')
    if (Test-Path -LiteralPath (Join-Path $windowsKitsLib "$candidate/um/x86/kernel32.lib")) { $sdkVersion = $candidate }
}
if (-not $sdkVersion -and (Test-Path -LiteralPath $windowsKitsLib -PathType Container)) {
    $sdkVersion = Get-ChildItem -LiteralPath $windowsKitsLib -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um/x86/kernel32.lib') } |
        Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
        Select-Object -First 1 -ExpandProperty Name
}
if (-not $sdkVersion) { throw 'No Windows 10/11 SDK x86 libraries found. Install a Windows SDK in Visual Studio Installer.' }

$project = Join-Path $PSScriptRoot 'tools/OpenVRVulkanSmokeTest/OpenVRVulkanSmokeTest.vcxproj'
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$log = Join-Path $PSScriptRoot "openvr_vulkan_smoketest_$($Configuration.ToLowerInvariant())_build.log"
Write-Host "Building standalone OpenVR/Vulkan smoke test: $Configuration | Win32"
Write-Host "Windows SDK: $sdkVersion"
Write-Host "Build log: $log"
& $msbuild $project /m "/t:$target" "/p:Configuration=$Configuration" /p:Platform=Win32 "/p:WindowsTargetPlatformVersion=$sdkVersion" /p:PreferredToolArchitecture=x64 /nologo /v:minimal "/flp:logfile=$log;verbosity=normal"
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE. Send $log" }

$output = Join-Path $PSScriptRoot "tools/OpenVRVulkanSmokeTest/bin/$Configuration"
$exe = Join-Path $output 'Portal2VR_OpenVR_VulkanSmokeTest.exe'
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Build succeeded but EXE was not found: $exe" }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'thirdparty/openvr/bin/win32/openvr_api.dll') -Destination (Join-Path $output 'openvr_api.dll') -Force
Write-Host ''
Write-Host 'Standalone OpenVR/Vulkan smoke test built successfully.' -ForegroundColor Green
Write-Host "EXE: $exe"
Write-Host "Runtime DLL: $(Join-Path $output 'openvr_api.dll')"
Write-Host 'This EXE is independent of hl2.exe and does not install Portal 2 hooks.' -ForegroundColor Yellow
