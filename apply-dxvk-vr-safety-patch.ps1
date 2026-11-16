$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

$sourcePath = Join-Path $PSScriptRoot 'dxvk/src/d3d9/d3d9_device.cpp'
if (!(Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "DXVK source file not found: $sourcePath. Run bootstrap-dependencies.ps1 first."
}

$content = [System.IO.File]::ReadAllText($sourcePath)
$newline = [regex]::Match($content, "\r\n|\n").Value
if ([string]::IsNullOrEmpty($newline)) {
    $newline = [Environment]::NewLine
}

$oldGuard = 'if (g_Game && !g_Game->m_EngineClient->IsInGame())'
$newGuard = @(
    'if (pViewport != nullptr'
    '        && g_Game != nullptr'
    '        && g_Game->m_Initialized'
    '        && g_Game->m_EngineClient != nullptr'
    '        && g_Game->m_VR != nullptr'
    '        && g_Game->m_VR->m_IsInitialized'
    '        && !g_Game->m_EngineClient->IsInGame())'
) -join $newline

$signature = 'HRESULT STDMETHODCALLTYPE D3D9DeviceEx::SetViewport(const D3DVIEWPORT9* pViewport) {'
$nullCheck = @(
    '    if (pViewport == nullptr)'
    '      return D3DERR_INVALIDCALL;'
) -join $newline

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

$oldResolve = @(
    '    if (g_Game && g_Game->m_VR && g_Game->m_VR->m_CreatedVRTextures) {'
    '      ResolveImage(GetCommonTexture(g_Game->m_VR->m_D9LeftEyeSurface));'
    '      ResolveImage(GetCommonTexture(g_Game->m_VR->m_D9RightEyeSurface));'
    '    }'
) -join $newline
$newResolve = @(
    '    if (g_Game != nullptr && g_Game->m_VR != nullptr &&'
    '        g_Game->m_VR->m_CreatedVRTextures &&'
    '        g_Game->m_VR->m_D9LeftEyeSurface != nullptr &&'
    '        g_Game->m_VR->m_D9RightEyeSurface != nullptr) {'
    '      ResolveImage(GetCommonTexture(g_Game->m_VR->m_D9LeftEyeSurface));'
    '      ResolveImage(GetCommonTexture(g_Game->m_VR->m_D9RightEyeSurface));'
    '    }'
) -join $newline

$oldWait = "`tg_D3DVR9->WaitDeviceIdle();"
$newWait = @(
    "`tif (g_D3DVR9 != nullptr)"
    "`t  g_D3DVR9->WaitDeviceIdle();"
) -join $newline

$hasResolveGuard = $content.Contains($newResolve)
$hasWaitGuard = $content.Contains($newWait)

if (!$hasResolveGuard) {
    if (!$content.Contains($oldResolve)) {
        throw 'Could not find the expected PresentEx eye-surface resolve block. Refusing to modify an unknown DXVK source version.'
    }
    $content = $content.Replace($oldResolve, $newResolve)
}

if (!$hasWaitGuard) {
    if (!$content.Contains($oldWait)) {
        throw 'Could not find the expected PresentEx D3D9 bridge wait call.'
    }
    $content = $content.Replace($oldWait, $newWait)
}

if (!$content.Contains($newResolve) -or !$content.Contains($newWait)) {
    throw 'PresentEx VR surface safety patch verification failed.'
}

if ($content -ne $originalContent) {
    [System.IO.File]::WriteAllText($sourcePath, $content, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host 'Applied SetViewport and PresentEx VR-safety patches to DXVK.' -ForegroundColor Green
} else {
    Write-Host 'SetViewport and PresentEx VR-safety patches are already applied.' -ForegroundColor Green
}
