$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

$devicePath = Join-Path $PSScriptRoot 'dxvk/src/d3d9/d3d9_device.cpp'
$vrPath = Join-Path $PSScriptRoot 'dxvk/src/d3d9/d3d9_vr.cpp'
foreach ($path in @($devicePath, $vrPath)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required DXVK source file not found: $path. Run bootstrap-dependencies.ps1 first."
    }
}

function Get-SourceNewline([string]$content) {
    $newline = [regex]::Match($content, "\r\n|\n").Value
    if ([string]::IsNullOrEmpty($newline)) { return [Environment]::NewLine }
    return $newline
}

function Replace-SourceBlock(
    [string]$content,
    [string]$oldBlock,
    [string]$newBlock,
    [string]$label
) {
    if ($content.Contains($newBlock)) { return $content }
    if (!$content.Contains($oldBlock)) {
        throw "Could not find the expected $label in the DXVK source. Refusing to modify an unknown source version."
    }
    return $content.Replace($oldBlock, $newBlock)
}

# D3D9 device: null guards plus one PresentEx-owned frame update. Capture the
# current backbuffer before Present rotates the swapchain to the next frame.
$device = [System.IO.File]::ReadAllText($devicePath)
$deviceNewline = Get-SourceNewline $device
$originalDevice = $device

$oldGuard = 'if (g_Game && !g_Game->m_EngineClient->IsInGame())'
$newGuard = @(
    'if (pViewport != nullptr'
    '        && g_Game != nullptr'
    '        && g_Game->m_Initialized'
    '        && g_Game->m_EngineClient != nullptr'
    '        && g_Game->m_VR != nullptr'
    '        && g_Game->m_VR->m_IsInitialized'
    '        && !g_Game->m_EngineClient->IsInGame())'
) -join $deviceNewline

$signature = 'HRESULT STDMETHODCALLTYPE D3D9DeviceEx::SetViewport(const D3DVIEWPORT9* pViewport) {'
$nullCheck = @(
    '    if (pViewport == nullptr)'
    '      return D3DERR_INVALIDCALL;'
) -join $deviceNewline

if (!$device.Contains($newGuard)) {
    if (!$device.Contains($oldGuard)) {
        throw 'Could not find the expected original SetViewport VR override.'
    }
    $device = $device.Replace($oldGuard, $newGuard)
}
if (!$device.Contains($signature + $deviceNewline + $nullCheck)) {
    if (!$device.Contains($signature)) { throw 'Could not find D3D9DeviceEx::SetViewport.' }
    $device = $device.Replace($signature, $signature + $deviceNewline + $nullCheck)
}

$oldResolve = @(
    '    if (g_Game && g_Game->m_VR && g_Game->m_VR->m_CreatedVRTextures) {'
    '      ResolveImage(GetCommonTexture(g_Game->m_VR->m_D9LeftEyeSurface));'
    '      ResolveImage(GetCommonTexture(g_Game->m_VR->m_D9RightEyeSurface));'
    '    }'
) -join $deviceNewline
$newResolve = @(
    '    if (g_Game != nullptr && g_Game->m_VR != nullptr &&'
    '        g_Game->m_VR->m_CreatedVRTextures &&'
    '        g_Game->m_VR->m_D9LeftEyeSurface != nullptr &&'
    '        g_Game->m_VR->m_D9RightEyeSurface != nullptr) {'
    '      ResolveImage(GetCommonTexture(g_Game->m_VR->m_D9LeftEyeSurface));'
    '      ResolveImage(GetCommonTexture(g_Game->m_VR->m_D9RightEyeSurface));'
    '    }'
) -join $deviceNewline
$device = Replace-SourceBlock $device $oldResolve $newResolve 'PresentEx eye-surface resolve block'

$oldWait = ([string][char]9) + 'g_D3DVR9->WaitDeviceIdle();'
$newWait = @(
    '    if (g_D3DVR9 != nullptr)'
    '      g_D3DVR9->WaitDeviceIdle();'
) -join $deviceNewline
$device = Replace-SourceBlock $device $oldWait $newWait 'PresentEx D3D9 bridge wait call'

$oldPresentCall = '    HRESULT result = m_implicitSwapchain->Present('
$newPresentCall = @(
    '    // Export the just-rendered menu/backbuffer before Present rotates the swapchain.'
    '    if (g_Game != nullptr && g_Game->m_Initialized &&'
    '        g_Game->m_VR != nullptr && g_Game->m_VR->m_IsInitialized &&'
    '        g_D3DVR9 != nullptr) {'
    '      const HRESULT backBufferResult = g_D3DVR9->GetBackBufferData(&g_Game->m_VR->m_VKBackBuffer);'
    '      if (FAILED(backBufferResult))'
    '        Logger::warn(str::format("Portal2VR: failed to refresh the D3D9 menu backbuffer (HRESULT ", backBufferResult, ")."));'
    '    }'
    ''
    '    HRESULT result = m_implicitSwapchain->Present('
) -join $deviceNewline
$device = Replace-SourceBlock $device $oldPresentCall $newPresentCall 'PresentEx backbuffer capture point'

