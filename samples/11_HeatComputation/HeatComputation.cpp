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
    struct RenderingResource
    {
        vk::Image imageHandle;
        VulkanHolder<vk::ImageView> imageView;
        VulkanHolder<vk::CommandBuffer> commandBuffer;
        VulkanHolder<vk::Semaphore> semaphoreAvailable;
        VulkanHolder<vk::Semaphore> semaphoreFinished;
        VulkanHolder<vk::Fence> fence;
        VulkanHolder<vk::DescriptorSet> descriptorSet;
        bool undefinedLaout;
    };

    struct ComputeResource
    {
        VulkanHolder<vk::Image> image;
        VulkanHolder<vk::DeviceMemory> memory;
        VulkanHolder<vk::ImageView> view;
        VulkanHolder<vk::Sampler> sampler;
    };

    VulkanHolder<vk::Instance> mVulkan;
    VulkanHolder<vk::Device> mDevice;
    VulkanHolder<vk::SurfaceKHR> mSurface;
    VulkanHolder<vk::SwapchainKHR> mSwapChain;

    VulkanHolder<vk::ShaderModule> mConversionShader;
    VulkanHolder<vk::ShaderModule> mHeatIterationShader;

    VulkanHolder<vk::DescriptorSetLayout> mDescriptorSetLayout;
    VulkanHolder<vk::DescriptorSetLayout> mIterationDescriptorSetLayout;
    
    VulkanHolder<vk::DescriptorPool> mDescriptorPool;
    VulkanHolder<vk::DescriptorSet> mIterationDescriptorSet;

    // Conversion
    VulkanHolder<vk::PipelineLayout> mConversionPipelineLayout;
    VulkanHolder<vk::Pipeline> mConversionPipeline;

    //Iteration
    VulkanHolder<vk::PipelineLayout> mIterationPipelineLayout;
    VulkanHolder<vk::Pipeline> mIterationPipeline;

    VulkanHolder<vk::CommandPool> mCommandPool;

    std::array<ComputeResource, 2> mComputeResources; // ping-pong
    uint32_t mNextComputeResIdx = 0;

    VulkanHolder<vk::Image> mInitialImage;
    VulkanHolder<vk::DeviceMemory> mInitialImageMemory;

    std::vector<RenderingResource> mRenderingResources;
    decltype(mRenderingResources)::iterator mRenderingResourceIter;

    vk::PhysicalDevice mPhysicalDevice;
    vk::Queue mCommandQueue;

    uint32_t mQueueFamilyGraphics = std::numeric_limits<uint32_t>::max();
    uint32_t mQueueFamilyPresent  = std::numeric_limits<uint32_t>::max();

    vk::Extent2D mFramebufferExtents;
    vk::Extent2D mComputeImageExtents;

    bool mFirstDraw = true;

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
        (void)width;
        (void)height;

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
        // check that image can be used as image2D in compute shaders VK_IMAGE_USAGE_STORAGE_BIT 
        // https://redd.it/5nd7tj
        if (!(surfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eStorage)) {
            std::runtime_error("ImageUsageFlagBits::eStorage is not supported by swapchain");
        }
        //const uint32_t imagesCount = std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount);
        const uint32_t imagesCount = 2;
        //vk::Extent2D imageSize = vk::Extent2D(width, height);
        //if (!(surfaceCapabilities.minImageExtent.width <= imageSize.width  && imageSize.width <= surfaceCapabilities.maxImageExtent.width ||
        //    surfaceCapabilities.minImageExtent.height <= imageSize.height && imageSize.height <= surfaceCapabilities.maxImageExtent.height)) {
        //    throw std::runtime_error("Unsupported image extent");
        //}
        vk::Extent2D imageSize = surfaceCapabilities.currentExtent;
        
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
        swapchainInfo.imageUsage = vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage);
        swapchainInfo.presentMode = vk::PresentModeKHR::eMailbox;
        swapchainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchainInfo.preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        swapchainInfo.clipped = true;
        mSwapChain = MakeHolder(mDevice->createSwapchainKHR(swapchainInfo), [this](vk::SwapchainKHR & swapchain) { mDevice->destroySwapchainKHR(swapchain); });
        std::cout << "OK" << std::endl;

        auto swapchainImages = mDevice->getSwapchainImagesKHR(mSwapChain);

        mRenderingResources.resize(swapchainImages.size());
        mFramebufferExtents = imageSize;


        std::cout << "Loading shader... ";
        {
            {
                auto code = GetBinaryFileContents(QUOTE(SHADERS_DIR) "/spv/11.cvt.comp.spv");
                if (code.empty()) {
                    throw std::runtime_error("LoadShader: Failed to read shader file!");
                }
                vk::ShaderModuleCreateInfo shaderInfo;
                shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
                shaderInfo.setCodeSize(code.size());

                mConversionShader = MakeHolder(mDevice->createShaderModule(shaderInfo), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader); });
            }
            {
                auto code = GetBinaryFileContents(QUOTE(SHADERS_DIR) "/spv/11.heat.comp.spv");
                if (code.empty()) {
                    throw std::runtime_error("LoadShader: Failed to read shader file!");
                }
                vk::ShaderModuleCreateInfo shaderInfo;
                shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
                shaderInfo.setCodeSize(code.size());

                mHeatIterationShader = MakeHolder(mDevice->createShaderModule(shaderInfo), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader); });
            }
            // Descriptors layout for conversion
            {
                std::array<vk::DescriptorSetLayoutBinding, 2> bindings;

                // Conversion shader, input sampler
                bindings[0].setBinding(0);
                bindings[0].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
                bindings[0].setDescriptorCount(1);
                bindings[0].setStageFlags(vk::ShaderStageFlagBits::eCompute);

                // Conversion shader, destination image (swapchain)
                bindings[1].setBinding(1);
                bindings[1].setDescriptorType(vk::DescriptorType::eStorageImage);
                bindings[1].setDescriptorCount(1);
                bindings[1].setStageFlags(vk::ShaderStageFlagBits::eCompute);

                vk::DescriptorSetLayoutCreateInfo descriptorSetInfo;
                descriptorSetInfo.setBindingCount(static_cast<uint32_t>(bindings.size()));
                descriptorSetInfo.setPBindings(&bindings[0]);
                mDescriptorSetLayout = MakeHolder(mDevice->createDescriptorSetLayout(descriptorSetInfo), [this](vk::DescriptorSetLayout & layout) { mDevice->destroyDescriptorSetLayout(layout); });
            }

            // Descriptors layout for conversion
            {
                std::array<vk::DescriptorSetLayoutBinding, 2> bindings;

                // Iteration shader, input
                bindings[0].setBinding(2);
                bindings[0].setDescriptorType(vk::DescriptorType::eStorageImage);
                bindings[0].setDescriptorCount(1);
                bindings[0].setStageFlags(vk::ShaderStageFlagBits::eCompute);

                // Iteration shader, output
                bindings[1].setBinding(3);
                bindings[1].setDescriptorType(vk::DescriptorType::eStorageImage);
                bindings[1].setDescriptorCount(1);
                bindings[1].setStageFlags(vk::ShaderStageFlagBits::eCompute);

                vk::DescriptorSetLayoutCreateInfo descriptorSetInfo;
                descriptorSetInfo.setBindingCount(static_cast<uint32_t>(bindings.size()));
                descriptorSetInfo.setPBindings(&bindings[0]);
                mIterationDescriptorSetLayout = MakeHolder(mDevice->createDescriptorSetLayout(descriptorSetInfo), [this](vk::DescriptorSetLayout & layout) { mDevice->destroyDescriptorSetLayout(layout); });
            }

            // Pool
            {
                std::array<vk::DescriptorPoolSize, 2> poolSize;
                poolSize[0].setType(vk::DescriptorType::eCombinedImageSampler);
                poolSize[0].setDescriptorCount(static_cast<uint32_t>(swapchainImages.size()));
                poolSize[1].setType(vk::DescriptorType::eStorageImage);
                poolSize[1].setDescriptorCount(3 * static_cast<uint32_t>(swapchainImages.size()));

                vk::DescriptorPoolCreateInfo poolInfo;
                poolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
                poolInfo.setMaxSets(1 + static_cast<uint32_t>(swapchainImages.size()));
                poolInfo.setPoolSizeCount(static_cast<uint32_t>(poolSize.size()));
                poolInfo.setPPoolSizes(&poolSize[0]);

                mDescriptorPool = MakeHolder(mDevice->createDescriptorPool(poolInfo), [this](vk::DescriptorPool & pool) { mDevice->destroyDescriptorPool(pool); });
            }

            // Descriptors set for iteration
            {
                vk::DescriptorSetAllocateInfo allocInfo;
                allocInfo.setDescriptorPool(mDescriptorPool);
                allocInfo.setDescriptorSetCount(1);
                allocInfo.setPSetLayouts(mIterationDescriptorSetLayout.get());

                vk::DescriptorSet decriptorSetTmp;
                if (vk::Result::eSuccess != mDevice->allocateDescriptorSets(&allocInfo, &decriptorSetTmp)) {
                    throw std::runtime_error("Failed to allocate descriptors set");
                }
                mIterationDescriptorSet = MakeHolder(decriptorSetTmp, [this](vk::DescriptorSet & set) { mDevice->freeDescriptorSets(mDescriptorPool, set); });
            }

            std::cout << "OK" << std::endl;
        }

        std::cout << "Create conversion pipeline...";
        {
            vk::PipelineShaderStageCreateInfo stageInfos[2];
            stageInfos[0].setStage(vk::ShaderStageFlagBits::eCompute);
            stageInfos[0].setModule(mConversionShader);
            stageInfos[0].setPName("main"); // Shader entry point

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
            pipelineLayoutInfo.setSetLayoutCount(1);
            pipelineLayoutInfo.setPSetLayouts(mDescriptorSetLayout.get());
            mConversionPipelineLayout = MakeHolder(mDevice->createPipelineLayout(pipelineLayoutInfo), [this](vk::PipelineLayout & layout) { mDevice->destroyPipelineLayout(layout); });

            vk::ComputePipelineCreateInfo computePipelineInfo;
            computePipelineInfo.setStage(stageInfos[0]);
            computePipelineInfo.setLayout(mConversionPipelineLayout);
            mConversionPipeline = MakeHolder(mDevice->createComputePipeline(vk::PipelineCache(), computePipelineInfo), [this](vk::Pipeline & pipeline) {mDevice->destroyPipeline(pipeline); });

            std::cout << "OK" << std::endl;
        }

        std::cout << "Create iteration pipeline...";
        {
            vk::PipelineShaderStageCreateInfo stageInfos[2];
            stageInfos[0].setStage(vk::ShaderStageFlagBits::eCompute);
            stageInfos[0].setModule(mHeatIterationShader);
            stageInfos[0].setPName("main"); // Shader entry point

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
            pipelineLayoutInfo.setSetLayoutCount(1);
            pipelineLayoutInfo.setPSetLayouts(mIterationDescriptorSetLayout.get());
            mIterationPipelineLayout = MakeHolder(mDevice->createPipelineLayout(pipelineLayoutInfo), [this](vk::PipelineLayout & layout) { mDevice->destroyPipelineLayout(layout); });

            vk::ComputePipelineCreateInfo computePipelineInfo;
            computePipelineInfo.setStage(stageInfos[0]);
            computePipelineInfo.setLayout(mIterationPipelineLayout);
            mIterationPipeline = MakeHolder(mDevice->createComputePipeline(vk::PipelineCache(), computePipelineInfo), [this](vk::Pipeline & pipeline) {mDevice->destroyPipeline(pipeline); });

            std::cout << "OK" << std::endl;
        }

        std::cout << "Create command buffers...";
        {
            vk::CommandPoolCreateInfo commandsPoolInfo;
            commandsPoolInfo.setQueueFamilyIndex(mQueueFamilyPresent);
            commandsPoolInfo.setFlags(vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
            mCommandPool = MakeHolder(mDevice->createCommandPool(commandsPoolInfo), [this](vk::CommandPool & pool) { mDevice->destroyCommandPool(pool); });

            for (auto & resource : mRenderingResources) {
                vk::CommandBufferAllocateInfo allocateInfo;
                allocateInfo.setCommandPool(mCommandPool);
                allocateInfo.setLevel(vk::CommandBufferLevel::ePrimary);
                allocateInfo.setCommandBufferCount(1);

                vk::CommandBuffer buffer;
                if (vk::Result::eSuccess != mDevice->allocateCommandBuffers(&allocateInfo, &buffer)) {
                    throw std::runtime_error("Failed to create command buffers");
                }
                resource.commandBuffer = VulkanHolder<vk::CommandBuffer>(buffer, [this](vk::CommandBuffer & buffer) { mDevice->freeCommandBuffers(mCommandPool, 1, &buffer); });
            }
            std::cout << "OK" << std::endl;
        }

        /**
         * Create two images to use as ping-pong buffer for computations
         * Images size can be arbitrary. Sampler2D with normalized coordinates will be used to access data and display on the screen
         */
        std::cout << "Create buffers...";
        {
            mComputeImageExtents = vk::Extent2D(256, 256);

            for (auto & computeResource : mComputeResources) {
                vk::ImageCreateInfo imageInfo;
                imageInfo.setImageType(vk::ImageType::e2D);
                imageInfo.setExtent(vk::Extent3D(mComputeImageExtents.width, mComputeImageExtents.height, 1));
                imageInfo.setMipLevels(1);
                imageInfo.setArrayLayers(1);
                imageInfo.setFormat(vk::Format::eR32Sfloat);
                imageInfo.setTiling(vk::ImageTiling::eOptimal);
                imageInfo.setInitialLayout(vk::ImageLayout::ePreinitialized);
                imageInfo.setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
                imageInfo.setSharingMode(vk::SharingMode::eExclusive);
                imageInfo.setSamples(vk::SampleCountFlagBits::e1);

                computeResource.image = MakeHolder(mDevice->createImage(imageInfo), [this](vk::Image & img) { mDevice->destroyImage(img); });

                vk::MemoryRequirements imageMemoryRequirments = mDevice->getImageMemoryRequirements(computeResource.image);
                vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties();
                for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                    if ((imageMemoryRequirments.memoryTypeBits & (1 << i)) &&
                        (memroProperties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eDeviceLocal))) {

                        vk::MemoryAllocateInfo allocateInfo;
                        allocateInfo.setAllocationSize(imageMemoryRequirments.size);
                        allocateInfo.setMemoryTypeIndex(i);
                        computeResource.memory = MakeHolder(mDevice->allocateMemory(allocateInfo), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory); });
                    }
                }
                if (!computeResource.memory) {
                    throw std::runtime_error("Failed to allocate memory for compute image");
                }
                mDevice->bindImageMemory(computeResource.image, computeResource.memory, 0);

                vk::ImageSubresourceRange range;
                range.setAspectMask(vk::ImageAspectFlagBits::eColor);
                range.setBaseMipLevel(0);
                range.setLevelCount(1);
                range.setBaseArrayLayer(0);
                range.setLayerCount(1);

                vk::ImageViewCreateInfo viewInfo;
                viewInfo.setImage(computeResource.image);
                viewInfo.setViewType(vk::ImageViewType::e2D);
                viewInfo.setFormat(vk::Format::eR32Sfloat);
                viewInfo.setSubresourceRange(range);
                computeResource.view = MakeHolder(mDevice->createImageView(viewInfo), [this](vk::ImageView & view) { mDevice->destroyImageView(view); });

                // https://vulkan-tutorial.com/Texture_mapping/Image_view_and_sampler
                vk::SamplerCreateInfo samplerInfo;
                samplerInfo.setMagFilter(vk::Filter::eLinear);
                samplerInfo.setMinFilter(vk::Filter::eLinear);
                samplerInfo.setAddressModeU(vk::SamplerAddressMode::eClampToBorder);
                samplerInfo.setAddressModeV(vk::SamplerAddressMode::eClampToBorder);
                samplerInfo.setAddressModeW(vk::SamplerAddressMode::eClampToBorder);
                samplerInfo.setAnisotropyEnable(VK_FALSE);
                samplerInfo.setBorderColor(vk::BorderColor::eIntOpaqueBlack);
                samplerInfo.setUnnormalizedCoordinates(VK_FALSE); // Use [0, 1)
                samplerInfo.setCompareEnable(VK_FALSE);
                samplerInfo.setMipmapMode(vk::SamplerMipmapMode::eNearest);
                samplerInfo.setMipLodBias(0.0f);
                samplerInfo.setMinLod(0.0f);
                samplerInfo.setMaxLod(0.0f);
                computeResource.sampler = MakeHolder(mDevice->createSampler(samplerInfo), [this](vk::Sampler & sampler) { mDevice->destroySampler(sampler); });
            }

            {
                vk::ImageCreateInfo imageInfo;
                imageInfo.setImageType(vk::ImageType::e2D);
                imageInfo.setExtent(vk::Extent3D(mComputeImageExtents.width, mComputeImageExtents.height, 1));
                imageInfo.setMipLevels(1);
                imageInfo.setArrayLayers(1);
                imageInfo.setFormat(vk::Format::eR32Sfloat);
                imageInfo.setTiling(vk::ImageTiling::eLinear);
                imageInfo.setInitialLayout(vk::ImageLayout::ePreinitialized);
                imageInfo.setUsage(vk::ImageUsageFlagBits::eTransferSrc);
                imageInfo.setSharingMode(vk::SharingMode::eExclusive);
                imageInfo.setSamples(vk::SampleCountFlagBits::e1);

                mInitialImage = MakeHolder(mDevice->createImage(imageInfo), [this](vk::Image & img) { mDevice->destroyImage(img); });

                vk::MemoryRequirements imageMemoryRequirments = mDevice->getImageMemoryRequirements(mInitialImage);
                vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties();
                for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                    if ((imageMemoryRequirments.memoryTypeBits & (1 << i)) &&
                        (memroProperties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))) {

                        vk::MemoryAllocateInfo allocateInfo;
                        allocateInfo.setAllocationSize(imageMemoryRequirments.size);
                        allocateInfo.setMemoryTypeIndex(i);
                        mInitialImageMemory = MakeHolder(mDevice->allocateMemory(allocateInfo), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory); });
                    }
                }
                if (!mInitialImageMemory) {
                    throw std::runtime_error("Failed to allocate memory for compute image");
                }
                mDevice->bindImageMemory(mInitialImage, mInitialImageMemory, 0);

                vk::ImageSubresource subres;
                subres.setMipLevel(0);
                subres.setArrayLayer(0);
                subres.setAspectMask(vk::ImageAspectFlagBits::eColor);

                vk::SubresourceLayout colorLayout = mDevice->getImageSubresourceLayout(mInitialImage, subres);

                void* imageDataRaw = mDevice->mapMemory(mInitialImageMemory, 0, mComputeImageExtents.width * mComputeImageExtents.height * sizeof(float));
                if (imageDataRaw == nullptr) {
                    throw std::runtime_error("Failed to map memory");
                }

                std::memset(imageDataRaw, 0, colorLayout.rowPitch * mComputeImageExtents.height);
                float* imageLine = static_cast<float*>(static_cast<void*>(static_cast<uint8_t*>(imageDataRaw) + colorLayout.rowPitch * (mComputeImageExtents.height - 1)));
                std::fill_n(imageLine, mComputeImageExtents.width, 512.0f);

                mDevice->unmapMemory(mInitialImageMemory);
            }

            std::cout << "OK" << std::endl;
        }

        // Prepareing sync resources
        for (uint32_t i = 0; i < mRenderingResources.size(); ++i) {
            mRenderingResources[i].imageHandle = swapchainImages[i];

            vk::ImageViewCreateInfo imageViewInfo;
            imageViewInfo.setImage(swapchainImages[i]);
            imageViewInfo.setViewType(vk::ImageViewType::e2D);
            imageViewInfo.setFormat(format.first);
            imageViewInfo.setComponents(vk::ComponentMapping());
            imageViewInfo.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

            mRenderingResources[i].imageView = MakeHolder(mDevice->createImageView(imageViewInfo), [this](vk::ImageView & view) { mDevice->destroyImageView(view); });

            mRenderingResources[i].semaphoreAvailable = MakeHolder(mDevice->createSemaphore(vk::SemaphoreCreateInfo()), [this](vk::Semaphore & sem) { mDevice->destroySemaphore(sem); });
            mRenderingResources[i].semaphoreFinished  = MakeHolder(mDevice->createSemaphore(vk::SemaphoreCreateInfo()), [this](vk::Semaphore & sem) { mDevice->destroySemaphore(sem); });

            vk::FenceCreateInfo fenceInfo;
            fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
            mRenderingResources[i].fence = MakeHolder(mDevice->createFence(fenceInfo), [this](vk::Fence & fence) { mDevice->destroyFence(fence); });

            mRenderingResources[i].undefinedLaout = true;

            // For each rendering resource prepare own descriptor set, with corresponding image

            vk::DescriptorSetAllocateInfo allocInfo;
            allocInfo.setDescriptorPool(mDescriptorPool);
            allocInfo.setDescriptorSetCount(1);
            allocInfo.setPSetLayouts(mDescriptorSetLayout.get());

            vk::DescriptorSet decriptorSetTmp;
            if (vk::Result::eSuccess != mDevice->allocateDescriptorSets(&allocInfo, &decriptorSetTmp)) {
                throw std::runtime_error("Failed to allocate descriptors set");
            }
            mRenderingResources[i].descriptorSet = MakeHolder(decriptorSetTmp, [this](vk::DescriptorSet & set) { mDevice->freeDescriptorSets(mDescriptorPool, set); });

            std::array<vk::WriteDescriptorSet, 2> writeDescriptorsInfo;
            std::array<vk::DescriptorImageInfo, 2> descriptorImageInfo;

            descriptorImageInfo[0].setImageView(mComputeResources[0].view);
            descriptorImageInfo[0].setSampler(mComputeResources[0].sampler);
            descriptorImageInfo[0].setImageLayout(vk::ImageLayout::eGeneral);

            descriptorImageInfo[1].setImageView(mRenderingResources[i].imageView);
            descriptorImageInfo[1].setImageLayout(vk::ImageLayout::eGeneral);

            writeDescriptorsInfo[0].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
            writeDescriptorsInfo[0].setDstSet(mRenderingResources[i].descriptorSet);
            writeDescriptorsInfo[0].setDstBinding(0);
            writeDescriptorsInfo[0].setDstArrayElement(0);
            writeDescriptorsInfo[0].setDescriptorCount(1);
            writeDescriptorsInfo[0].setPImageInfo(&descriptorImageInfo[0]);

            writeDescriptorsInfo[1].setDescriptorType(vk::DescriptorType::eStorageImage);
            writeDescriptorsInfo[1].setDstSet(mRenderingResources[i].descriptorSet);
            writeDescriptorsInfo[1].setDstBinding(1);
            writeDescriptorsInfo[1].setDstArrayElement(0);
            writeDescriptorsInfo[1].setDescriptorCount(1);
            writeDescriptorsInfo[1].setPImageInfo(&descriptorImageInfo[1]);

            mDevice->updateDescriptorSets(static_cast<uint32_t>(writeDescriptorsInfo.size()), &writeDescriptorsInfo[0], 0, nullptr);
        }

        mRenderingResourceIter = mRenderingResources.begin();
        CanRender = true;
    }

    bool OnWindowSizeChanged() override 
    {
        return true;
    }

    bool Draw() override
    {
        constexpr uint64_t TIMEOUT = 1 * 1000 * 1000 * 1000; // 1 second in nanos
        auto & renderingResource = *mRenderingResourceIter;

        mDevice->resetFences(1, renderingResource.fence.get());

        auto imageIdx = mDevice->acquireNextImageKHR(mSwapChain, TIMEOUT, renderingResource.semaphoreAvailable, nullptr);
        if (imageIdx.result != vk::Result::eSuccess) {
            std::cout << "Failed to acquire image! Stoppping." << std::endl;
            return false;
        }

        // Preapare command buffer
        auto& cmdBuffer = renderingResource.commandBuffer;
        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuffer->begin(beginInfo);

        vk::ImageSubresourceRange range;
        range.aspectMask = vk::ImageAspectFlagBits::eColor;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        if (mFirstDraw) {
            mNextComputeResIdx = 0;

            vk::ImageMemoryBarrier barrierFromPreinitToTransSrc;
            barrierFromPreinitToTransSrc.srcAccessMask = vk::AccessFlagBits::eHostWrite;
            barrierFromPreinitToTransSrc.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            barrierFromPreinitToTransSrc.oldLayout = vk::ImageLayout::ePreinitialized;
            barrierFromPreinitToTransSrc.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrierFromPreinitToTransSrc.srcQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromPreinitToTransSrc.dstQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromPreinitToTransSrc.image = mInitialImage;
            barrierFromPreinitToTransSrc.subresourceRange = range;
            cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromPreinitToTransSrc);

            for (uint32_t idx = 0; idx < 2; ++idx) {
                vk::ImageMemoryBarrier barrierFromPreinitToTransDst;
                barrierFromPreinitToTransDst.srcAccessMask = vk::AccessFlagBits::eHostWrite;
                barrierFromPreinitToTransDst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
                barrierFromPreinitToTransDst.oldLayout = vk::ImageLayout::ePreinitialized;
                barrierFromPreinitToTransDst.newLayout = vk::ImageLayout::eTransferDstOptimal;
                barrierFromPreinitToTransDst.srcQueueFamilyIndex = mQueueFamilyPresent;
                barrierFromPreinitToTransDst.dstQueueFamilyIndex = mQueueFamilyPresent;
                barrierFromPreinitToTransDst.image = mComputeResources[idx].image;
                barrierFromPreinitToTransDst.subresourceRange = range;
                cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromPreinitToTransDst);

                vk::ImageSubresourceLayers subResource;
                subResource.setAspectMask(vk::ImageAspectFlagBits::eColor);
                subResource.setBaseArrayLayer(0);
                subResource.setMipLevel(0);
                subResource.setLayerCount(1);

                vk::ImageCopy copyInfo;
                copyInfo.setSrcSubresource(subResource);
                copyInfo.setDstSubresource(subResource);
                copyInfo.setSrcOffset(vk::Offset3D(0, 0, 0));
                copyInfo.setDstOffset(vk::Offset3D(0, 0, 0));
                copyInfo.setExtent(vk::Extent3D(mComputeImageExtents.width, mComputeImageExtents.height, 1));

                cmdBuffer->copyImage(mInitialImage, vk::ImageLayout::eTransferSrcOptimal, mComputeResources[idx].image, vk::ImageLayout::eTransferDstOptimal, 1, &copyInfo);

                vk::ImageMemoryBarrier barrierFromTransDstToGeneral;
                barrierFromTransDstToGeneral.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
                barrierFromTransDstToGeneral.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                barrierFromTransDstToGeneral.oldLayout = vk::ImageLayout::eTransferDstOptimal;
                barrierFromTransDstToGeneral.newLayout = vk::ImageLayout::eGeneral;
                barrierFromTransDstToGeneral.srcQueueFamilyIndex = mQueueFamilyPresent;
                barrierFromTransDstToGeneral.dstQueueFamilyIndex = mQueueFamilyPresent;
                barrierFromTransDstToGeneral.image = mComputeResources[idx].image;
                barrierFromTransDstToGeneral.subresourceRange = range;
                cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromTransDstToGeneral);
            }
        }

        /* 
         * Iteration
         * Swap buffers bindings
         */
        {
            std::array<vk::WriteDescriptorSet, 2> writeDescriptorsInfo;
            std::array<vk::DescriptorImageInfo, 2> descriptorImageInfo;

            descriptorImageInfo[0].setImageView(mComputeResources[mNextComputeResIdx].view);
            descriptorImageInfo[0].setImageLayout(vk::ImageLayout::eGeneral);

            descriptorImageInfo[1].setImageView(mComputeResources[1 - mNextComputeResIdx].view);
            descriptorImageInfo[1].setImageLayout(vk::ImageLayout::eGeneral);

            writeDescriptorsInfo[0].setDescriptorType(vk::DescriptorType::eStorageImage);
            writeDescriptorsInfo[0].setDstSet(mIterationDescriptorSet);
            writeDescriptorsInfo[0].setDstBinding(2);
            writeDescriptorsInfo[0].setDstArrayElement(0);
            writeDescriptorsInfo[0].setDescriptorCount(1);
            writeDescriptorsInfo[0].setPImageInfo(&descriptorImageInfo[0]);

            writeDescriptorsInfo[1].setDescriptorType(vk::DescriptorType::eStorageImage);
            writeDescriptorsInfo[1].setDstSet(mIterationDescriptorSet);
            writeDescriptorsInfo[1].setDstBinding(3);
            writeDescriptorsInfo[1].setDstArrayElement(0);
            writeDescriptorsInfo[1].setDescriptorCount(1);
            writeDescriptorsInfo[1].setPImageInfo(&descriptorImageInfo[1]);

            mDevice->updateDescriptorSets(static_cast<uint32_t>(writeDescriptorsInfo.size()), &writeDescriptorsInfo[0], 0, nullptr);
        }

        /* 
         * Conversion
         * Bind another sampler
         * On the each draw ping-pong buffer is switched
         */
        {
            std::array<vk::WriteDescriptorSet, 1> writeDescriptorsInfo;
            std::array<vk::DescriptorImageInfo, 1> descriptorImageInfo;

            descriptorImageInfo[0].setImageView(mComputeResources[1 - mNextComputeResIdx].view);
            descriptorImageInfo[0].setSampler(mComputeResources[1 - mNextComputeResIdx].sampler);
            //descriptorImageInfo[0].setImageView(mComputeResources[1].view);
            //descriptorImageInfo[0].setSampler(mComputeResources[1].sampler);
            descriptorImageInfo[0].setImageLayout(vk::ImageLayout::eGeneral);

            writeDescriptorsInfo[0].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
            writeDescriptorsInfo[0].setDstSet(renderingResource.descriptorSet);
            writeDescriptorsInfo[0].setDstBinding(0);
            writeDescriptorsInfo[0].setDstArrayElement(0);
            writeDescriptorsInfo[0].setDescriptorCount(1);
            writeDescriptorsInfo[0].setPImageInfo(&descriptorImageInfo[0]);

            mDevice->updateDescriptorSets(static_cast<uint32_t>(writeDescriptorsInfo.size()), &writeDescriptorsInfo[0], 0, nullptr);
        }

        vk::ImageMemoryBarrier barrierFromPresentToDraw;
        barrierFromPresentToDraw.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
        barrierFromPresentToDraw.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
        barrierFromPresentToDraw.oldLayout = renderingResource.undefinedLaout ? vk::ImageLayout::eUndefined : vk::ImageLayout::ePresentSrcKHR;
        barrierFromPresentToDraw.newLayout = vk::ImageLayout::eGeneral;
        barrierFromPresentToDraw.srcQueueFamilyIndex = mQueueFamilyPresent;
        barrierFromPresentToDraw.dstQueueFamilyIndex = mQueueFamilyGraphics;
        barrierFromPresentToDraw.image = renderingResource.imageHandle;
        barrierFromPresentToDraw.subresourceRange = range;
        cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromPresentToDraw);
        renderingResource.undefinedLaout = false;

        vk::ClearColorValue targetColor = std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f };
        vk::ClearValue clearValue(targetColor);

        cmdBuffer->clearColorImage(renderingResource.imageHandle, vk::ImageLayout::eGeneral, &targetColor, 1, &range);

        // Make iteration
        cmdBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, mIterationPipeline);

        cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, mIterationPipelineLayout, 0, 1, mIterationDescriptorSet.get(), 0, nullptr);

        cmdBuffer->dispatch(mComputeImageExtents.width - 2, mComputeImageExtents.height - 2, 1);

        // Make conversion
        cmdBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, mConversionPipeline);

        cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, mConversionPipelineLayout, 0, 1, renderingResource.descriptorSet.get(), 0, nullptr);

        cmdBuffer->dispatch(mFramebufferExtents.width, mFramebufferExtents.height, 1);

        vk::ImageMemoryBarrier barrierFromDrawToPresent;
        barrierFromDrawToPresent.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
        barrierFromDrawToPresent.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
        barrierFromDrawToPresent.oldLayout = vk::ImageLayout::eGeneral;
        barrierFromDrawToPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
        barrierFromDrawToPresent.srcQueueFamilyIndex = mQueueFamilyGraphics;
        barrierFromDrawToPresent.dstQueueFamilyIndex = mQueueFamilyPresent;
        barrierFromDrawToPresent.image = renderingResource.imageHandle;
        barrierFromDrawToPresent.subresourceRange = range;
        cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromDrawToPresent);
        
        cmdBuffer->end();

        // Submit
        vk::PipelineStageFlags waitDstStageMask = vk::PipelineStageFlagBits::eTransfer;

        vk::SubmitInfo submitInfo;
        submitInfo.pWaitDstStageMask = &waitDstStageMask;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = renderingResource.semaphoreAvailable.get();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = renderingResource.commandBuffer.get();
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = renderingResource.semaphoreFinished.get();
        if (vk::Result::eSuccess != mCommandQueue.submit(1, &submitInfo, renderingResource.fence)) {
            std::cout << "Failed to submit command! Stoppping." << std::endl;
            return false;
        }

        vk::PresentInfoKHR presentInfo;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = renderingResource.semaphoreFinished.get();
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = mSwapChain.get();
        presentInfo.pImageIndices = &imageIdx.value;
        auto result = mCommandQueue.presentKHR(&presentInfo);
        if (result != vk::Result::eSuccess) {
            std::cout << "Failed to present image! Stoppping." << std::endl;
            return false;
        }

        /* 
         * Wait for finish
         * Using this 'syncronous' mode for simplicity
         */
        if (vk::Result::eSuccess != mDevice->waitForFences(1, renderingResource.fence.get(), VK_FALSE, TIMEOUT)) {
            std::cout << "Waiting for fence takes too long!" << std::endl;
            return false;
        }

        ++mRenderingResourceIter;
        if (mRenderingResourceIter == mRenderingResources.end()) {
            mRenderingResourceIter = mRenderingResources.begin();
        }
        mNextComputeResIdx = 1 - mNextComputeResIdx;
        mFirstDraw = false;
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
        if (!window.Create("11 - Heat map", 512, 512)) {
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
