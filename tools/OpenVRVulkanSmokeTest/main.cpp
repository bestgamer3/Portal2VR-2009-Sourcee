#define NOMINMAX
#include <vulkan/vulkan.h>
#include <openvr.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>
#include <conio.h>

namespace
{
    constexpr uint32_t kEyeCount = 2;
    constexpr VkFormat kEyeFormat = VK_FORMAT_R8G8B8A8_UNORM;

    template <typename T>
    T FromOpenVrHandle(uint64_t value)
    {
        if constexpr (std::is_pointer_v<T>)
            return reinterpret_cast<T>(static_cast<uintptr_t>(value));
        else
            return static_cast<T>(value);
    }

    template <typename T>
    uint64_t ToOpenVrHandle(T value)
    {
        if constexpr (std::is_pointer_v<T>)
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
        else
            return static_cast<uint64_t>(value);
    }

    std::vector<std::string> SplitExtensionList(const std::string& text)
    {
        std::vector<std::string> result;
        std::string current;
        for (char ch : text)
        {
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            {
                if (!current.empty())
                {
                    result.push_back(current);
                    current.clear();
                }
            }
            else
            {
                current.push_back(ch);
            }
        }
        if (!current.empty())
            result.push_back(current);
        return result;
    }

    bool ReadOpenVrExtensionList(vr::IVRCompositor* compositor,
        bool instanceExtensions, VkPhysicalDevice physicalDevice,
        std::vector<std::string>& output, std::string& error)
    {
        if (!compositor)
        {
            error = "OpenVR compositor interface is unavailable.";
            return false;
        }

        uint32_t byteCount = instanceExtensions
            ? compositor->GetVulkanInstanceExtensionsRequired(nullptr, 0)
            : compositor->GetVulkanDeviceExtensionsRequired(
                reinterpret_cast<VkPhysicalDevice_T*>(physicalDevice), nullptr, 0);

        if (byteCount == 0)
        {
            output.clear();
            return true;
        }

        std::vector<char> buffer(byteCount, '\0');
        uint32_t written = instanceExtensions
            ? compositor->GetVulkanInstanceExtensionsRequired(buffer.data(), byteCount)
            : compositor->GetVulkanDeviceExtensionsRequired(
                reinterpret_cast<VkPhysicalDevice_T*>(physicalDevice), buffer.data(), byteCount);

        if (written == 0 || buffer[0] == '\0')
        {
            error = instanceExtensions
                ? "OpenVR did not return its required Vulkan instance extensions."
                : "OpenVR did not return its required Vulkan device extensions.";
            return false;
        }

        output = SplitExtensionList(buffer.data());
        return true;
    }

    bool CheckInstanceExtensions(const std::vector<std::string>& required, std::string& error)
    {
        uint32_t count = 0;
        VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        if (result != VK_SUCCESS)
        {
            error = "vkEnumerateInstanceExtensionProperties(count) failed: " + std::to_string(result);
            return false;
        }

        std::vector<VkExtensionProperties> available(count);
        result = vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
        if (result != VK_SUCCESS)
        {
            error = "vkEnumerateInstanceExtensionProperties(list) failed: " + std::to_string(result);
            return false;
        }

        for (const std::string& extension : required)
        {
            const bool found = std::any_of(available.begin(), available.end(),
                [&extension](const VkExtensionProperties& item)
                {
                    return extension == item.extensionName;
                });
            if (!found)
            {
                error = "Required Vulkan instance extension is unavailable: " + extension;
                return false;
            }
        }
        return true;
    }

    bool CheckDeviceExtensions(VkPhysicalDevice physicalDevice,
        const std::vector<std::string>& required, std::string& error)
    {
        uint32_t count = 0;
        VkResult result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
        if (result != VK_SUCCESS)
        {
            error = "vkEnumerateDeviceExtensionProperties(count) failed: " + std::to_string(result);
            return false;
        }

        std::vector<VkExtensionProperties> available(count);
        result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, available.data());
        if (result != VK_SUCCESS)
        {
            error = "vkEnumerateDeviceExtensionProperties(list) failed: " + std::to_string(result);
            return false;
        }