$oldUpdate = @(
    '    if (g_Game && g_Game->m_VR)'
    '    {'
    '        g_Game->m_VR->Update();'
    '    }'
) -join $deviceNewline
$newUpdate = @(
    '    // The DXVK PresentEx bridge is the sole once-per-frame VR update point.'
    '    if (g_Game != nullptr && g_Game->m_Initialized &&'
    '        g_Game->m_VR != nullptr && g_Game->m_VR->m_IsInitialized)'
    '    {'
    '        g_Game->m_VR->Update();'
    '    }'
) -join $deviceNewline
$device = Replace-SourceBlock $device $oldUpdate $newUpdate 'PresentEx OpenVR update block'

if (!$device.Contains($newGuard) -or
    !$device.Contains($signature + $deviceNewline + $nullCheck) -or
    !$device.Contains($newResolve) -or
    !$device.Contains($newWait) -or
    !$device.Contains($newPresentCall) -or
    !$device.Contains($newUpdate)) {
    throw 'D3D9 device VR-safety/frame-bridge patch verification failed.'
}

# GetBackBufferData runs on every PresentEx. Release the temporary COM reference
# and check both GetBackBuffer and GetVRDesc before publishing the Vulkan handle.
$vrSource = [System.IO.File]::ReadAllText($vrPath)
$vrNewline = Get-SourceNewline $vrSource
$originalVrSource = $vrSource

$oldBackBufferFunction = @(
    '        HRESULT STDMETHODCALLTYPE GetBackBufferData(SharedTextureHolder* backBufferData)'
    '        {'
    '            IDirect3DSurface9 *backBufferSurface;'
    ''
    '            HRESULT res = m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBufferSurface);'
    ''
    '            vr::VRVulkanTextureData_t vulkanData;'
    '            memset(&vulkanData, 0, sizeof(vr::VRVulkanTextureData_t));'
    ''
    '            SharedTextureHolder *textureTarget;'
    '            D3D9_TEXTURE_VR_DESC textureDesc;'
    '            '
    '            GetVRDesc(backBufferSurface, &textureDesc);'
    '            '
    '            memcpy(&backBufferData->m_VulkanData, &textureDesc, sizeof(vr::VRVulkanTextureData_t));'
    '            backBufferData->m_VRTexture.handle = &backBufferData->m_VulkanData;'
    '            backBufferData->m_VRTexture.eColorSpace = vr::ColorSpace_Auto;'
    '            backBufferData->m_VRTexture.eType = vr::TextureType_Vulkan;'
    '        '
    '            return res;'
    '        }'
) -join $vrNewline
$newBackBufferFunction = @(
    '        HRESULT STDMETHODCALLTYPE GetBackBufferData(SharedTextureHolder* backBufferData)'
    '        {'
    '            if (backBufferData == nullptr)'
    '                return D3DERR_INVALIDCALL;'
    ''
    '            IDirect3DSurface9 *backBufferSurface = nullptr;'
    '            HRESULT res = m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBufferSurface);'
    '            if (FAILED(res) || backBufferSurface == nullptr)'
    '                return FAILED(res) ? res : D3DERR_INVALIDCALL;'
    ''
    '            D3D9_TEXTURE_VR_DESC textureDesc{};'
    '            HRESULT descResult = GetVRDesc(backBufferSurface, &textureDesc);'
    '            backBufferSurface->Release();'
    '            if (FAILED(descResult))'
    '                return descResult;'
    ''
    '            memcpy(&backBufferData->m_VulkanData, &textureDesc, sizeof(vr::VRVulkanTextureData_t));'
    '            backBufferData->m_VRTexture.handle = &backBufferData->m_VulkanData;'
    '            backBufferData->m_VRTexture.eColorSpace = vr::ColorSpace_Auto;'
    '            backBufferData->m_VRTexture.eType = vr::TextureType_Vulkan;'
    ''
    '            return D3D_OK;'
    '        }'
) -join $vrNewline
$vrSource = Replace-SourceBlock $vrSource $oldBackBufferFunction $newBackBufferFunction 'D3D9VR::GetBackBufferData'

if (!$vrSource.Contains($newBackBufferFunction)) {
    throw 'D3D9VR backbuffer export patch verification failed.'
}

if ($device -ne $originalDevice) {
    [System.IO.File]::WriteAllText($devicePath, $device, (New-Object System.Text.UTF8Encoding($false)))
}
if ($vrSource -ne $originalVrSource) {
    [System.IO.File]::WriteAllText($vrPath, $vrSource, (New-Object System.Text.UTF8Encoding($false)))
}

Write-Host 'Applied Portal2VR D3D9 safety, menu-backbuffer, and single PresentEx update patches.' -ForegroundColor Green
