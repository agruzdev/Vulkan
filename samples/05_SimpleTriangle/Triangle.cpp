/**
* Vulkan samples
*
* The MIT License (MIT)
* Copyright (c) 2016 Alexey Gruzdev
*/

#include <chrono>
#include <iostream>
#include <limits>
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

    VulkanHolder<vk::RenderPass> mRenderPass;

    std::vector<VulkanHolder<vk::ImageView>> mImageViews;
    std::vector<VulkanHolder<vk::Framebuffer>> mFramebuffers;

    VulkanHolder<vk::ShaderModule> mVertexShader;
    VulkanHolder<vk::ShaderModule> mFragmentShader;

    VulkanHolder<vk::PipelineLayout> mPipelineLayout;
    VulkanHolder<vk::Pipeline> mPipeline;

    VulkanHolder<vk::CommandPool> mCommandPool;
    VulkanHolder<std::vector<vk::CommandBuffer>> mCommandBuffers;

    VulkanHolder<vk::Semaphore> mSemaphoreImageAcquired;
    VulkanHolder<vk::Semaphore> mSemaphoreImageReady;

    vk::PhysicalDevice mPhysicalDevice;
    vk::Queue mCommandQueue;

    uint32_t mQueueFamilyGraphics = std::numeric_limits<uint32_t>::max();
    uint32_t mQueueFamilyPresent  = std::numeric_limits<uint32_t>::max();

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

    /** 
     * Load shader module from SPIR-V file
     */
    VulkanHolder<vk::ShaderModule> LoadShader(const std::string & filename)
    {
        auto code = GetBinaryFileContents(filename);
        if (code.empty()) {
            throw std::runtime_error("LoadShader: Failed to read shader file!");
        }
        vk::ShaderModuleCreateInfo shaderInfo;
        shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
        shaderInfo.setCodeSize(code.size());

        return MakeHolder(mDevice->createShaderModule(shaderInfo), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader); });
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
        if (mQueueFamilyGraphics >= queueProperties.size()) {
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
        *  a given hardware and platform. To do this we query the platform�s or window�s properties
        *  about the availability of and compatibility with several different features, that is,
        *  supported image formats or present modes (how images are presented on screen).
        *  So before we can create a swap chain we must check what is possible with a given platform
        *  and how we can create a swap chain.
        */

        auto surfaceCapabilities = mPhysicalDevice.getSurfaceCapabilitiesKHR(mSurface);
        if (surfaceCapabilities.maxImageCount < 1) {
            throw std::runtime_error("Invalid capabilities");
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
            throw std::runtime_error("Failed to get supported surface formats");
        }
        const auto format = std::make_pair(vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear);
        if (!CheckFormat(supportedFormats, format)) {
            throw std::runtime_error("Format BGRA_Unorm/SrgbNonlinear is not supported");
        }

        auto presentModes = mPhysicalDevice.getSurfacePresentModesKHR(mSurface);
        if (presentModes.empty()) {
            throw std::runtime_error("Failed to get supported surface present modes");
        }

        //Finally!
        /* https://software.intel.com/en-us/articles/api-without-secrets-introduction-to-vulkan-part-2
        * pNext � Pointer reserved for future use (for some extensions to this extension).
        * flags � Value reserved for future use; currently must be set to zero.
        * surface � A handle of a created surface that represents windowing system (our application�s window).
        * minImageCount � Minimal number of images the application requests for a swap chain (must fit into available constraints).
        * imageFormat � Application-selected format for swap chain images; must be one of the supported surface formats.
        * imageColorSpace � Colorspace for swap chain images; only enumerated values of format-colorspace pairs may be used for imageFormat and imageColorSpace (we can�t use format from one pair and colorspace from another pair).
        * imageExtent � Size (dimensions) of swap chain images defined in pixels; must fit into available constraints.
        * imageArrayLayers � Defines the number of layers in a swap chain images (that is, views); typically this value will be one but if we want to create multiview or stereo (stereoscopic 3D) images, we can set it to some higher value.
        * imageUsage � Defines how application wants to use images; it may contain only values of supported usages; color attachment usage is always supported.
        * imageSharingMode � Describes image-sharing mode when multiple queues are referencing images (I will describe this in more detail later).
        * queueFamilyIndexCount � The number of different queue families from which swap chain images will be referenced; this parameter matters only when VK_SHARING_MODE_CONCURRENT sharing mode is used.
        * pQueueFamilyIndices � An array containing all the indices of queue families that will be referencing swap chain images; must contain at least queueFamilyIndexCount elements and as in queueFamilyIndexCount this parameter matters only when VK_SHARING_MODE_CONCURRENT sharing mode is used.
        * preTransform � Transformations applied to the swap chain image before it can be presented; must be one of the supported values.
        * compositeAlpha � This parameter is used to indicate how the surface (image) should be composited (blended?) with other surfaces on some windowing systems; this value must also be one of the possible values (bits) returned in surface capabilities, but it looks like opaque composition (no blending, alpha ignored) will be always supported (as most of the games will want to use this mode).
        * presentMode � Presentation mode that will be used by a swap chain; only supported mode may be selected.
        * clipped � Connected with ownership of pixels; in general it should be set to VK_TRUE if application doesn�t want to read from swap chain images (like ReadPixels()) as it will allow some platforms to use more optimal presentation methods; VK_FALSE value is used in some specific scenarios (if I learn more about these scenario I will write about them).
        * oldSwapchain � If we are recreating a swap chain, this parameter defines an old swap chain that will be replaced by a newly created one.
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

        std::cout << "Create command buffers...";
        mCommandPool = MakeHolder(mDevice->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlags(), static_cast<uint32_t>(mQueueFamilyPresent))),
            [this](vk::CommandPool & pool) { mDevice->destroyCommandPool(pool); });
        mCommandBuffers = MakeHolder(mDevice->allocateCommandBuffers(vk::CommandBufferAllocateInfo(mCommandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(swapchainImages.size()))),
            [this](std::vector<vk::CommandBuffer> & buffers) { mDevice->freeCommandBuffers(mCommandPool, buffers); });
        if (mCommandBuffers->size() != static_cast<uint32_t>(swapchainImages.size())) {
            throw std::runtime_error("Failed to create all command buffers");
        }
        std::cout << "OK" << std::endl;


        /* Setting up a render pass now
         * https://software.intel.com/en-us/articles/api-without-secrets-introduction-to-vulkan-part-3
         * What is a render pass? A general picture can give us a �logical� render pass that may be found 
         * in many known rendering techniques like deferred shading. This technique consists of many subpasses. 
         * The first subpass draws the geometry with shaders that fill the G-Buffer: store diffuse color 
         * in one texture, normal vectors in another, shininess in another, depth (position) in yet another. 
         * Next for each light source, drawing is performed that reads some of the data (normal vectors, 
         * shininess, depth/position), calculates lighting and stores it in another texture. 
         * Final pass aggregates lighting data with diffuse color. This is a (very rough) explanation of 
         * deferred shading but describes the render pass�a set of data required to perform some drawing 
         * operations: storing data in textures and reading data from other textures.
         */
        {
            std::cout << "Create render pass... ";
            vk::AttachmentDescription colorAttachmentDesc;
            colorAttachmentDesc.setFormat(format.first);
            colorAttachmentDesc.setSamples(vk::SampleCountFlagBits::e1);
            colorAttachmentDesc.setLoadOp(vk::AttachmentLoadOp::eClear);
            colorAttachmentDesc.setStoreOp(vk::AttachmentStoreOp::eStore);
            colorAttachmentDesc.setInitialLayout(vk::ImageLayout::ePresentSrcKHR);
            colorAttachmentDesc.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

            // 0 is index into an vk::AttachmentDescriptions array of vk::RenderPassCreateInfo.
            vk::AttachmentReference colorAttachmentRef = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);

            vk::SubpassDescription subpass;
            subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
            subpass.setInputAttachmentCount(0);
            subpass.setColorAttachmentCount(1);
            subpass.setPColorAttachments(&colorAttachmentRef);

            vk::RenderPassCreateInfo renderPassInfo;
            renderPassInfo.setAttachmentCount(1);
            renderPassInfo.setPAttachments(&colorAttachmentDesc);
            renderPassInfo.setSubpassCount(1);
            renderPassInfo.setPSubpasses(&subpass);

            mRenderPass = MakeHolder(mDevice->createRenderPass(renderPassInfo), [this](vk::RenderPass & rpass) { mDevice->destroyRenderPass(rpass); });
            std::cout << "OK" << std::endl;
        }

        const uint32_t frameBufferWidth  = imageSize.width;
        const uint32_t frameBufferHeight = imageSize.height;
        {
            std::cout << "Create framebuffers... ";
            mImageViews.resize(swapchainImages.size());
            mFramebuffers.resize(swapchainImages.size());
            for (size_t i = 0; i < swapchainImages.size(); ++i) {
                vk::ImageViewCreateInfo imageViewInfo;
                imageViewInfo.setImage(swapchainImages[i]);
                imageViewInfo.setViewType(vk::ImageViewType::e2D);
                imageViewInfo.setFormat(format.first);
                imageViewInfo.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity));
                imageViewInfo.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

                mImageViews[i] = MakeHolder(mDevice->createImageView(imageViewInfo), [this](vk::ImageView & view) { mDevice->destroyImageView(view); });

                vk::FramebufferCreateInfo framebufferInfo;
                framebufferInfo.setRenderPass(*mRenderPass);
                framebufferInfo.setAttachmentCount(1);
                framebufferInfo.setPAttachments(mImageViews[i].get());
                framebufferInfo.setWidth(frameBufferWidth);
                framebufferInfo.setHeight(frameBufferHeight);
                framebufferInfo.setLayers(1);

                mFramebuffers[i] = MakeHolder(mDevice->createFramebuffer(framebufferInfo), [this](vk::Framebuffer & f) { mDevice->destroyFramebuffer(f); });
            }
            std::cout << "OK" << std::endl;
        }

        /* Create graphics pipeline now
         */

        std::cout << "Loading vertex shader... ";
        mVertexShader = LoadShader(QUOTE(SHADERS_DIR) "/spv/05.vert.spv");
        std::cout << "OK" << std::endl;

        std::cout << "Loading fragment shader... ";
        mFragmentShader = LoadShader(QUOTE(SHADERS_DIR) "/spv/05.frag.spv");
        std::cout << "OK" << std::endl;

        /**
         * A pipeline is a collection of stages that process data one stage after another. 
         * In Vulkan there is currently a compute pipeline and a graphics pipeline. 
         * The compute pipeline allows us to perform some computational work, such as 
         * performing physics calculations for objects in games. The graphics 
         * pipeline is used for drawing operations.
         */
        {
            std::cout << "Create pipeline... ";

            vk::PipelineShaderStageCreateInfo stageInfos[2];
            stageInfos[0].setStage(vk::ShaderStageFlagBits::eVertex);
            stageInfos[0].setModule(mVertexShader);
            stageInfos[0].setPName("main"); // Shader entry point

            stageInfos[1].setStage(vk::ShaderStageFlagBits::eFragment);
            stageInfos[1].setModule(mFragmentShader);
            stageInfos[1].setPName("main"); // Shader entry point

            // by default because verticles are hardcoded in shader
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo; 

            vk::PipelineInputAssemblyStateCreateInfo inputAssembleInfo;
            inputAssembleInfo.setTopology(vk::PrimitiveTopology::eTriangleList);

            vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(frameBufferWidth), static_cast<float>(frameBufferHeight), 0.0f, 1.0f);

            vk::Rect2D scissor(vk::Offset2D(0, 0), vk::Extent2D(frameBufferWidth, frameBufferHeight));

            vk::PipelineViewportStateCreateInfo viewportInfo;
            viewportInfo.setViewportCount(1);  
            viewportInfo.setPViewports(&viewport);
            viewportInfo.setScissorCount(1);
            viewportInfo.setPScissors(&scissor);
            // the viewportCount and scissorCount parameters must be equal

            vk::PipelineRasterizationStateCreateInfo rasterizationInfo;
            rasterizationInfo.setDepthClampEnable(VK_FALSE);
            rasterizationInfo.setCullMode(vk::CullModeFlagBits::eBack);
            rasterizationInfo.setPolygonMode(vk::PolygonMode::eFill);
            rasterizationInfo.setFrontFace(vk::FrontFace::eCounterClockwise);
            rasterizationInfo.setLineWidth(1.0f);

            vk::PipelineMultisampleStateCreateInfo multisampleInfo;
            multisampleInfo.setRasterizationSamples(vk::SampleCountFlagBits::e1);
            multisampleInfo.setMinSampleShading(1.0f);

            vk::PipelineColorBlendAttachmentState colorAttachmentBlending;
            colorAttachmentBlending.setBlendEnable(VK_FALSE);
            colorAttachmentBlending.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

            vk::PipelineColorBlendStateCreateInfo blendingInfo;
            blendingInfo.setLogicOpEnable(VK_FALSE);
            blendingInfo.setAttachmentCount(1);
            blendingInfo.setPAttachments(&colorAttachmentBlending);

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
            mPipelineLayout = MakeHolder(mDevice->createPipelineLayout(pipelineLayoutInfo), [this](vk::PipelineLayout & layout) { mDevice->destroyPipelineLayout(layout); });

            vk::GraphicsPipelineCreateInfo grapichsPipelineInfo;
            grapichsPipelineInfo.setStageCount(2);
            grapichsPipelineInfo.setPStages(&stageInfos[0]);
            grapichsPipelineInfo.setPVertexInputState(&vertexInputInfo);
            grapichsPipelineInfo.setPInputAssemblyState(&inputAssembleInfo);
            grapichsPipelineInfo.setPViewportState(&viewportInfo);
            grapichsPipelineInfo.setPRasterizationState(&rasterizationInfo);
            grapichsPipelineInfo.setPMultisampleState(&multisampleInfo);
            grapichsPipelineInfo.setPColorBlendState(&blendingInfo);
            grapichsPipelineInfo.setLayout(mPipelineLayout);
            grapichsPipelineInfo.setRenderPass(mRenderPass);
            
            mPipeline = MakeHolder(mDevice->createGraphicsPipeline(vk::PipelineCache(), grapichsPipelineInfo), [this](vk::Pipeline & pipeline) {mDevice->destroyPipeline(pipeline); });

            std::cout << "OK" << std::endl;
        }

        /* Preaparing command buffers
         */

        std::cout << "Prepare command buffers...";
        {
            vk::ClearColorValue targetColor = std::array<float, 4>{ 0.1f, 1.0f, 0.1f, 1.0f };
            vk::ClearValue clearValue(targetColor);

            vk::ImageSubresourceRange range;
            range.aspectMask = vk::ImageAspectFlagBits::eColor;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            for (uint32_t idx = 0; idx < static_cast<uint32_t>(mCommandBuffers->size()); ++idx) {
                vk::CommandBuffer & cmdBuffer = (*mCommandBuffers)[idx];
                cmdBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse));

                if (mQueueFamilyPresent != mQueueFamilyGraphics) {
                    vk::ImageMemoryBarrier barrierFromPresentToDraw;
                    barrierFromPresentToDraw.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
                    barrierFromPresentToDraw.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
                    barrierFromPresentToDraw.oldLayout = vk::ImageLayout::ePresentSrcKHR;
                    barrierFromPresentToDraw.newLayout = vk::ImageLayout::ePresentSrcKHR;
                    barrierFromPresentToDraw.srcQueueFamilyIndex = mQueueFamilyPresent;
                    barrierFromPresentToDraw.dstQueueFamilyIndex = mQueueFamilyGraphics;
                    barrierFromPresentToDraw.image = swapchainImages[idx];
                    barrierFromPresentToDraw.subresourceRange = range;
                    cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromPresentToDraw);
                }
                vk::RenderPassBeginInfo renderPassInfo;
                renderPassInfo.setRenderPass(mRenderPass);
                renderPassInfo.setFramebuffer(mFramebuffers[idx]);
                renderPassInfo.setRenderArea(vk::Rect2D(vk::Offset2D(0, 0), vk::Extent2D(frameBufferWidth, frameBufferHeight)));
                renderPassInfo.setClearValueCount(1);
                renderPassInfo.setPClearValues(&clearValue);
                cmdBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

                //cmdBuffer.clearColorImage(swapchainImages[idx], vk::ImageLayout::ePresentSrcKHR, &targetColor, 1, &range);

                cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, mPipeline);

                cmdBuffer.draw(3, 1, 0, 0);

                cmdBuffer.endRenderPass();

                if (mQueueFamilyPresent != mQueueFamilyGraphics) {
                    vk::ImageMemoryBarrier barrierFromDrawToPresent;
                    barrierFromDrawToPresent.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
                    barrierFromDrawToPresent.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
                    barrierFromDrawToPresent.oldLayout = vk::ImageLayout::ePresentSrcKHR;
                    barrierFromDrawToPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
                    barrierFromDrawToPresent.srcQueueFamilyIndex = mQueueFamilyGraphics;
                    barrierFromDrawToPresent.dstQueueFamilyIndex = mQueueFamilyPresent;
                    barrierFromDrawToPresent.image = swapchainImages[idx];
                    barrierFromDrawToPresent.subresourceRange = range;
                    cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromDrawToPresent);
                }
                cmdBuffer.end();
            }
        }
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
        constexpr uint64_t TIMEOUT = 1 * 1000 * 1000 * 1000; // 1 second in nanos
        auto imageIdx = mDevice->acquireNextImageKHR(mSwapChain, TIMEOUT, mSemaphoreImageAcquired, nullptr);
        if (imageIdx.result != vk::Result::eSuccess) {
            std::cout << "Failed to acquire image! Stoppping." << std::endl;
            return false;
        }

        vk::PipelineStageFlags waitDstStageMask = vk::PipelineStageFlagBits::eTransfer;

        vk::SubmitInfo submitInfo;
        submitInfo.pWaitDstStageMask = &waitDstStageMask;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = mSemaphoreImageAcquired.get();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &(*mCommandBuffers)[imageIdx.value];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = mSemaphoreImageReady.get();
        if (vk::Result::eSuccess != mCommandQueue.submit(1, &submitInfo, nullptr)) {
            std::cout << "Failed to submit command! Stoppping." << std::endl;
            return false;
        }

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
        if (!window.Create("05 - Simple triangle", 512, 512)) {
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
