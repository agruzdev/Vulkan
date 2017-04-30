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
    static const uint32_t BLOCK_SIZE = 8;

    struct RenderingResource
    {
        vk::Image imageHandle;
        VulkanHolder<vk::ImageView> imageView;
        VulkanHolder<vk::CommandBuffer> commandBuffer;
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

    vk::Extent2D mTextureExtents;

    VulkanHolder<vk::Image> mStagingImage;
    VulkanHolder<vk::DeviceMemory> mStagingImageMemory;

    VulkanHolder<vk::Sampler> mProcessedImageSampler;

    VulkanHolder<vk::ShaderModule> mDrawShader;
    VulkanHolder<vk::ShaderModule> mBlurShader;

    VulkanHolder<vk::DescriptorSetLayout> mDrawDescriptorSetLayout;
    VulkanHolder<vk::DescriptorSetLayout> mBlurDescriptorSetLayout;
    
    VulkanHolder<vk::DescriptorPool> mDescriptorPool;
    std::array<VulkanHolder<vk::DescriptorSet>, 2> mBlurDescriptorSets;

    // Draw
    VulkanHolder<vk::PipelineLayout> mDrawPipelineLayout;
    VulkanHolder<vk::Pipeline> mDrawPipeline;

    //Iteration
    VulkanHolder<vk::PipelineLayout> mBlurPipelineLayout;
    VulkanHolder<vk::Pipeline> mBlurPipeline;

    VulkanHolder<vk::CommandPool> mCommandPool;

    std::array<ComputeResource, 2> mComputeResources; // ping-pong

    std::vector<RenderingResource> mRenderingResources;

    vk::PhysicalDevice mPhysicalDevice;
    vk::Queue mCommandQueue;

    uint32_t mQueueFamilyGraphics = std::numeric_limits<uint32_t>::max();
    uint32_t mQueueFamilyPresent  = std::numeric_limits<uint32_t>::max();

    vk::Extent2D mFramebufferExtents;

    VulkanHolder<vk::Semaphore> mSemaphoreAvailable;
    VulkanHolder<vk::Semaphore> mSemaphoreFinished;

    VulkanHolder<vk::QueryPool> mQueryPool;

    uint64_t mFrameCounter;

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
            throw std::runtime_error("Invalid capabilities");
        }
        // check that image can be used as image2D in compute shaders VK_IMAGE_USAGE_STORAGE_BIT 
        // https://redd.it/5nd7tj
        if (!(surfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eStorage)) {
            throw std::runtime_error("ImageUsageFlagBits::eStorage is not supported by swapchain");
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

        std::cout << "Load texture...";
        {
            RgbaImage rgbaImage = LoadBmpImage(QUOTE(RESOURCES_DIR) "/16_texture.bmp");
            if (rgbaImage.pixels.empty()) {
                throw std::runtime_error("Failed to load texture");
            }
            mTextureExtents.setWidth(rgbaImage.width);
            mTextureExtents.setHeight(rgbaImage.height);

            if (mTextureExtents.width % BLOCK_SIZE != 0 || mTextureExtents.height % BLOCK_SIZE != 0) {
                throw std::runtime_error("Image extents should be dividable by " + std::to_string(BLOCK_SIZE));
            }

            vk::ImageCreateInfo imageInfo;
            imageInfo.setImageType(vk::ImageType::e2D);
            imageInfo.setExtent(vk::Extent3D(mTextureExtents.width, mTextureExtents.height, 1));
            imageInfo.setMipLevels(1);
            imageInfo.setArrayLayers(1);
            imageInfo.setFormat(vk::Format::eR8G8B8A8Uint);
            imageInfo.setTiling(vk::ImageTiling::eLinear);
            imageInfo.setInitialLayout(vk::ImageLayout::ePreinitialized);
            imageInfo.setUsage(vk::ImageUsageFlagBits::eTransferSrc);
            imageInfo.setSharingMode(vk::SharingMode::eExclusive);
            imageInfo.setSamples(vk::SampleCountFlagBits::e1);

            mStagingImage = MakeHolder(mDevice->createImage(imageInfo), [this](vk::Image & img) { mDevice->destroyImage(img); });

            vk::MemoryRequirements imageMemoryRequirments = mDevice->getImageMemoryRequirements(mStagingImage);
            vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties();
            for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                if ((imageMemoryRequirments.memoryTypeBits & (1 << i)) &&
                    (memroProperties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eHostVisible))) {

                    vk::MemoryAllocateInfo allocateInfo;
                    allocateInfo.setAllocationSize(imageMemoryRequirments.size);
                    allocateInfo.setMemoryTypeIndex(i);
                    mStagingImageMemory = MakeHolder(mDevice->allocateMemory(allocateInfo), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory); });
                }
            }
            if (!mStagingImageMemory) {
                throw std::runtime_error("Failed to allocate memory for compute image");
            }
            mDevice->bindImageMemory(mStagingImage, mStagingImageMemory, 0);

            vk::ImageSubresource subres;
            subres.setMipLevel(0);
            subres.setArrayLayer(0);
            subres.setAspectMask(vk::ImageAspectFlagBits::eColor);

            vk::SubresourceLayout layout = mDevice->getImageSubresourceLayout(mStagingImage, subres);

            uint8_t* devicePtr = static_cast<uint8_t*>(mDevice->mapMemory(mStagingImageMemory, layout.offset, layout.size));
            if(devicePtr == nullptr) {
                throw std::runtime_error("Failed to map texture memory!");
            }

            const uint8_t* hostPtr = rgbaImage.pixels.data();
            for (uint32_t y = 0; y < mTextureExtents.height; ++y, hostPtr += mTextureExtents.width * 4, devicePtr += layout.rowPitch) {
                std::memcpy(devicePtr, hostPtr, mTextureExtents.width * 4);
            }

            vk::MappedMemoryRange mappedRange;
            mappedRange.setMemory(mStagingImageMemory);
            mappedRange.setOffset(0);
            mappedRange.setSize(VK_WHOLE_SIZE);
            mDevice->flushMappedMemoryRanges(mappedRange);

            mDevice->unmapMemory(mStagingImageMemory);

            std::cout << "OK" << std::endl;
        }


        std::cout << "Loading shader... ";
        {
            {
                auto code = GetBinaryShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/16.draw.comp");
                if (code.empty()) {
                    throw std::runtime_error("LoadShader: Failed to read shader file!");
                }
                vk::ShaderModuleCreateInfo shaderInfo;
                shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
                shaderInfo.setCodeSize(code.size());

                mDrawShader = MakeHolder(mDevice->createShaderModule(shaderInfo), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader); });
            }
            {
                auto code = GetBinaryShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/16.blur.comp");
                if (code.empty()) {
                    throw std::runtime_error("LoadShader: Failed to read shader file!");
                }
                vk::ShaderModuleCreateInfo shaderInfo;
                shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
                shaderInfo.setCodeSize(code.size());

                mBlurShader = MakeHolder(mDevice->createShaderModule(shaderInfo), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader); });
            }

            // Descriptors layout for draw
            {
                std::array<vk::DescriptorSetLayoutBinding, 2> bindings;

                // Draw shader, input sampler
                bindings[0].setBinding(0);
                bindings[0].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
                bindings[0].setDescriptorCount(1);
                bindings[0].setStageFlags(vk::ShaderStageFlagBits::eCompute);

                // Draw shader, destination image (swapchain)
                bindings[1].setBinding(1);
                bindings[1].setDescriptorType(vk::DescriptorType::eStorageImage);
                bindings[1].setDescriptorCount(1);
                bindings[1].setStageFlags(vk::ShaderStageFlagBits::eCompute);

                vk::DescriptorSetLayoutCreateInfo descriptorSetInfo;
                descriptorSetInfo.setBindingCount(static_cast<uint32_t>(bindings.size()));
                descriptorSetInfo.setPBindings(&bindings[0]);
                mDrawDescriptorSetLayout = MakeHolder(mDevice->createDescriptorSetLayout(descriptorSetInfo), [this](vk::DescriptorSetLayout & layout) { mDevice->destroyDescriptorSetLayout(layout); });
            }

            // Descriptors layout for iteration
            {
                std::array<vk::DescriptorSetLayoutBinding, 2> bindings;

                bindings[0].setBinding(0);
                bindings[0].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
                bindings[0].setDescriptorCount(1);
                bindings[0].setStageFlags(vk::ShaderStageFlagBits::eCompute);

                bindings[1].setBinding(1);
                bindings[1].setDescriptorType(vk::DescriptorType::eStorageImage);
                bindings[1].setDescriptorCount(1);
                bindings[1].setStageFlags(vk::ShaderStageFlagBits::eCompute);

                vk::DescriptorSetLayoutCreateInfo descriptorSetInfo;
                descriptorSetInfo.setBindingCount(static_cast<uint32_t>(bindings.size()));
                descriptorSetInfo.setPBindings(&bindings[0]);
                mBlurDescriptorSetLayout = MakeHolder(mDevice->createDescriptorSetLayout(descriptorSetInfo), [this](vk::DescriptorSetLayout & layout) { mDevice->destroyDescriptorSetLayout(layout); });
            }

            // Pool
            {
                std::array<vk::DescriptorPoolSize, 2> poolSize;

                poolSize[0].setType(vk::DescriptorType::eCombinedImageSampler);
                poolSize[0].setDescriptorCount(2 + static_cast<uint32_t>(swapchainImages.size()));

                poolSize[1].setType(vk::DescriptorType::eStorageImage);
                poolSize[1].setDescriptorCount(2 + static_cast<uint32_t>(swapchainImages.size()));

                vk::DescriptorPoolCreateInfo poolInfo;
                poolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
                poolInfo.setMaxSets(2 + static_cast<uint32_t>(swapchainImages.size()));
                poolInfo.setPoolSizeCount(static_cast<uint32_t>(poolSize.size()));
                poolInfo.setPPoolSizes(&poolSize[0]);

                mDescriptorPool = MakeHolder(mDevice->createDescriptorPool(poolInfo), [this](vk::DescriptorPool & pool) { mDevice->destroyDescriptorPool(pool); });
            }

            // Descriptors set for iteration
            {
                vk::DescriptorSetAllocateInfo allocInfo;
                allocInfo.setDescriptorPool(mDescriptorPool);
                allocInfo.setDescriptorSetCount(1);
                allocInfo.setPSetLayouts(mBlurDescriptorSetLayout.get());

                vk::DescriptorSet decriptorSetTmp;
                if (vk::Result::eSuccess != mDevice->allocateDescriptorSets(&allocInfo, &decriptorSetTmp)) {
                    throw std::runtime_error("Failed to allocate descriptors set");
                }
                mBlurDescriptorSets[0] = MakeHolder(decriptorSetTmp, [this](vk::DescriptorSet & set) { mDevice->freeDescriptorSets(mDescriptorPool, set); });

                if (vk::Result::eSuccess != mDevice->allocateDescriptorSets(&allocInfo, &decriptorSetTmp)) {
                    throw std::runtime_error("Failed to allocate descriptors set");
                }
                mBlurDescriptorSets[1] = MakeHolder(decriptorSetTmp, [this](vk::DescriptorSet & set) { mDevice->freeDescriptorSets(mDescriptorPool, set); });
            }
            std::cout << "OK" << std::endl;
        }

        std::cout << "Create draw pipeline...";
        {
            std::array<vk::PipelineShaderStageCreateInfo, 1> stageInfos;
            stageInfos[0].setStage(vk::ShaderStageFlagBits::eCompute);
            stageInfos[0].setModule(mDrawShader);
            stageInfos[0].setPName("main"); // Shader entry point

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
            pipelineLayoutInfo.setSetLayoutCount(1);
            pipelineLayoutInfo.setPSetLayouts(mDrawDescriptorSetLayout.get());
            mDrawPipelineLayout = MakeHolder(mDevice->createPipelineLayout(pipelineLayoutInfo), [this](vk::PipelineLayout & layout) { mDevice->destroyPipelineLayout(layout); });

            vk::ComputePipelineCreateInfo computePipelineInfo;
            computePipelineInfo.setStage(stageInfos[0]);
            computePipelineInfo.setLayout(mDrawPipelineLayout);
            mDrawPipeline = MakeHolder(mDevice->createComputePipeline(vk::PipelineCache(), computePipelineInfo), [this](vk::Pipeline & pipeline) {mDevice->destroyPipeline(pipeline); });

            std::cout << "OK" << std::endl;
        }

        std::cout << "Create blur pipeline...";
        {
            std::array<vk::PipelineShaderStageCreateInfo, 1> stageInfos;
            stageInfos[0].setStage(vk::ShaderStageFlagBits::eCompute);
            stageInfos[0].setModule(mBlurShader);
            stageInfos[0].setPName("main"); // Shader entry point

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
            pipelineLayoutInfo.setSetLayoutCount(1);
            pipelineLayoutInfo.setPSetLayouts(mBlurDescriptorSetLayout.get());
            mBlurPipelineLayout = MakeHolder(mDevice->createPipelineLayout(pipelineLayoutInfo), [this](vk::PipelineLayout & layout) { mDevice->destroyPipelineLayout(layout); });

            vk::ComputePipelineCreateInfo computePipelineInfo;
            computePipelineInfo.setStage(stageInfos[0]);
            computePipelineInfo.setLayout(mBlurPipelineLayout);
            mBlurPipeline = MakeHolder(mDevice->createComputePipeline(vk::PipelineCache(), computePipelineInfo), [this](vk::Pipeline & pipeline) {mDevice->destroyPipeline(pipeline); });

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
         */
        std::cout << "Create buffers...";
        {
            // Second image will be transposed
            for (uint32_t j = 0; j < 2; ++j) {
                vk::ImageCreateInfo imageInfo;
                imageInfo.setImageType(vk::ImageType::e2D);
                if (j == 0) {
                    imageInfo.setExtent(vk::Extent3D(mTextureExtents.width, mTextureExtents.height, 1));
                } else {
                    imageInfo.setExtent(vk::Extent3D(mTextureExtents.height, mTextureExtents.width, 1));
                }
                imageInfo.setMipLevels(1);
                imageInfo.setArrayLayers(1);
                imageInfo.setFormat(vk::Format::eR8G8B8A8Uint);
                imageInfo.setTiling(vk::ImageTiling::eOptimal);
                imageInfo.setInitialLayout(vk::ImageLayout::eUndefined);
                imageInfo.setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
                imageInfo.setSharingMode(vk::SharingMode::eExclusive);
                imageInfo.setSamples(vk::SampleCountFlagBits::e1);

                mComputeResources[j].image = MakeHolder(mDevice->createImage(imageInfo), [this](vk::Image & img) { mDevice->destroyImage(img); });

                vk::MemoryRequirements imageMemoryRequirments = mDevice->getImageMemoryRequirements(mComputeResources[j].image);
                vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties();
                for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                    if ((imageMemoryRequirments.memoryTypeBits & (1 << i)) &&
                        (memroProperties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eDeviceLocal))) {

                        vk::MemoryAllocateInfo allocateInfo;
                        allocateInfo.setAllocationSize(imageMemoryRequirments.size);
                        allocateInfo.setMemoryTypeIndex(i);
                        mComputeResources[j].memory = MakeHolder(mDevice->allocateMemory(allocateInfo), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory); });
                    }
                }
                if (!mComputeResources[j].memory) {
                    throw std::runtime_error("Failed to allocate memory for compute image");
                }
                mDevice->bindImageMemory(mComputeResources[j].image, mComputeResources[j].memory, 0);

                vk::ImageSubresourceRange range;
                range.setAspectMask(vk::ImageAspectFlagBits::eColor);
                range.setBaseMipLevel(0);
                range.setLevelCount(1);
                range.setBaseArrayLayer(0);
                range.setLayerCount(1);

                vk::ImageViewCreateInfo viewInfo;
                viewInfo.setImage(mComputeResources[j].image);
                viewInfo.setViewType(vk::ImageViewType::e2D);
                viewInfo.setFormat(vk::Format::eR8G8B8A8Uint);
                viewInfo.setSubresourceRange(range);
                mComputeResources[j].view = MakeHolder(mDevice->createImageView(viewInfo), [this](vk::ImageView & view) { mDevice->destroyImageView(view); });

                // Samplers for blur passes
                vk::SamplerCreateInfo samplerInfo;
                samplerInfo.setMagFilter(vk::Filter::eLinear);
                samplerInfo.setMinFilter(vk::Filter::eLinear);
                samplerInfo.setAddressModeU(vk::SamplerAddressMode::eClampToEdge);
                samplerInfo.setAddressModeV(vk::SamplerAddressMode::eClampToEdge);
                samplerInfo.setAddressModeW(vk::SamplerAddressMode::eClampToEdge);
                samplerInfo.setAnisotropyEnable(VK_FALSE);
                samplerInfo.setMaxAnisotropy(1.0f);
                samplerInfo.setBorderColor(vk::BorderColor::eIntOpaqueBlack);
                samplerInfo.setUnnormalizedCoordinates(VK_TRUE); // Use [0, Size)
                samplerInfo.setCompareEnable(VK_FALSE);
                samplerInfo.setMipmapMode(vk::SamplerMipmapMode::eNearest);
                samplerInfo.setMipLodBias(0.0f);
                samplerInfo.setMinLod(0.0f);
                samplerInfo.setMaxLod(0.0f);
                mComputeResources[j].sampler = MakeHolder(mDevice->createSampler(samplerInfo), [this](vk::Sampler & sampler) { mDevice->destroySampler(sampler); });
            }

            // Sampler for drawing
            vk::SamplerCreateInfo samplerInfo;
            samplerInfo.setMagFilter(vk::Filter::eLinear);
            samplerInfo.setMinFilter(vk::Filter::eLinear);
            samplerInfo.setAddressModeU(vk::SamplerAddressMode::eClampToBorder);
            samplerInfo.setAddressModeV(vk::SamplerAddressMode::eClampToBorder);
            samplerInfo.setAddressModeW(vk::SamplerAddressMode::eClampToBorder);
            samplerInfo.setAnisotropyEnable(VK_FALSE);
            samplerInfo.setMaxAnisotropy(1.0f);
            samplerInfo.setBorderColor(vk::BorderColor::eIntOpaqueBlack);
            samplerInfo.setUnnormalizedCoordinates(VK_FALSE); // Use [0, 1)
            samplerInfo.setCompareEnable(VK_FALSE);
            samplerInfo.setMipmapMode(vk::SamplerMipmapMode::eNearest);
            samplerInfo.setMipLodBias(0.0f);
            samplerInfo.setMinLod(0.0f);
            samplerInfo.setMaxLod(0.0f);
            mProcessedImageSampler = MakeHolder(mDevice->createSampler(samplerInfo), [this](vk::Sampler & sampler) { mDevice->destroySampler(sampler); });
            
            std::cout << "OK" << std::endl;
        }

        // Preparing rendering resources
        for (uint32_t i = 0; i < mRenderingResources.size(); ++i) {
            mRenderingResources[i].imageHandle = swapchainImages[i];

            vk::ImageViewCreateInfo imageViewInfo;
            imageViewInfo.setImage(swapchainImages[i]);
            imageViewInfo.setViewType(vk::ImageViewType::e2D);
            imageViewInfo.setFormat(format.first);
            imageViewInfo.setComponents(vk::ComponentMapping());
            imageViewInfo.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

            mRenderingResources[i].imageView = MakeHolder(mDevice->createImageView(imageViewInfo), [this](vk::ImageView & view) { mDevice->destroyImageView(view); });

            vk::FenceCreateInfo fenceInfo;
            fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
            mRenderingResources[i].fence = MakeHolder(mDevice->createFence(fenceInfo), [this](vk::Fence & fence) { mDevice->destroyFence(fence); });

            mRenderingResources[i].undefinedLaout = true;

            // For each rendering resource prepare own descriptor set, with corresponding image

            vk::DescriptorSetAllocateInfo allocInfo;
            allocInfo.setDescriptorPool(mDescriptorPool);
            allocInfo.setDescriptorSetCount(1);
            allocInfo.setPSetLayouts(mDrawDescriptorSetLayout.get());

            vk::DescriptorSet decriptorSetTmp;
            if (vk::Result::eSuccess != mDevice->allocateDescriptorSets(&allocInfo, &decriptorSetTmp)) {
                throw std::runtime_error("Failed to allocate descriptors set");
            }
            mRenderingResources[i].descriptorSet = MakeHolder(decriptorSetTmp, [this](vk::DescriptorSet & set) { mDevice->freeDescriptorSets(mDescriptorPool, set); });

            std::array<vk::WriteDescriptorSet, 2> writeDescriptorsInfo;
            std::array<vk::DescriptorImageInfo, 2> descriptorImageInfo;

            descriptorImageInfo[0].setImageView(mComputeResources[0].view);
            descriptorImageInfo[0].setSampler(mProcessedImageSampler);
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

        // Preapare descrptors for blur
        {
            {
                std::array<vk::WriteDescriptorSet, 2> writeDescriptorsInfo;
                std::array<vk::DescriptorImageInfo, 2> descriptorImageInfo;

                descriptorImageInfo[0].setImageView(mComputeResources[0].view);
                descriptorImageInfo[0].setSampler(mComputeResources[0].sampler);
                descriptorImageInfo[0].setImageLayout(vk::ImageLayout::eGeneral);

                descriptorImageInfo[1].setImageView(mComputeResources[1].view);
                descriptorImageInfo[1].setImageLayout(vk::ImageLayout::eGeneral);

                writeDescriptorsInfo[0].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
                writeDescriptorsInfo[0].setDstSet(mBlurDescriptorSets[0]);
                writeDescriptorsInfo[0].setDstBinding(0);
                writeDescriptorsInfo[0].setDstArrayElement(0);
                writeDescriptorsInfo[0].setDescriptorCount(1);
                writeDescriptorsInfo[0].setPImageInfo(&descriptorImageInfo[0]);

                writeDescriptorsInfo[1].setDescriptorType(vk::DescriptorType::eStorageImage);
                writeDescriptorsInfo[1].setDstSet(mBlurDescriptorSets[0]);
                writeDescriptorsInfo[1].setDstBinding(1);
                writeDescriptorsInfo[1].setDstArrayElement(0);
                writeDescriptorsInfo[1].setDescriptorCount(1);
                writeDescriptorsInfo[1].setPImageInfo(&descriptorImageInfo[1]);

                mDevice->updateDescriptorSets(static_cast<uint32_t>(writeDescriptorsInfo.size()), &writeDescriptorsInfo[0], 0, nullptr);
            }
            {
                std::array<vk::WriteDescriptorSet, 2> writeDescriptorsInfo;
                std::array<vk::DescriptorImageInfo, 2> descriptorImageInfo;

                descriptorImageInfo[0].setImageView(mComputeResources[1].view);
                descriptorImageInfo[0].setSampler(mComputeResources[1].sampler);
                descriptorImageInfo[0].setImageLayout(vk::ImageLayout::eGeneral);

                descriptorImageInfo[1].setImageView(mComputeResources[0].view);
                descriptorImageInfo[1].setImageLayout(vk::ImageLayout::eGeneral);

                writeDescriptorsInfo[0].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
                writeDescriptorsInfo[0].setDstSet(mBlurDescriptorSets[1]);
                writeDescriptorsInfo[0].setDstBinding(0);
                writeDescriptorsInfo[0].setDstArrayElement(0);
                writeDescriptorsInfo[0].setDescriptorCount(1);
                writeDescriptorsInfo[0].setPImageInfo(&descriptorImageInfo[0]);

                writeDescriptorsInfo[1].setDescriptorType(vk::DescriptorType::eStorageImage);
                writeDescriptorsInfo[1].setDstSet(mBlurDescriptorSets[1]);
                writeDescriptorsInfo[1].setDstBinding(1);
                writeDescriptorsInfo[1].setDstArrayElement(0);
                writeDescriptorsInfo[1].setDescriptorCount(1);
                writeDescriptorsInfo[1].setPImageInfo(&descriptorImageInfo[1]);

                mDevice->updateDescriptorSets(static_cast<uint32_t>(writeDescriptorsInfo.size()), &writeDescriptorsInfo[0], 0, nullptr);
            }
        }

        mSemaphoreAvailable = MakeHolder(mDevice->createSemaphore(vk::SemaphoreCreateInfo()), [this](vk::Semaphore & sem) { mDevice->destroySemaphore(sem); });
        mSemaphoreFinished  = MakeHolder(mDevice->createSemaphore(vk::SemaphoreCreateInfo()), [this](vk::Semaphore & sem) { mDevice->destroySemaphore(sem); });

        vk::QueryPoolCreateInfo queryPoolInfo;
        queryPoolInfo.setQueryCount(2);
        queryPoolInfo.setQueryType(vk::QueryType::eTimestamp);
        mQueryPool = MakeHolder(mDevice->createQueryPool(queryPoolInfo), [this](vk::QueryPool & pool) { mDevice->destroyQueryPool(pool); });

        CanRender = true;
        mFrameCounter = 0;
    }

    bool OnWindowSizeChanged() override 
    {
        return true;
    }

    bool Draw() override
    {
        constexpr uint64_t TIMEOUT = 1 * 1000 * 1000 * 1000; // 1 second in nanos

        auto imageIdx = mDevice->acquireNextImageKHR(mSwapChain, TIMEOUT, mSemaphoreAvailable, nullptr);
        if (imageIdx.result != vk::Result::eSuccess) {
            std::cout << "Failed to acquire image! Stoppping." << std::endl;
            return false;
        }

        auto & renderingResource = mRenderingResources[imageIdx.value];

        mDevice->resetFences(1, renderingResource.fence.get());

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

        if (mFrameCounter == 0) {
            vk::ImageMemoryBarrier barrierStagingImage;
            barrierStagingImage.srcAccessMask = vk::AccessFlagBits::eHostWrite;
            barrierStagingImage.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            barrierStagingImage.oldLayout = vk::ImageLayout::ePreinitialized;
            barrierStagingImage.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrierStagingImage.srcQueueFamilyIndex = mQueueFamilyPresent;
            barrierStagingImage.dstQueueFamilyIndex = mQueueFamilyPresent;
            barrierStagingImage.image = mStagingImage;
            barrierStagingImage.subresourceRange = range;
            cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierStagingImage);

            for(auto & cres : mComputeResources) {

                vk::ImageMemoryBarrier barrierComputeImage;
                barrierComputeImage.srcAccessMask = vk::AccessFlags();
                barrierComputeImage.dstAccessMask = vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
                barrierComputeImage.oldLayout = vk::ImageLayout::eUndefined;
                barrierComputeImage.newLayout = vk::ImageLayout::eGeneral;
                barrierComputeImage.srcQueueFamilyIndex = mQueueFamilyPresent;
                barrierComputeImage.dstQueueFamilyIndex = mQueueFamilyPresent;
                barrierComputeImage.image = cres.image;
                barrierComputeImage.subresourceRange = range;

                cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierComputeImage);
            }
        }

        /**
         * Init image
         */
        {
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
            copyInfo.setExtent(vk::Extent3D(mTextureExtents.width, mTextureExtents.height, 1));

            cmdBuffer->copyImage(mStagingImage, vk::ImageLayout::eTransferSrcOptimal, mComputeResources[0].image, vk::ImageLayout::eGeneral, 1, &copyInfo);
        }

        cmdBuffer->resetQueryPool(mQueryPool, 0, 2);

        cmdBuffer->writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, mQueryPool, 0);

        /*
         * Make blur iterations
         */
        for(int i = 0; i < 8; ++i) {
            cmdBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, mBlurPipeline);

            // from 0 to 1
            cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, mBlurPipelineLayout, 0, 1, mBlurDescriptorSets[0].get(), 0, nullptr);

            cmdBuffer->dispatch(mTextureExtents.width / BLOCK_SIZE, mTextureExtents.height / BLOCK_SIZE, 1);

            // from 1 to 0
            cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, mBlurPipelineLayout, 0, 1, mBlurDescriptorSets[1].get(), 0, nullptr);

            cmdBuffer->dispatch(mTextureExtents.height / BLOCK_SIZE, mTextureExtents.width / BLOCK_SIZE, 1);
        }

        cmdBuffer->writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, mQueryPool, 1);

        /* 
         * Draw from first image
         */
        {
            vk::ImageMemoryBarrier barrierFromPresentToDraw;
            barrierFromPresentToDraw.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
            barrierFromPresentToDraw.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrierFromPresentToDraw.oldLayout = renderingResource.undefinedLaout ? vk::ImageLayout::eUndefined : vk::ImageLayout::ePresentSrcKHR;
            barrierFromPresentToDraw.newLayout = vk::ImageLayout::eGeneral;
            barrierFromPresentToDraw.srcQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromPresentToDraw.dstQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromPresentToDraw.image = renderingResource.imageHandle;
            barrierFromPresentToDraw.subresourceRange = range;

            cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromPresentToDraw);

            renderingResource.undefinedLaout = false;


            cmdBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, mDrawPipeline);

            cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, mDrawPipelineLayout, 0, 1, renderingResource.descriptorSet.get(), 0, nullptr);

            cmdBuffer->dispatch(mFramebufferExtents.width, mFramebufferExtents.height, 1);


            vk::ImageMemoryBarrier barrierFromDrawToPresent;
            barrierFromDrawToPresent.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrierFromDrawToPresent.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
            barrierFromDrawToPresent.oldLayout = vk::ImageLayout::eGeneral;
            barrierFromDrawToPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
            barrierFromDrawToPresent.srcQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromDrawToPresent.dstQueueFamilyIndex = mQueueFamilyPresent;
            barrierFromDrawToPresent.image = renderingResource.imageHandle;
            barrierFromDrawToPresent.subresourceRange = range;
            cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromDrawToPresent);

        }
        cmdBuffer->end();

        // Submit
        vk::PipelineStageFlags waitDstStageMask = vk::PipelineStageFlagBits::eTopOfPipe;

        vk::SubmitInfo submitInfo;
        submitInfo.pWaitDstStageMask = &waitDstStageMask;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = mSemaphoreAvailable.get();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = renderingResource.commandBuffer.get();
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = mSemaphoreFinished.get();
        if (vk::Result::eSuccess != mCommandQueue.submit(1, &submitInfo, renderingResource.fence)) {
            std::cout << "Failed to submit command! Stoppping." << std::endl;
            return false;
        }

        vk::PresentInfoKHR presentInfo;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = mSemaphoreFinished.get();
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
         * Using this 'syncronous' mode for simplicity and time measurment
         */
        if (vk::Result::eSuccess != mDevice->waitForFences(1, renderingResource.fence.get(), VK_FALSE, TIMEOUT)) {
            std::cout << "Waiting for fence takes too long!" << std::endl;
            return false;
        }

        if(mFrameCounter < 8) {
            std::array<uint64_t, 2> timestamps = { 0, 0 }; // nanoseconds 
            mDevice->getQueryPoolResults(mQueryPool, 0, 2, sizeof(timestamps), &timestamps[0], sizeof(uint64_t), vk::QueryResultFlagBits::e64);

            std::cout << "Execution time = " << (timestamps[1] - timestamps[0]) / 1e6 << " ms" << std::endl;
        }

        ++mFrameCounter;
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
        if (!window.Create("16 - Blur", 512, 512)) {
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
