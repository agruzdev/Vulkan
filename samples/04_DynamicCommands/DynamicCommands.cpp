/**
* Vulkan samples
*
* The MIT License (MIT)
* Copyright (c) 2016 Alexey Gruzdev
*/

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

#include <VulkanUtility.h>
#include <OperatingSystem.h>

class Sample_03_Window
    : public ApiWithoutSecrets::OS::TutorialBase
{
    VulkanHolder<vk::Instance> mVulkan;
    VulkanHolder<vk::Device> mDevice;
    VulkanHolder<vk::SurfaceKHR> mSurface;
    VulkanHolder<vk::SwapchainKHR> mSwapChain;

    VulkanHolder<vk::CommandPool> mCommandPool;

    VulkanHolder<vk::Semaphore> mSemaphoreImageAcquired;
    VulkanHolder<vk::Semaphore> mSemaphoreImageReady;

    vk::PhysicalDevice mPhysicalDevice;
    vk::Queue mCommandQueue;

    uint32_t mQueueFamilyGraphics = std::numeric_limits<uint32_t>::max();
    uint32_t mQueueFamilyPresent = std::numeric_limits<uint32_t>::max();

    std::array< std::array<float, 4>, 6 > mColors = {
        std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 1.0f },
                            { 1.0f, 1.0f, 0.0f, 1.0f },
                            { 0.0f, 1.0f, 0.0f, 1.0f },
                            { 0.0f, 1.0f, 1.0f, 1.0f },
                            { 0.0f, 0.0f, 1.0f, 1.0f },
                            { 1.0f, 0.0f, 1.0f, 1.0f }
    };
    uint32_t mCurrentColorIdx = 0;
    uint32_t mCurrentLerpFactor = 0;
    const uint32_t mMaxLerpFactor = 1024;

    std::array<float, 4> GetNextColor()
    {
        const uint32_t idx1 = mCurrentColorIdx;
        const uint32_t idx2 = mCurrentColorIdx + 1 < mColors.size() ? mCurrentColorIdx + 1 : 0;
        const float alpha = static_cast<float>(mCurrentLerpFactor) / mMaxLerpFactor;

        std::array<float, 4> color;
        std::transform(std::cbegin(mColors[idx1]), std::cend(mColors[idx1]), std::begin(mColors[idx2]), std::begin(color), [alpha](float val1, float val2) {
            return val1 + alpha * (val2 - val1);
        });

        ++mCurrentLerpFactor;
        if (mCurrentLerpFactor > mMaxLerpFactor) {
            mCurrentLerpFactor = 0;
            ++mCurrentColorIdx;
            if (mCurrentColorIdx >= mColors.size()) {
                mCurrentColorIdx = 0;
            }
        }
        return color;
    }

