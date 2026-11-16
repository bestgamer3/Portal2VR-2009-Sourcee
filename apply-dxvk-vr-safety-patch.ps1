$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

$sourcePath = Join-Path $PSScriptRoot 'dxvk/src/d3d9/d3d9_device.cpp'
if (!(Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "DXVK source file not found: $sourcePath. Run bootstrap-dependencies.ps1 first."
}

$content = [System.IO.File]::ReadAllText($sourcePath)
$newline = [Environment]::NewLine

$oldGuard = 'if (g_Game && !g_Game->m_EngineClient->IsInGame())'
$newGuard = @'
if (pViewport != nullptr
        && g_Game != nullptr
        && g_Game->m_Initialized
        && g_Game->m_EngineClient != nullptr
        && g_Game->m_VR != nullptr
        && g_Game->m_VR->m_IsInitialized
        && !g_Game->m_EngineClient->IsInGame())
'@.TrimEnd()

$signature = 'HRESULT STDMETHODCALLTYPE D3D9DeviceEx::SetViewport(const D3DVIEWPORT9* pViewport) {'
$nullCheck = @'
    if (pViewport == nullptr)
      return D3DERR_INVALIDCALL;
'@.TrimEnd()

$originalContent = $content
$hasGuard = $content.Contains($newGuard)
$hasNullCheck = $content.Contains($signature + $newline + $nullCheck)

if (!$hasGuard) {
    if (!$content.Contains($oldGuard)) {
        throw 'Could not find the expected original SetViewport VR override. Refusing to modify an unknown DXVK source version.'
    }
    $content = $content.Replace($oldGuard, $newGuard)
}

if (!$hasNullCheck) {
    if (!$content.Contains($signature)) {
        throw 'Could not find D3D9DeviceEx::SetViewport signature.'
    }
    $content = $content.Replace($signature, $signature + $newline + $nullCheck)
}

if (!$content.Contains($newGuard) -or !$content.Contains($signature + $newline + $nullCheck)) {
    throw 'SetViewport safety patch verification failed.'
}

if ($content -ne $originalContent) {
    [System.IO.File]::WriteAllText($sourcePath, $content, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host 'Applied SetViewport null/VR-state safety patch to DXVK.' -ForegroundColor Green
} else {
    Write-Host 'SetViewport safety patch is already applied.' -ForegroundColor Green
}