        for (const std::string& extension : required)
        {
            const bool found = std::any_of(available.begin(), available.end(),
                [&extension](const VkExtensionProperties& item)
                {
                    return extension == item.extensionName;
                });
            if (!found)
            {
                error = "Required Vulkan device extension is unavailable: " + extension;
                return false;
            }
        }
        return true;
    }

    int FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits,
        VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
    {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);

        int fallback = -1;
        for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            if ((typeBits & (1u << index)) == 0)
                continue;

            const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
            if ((flags & required) != required)
                continue;

            if ((flags & preferred) == preferred)
                return static_cast<int>(index);

            if (fallback < 0)
                fallback = static_cast<int>(index);
        }
        return fallback;
    }

    struct EyeImage
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        vr::VRVulkanTextureData_t openVrData{};
        vr::Texture_t texture{};
    };

    class SmokeTest
    {
    public:
        bool Run()
        {
            log_.open("portal2vr_openvr_vulkan_smoketest.log", std::ios::out | std::ios::trunc);
            if (!log_)
            {
                std::cerr << "Could not create portal2vr_openvr_vulkan_smoketest.log\n";
                return false;
            }

            Log("Portal 2 2009 - standalone OpenVR/Vulkan compositor smoke test");
            Log("This program does not load hl2.exe, d3d9.dll, Source interfaces, or gameplay hooks.");
            Log("It creates two Vulkan test images and submits them as left/right eye frames.");

            if (!InitializeOpenVr())
                return false;
            if (!InitializeVulkan())
                return false;
            if (!CheckEyeFormatSupport())
                return false;
            if (!CreateEyeImage(0, width_, height_))
                return false;
            if (!CreateEyeImage(1, width_, height_))
                return false;

            left_.texture.handle = &left_.openVrData;
            left_.texture.eType = vr::TextureType_Vulkan;
            left_.texture.eColorSpace = vr::ColorSpace_Gamma;

            right_.texture.handle = &right_.openVrData;
            right_.texture.eType = vr::TextureType_Vulkan;
            right_.texture.eColorSpace = vr::ColorSpace_Gamma;

            Log("Both Vulkan eye images were created and uploaded.");
            Log("Submitting frames. Press ESC in this console to stop the test.");
            std::cout << "\nOpenVR + Vulkan initialized.\n"
                << "Left eye: blue/cyan checker pattern. Right eye: orange/green checker pattern.\n"
                << "Look into the Rift CV1. Press ESC here to stop.\n\n";

            return SubmitFrames();
        }

        ~SmokeTest()
        {
            // OpenVR requires VR_Shutdown before the Vulkan resources represented by
            // VRVulkanTextureData_t are destroyed.
            if (vrInitialized_)
            {
                vr::VR_Shutdown();
                vrInitialized_ = false;
                Log("VR_Shutdown completed.");
            }

            if (device_ != VK_NULL_HANDLE)
                vkDeviceWaitIdle(device_);

            DestroyEyeImage(left_);
            DestroyEyeImage(right_);

            if (commandPool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE)
                vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;

            if (device_ != VK_NULL_HANDLE)
                vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;

            if (instance_ != VK_NULL_HANDLE)
                vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;

            if (log_.is_open())
            {
                Log("Smoke test finished.");
                log_.close();
            }
        }

    private:
        std::ofstream log_;
        bool vrInitialized_ = false;
        vr::IVRSystem* system_ = nullptr;
        vr::IVRCompositor* compositor_ = nullptr;
        VkInstance instance_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        VkQueue queue_ = VK_NULL_HANDLE;
        uint32_t queueFamily_ = 0;
        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        EyeImage left_{};
        EyeImage right_{};

        void Log(const std::string& message)
        {
            std::cout << message << "\n";
            if (log_.is_open())
            {
                log_ << message << std::endl;
                log_.flush();
            }
        }

        bool Fail(const std::string& message)
        {
            Log("ERROR: " + message);
            return false;
        }

        bool InitializeOpenVr()
        {
            if (!vr::VR_IsHmdPresent())
                Log("Warning: VR_IsHmdPresent returned false; trying VR_Init anyway.");

            vr::EVRInitError initError = vr::VRInitError_None;
            Log("Calling VR_Init(VRApplication_Scene)...");
            system_ = vr::VR_Init(&initError, vr::VRApplication_Scene);
            if (initError != vr::VRInitError_None || !system_)
            {
                const char* description = vr::VR_GetVRInitErrorAsEnglishDescription(initError);
                return Fail(std::string("VR_Init failed: ") + (description ? description : "unknown OpenVR error"));
            }
            vrInitialized_ = true;
            compositor_ = vr::VRCompositor();
            if (!compositor_)
                return Fail("VR_Init succeeded but VRCompositor() returned null.");

            if (!system_->IsTrackedDeviceConnected(vr::k_unTrackedDeviceIndex_Hmd))
                Log("Warning: OpenVR initialized, but the HMD is not currently reported as connected.");

            Log("Querying recommended HMD render target size...");
            system_->GetRecommendedRenderTargetSize(&width_, &height_);
            if (width_ == 0 || height_ == 0)
                return Fail("OpenVR returned a zero-sized recommended eye target.");

            Log("OpenVR scene session initialized.");
            Log("Recommended per-eye target: " + std::to_string(width_) + " x " + std::to_string(height_));
            return true;
        }

        bool InitializeVulkan()
        {
            std::string error;
            std::vector<std::string> instanceExtensions;
            if (!ReadOpenVrExtensionList(compositor_, true, VK_NULL_HANDLE, instanceExtensions, error))
                return Fail(error);

            if (!CheckInstanceExtensions(instanceExtensions, error))
                return Fail(error);

            std::vector<const char*> instanceExtensionNames;
            for (const std::string& name : instanceExtensions)
                instanceExtensionNames.push_back(name.c_str());

            VkApplicationInfo applicationInfo{};
            applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            applicationInfo.pApplicationName = "Portal2VR OpenVR Vulkan Smoke Test";
            applicationInfo.applicationVersion = 1;
            applicationInfo.pEngineName = "Standalone Vulkan compositor probe";
            applicationInfo.engineVersion = 1;
            applicationInfo.apiVersion = VK_API_VERSION_1_0;

            VkInstanceCreateInfo instanceInfo{};
            instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            instanceInfo.pApplicationInfo = &applicationInfo;
            instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensionNames.size());
            instanceInfo.ppEnabledExtensionNames = instanceExtensionNames.empty() ? nullptr : instanceExtensionNames.data();

            Log("Calling vkCreateInstance...");
            VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance_);
            if (result != VK_SUCCESS)
                return Fail("vkCreateInstance failed with VkResult " + std::to_string(result));
            Log("Vulkan instance created. OpenVR instance extensions: " + std::to_string(instanceExtensions.size()));

            uint64_t outputDevice = 0;
            Log("Querying OpenVR's preferred Vulkan output device...");
            system_->GetOutputDevice(&outputDevice, vr::TextureType_Vulkan,
                reinterpret_cast<VkInstance_T*>(instance_));

            uint32_t physicalCount = 0;
            result = vkEnumeratePhysicalDevices(instance_, &physicalCount, nullptr);
            if (result != VK_SUCCESS || physicalCount == 0)
                return Fail("No Vulkan physical devices were found (VkResult " + std::to_string(result) + ").");

            std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
            result = vkEnumeratePhysicalDevices(instance_, &physicalCount, physicalDevices.data());
            if (result != VK_SUCCESS)
                return Fail("vkEnumeratePhysicalDevices(list) failed with VkResult " + std::to_string(result));

            Log("OpenVR output-device handle: " + std::to_string(outputDevice));
            physicalDevice_ = FromOpenVrHandle<VkPhysicalDevice>(outputDevice);
            const bool outputDeviceFound = physicalDevice_ != VK_NULL_HANDLE &&
                std::find(physicalDevices.begin(), physicalDevices.end(), physicalDevice_) != physicalDevices.end();
            if (!outputDeviceFound)
            {
                Log("Warning: OpenVR's preferred Vulkan output device was not in the enumerated device list; using the first device.");
                physicalDevice_ = physicalDevices.front();
            }

            VkPhysicalDeviceProperties deviceProperties{};
            vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProperties);
            Log(std::string("Vulkan device: ") + deviceProperties.deviceName);

            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);
            if (queueFamilyCount == 0)
                return Fail("The selected Vulkan device exposes no queue families.");

            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, queueFamilies.data());
            bool foundGraphicsQueue = false;
            for (uint32_t index = 0; index < queueFamilyCount; ++index)
            {
                if (queueFamilies[index].queueCount > 0 &&
                    (queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
                {
                    queueFamily_ = index;
                    foundGraphicsQueue = true;
                    break;
                }
            }
            if (!foundGraphicsQueue)
                return Fail("The selected Vulkan device has no graphics-capable queue family.");

            std::vector<std::string> deviceExtensions;
            if (!ReadOpenVrExtensionList(compositor_, false, physicalDevice_, deviceExtensions, error))
                return Fail(error);
            if (!CheckDeviceExtensions(physicalDevice_, deviceExtensions, error))
                return Fail(error);

            std::vector<const char*> deviceExtensionNames;
            for (const std::string& name : deviceExtensions)
                deviceExtensionNames.push_back(name.c_str());

            const float queuePriority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamily_;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;

            VkPhysicalDeviceFeatures features{};
            VkDeviceCreateInfo deviceInfo{};
            deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceInfo.queueCreateInfoCount = 1;
            deviceInfo.pQueueCreateInfos = &queueInfo;
            deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensionNames.size());
            deviceInfo.ppEnabledExtensionNames = deviceExtensionNames.empty() ? nullptr : deviceExtensionNames.data();
            deviceInfo.pEnabledFeatures = &features;

            Log("Calling vkCreateDevice with OpenVR's required device extensions...");
            result = vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_);
            if (result != VK_SUCCESS)
                return Fail("vkCreateDevice failed with VkResult " + std::to_string(result));

            vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
            if (queue_ == VK_NULL_HANDLE)
                return Fail("vkGetDeviceQueue returned a null queue.");

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = queueFamily_;
            result = vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
            if (result != VK_SUCCESS)
                return Fail("vkCreateCommandPool failed with VkResult " + std::to_string(result));

            Log("Vulkan device and graphics queue created. OpenVR device extensions: " + std::to_string(deviceExtensions.size()));
            return true;
        }

        bool CheckEyeFormatSupport()
        {
            VkImageFormatProperties properties{};
            const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            const VkResult result = vkGetPhysicalDeviceImageFormatProperties(
                physicalDevice_, kEyeFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                usage, 0, &properties);
            if (result != VK_SUCCESS)
                return Fail("The selected Vulkan GPU cannot create VK_FORMAT_R8G8B8A8_UNORM eye images with transfer-src + transfer-dst + sampled usage (VkResult " + std::to_string(result) + ").");
            if (width_ > properties.maxExtent.width || height_ > properties.maxExtent.height ||
                (properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0)
                return Fail("The HMD's recommended eye target dimensions/sample count exceed the selected GPU's Vulkan image-format limits.");
            Log("Eye image format and requested transfer-src + transfer-dst + sampled usage are supported.");
            return true;
        }

        bool UploadEyePattern(VkImage image, uint32_t eyeIndex)
        {
            const uint64_t pixelCount = static_cast<uint64_t>(width_) * static_cast<uint64_t>(height_);
            if (pixelCount > (std::numeric_limits<VkDeviceSize>::max() / 4) ||
                pixelCount > (std::numeric_limits<size_t>::max() / 4))
                return Fail("The recommended eye texture dimensions are too large for a staging buffer.");
            const VkDeviceSize byteCount = static_cast<VkDeviceSize>(pixelCount * 4);

            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            void* mapped = nullptr;

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = byteCount;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VkResult result = vkCreateBuffer(device_, &bufferInfo, nullptr, &stagingBuffer);
            if (result != VK_SUCCESS)
                return Fail("vkCreateBuffer(staging) failed with VkResult " + std::to_string(result));

            VkMemoryRequirements bufferRequirements{};
            vkGetBufferMemoryRequirements(device_, stagingBuffer, &bufferRequirements);
            const int stagingType = FindMemoryType(physicalDevice_, bufferRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (stagingType < 0)
            {
                vkDestroyBuffer(device_, stagingBuffer, nullptr);
                return Fail("No host-visible Vulkan memory type is available for the staging buffer.");
            }

            VkMemoryAllocateInfo stagingAllocation{};
            stagingAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            stagingAllocation.allocationSize = bufferRequirements.size;
            stagingAllocation.memoryTypeIndex = static_cast<uint32_t>(stagingType);
            result = vkAllocateMemory(device_, &stagingAllocation, nullptr, &stagingMemory);
            if (result != VK_SUCCESS)
            {
                vkDestroyBuffer(device_, stagingBuffer, nullptr);
                return Fail("vkAllocateMemory(staging) failed with VkResult " + std::to_string(result));
            }

            result = vkBindBufferMemory(device_, stagingBuffer, stagingMemory, 0);
            if (result != VK_SUCCESS)
            {
                vkFreeMemory(device_, stagingMemory, nullptr);
                vkDestroyBuffer(device_, stagingBuffer, nullptr);
                return Fail("vkBindBufferMemory failed with VkResult " + std::to_string(result));
            }

            result = vkMapMemory(device_, stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
            if (result != VK_SUCCESS || !mapped)
            {
                vkFreeMemory(device_, stagingMemory, nullptr);
                vkDestroyBuffer(device_, stagingBuffer, nullptr);
                return Fail("vkMapMemory(staging) failed with VkResult " + std::to_string(result));
            }

            uint8_t* pixels = static_cast<uint8_t*>(mapped);
            const uint32_t tileW = std::max(1u, width_ / 16u);
            const uint32_t tileH = std::max(1u, height_ / 16u);
            for (uint32_t y = 0; y < height_; ++y)
            {
                for (uint32_t x = 0; x < width_; ++x)
                {
                    const bool checker = (((x / tileW) + (y / tileH)) & 1u) != 0;
                    const bool border = x < std::max(2u, width_ / 80u) ||
                        y < std::max(2u, height_ / 80u) ||
                        x >= width_ - std::max(2u, width_ / 80u) ||
                        y >= height_ - std::max(2u, height_ / 80u);
                    const bool cross =
                        (x > width_ * 7u / 16u && x < width_ * 9u / 16u) ||
                        (y > height_ * 7u / 16u && y < height_ * 9u / 16u);

                    uint8_t r = 0, g = 0, b = 0;
                    if (eyeIndex == 0)
                    {
                        r = checker ? 20 : 5;
                        g = checker ? 170 : 55;
                        b = checker ? 255 : 135;
                    }
                    else
                    {
                        r = checker ? 255 : 140;
                        g = checker ? 155 : 35;
                        b = checker ? 25 : 5;
                    }
                    if (border || cross)
                        r = g = b = 245;

                    const size_t offset = (static_cast<size_t>(y) * width_ + x) * 4;
                    pixels[offset + 0] = r;
                    pixels[offset + 1] = g;
                    pixels[offset + 2] = b;
                    pixels[offset + 3] = 255;
                }
            }

            // The staging type may be host-visible but not coherent. Flush the mapped range
            // in that case before the transfer command reads it.
            VkPhysicalDeviceMemoryProperties memoryProperties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
            const VkMemoryPropertyFlags stagingFlags = memoryProperties.memoryTypes[stagingType].propertyFlags;
            if ((stagingFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
            {
                VkMappedMemoryRange mappedRange{};
                mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                mappedRange.memory = stagingMemory;
                mappedRange.offset = 0;
                mappedRange.size = VK_WHOLE_SIZE;
                result = vkFlushMappedMemoryRanges(device_, 1, &mappedRange);
                if (result != VK_SUCCESS)
                {
                    vkUnmapMemory(device_, stagingMemory);
                    vkFreeMemory(device_, stagingMemory, nullptr);
                    vkDestroyBuffer(device_, stagingBuffer, nullptr);
                    return Fail("vkFlushMappedMemoryRanges failed with VkResult " + std::to_string(result));
                }
            }
            vkUnmapMemory(device_, stagingMemory);

            result = vkResetCommandPool(device_, commandPool_, 0);
            if (result != VK_SUCCESS)
            {
                vkFreeMemory(device_, stagingMemory, nullptr);
                vkDestroyBuffer(device_, stagingBuffer, nullptr);
                return Fail("vkResetCommandPool failed with VkResult " + std::to_string(result));
            }

            VkCommandBufferAllocateInfo commandAllocation{};
            commandAllocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandAllocation.commandPool = commandPool_;
            commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandAllocation.commandBufferCount = 1;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            result = vkAllocateCommandBuffers(device_, &commandAllocation, &commandBuffer);
            if (result != VK_SUCCESS)
            {
                vkFreeMemory(device_, stagingMemory, nullptr);
                vkDestroyBuffer(device_, stagingBuffer, nullptr);
                return Fail("vkAllocateCommandBuffers failed with VkResult " + std::to_string(result));
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
            if (result != VK_SUCCESS)
            {
                vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
                vkFreeMemory(device_, stagingMemory, nullptr);
                vkDestroyBuffer(device_, stagingBuffer, nullptr);
                return Fail("vkBeginCommandBuffer failed with VkResult " + std::to_string(result));
            }

            VkImageMemoryBarrier toTransfer{};
            toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.srcAccessMask = 0;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = image;
            toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            toTransfer.subresourceRange.baseMipLevel = 0;
            toTransfer.subresourceRange.levelCount = 1;
            toTransfer.subresourceRange.baseArrayLayer = 0;
            toTransfer.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = 0;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageOffset = { 0, 0, 0 };
            copyRegion.imageExtent = { width_, height_, 1 };
            vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            VkImageMemoryBarrier toSubmit{};
            toSubmit.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toSubmit.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toSubmit.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            toSubmit.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toSubmit.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toSubmit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSubmit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSubmit.image = image;
            toSubmit.subresourceRange = toTransfer.subresourceRange;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSubmit);

            result = vkEndCommandBuffer(commandBuffer);
            if (result == VK_SUCCESS)
            {
                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffer;
                result = vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
            }
            if (result == VK_SUCCESS)
                result = vkQueueWaitIdle(queue_);

            vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
            vkFreeMemory(device_, stagingMemory, nullptr);
            vkDestroyBuffer(device_, stagingBuffer, nullptr);

            if (result != VK_SUCCESS)
                return Fail("Vulkan eye texture upload failed with VkResult " + std::to_string(result));

            return true;
        }

        bool CreateEyeImage(uint32_t eyeIndex, uint32_t width, uint32_t height)
        {
            EyeImage& eye = eyeIndex == 0 ? left_ : right_;
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = kEyeFormat;
            imageInfo.extent = { width, height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            Log("Creating eye " + std::to_string(eyeIndex) + " Vulkan image (" +
                std::to_string(width) + " x " + std::to_string(height) + ")...");
            VkResult result = vkCreateImage(device_, &imageInfo, nullptr, &eye.image);
            if (result != VK_SUCCESS)
                return Fail("vkCreateImage for eye " + std::to_string(eyeIndex) + " failed with VkResult " + std::to_string(result));

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device_, eye.image, &requirements);
            const int memoryType = FindMemoryType(physicalDevice_, requirements.memoryTypeBits, 0,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (memoryType < 0)
                return Fail("No compatible Vulkan memory type for eye " + std::to_string(eyeIndex) + ".");

            VkMemoryAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = static_cast<uint32_t>(memoryType);
            result = vkAllocateMemory(device_, &allocation, nullptr, &eye.memory);
            if (result != VK_SUCCESS)
                return Fail("vkAllocateMemory for eye " + std::to_string(eyeIndex) + " failed with VkResult " + std::to_string(result));

            result = vkBindImageMemory(device_, eye.image, eye.memory, 0);
            if (result != VK_SUCCESS)
                return Fail("vkBindImageMemory for eye " + std::to_string(eyeIndex) + " failed with VkResult " + std::to_string(result));

            if (!UploadEyePattern(eye.image, eyeIndex))
                return false;

            eye.openVrData.m_nImage = ToOpenVrHandle(eye.image);
            eye.openVrData.m_pDevice = reinterpret_cast<VkDevice_T*>(device_);
            eye.openVrData.m_pPhysicalDevice = reinterpret_cast<VkPhysicalDevice_T*>(physicalDevice_);
            eye.openVrData.m_pInstance = reinterpret_cast<VkInstance_T*>(instance_);
            eye.openVrData.m_pQueue = reinterpret_cast<VkQueue_T*>(queue_);
            eye.openVrData.m_nQueueFamilyIndex = queueFamily_;
            eye.openVrData.m_nWidth = width;
            eye.openVrData.m_nHeight = height;
            eye.openVrData.m_nFormat = static_cast<uint32_t>(kEyeFormat);
            eye.openVrData.m_nSampleCount = 1;
            return true;
        }

        void DestroyEyeImage(EyeImage& eye)
        {
            if (device_ != VK_NULL_HANDLE && eye.image != VK_NULL_HANDLE)
                vkDestroyImage(device_, eye.image, nullptr);
            if (device_ != VK_NULL_HANDLE && eye.memory != VK_NULL_HANDLE)
                vkFreeMemory(device_, eye.memory, nullptr);
            eye.image = VK_NULL_HANDLE;
            eye.memory = VK_NULL_HANDLE;
        }

        bool SubmitFrames()
        {
            vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
            bool loggedFirstFrame = false;
            uint64_t frameCount = 0;
            uint32_t lastPoseError = 0xFFFFFFFFu;

            while (!_kbhit())
            {
                const vr::EVRCompositorError poseResult = compositor_->WaitGetPoses(
                    poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
                if (poseResult != vr::VRCompositorError_None)
                {
                    const uint32_t code = static_cast<uint32_t>(poseResult);
                    if (code != lastPoseError)
                    {
                        Log("WaitGetPoses returned compositor error " + std::to_string(code));
                        lastPoseError = code;
                    }
                    continue;
                }

                const vr::EVRCompositorError leftResult = compositor_->Submit(
                    vr::Eye_Left, &left_.texture, nullptr, vr::Submit_Default);
                const vr::EVRCompositorError rightResult = compositor_->Submit(
                    vr::Eye_Right, &right_.texture, nullptr, vr::Submit_Default);
                compositor_->PostPresentHandoff();

                if (!loggedFirstFrame)
                {
                    Log("First Submit(left) result: " + std::to_string(static_cast<uint32_t>(leftResult)));
                    Log("First Submit(right) result: " + std::to_string(static_cast<uint32_t>(rightResult)));
                    loggedFirstFrame = true;
                }

                if (leftResult != vr::VRCompositorError_None || rightResult != vr::VRCompositorError_None)
                {
                    Log("A compositor Submit call failed. Check the numeric result codes above.");
                    return false;
                }

                ++frameCount;
                if (frameCount % 300 == 0)
                    std::cout << "Submitted " << frameCount << " stereo frames. Press ESC to stop.\r";
            }

            // Consume the escape key pressed in the console.
            while (_kbhit())
                (void)_getch();

            Log("Stopped after submitting " + std::to_string(frameCount) + " stereo frames.");
            return loggedFirstFrame;
        }
    };
}

int main()
{
    SmokeTest test;
    const bool ok = test.Run();
    if (!ok)
    {
        std::cout << "\nThe OpenVR/Vulkan smoke test failed. See portal2vr_openvr_vulkan_smoketest.log.\n";
        std::cout << "Press Enter to close...";
        std::cin.get();
        return 1;
    }

    std::cout << "\nThe OpenVR/Vulkan smoke test completed. See portal2vr_openvr_vulkan_smoketest.log.\n";
    std::cout << "Press Enter to close...";
    std::cin.get();
    return 0;
}