public:
    
    bool CheckPhysicalDeviceProperties(const vk::PhysicalDevice & physicalDevice, uint32_t &selected_graphics_queue_family_index, uint32_t &selected_present_queue_family_index)
    {
        auto deviceProperties = physicalDevice.getProperties();
        auto deviceFeatures = physicalDevice.getFeatures();

        if ((VK_VERSION_MAJOR(deviceProperties.apiVersion) < 1) || (deviceProperties.limits.maxImageDimension2D < 4096)) {
            std::cout << "Physical device " << physicalDevice << " doesn't support required parameters!" << std::endl;
            return false;
        }

        auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
        std::vector<vk::Bool32> queuePresentSupport(queueFamilyProperties.size());

        uint32_t graphics_queue_family_index = UINT32_MAX;
        uint32_t present_queue_family_index = UINT32_MAX;

        for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
            queuePresentSupport[i] = physicalDevice.getSurfaceSupportKHR(i, mSurface);

            if ((queueFamilyProperties[i].queueCount > 0) &&
                (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics)) {
                // Select first queue that supports graphics
                if (graphics_queue_family_index == UINT32_MAX) {
                    graphics_queue_family_index = i;
                }

                // If there is queue that supports both graphics and present - prefer it
                if (queuePresentSupport[i]) {
                    selected_graphics_queue_family_index = i;
                    selected_present_queue_family_index = i;
                    return true;
                }
            }
        }

        // We don't have queue that supports both graphics and present so we have to use separate queues
        for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
            if (queuePresentSupport[i]) {
                present_queue_family_index = i;
                break;
            }
        }

        // If this device doesn't support queues with graphics and present capabilities don't use it
        if ((graphics_queue_family_index == UINT32_MAX) || (present_queue_family_index == UINT32_MAX)) {
            std::cout << "Could not find queue family with required properties on physical device " << physicalDevice << "!" << std::endl;
            return false;
        }

        selected_graphics_queue_family_index = graphics_queue_family_index;
        selected_present_queue_family_index = present_queue_family_index;
        return true;
    }

    Sample_03_Window(ApiWithoutSecrets::OS::WindowParameters window, uint32_t width, uint32_t height)
    {
        vk::ApplicationInfo applicationInfo;
        applicationInfo.pApplicationName = "Vulkan sample: Window";
        applicationInfo.pEngineName = "Vulkan";
        applicationInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

        /*
         * Check that al necessary extensions are presented
         */
        std::vector<const char*> extensions = {
#ifndef NDEBUG
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME
        };
        std::cout << "Check extensions...";
        CheckExtensions(extensions);
        std::cout << "OK" << std::endl;

        /*
         * Create Vulkan instance
         * All used extensions should be specified in the create info
         */
        std::cout << "Create Vulkan Instance...";
        vk::InstanceCreateInfo instanceCreateInfo;
        instanceCreateInfo.pApplicationInfo = &applicationInfo;
        instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instanceCreateInfo.ppEnabledExtensionNames = &extensions[0];
#ifndef NDEBUG
        std::vector<const char*> layers = { "VK_LAYER_LUNARG_standard_validation" };
        CheckLayers(layers);
        instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        instanceCreateInfo.ppEnabledLayerNames = &layers[0];
#endif
        mVulkan = vk::createInstance(instanceCreateInfo);
        if (!mVulkan) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }
        std::cout << "OK" << std::endl;

        std::cout << "Find Vulkan physical device...";
        std::vector<vk::PhysicalDevice> devices = mVulkan->enumeratePhysicalDevices();
        if (devices.empty()) {
            throw std::runtime_error("Physical device was not found");
        }
        mPhysicalDevice = devices.front();
        std::cout << "OK" << std::endl;

        /*
         * Create surface for the created window
         * Requires VK_KHR_SURFACE_EXTENSION_NAME and VK_KHR_WIN32_SURFACE_EXTENSION_NAME
         */
        
        mSurface = MakeHolder(mVulkan->createWin32SurfaceKHR(vk::Win32SurfaceCreateInfoKHR(vk::Win32SurfaceCreateFlagsKHR(), window.Instance, window.Handle)),
            [this](vk::SurfaceKHR & surface) { mVulkan->destroySurfaceKHR(surface); });

        /*
         * Choose a queue with supports creating swapchain
         */
        auto queueProperties = mPhysicalDevice.getQueueFamilyProperties();
        CheckPhysicalDeviceProperties(mPhysicalDevice, mQueueFamilyGraphics, mQueueFamilyPresent);
        if (mQueueFamilyPresent >= queueProperties.size()) {
            throw std::runtime_error("Device doesn't support rendering to VkSurface");
        }

        std::cout << "Check device extensions...";
        std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        CheckDeviceExtensions(mPhysicalDevice, deviceExtensions);
        std::cout << "OK" << std::endl;

        /*
        * Create with extension VK_KHR_SWAPCHAIN_EXTENSION_NAMEto enable SwapChain support
        */
        std::cout << "Create logical device...";
        std::vector<float> queuePriorities = { 1.0f };
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.queueFamilyIndex = static_cast<uint32_t>(mQueueFamilyPresent);
        queueCreateInfo.queueCount = static_cast<uint32_t>(queuePriorities.size());
        queueCreateInfo.pQueuePriorities = &queuePriorities[0];
        vk::DeviceCreateInfo deviceCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = &deviceExtensions[0];
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        mDevice = mPhysicalDevice.createDevice(deviceCreateInfo);
        mDevice->waitIdle();
        std::cout << "OK" << std::endl;
        
        /*
        * Retrieve a command queue
        */
        mCommandQueue = mDevice->getQueue(static_cast<uint32_t>(mQueueFamilyPresent), 0);

        /*
        *  https://software.intel.com/en-us/articles/api-without-secrets-introduction-to-vulkan-part-2
        *  To create a swap chain, we call the vkCreateSwapchainKHR() function.
        *  It requires us to provide an address of a variable of type VkSwapchainCreateInfoKHR,
        *  which informs the driver about the properties of a swap chain that is being created.
        *  To fill this structure with the proper values, we must determine what is possible on
        *  a given hardware and platform. To do this we query the platform’s or window’s properties
        *  about the availability of and compatibility with several different features, that is,
        *  supported image formats or present modes (how images are presented on screen).
        *  So before we can create a swap chain we must check what is possible with a given platform
        *  and how we can create a swap chain.
        */

        auto surfaceCapabilities = mPhysicalDevice.getSurfaceCapabilitiesKHR(mSurface);
        if (surfaceCapabilities.maxImageCount < 1) {
            std::runtime_error("Invalid capabilities");
        }
        const uint32_t imagesCount = std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount);
        vk::Extent2D imageSize = vk::Extent2D(width, height);
        if (!(surfaceCapabilities.minImageExtent.width <= imageSize.width  && imageSize.width <= surfaceCapabilities.maxImageExtent.width ||
            surfaceCapabilities.minImageExtent.height <= imageSize.height && imageSize.height <= surfaceCapabilities.maxImageExtent.height)) {
            throw std::runtime_error("Unsupported image extent");
        }
        imageSize = surfaceCapabilities.currentExtent;
        
        auto supportedFormats = mPhysicalDevice.getSurfaceFormatsKHR(mSurface);
        if (supportedFormats.empty()) {
            std::runtime_error("Failed to get supported surface formats");
        }
        const auto format = std::make_pair(vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear);
        if (!CheckFormat(supportedFormats, format)) {
            std::runtime_error("Format BGRA_Unorm/SrgbNonlinear is not supported");
        }

        auto presentModes = mPhysicalDevice.getSurfacePresentModesKHR(mSurface);
        if (presentModes.empty()) {
            std::runtime_error("Failed to get supported surface present modes");
        }

        //Finally!
        /* https://software.intel.com/en-us/articles/api-without-secrets-introduction-to-vulkan-part-2
        * pNext – Pointer reserved for future use (for some extensions to this extension).
        * flags – Value reserved for future use; currently must be set to zero.
        * surface – A handle of a created surface that represents windowing system (our application’s window).
        * minImageCount – Minimal number of images the application requests for a swap chain (must fit into available constraints).
        * imageFormat – Application-selected format for swap chain images; must be one of the supported surface formats.
        * imageColorSpace – Colorspace for swap chain images; only enumerated values of format-colorspace pairs may be used for imageFormat and imageColorSpace (we can’t use format from one pair and colorspace from another pair).
        * imageExtent – Size (dimensions) of swap chain images defined in pixels; must fit into available constraints.
        * imageArrayLayers – Defines the number of layers in a swap chain images (that is, views); typically this value will be one but if we want to create multiview or stereo (stereoscopic 3D) images, we can set it to some higher value.
        * imageUsage – Defines how application wants to use images; it may contain only values of supported usages; color attachment usage is always supported.
        * imageSharingMode – Describes image-sharing mode when multiple queues are referencing images (I will describe this in more detail later).
        * queueFamilyIndexCount – The number of different queue families from which swap chain images will be referenced; this parameter matters only when VK_SHARING_MODE_CONCURRENT sharing mode is used.
        * pQueueFamilyIndices – An array containing all the indices of queue families that will be referencing swap chain images; must contain at least queueFamilyIndexCount elements and as in queueFamilyIndexCount this parameter matters only when VK_SHARING_MODE_CONCURRENT sharing mode is used.
        * preTransform – Transformations applied to the swap chain image before it can be presented; must be one of the supported values.
        * compositeAlpha – This parameter is used to indicate how the surface (image) should be composited (blended?) with other surfaces on some windowing systems; this value must also be one of the possible values (bits) returned in surface capabilities, but it looks like opaque composition (no blending, alpha ignored) will be always supported (as most of the games will want to use this mode).
        * presentMode – Presentation mode that will be used by a swap chain; only supported mode may be selected.
        * clipped – Connected with ownership of pixels; in general it should be set to VK_TRUE if application doesn’t want to read from swap chain images (like ReadPixels()) as it will allow some platforms to use more optimal presentation methods; VK_FALSE value is used in some specific scenarios (if I learn more about these scenario I will write about them).
        * oldSwapchain – If we are recreating a swap chain, this parameter defines an old swap chain that will be replaced by a newly created one.
        */
        std::cout << "Create SwapChain...";
        vk::SwapchainCreateInfoKHR swapchainInfo;
        swapchainInfo.surface = mSurface;
        swapchainInfo.imageExtent = imageSize;
        swapchainInfo.imageFormat = format.first;
        swapchainInfo.imageColorSpace = format.second;
        swapchainInfo.minImageCount = imagesCount;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst);
        swapchainInfo.presentMode = vk::PresentModeKHR::eMailbox;
        swapchainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchainInfo.preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        swapchainInfo.clipped = true;
        mSwapChain = MakeHolder(mDevice->createSwapchainKHR(swapchainInfo), [this](vk::SwapchainKHR & swapchain) { mDevice->destroySwapchainKHR(swapchain); });
        std::cout << "OK" << std::endl;

        auto swapchainImages = mDevice->getSwapchainImagesKHR(mSwapChain);

        std::cout << "Create commands pool...";
        mCommandPool = MakeHolder(mDevice->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlags(), static_cast<uint32_t>(mQueueFamilyPresent))),
            [this](vk::CommandPool & pool) { mDevice->destroyCommandPool(pool); });
        std::cout << "OK" << std::endl;

        mSemaphoreImageAcquired = MakeHolder(mDevice->createSemaphore(vk::SemaphoreCreateInfo()), [this](vk::Semaphore & sem) { mDevice->destroySemaphore(sem); });
        mSemaphoreImageReady    = MakeHolder(mDevice->createSemaphore(vk::SemaphoreCreateInfo()), [this](vk::Semaphore & sem) { mDevice->destroySemaphore(sem); });

        CanRender = true;

    }

    bool OnWindowSizeChanged() override 
    {
        return true;
    }

    bool Draw() override
    {
        // Get current frame color
        vk::ClearColorValue targetColor = GetNextColor();

        // Firstly aqcuire next image
        // It's index will be necessary for command configuration
        constexpr uint64_t TIMEOUT = 1 * 1000 * 1000 * 1000; // 1 second in nanos
        auto swapchainImages = mDevice->getSwapchainImagesKHR(mSwapChain);
        auto imageIdx = mDevice->acquireNextImageKHR(mSwapChain, TIMEOUT, mSemaphoreImageAcquired, nullptr);
        if (imageIdx.result != vk::Result::eSuccess) {
            std::cout << "Failed to acquire image! Stoppping." << std::endl;
            return false;
        }

        // Prepare command
        auto commandBuffers = MakeHolder(mDevice->allocateCommandBuffers(vk::CommandBufferAllocateInfo(mCommandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(1))),
            [this](std::vector<vk::CommandBuffer> & buffers) { mDevice->freeCommandBuffers(mCommandPool, buffers); });
        auto cmdBuffer = commandBuffers->front();

        {
            vk::ImageSubresourceRange range;
            range.aspectMask = vk::ImageAspectFlagBits::eColor;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            cmdBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); 

            vk::ImageMemoryBarrier barrierFromPresentToClear;
            barrierFromPresentToClear.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
            barrierFromPresentToClear.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrierFromPresentToClear.oldLayout = vk::ImageLayout::eUndefined;
            barrierFromPresentToClear.newLayout = vk::ImageLayout::eTransferDstOptimal;
            barrierFromPresentToClear.srcQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromPresentToClear.dstQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromPresentToClear.image = swapchainImages[imageIdx.value];
            barrierFromPresentToClear.subresourceRange = range;
            cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromPresentToClear);

            cmdBuffer.clearColorImage(swapchainImages[imageIdx.value], vk::ImageLayout::eTransferDstOptimal, &targetColor, 1, &range);

            vk::ImageMemoryBarrier barrierFromClearToPresent;
            barrierFromClearToPresent.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrierFromClearToPresent.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
            barrierFromClearToPresent.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrierFromClearToPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
            barrierFromClearToPresent.srcQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromClearToPresent.dstQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromClearToPresent.image = swapchainImages[imageIdx.value];
            barrierFromClearToPresent.subresourceRange = range;
            cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromClearToPresent);

            cmdBuffer.end();
        }

        // Now submit the newly created command

        vk::PipelineStageFlags waitDstStageMask = vk::PipelineStageFlagBits::eTransfer;

        vk::SubmitInfo submitInfo;
        submitInfo.pWaitDstStageMask = &waitDstStageMask;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = mSemaphoreImageAcquired.get();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = mSemaphoreImageReady.get();
        if (vk::Result::eSuccess != mCommandQueue.submit(1, &submitInfo, nullptr)) {
            std::cout << "Failed to submit command! Stoppping." << std::endl;
            return false;
        }

        // Present image

        vk::PresentInfoKHR presentInfo;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = mSemaphoreImageReady.get();
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = mSwapChain.get();
        presentInfo.pImageIndices = &imageIdx.value;
        auto result = mCommandQueue.presentKHR(&presentInfo);
        if (result != vk::Result::eSuccess) {
            std::cout << "Failed to present image! Stoppping." << std::endl;
            return false;
        }

        mDevice->waitIdle(); // bruteforce

        return true;
    }

    void Shutdown() override
    {
        if (*mDevice) {
            mDevice->waitIdle();
        }
    }
};

int main() {
    try {
        ApiWithoutSecrets::OS::Window window;
        // Window creation
        if (!window.Create("04 - Dynamic command buffers", 512, 512)) {
            return -1;
        }

        // Render loop
        Sample_03_Window application(window.GetParameters(), 512, 512);
        if (!window.RenderingLoop(application)) {
            return -1;
        }
    }
    catch (std::runtime_error & err) {
        std::cout << "Error!" << std::endl;
        std::cout << err.what() << std::endl;
    }
}
