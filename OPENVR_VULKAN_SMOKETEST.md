# Standalone OpenVR + Vulkan stereo compositor test

This is a separate diagnostic executable. It does **not** inject into `hl2.exe`, load the Portal 2 D3D9 proxy, call Source Engine interfaces, or enable any Portal 2 gameplay hooks.

Its single purpose is to answer this question independently of the game:

> Can this Windows PC initialize an OpenVR scene session, create Vulkan images on the GPU SteamVR selects for the HMD, and submit distinct left/right eye images to the SteamVR compositor?

## Build on Windows

Requirements:

- Visual Studio 2026 with **Desktop development with C++**.
- MSVC **v145 x86/x64 tools** and a Windows 10/11 SDK.
- Git for Windows.
- SteamVR installed and configured for the Oculus Rift CV1.

From the root of the extracted source package, open PowerShell and run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build-openvr-vulkan-smoketest.ps1 -Rebuild
```

The script bootstraps the recorded DXVK dependency if its Vulkan headers/import library are missing, locates Visual Studio/MSBuild and an installed SDK, and builds an x86 console application.

Expected output:

```text
tools\OpenVRVulkanSmokeTest\bin\Release\Portal2VR_OpenVR_VulkanSmokeTest.exe
tools\OpenVRVulkanSmokeTest\bin\Release\openvr_api.dll
```

## Run the test

1. Close Portal 2 2009 and any other VR game.
2. Start SteamVR and wait until the Rift CV1 is shown as ready.
3. Run `Portal2VR_OpenVR_VulkanSmokeTest.exe` from its output folder.
4. Look into the headset. The left eye should show a blue/cyan checker pattern and the right eye an orange pattern with a white cross/border.
5. Press a key in the console (ESC is recommended) to stop the test.
6. Send `portal2vr_openvr_vulkan_smoketest.log` from the current working folder.

The log reports each initialization stage, the Vulkan GPU chosen, the target dimensions, and the numeric result from the first left/right `IVRCompositor::Submit` calls.

## Interpreting the result

- **Both different patterns appear in the headset:** OpenVR startup, Vulkan interop, and stereo compositor submission work independently of Portal 2. The remaining blank-screen problem is then in the game integration / D3D9-Vulkan bridge / engine hook path.
- **OpenVR initializes but Vulkan setup fails:** Send the log. The exact Vulkan call and `VkResult` identify the failed stage.
- **Vulkan setup succeeds but `Submit` returns an error:** Send the log and SteamVR `vrcompositor.txt`; the numeric compositor error will narrow down image metadata, format, queue, or timing issues.
- **Both submissions report success but the headset remains blank:** Send the log and a fresh `vrcompositor.txt`; that points to compositor display/focus/runtime state rather than the Source engine hooks.

## Important limitations

- This test renders a static diagnostic image. It is not Portal 2 VR and does not validate head-tracked scene rendering.
- It uses the runtime-reported Vulkan instance/device extension lists. Valve notes that these lists must be queried at runtime because they can change with SteamVR versions.
- The submitted images are created with `VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT` (plus transfer-destination usage for the initial upload) and are transitioned to `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` before submission, as required by Valve's OpenVR Vulkan submission documentation.
- The smoke-test executable has not been compiled with MSVC or run on the user's Windows machine in this environment. Static project/source checks are not a substitute for that test.
