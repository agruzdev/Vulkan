/**
* Vulkan samples
*
* The MIT License (MIT)
* Copyright (c) 2016 Alexey Gruzdev
*/

#define _USE_MATH_DEFINES

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

#include <VulkanUtility.h>
#include <OperatingSystem.h>

#include <math/OgreVector2.h>
#include <math/OgreVector4.h>
#include <math/OgreMatrix4.h>

class Sample_03_Window
    : public ApiWithoutSecrets::OS::TutorialBase
    , public ApiWithoutSecrets::OS::MouseListener
{

    struct VertexData
    {
        Ogre::Vector4 position;
        Ogre::Vector4 normal;
    };

    struct Mesh
    {
        std::vector<VertexData> vertexes;
        std::vector<uint16_t> indexes;
    };

    struct VertexUniformBuffer
    {
        Ogre::Matrix4 modelView = Ogre::Matrix4::IDENTITY;
        Ogre::Matrix4 projection = Ogre::Matrix4::IDENTITY;
    };

    struct RenderingResource
    {
        vk::Image imageHandle;
        VulkanHolder<vk::CommandBuffer> commandBuffer;
        VulkanHolder<vk::ImageView> imageView;
        VulkanHolder<vk::Framebuffer> framebuffer;
        VulkanHolder<vk::Fence> fence;
        bool undefinedLaout;
    };

    VulkanHolder<vk::Instance> mVulkan;
    VulkanHolder<vk::Device> mDevice;
    VulkanHolder<vk::SurfaceKHR> mSurface;
    VulkanHolder<vk::SwapchainKHR> mSwapChain;

    VulkanHolder<vk::RenderPass> mRenderPass;

    VulkanHolder<vk::ShaderModule> mVertexShader;
    VulkanHolder<vk::ShaderModule> mGeometryShader;
    VulkanHolder<vk::ShaderModule> mFragmentShader;
    VulkanHolder<vk::ShaderModule> mFragmentShaderSecondary;

    VulkanHolder<vk::DescriptorSetLayout> mDescriptorSetLayout;
    VulkanHolder<vk::DescriptorPool> mDescriptorPool;
    VulkanHolder<vk::DescriptorSet> mDescriptorSet;

    VulkanHolder<vk::PipelineLayout> mPipelineLayout;
    VulkanHolder<vk::Pipeline> mPipelinePrimary;
    VulkanHolder<vk::Pipeline> mPipelineSecondary;

    VulkanHolder<vk::Buffer> mVertexBuffer;
    VulkanHolder<vk::DeviceMemory> mVertexMemory;

    VulkanHolder<vk::Buffer> mIndexesBuffer;
    VulkanHolder<vk::DeviceMemory> mIndexesMemory;
    uint32_t mIndexesNumber = 0;

    VulkanHolder<vk::Buffer> mMatrixesBuffer;
    VulkanHolder<vk::DeviceMemory> mMatrixesMemory;

    VulkanHolder<vk::CommandPool> mCommandPool;

    std::vector<RenderingResource> mRenderingResources;

    vk::PhysicalDevice mPhysicalDevice;
    vk::Queue mCommandQueue;

    uint32_t mQueueFamilyGraphics = std::numeric_limits<uint32_t>::max();
    uint32_t mQueueFamilyPresent = std::numeric_limits<uint32_t>::max();

    VulkanHolder<vk::Semaphore> mSemaphoreAvailable;
    VulkanHolder<vk::Semaphore> mSemaphoreFinished;

    vk::Extent2D mFramebufferExtents;

    VertexUniformBuffer mMatrixes;
    Ogre::Vector3 mPosition;

    Ogre::Vector2 mMousePosition;
    bool mIsMouseDown = false;
    Ogre::Quaternion mDefaultOrientation;
    Ogre::Quaternion mRotationX;
    Ogre::Quaternion mRotationY;

    VulkanHolder<vk::Image> mDepthImage;
    VulkanHolder<vk::DeviceMemory> mDepthImageMemory;
    VulkanHolder<vk::ImageView> mDepthView;

    bool mFirstDraw = true;

    static Mesh GenerateSphere(const float radius, const uint16_t rings, const uint16_t segments)
    {
        assert(rings > 1);
        assert(segments > 2);

        Mesh sphere;

        // Code adopted from Ogre::PrefabFactory
        // https://bitbucket.org/sinbad/ogre/src

        const float deltaRingAngle = static_cast<float>(M_PI / rings);
        const float deltaSegAngle = static_cast<float>(2.0 * M_PI / segments);
        uint16_t verticeIndex = 0;

        // Generate the group of rings for the sphere
        for (uint16_t ringIdx = 0; ringIdx <= rings; ++ringIdx) {
            float r0 = radius * std::sin(ringIdx * deltaRingAngle);
            float y0 = radius * std::cos(ringIdx * deltaRingAngle);
            // Generate the group of segments for the current ring
            for (uint16_t segIdx = 0; segIdx <= segments; ++segIdx) {
                float x0 = r0 * std::sin(segIdx * deltaSegAngle);
                float z0 = r0 * std::cos(segIdx * deltaSegAngle);

                Ogre::Vector3 normal = Ogre::Vector3(x0, y0, z0).normalisedCopy();

                VertexData vertex;
                vertex.position = { x0, y0, z0, 1.0f };
                vertex.normal = { normal.x, normal.y, normal.z, 0.0f };
                sphere.vertexes.push_back(vertex);

                if (ringIdx != rings) {
                    // each vertex (except the last) has six indicies pointing to it
                    sphere.indexes.push_back(verticeIndex + segments + 1);
                    sphere.indexes.push_back(verticeIndex);
                    sphere.indexes.push_back(verticeIndex + segments);
                    sphere.indexes.push_back(verticeIndex + segments + 1);
                    sphere.indexes.push_back(verticeIndex + 1);
                    sphere.indexes.push_back(verticeIndex);
                    ++verticeIndex;
                }
            }
        }
        return sphere;
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

    /**
     * Load shader module from glsl source file
     */
    VulkanHolder<vk::ShaderModule> LoadShaderFromSourceFile(const std::string & filename)
    {
        auto code = GetBinaryShaderFromSourceFile(filename);
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
        for (const auto& pd: devices) {
            if (pd.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                mPhysicalDevice = pd;
            }
        }
        std::cout << "Using device: " << mPhysicalDevice.getProperties().deviceName << std::endl;
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
        queueCreateInfo.setQueueFamilyIndex(static_cast<uint32_t>(mQueueFamilyPresent));
        queueCreateInfo.setQueueCount(static_cast<uint32_t>(queuePriorities.size()));
        queueCreateInfo.setPQueuePriorities(&queuePriorities[0]);

        vk::PhysicalDeviceFeatures features;
        features.setGeometryShader(VK_TRUE);

        vk::DeviceCreateInfo deviceCreateInfo;
        deviceCreateInfo.setEnabledExtensionCount(static_cast<uint32_t>(deviceExtensions.size()));
        deviceCreateInfo.setPpEnabledExtensionNames(&deviceExtensions[0]);
        deviceCreateInfo.setQueueCreateInfoCount(1);
        deviceCreateInfo.setPQueueCreateInfos(&queueCreateInfo);
        deviceCreateInfo.setPEnabledFeatures(&features);

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
        vk::Extent2D imageSize = surfaceCapabilities.currentExtent; // Use current surface size

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

        mRenderingResources.resize(swapchainImages.size());
        mFramebufferExtents = imageSize;

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
            std::array<vk::AttachmentDescription, 2> attachments;
            // Color
            attachments[0].setFormat(format.first);
            attachments[0].setSamples(vk::SampleCountFlagBits::e1);
            attachments[0].setLoadOp(vk::AttachmentLoadOp::eClear);
            attachments[0].setStoreOp(vk::AttachmentStoreOp::eStore);
            attachments[0].setInitialLayout(vk::ImageLayout::ePresentSrcKHR);
            attachments[0].setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

            // https://vulkan-tutorial.com/Depth_buffering
            // Depth
            attachments[1].setFormat(vk::Format::eD32Sfloat);
            attachments[1].setSamples(vk::SampleCountFlagBits::e1);
            attachments[1].setLoadOp(vk::AttachmentLoadOp::eClear);
            attachments[1].setStoreOp(vk::AttachmentStoreOp::eDontCare);
            attachments[1].setInitialLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
            attachments[1].setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

            // 0 is index into an vk::AttachmentDescriptions array of vk::RenderPassCreateInfo.
            vk::AttachmentReference colorAttachmentRef = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);
            vk::AttachmentReference depthAttachmentRef = vk::AttachmentReference(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

            vk::SubpassDescription subpass;
            subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
            subpass.setInputAttachmentCount(0);
            subpass.setColorAttachmentCount(1);
            subpass.setPColorAttachments(&colorAttachmentRef);
            subpass.setPDepthStencilAttachment(&depthAttachmentRef);

            std::array<vk::SubpassDependency, 2> subpassDependencies;
            // extern -> 0
            subpassDependencies[0].setSrcSubpass(VK_SUBPASS_EXTERNAL);
            subpassDependencies[0].setDstSubpass(0);
            subpassDependencies[0].setSrcStageMask(vk::PipelineStageFlagBits::eBottomOfPipe);
            subpassDependencies[0].setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
            subpassDependencies[0].setSrcAccessMask(vk::AccessFlagBits::eMemoryRead);
            subpassDependencies[0].setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
            subpassDependencies[0].setDependencyFlags(vk::DependencyFlagBits::eByRegion);
            // 0 -> extern
            subpassDependencies[1].setSrcSubpass(0);
            subpassDependencies[1].setDstSubpass(VK_SUBPASS_EXTERNAL);
            subpassDependencies[1].setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
            subpassDependencies[1].setDstStageMask(vk::PipelineStageFlagBits::eBottomOfPipe);
            subpassDependencies[1].setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
            subpassDependencies[1].setDstAccessMask(vk::AccessFlagBits::eMemoryRead);
            subpassDependencies[1].setDependencyFlags(vk::DependencyFlagBits::eByRegion);

            vk::RenderPassCreateInfo renderPassInfo;
            renderPassInfo.setAttachmentCount(static_cast<uint32_t>(attachments.size()));
            renderPassInfo.setPAttachments(&attachments[0]);
            renderPassInfo.setSubpassCount(1);
            renderPassInfo.setPSubpasses(&subpass);
            renderPassInfo.setDependencyCount(static_cast<uint32_t>(subpassDependencies.size()));
            renderPassInfo.setPDependencies(&subpassDependencies[0]);

            mRenderPass = MakeHolder(mDevice->createRenderPass(renderPassInfo), [this](vk::RenderPass & rpass) { mDevice->destroyRenderPass(rpass); });
            std::cout << "OK" << std::endl;
        }

        std::cout << "Create depth image... ";
        {
            vk::ImageCreateInfo imageInfo;
            imageInfo.setImageType(vk::ImageType::e2D);
            imageInfo.setExtent(vk::Extent3D(imageSize.width, imageSize.height, 1));
            imageInfo.setMipLevels(1);
            imageInfo.setArrayLayers(1);
            imageInfo.setFormat(vk::Format::eD32Sfloat);
            imageInfo.setTiling(vk::ImageTiling::eOptimal);
            imageInfo.setInitialLayout(vk::ImageLayout::ePreinitialized);
            imageInfo.setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);
            imageInfo.setSharingMode(vk::SharingMode::eExclusive);
            imageInfo.setSamples(vk::SampleCountFlagBits::e1);
            mDepthImage = MakeHolder(mDevice->createImage(imageInfo), [this](vk::Image & img) { mDevice->destroyImage(img); });

            vk::MemoryRequirements imageMemoryRequirments = mDevice->getImageMemoryRequirements(mDepthImage);

            vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties();
            for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                if ((imageMemoryRequirments.memoryTypeBits & (1 << i)) &&
                    (memroProperties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eDeviceLocal))) {

                    vk::MemoryAllocateInfo allocateInfo;
                    allocateInfo.setAllocationSize(imageMemoryRequirments.size);
                    allocateInfo.setMemoryTypeIndex(i);
                    mDepthImageMemory = MakeHolder(mDevice->allocateMemory(allocateInfo), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory); });
                }
            }
            if (!mDepthImageMemory) {
                throw std::runtime_error("Failed to allocate memory for staging image");
            }
            mDevice->bindImageMemory(mDepthImage, mDepthImageMemory, 0);
            std::cout << "OK" << std::endl;

            vk::ImageViewCreateInfo imageViewInfo;
            imageViewInfo.setImage(mDepthImage);
            imageViewInfo.setViewType(vk::ImageViewType::e2D);
            imageViewInfo.setFormat(vk::Format::eD32Sfloat);
            imageViewInfo.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity));
            imageViewInfo.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1));

            mDepthView = MakeHolder(mDevice->createImageView(imageViewInfo), [this](vk::ImageView & view) { mDevice->destroyImageView(view); });
        }

        {
            std::cout << "Create framebuffers... ";
            for (size_t i = 0; i < swapchainImages.size(); ++i) {
                mRenderingResources[i].imageHandle = swapchainImages[i];

                vk::ImageViewCreateInfo imageViewInfo;
                imageViewInfo.setImage(swapchainImages[i]);
                imageViewInfo.setViewType(vk::ImageViewType::e2D);
                imageViewInfo.setFormat(format.first);
                imageViewInfo.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity));
                imageViewInfo.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

                mRenderingResources[i].imageView = MakeHolder(mDevice->createImageView(imageViewInfo), [this](vk::ImageView & view) { mDevice->destroyImageView(view); });

                std::array<vk::ImageView, 2> attachments = { mRenderingResources[i].imageView, mDepthView };

                vk::FramebufferCreateInfo framebufferInfo;
                framebufferInfo.setRenderPass(*mRenderPass);
                framebufferInfo.setAttachmentCount(static_cast<uint32_t>(attachments.size()));
                framebufferInfo.setPAttachments(&attachments[0]);
                framebufferInfo.setWidth(imageSize.width);
                framebufferInfo.setHeight(imageSize.height);
                framebufferInfo.setLayers(1);

                mRenderingResources[i].framebuffer = MakeHolder(mDevice->createFramebuffer(framebufferInfo), [this](vk::Framebuffer & f) { mDevice->destroyFramebuffer(f); });
            }
            std::cout << "OK" << std::endl;
        }

        /* Create graphics pipeline now
        */

        std::cout << "Loading vertex shaders... " << std::endl;
        mVertexShader = LoadShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/13.vert");
        std::cout << "OK" << std::endl;

        std::cout << "Loading vertex shaders... " << std::endl;
        mGeometryShader = LoadShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/13.geom");
        std::cout << "OK" << std::endl;

        std::cout << "Loading fragment shaders... " << std::endl;
        mFragmentShader = LoadShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/13.frag");
        mFragmentShaderSecondary = LoadShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/13.2.frag");
        std::cout << "OK" << std::endl;


        std::cout << "Create descriptors set... ";
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings;
            bindings[0].setBinding(0);
            bindings[0].setDescriptorType(vk::DescriptorType::eUniformBuffer);
            bindings[0].setDescriptorCount(1);
            bindings[0].setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry);

            bindings[1].setBinding(1);
            bindings[1].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
            bindings[1].setDescriptorCount(1);
            bindings[1].setStageFlags(vk::ShaderStageFlagBits::eFragment);

            vk::DescriptorSetLayoutCreateInfo descriptorSetInfo;
            descriptorSetInfo.setBindingCount(static_cast<uint32_t>(bindings.size()));
            descriptorSetInfo.setPBindings(&bindings[0]);

            mDescriptorSetLayout = MakeHolder(mDevice->createDescriptorSetLayout(descriptorSetInfo), [this](vk::DescriptorSetLayout & layout) { mDevice->destroyDescriptorSetLayout(layout); });

            std::array<vk::DescriptorPoolSize, 2> poolSize;
            poolSize[0].setType(vk::DescriptorType::eUniformBuffer);
            poolSize[0].setDescriptorCount(1);

            poolSize[1].setType(vk::DescriptorType::eCombinedImageSampler);
            poolSize[1].setDescriptorCount(1);

            vk::DescriptorPoolCreateInfo poolInfo;
            poolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
            poolInfo.setMaxSets(1);
            poolInfo.setPoolSizeCount(static_cast<uint32_t>(poolSize.size()));
            poolInfo.setPPoolSizes(&poolSize[0]);

            mDescriptorPool = MakeHolder(mDevice->createDescriptorPool(poolInfo), [this](vk::DescriptorPool & pool) { mDevice->destroyDescriptorPool(pool); });

            vk::DescriptorSetAllocateInfo allocInfo;
            allocInfo.setDescriptorPool(mDescriptorPool);
            allocInfo.setDescriptorSetCount(1);
            allocInfo.setPSetLayouts(mDescriptorSetLayout.get());

            vk::DescriptorSet decriptorSet;
            if (vk::Result::eSuccess != mDevice->allocateDescriptorSets(&allocInfo, &decriptorSet)) {
                throw std::runtime_error("Failed to allocate descriptors set");
            }
            mDescriptorSet = MakeHolder(decriptorSet, [this](vk::DescriptorSet & set) { mDevice->freeDescriptorSets(mDescriptorPool, set); });
            std::cout << "OK" << std::endl;
        }

        /**
        * A pipeline is a collection of stages that process data one stage after another.
        * In Vulkan there is currently a compute pipeline and a graphics pipeline.
        * The compute pipeline allows us to perform some computational work, such as
        * performing physics calculations for objects in games. The graphics
        * pipeline is used for drawing operations.
        */
        {
            std::cout << "Create pipeline... ";

            std::array<vk::PipelineShaderStageCreateInfo, 2> stageInfosPrimary;
            stageInfosPrimary[0].setStage(vk::ShaderStageFlagBits::eVertex);
            stageInfosPrimary[0].setModule(mVertexShader);
            stageInfosPrimary[0].setPName("main"); // Shader entry point

            stageInfosPrimary[1].setStage(vk::ShaderStageFlagBits::eFragment);
            stageInfosPrimary[1].setModule(mFragmentShader);
            stageInfosPrimary[1].setPName("main"); // Shader entry point



            std::array<vk::PipelineShaderStageCreateInfo, 3> stageInfosSecondary;
            stageInfosSecondary[0].setStage(vk::ShaderStageFlagBits::eVertex);
            stageInfosSecondary[0].setModule(mVertexShader);
            stageInfosSecondary[0].setPName("main"); // Shader entry point

            stageInfosSecondary[1].setStage(vk::ShaderStageFlagBits::eGeometry);
            stageInfosSecondary[1].setModule(mGeometryShader);
            stageInfosSecondary[1].setPName("main"); // Shader entry point

            stageInfosSecondary[2].setStage(vk::ShaderStageFlagBits::eFragment);
            stageInfosSecondary[2].setModule(mFragmentShaderSecondary);
            stageInfosSecondary[2].setPName("main"); // Shader entry point



            vk::VertexInputBindingDescription inputBindingInfo;
            inputBindingInfo.setBinding(0);
            inputBindingInfo.setStride(sizeof(VertexData));
            inputBindingInfo.setInputRate(vk::VertexInputRate::eVertex); // consumed per vertex

            std::vector<vk::VertexInputAttributeDescription> attributeInfos;
            {
                // Position
                vk::VertexInputAttributeDescription attr;
                attr.setLocation(0);
                attr.setBinding(inputBindingInfo.binding);
                attr.setFormat(vk::Format::eR32G32B32A32Sfloat);
                attr.setOffset(offsetof(VertexData, position));
                attributeInfos.push_back(attr);
            }
            {
                // Normal
                vk::VertexInputAttributeDescription attr;
                attr.setLocation(1);
                attr.setBinding(inputBindingInfo.binding);
                attr.setFormat(vk::Format::eR32G32B32A32Sfloat);
                attr.setOffset(offsetof(VertexData, normal));
                attributeInfos.push_back(attr);
            }

            vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
            vertexInputInfo.setVertexBindingDescriptionCount(1);
            vertexInputInfo.setPVertexBindingDescriptions(&inputBindingInfo);
            vertexInputInfo.setVertexAttributeDescriptionCount(static_cast<uint32_t>(attributeInfos.size()));
            vertexInputInfo.setPVertexAttributeDescriptions(&attributeInfos[0]);

            vk::PipelineInputAssemblyStateCreateInfo inputAssembleInfo;
            inputAssembleInfo.setTopology(vk::PrimitiveTopology::eTriangleList);

            vk::PipelineViewportStateCreateInfo viewportInfo;
            viewportInfo.setViewportCount(1);
            viewportInfo.setPViewports(nullptr);
            viewportInfo.setScissorCount(1);
            viewportInfo.setPScissors(nullptr);
            // the viewportCount and scissorCount parameters must be equal

            std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dynamicStateInfo;
            dynamicStateInfo.setDynamicStateCount(static_cast<uint32_t>(dynamicStates.size()));
            dynamicStateInfo.setPDynamicStates(&dynamicStates[0]);

            vk::PipelineRasterizationStateCreateInfo rasterizationInfo;
            rasterizationInfo.setDepthClampEnable(VK_FALSE);
            rasterizationInfo.setCullMode(vk::CullModeFlagBits::eBack);
            rasterizationInfo.setPolygonMode(vk::PolygonMode::eFill);
            rasterizationInfo.setFrontFace(vk::FrontFace::eCounterClockwise);
            rasterizationInfo.setLineWidth(1.0f);

            vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
            depthStencilInfo.setDepthTestEnable(VK_TRUE);
            depthStencilInfo.setDepthWriteEnable(VK_TRUE);
            depthStencilInfo.setDepthCompareOp(vk::CompareOp::eLess);
            depthStencilInfo.setDepthBoundsTestEnable(VK_FALSE);
            depthStencilInfo.setMinDepthBounds(0.0f);
            depthStencilInfo.setMaxDepthBounds(1.0f);
            depthStencilInfo.setStencilTestEnable(VK_FALSE);

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
            pipelineLayoutInfo.setSetLayoutCount(1);
            pipelineLayoutInfo.setPSetLayouts(mDescriptorSetLayout.get());
            mPipelineLayout = MakeHolder(mDevice->createPipelineLayout(pipelineLayoutInfo), [this](vk::PipelineLayout & layout) { mDevice->destroyPipelineLayout(layout); });

            vk::GraphicsPipelineCreateInfo grapichsPipelineInfo;
            grapichsPipelineInfo.setStageCount(static_cast<uint32_t>(stageInfosPrimary.size()));
            grapichsPipelineInfo.setPStages(&stageInfosPrimary[0]);
            grapichsPipelineInfo.setPVertexInputState(&vertexInputInfo);
            grapichsPipelineInfo.setPInputAssemblyState(&inputAssembleInfo);
            grapichsPipelineInfo.setPViewportState(&viewportInfo);
            grapichsPipelineInfo.setPRasterizationState(&rasterizationInfo);
            grapichsPipelineInfo.setPDepthStencilState(&depthStencilInfo);
            grapichsPipelineInfo.setPMultisampleState(&multisampleInfo);
            grapichsPipelineInfo.setPColorBlendState(&blendingInfo);
            grapichsPipelineInfo.setLayout(mPipelineLayout);
            grapichsPipelineInfo.setRenderPass(mRenderPass);
            grapichsPipelineInfo.setPDynamicState(&dynamicStateInfo);

            mPipelinePrimary = MakeHolder(mDevice->createGraphicsPipeline(vk::PipelineCache(), grapichsPipelineInfo), [this](vk::Pipeline & pipeline) {mDevice->destroyPipeline(pipeline); });

            grapichsPipelineInfo.setStageCount(static_cast<uint32_t>(stageInfosSecondary.size()));
            grapichsPipelineInfo.setPStages(&stageInfosSecondary[0]);

            mPipelineSecondary = MakeHolder(mDevice->createGraphicsPipeline(vk::PipelineCache(), grapichsPipelineInfo), [this](vk::Pipeline & pipeline) {mDevice->destroyPipeline(pipeline); });

            std::cout << "OK" << std::endl;
        }

        std::cout << "Prepare vertex buffer...";
        {
            auto mesh = GenerateSphere(0.7f, 32, 32);
            mIndexesNumber = static_cast<uint32_t>(mesh.indexes.size());
            const uint32_t vertexBufferSize = static_cast<uint32_t>(mesh.vertexes.size() * sizeof(decltype(mesh.vertexes)::value_type));
            const uint32_t indexesBufferSize = static_cast<uint32_t>(mesh.indexes.size() * sizeof(decltype(mesh.indexes)::value_type));

            {
                vk::BufferCreateInfo bufferInfo;
                bufferInfo.setSize(vertexBufferSize);
                bufferInfo.setUsage(vk::BufferUsageFlagBits::eVertexBuffer);
                bufferInfo.setSharingMode(vk::SharingMode::eExclusive);
                mVertexBuffer = MakeHolder(mDevice->createBuffer(bufferInfo), [this](vk::Buffer & buffer) { mDevice->destroyBuffer(buffer); });
            }

            {
                vk::BufferCreateInfo bufferInfo;
                bufferInfo.setSize(indexesBufferSize);
                bufferInfo.setUsage(vk::BufferUsageFlagBits::eIndexBuffer);
                bufferInfo.setSharingMode(vk::SharingMode::eExclusive);
                mIndexesBuffer = MakeHolder(mDevice->createBuffer(bufferInfo), [this](vk::Buffer & buffer) { mDevice->destroyBuffer(buffer); });
            }

            // Upload vertex data
            {
                vk::MemoryRequirements vertexMemoryRequirements = mDevice->getBufferMemoryRequirements(mVertexBuffer);

                vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties();
                for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                    if ((vertexMemoryRequirements.memoryTypeBits & (1 << i)) &&
                        (memroProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {

                        vk::MemoryAllocateInfo allocateInfo;
                        allocateInfo.setAllocationSize(vertexMemoryRequirements.size);
                        allocateInfo.setMemoryTypeIndex(i);
                        mVertexMemory = MakeHolder(mDevice->allocateMemory(allocateInfo), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory); });
                    }
                }
                if (!mVertexMemory) {
                    throw std::runtime_error("Failed to allocate memory for vertex buffer");
                }
                mDevice->bindBufferMemory(mVertexBuffer, mVertexMemory, 0);

                void* devicePtr = mDevice->mapMemory(mVertexMemory, 0, vertexBufferSize);
                if (devicePtr == nullptr) {
                    throw std::runtime_error("Failed to map memory for vertex buffer");
                }
                std::memcpy(devicePtr, mesh.vertexes.data(), vertexBufferSize);

                vk::MappedMemoryRange mappedRange;
                mappedRange.setMemory(mVertexMemory);
                mappedRange.setOffset(0);
                mappedRange.setSize(VK_WHOLE_SIZE);
                mDevice->flushMappedMemoryRanges(mappedRange);

                mDevice->unmapMemory(mVertexMemory);
            }

            // Upload index data
            {
                vk::MemoryRequirements indexMemoryRequirements = mDevice->getBufferMemoryRequirements(mIndexesBuffer);

                vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties();
                for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                    if ((indexMemoryRequirements.memoryTypeBits & (1 << i)) &&
                        (memroProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {

                        vk::MemoryAllocateInfo allocateInfo;
                        allocateInfo.setAllocationSize(indexMemoryRequirements.size);
                        allocateInfo.setMemoryTypeIndex(i);
                        mIndexesMemory = MakeHolder(mDevice->allocateMemory(allocateInfo), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory); });
                    }
                }
                if (!mIndexesMemory) {
                    throw std::runtime_error("Failed to allocate memory for index buffer");
                }
                mDevice->bindBufferMemory(mIndexesBuffer, mIndexesMemory, 0);

                void* devicePtr = mDevice->mapMemory(mIndexesMemory, 0, indexesBufferSize);
                if (devicePtr == nullptr) {
                    throw std::runtime_error("Failed to map memory for vertex buffer");
                }
                std::memcpy(devicePtr, mesh.indexes.data(), indexesBufferSize);

                vk::MappedMemoryRange mappedRange;
                mappedRange.setMemory(mIndexesMemory);
                mappedRange.setOffset(0);
                mappedRange.setSize(VK_WHOLE_SIZE);
                mDevice->flushMappedMemoryRanges(mappedRange);

                mDevice->unmapMemory(mIndexesMemory);
            }
            std::cout << "OK" << std::endl;
        }

        std::cout << "Prepare matrixes...";
        {
            mDefaultOrientation.FromAngleAxis(Ogre::Radian(-1.0f), Ogre::Vector3::UNIT_Y);
            mRotationX = Ogre::Quaternion::IDENTITY;
            mRotationY = Ogre::Quaternion::IDENTITY;
            MakePerspectiveProjectionMatrix(mMatrixes.projection, static_cast<float>(width) / height, 45.0f, 0.01f, 1000.0f);

            {
                vk::BufferCreateInfo bufferInfo;
                bufferInfo.setSize(sizeof(mMatrixes));
                bufferInfo.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
                bufferInfo.setSharingMode(vk::SharingMode::eExclusive);
                mMatrixesBuffer = MakeHolder(mDevice->createBuffer(bufferInfo), [this](vk::Buffer & buffer) { mDevice->destroyBuffer(buffer); });
            }

            /**
            * if propertyFlags has the VK_MEMORY_PROPERTY_HOST_COHERENT_BIT bit set, host cache management commands
            * vkFlushMappedMemoryRanges and vkInvalidateMappedMemoryRanges are not needed to make host writes visible
            * to the device or device writes visible to the host, respectively.
            */
            vk::MemoryRequirements matrixesRequirements = mDevice->getBufferMemoryRequirements(mMatrixesBuffer);

            vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties();
            for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                if ((matrixesRequirements.memoryTypeBits & (1 << i)) &&
                    (memroProperties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))) {

                    vk::MemoryAllocateInfo allocateInfo;
                    allocateInfo.setAllocationSize(matrixesRequirements.size);
                    allocateInfo.setMemoryTypeIndex(i);
                    mMatrixesMemory = MakeHolder(mDevice->allocateMemory(allocateInfo), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory); });
                }
            }
            if (!mMatrixesMemory) {
                throw std::runtime_error("Failed to allocate memory for matrix buffers");
            }
            mDevice->bindBufferMemory(mMatrixesBuffer, mMatrixesMemory, 0);

            std::cout << "OK" << std::endl;
        }

        std::cout << "Prepare descriptors set...";
        {

            std::array<vk::WriteDescriptorSet, 1> writeDescriptorsInfo;

            vk::DescriptorBufferInfo matrixesBufferInfo;
            matrixesBufferInfo.setBuffer(mMatrixesBuffer);
            matrixesBufferInfo.setRange(sizeof(mMatrixes));
            matrixesBufferInfo.setOffset(0);

            writeDescriptorsInfo[0].setDescriptorType(vk::DescriptorType::eUniformBuffer);
            writeDescriptorsInfo[0].setDstSet(mDescriptorSet);
            writeDescriptorsInfo[0].setDstBinding(0);
            writeDescriptorsInfo[0].setDstArrayElement(0);
            writeDescriptorsInfo[0].setDescriptorCount(1);
            writeDescriptorsInfo[0].setPBufferInfo(&matrixesBufferInfo);

            mDevice->updateDescriptorSets(static_cast<uint32_t>(writeDescriptorsInfo.size()), &writeDescriptorsInfo[0], 0, nullptr);

            std::cout << "OK" << std::endl;
        }

        // Preparing sync resources
        for (auto & resource : mRenderingResources) {
            vk::FenceCreateInfo fenceInfo;
            fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
            resource.fence = MakeHolder(mDevice->createFence(fenceInfo), [this](vk::Fence & fence) { mDevice->destroyFence(fence); });

            resource.undefinedLaout = true;
        }

        mSemaphoreAvailable = MakeHolder(mDevice->createSemaphore(vk::SemaphoreCreateInfo()), [this](vk::Semaphore & sem) { mDevice->destroySemaphore(sem); });
        mSemaphoreFinished  = MakeHolder(mDevice->createSemaphore(vk::SemaphoreCreateInfo()), [this](vk::Semaphore & sem) { mDevice->destroySemaphore(sem); });

        CanRender = true;
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

        if (vk::Result::eSuccess != mDevice->waitForFences(1, renderingResource.fence.get(), VK_FALSE, TIMEOUT)) {
            std::cout << "Waiting for fence takes too long!" << std::endl;
            return false;
        }
        mDevice->resetFences(1, renderingResource.fence.get());

        // Prepare command buffer
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
            vk::ImageSubresourceRange depthRange;
            depthRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            depthRange.baseMipLevel = 0;
            depthRange.levelCount = 1;
            depthRange.baseArrayLayer = 0;
            depthRange.layerCount = 1;

            vk::ImageMemoryBarrier barrierDepthPreinitToOptimal;
            barrierDepthPreinitToOptimal.srcAccessMask = vk::AccessFlags{};
            barrierDepthPreinitToOptimal.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            barrierDepthPreinitToOptimal.oldLayout = vk::ImageLayout::ePreinitialized;
            barrierDepthPreinitToOptimal.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            barrierDepthPreinitToOptimal.srcQueueFamilyIndex = mQueueFamilyPresent;
            barrierDepthPreinitToOptimal.dstQueueFamilyIndex = mQueueFamilyPresent;
            barrierDepthPreinitToOptimal.image = mDepthImage;
            barrierDepthPreinitToOptimal.subresourceRange = depthRange;
            cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierDepthPreinitToOptimal);
        }


        vk::ImageMemoryBarrier barrierFromPresentToDraw;
        barrierFromPresentToDraw.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
        barrierFromPresentToDraw.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        barrierFromPresentToDraw.oldLayout = renderingResource.undefinedLaout ? vk::ImageLayout::eUndefined : vk::ImageLayout::ePresentSrcKHR;
        barrierFromPresentToDraw.newLayout = vk::ImageLayout::ePresentSrcKHR;
        barrierFromPresentToDraw.srcQueueFamilyIndex = mQueueFamilyPresent;
        barrierFromPresentToDraw.dstQueueFamilyIndex = mQueueFamilyGraphics;
        barrierFromPresentToDraw.image = renderingResource.imageHandle;
        barrierFromPresentToDraw.subresourceRange = range;
        cmdBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, &barrierFromPresentToDraw);
        renderingResource.undefinedLaout = false;
        

        vk::ClearColorValue targetColor = std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f };
        std::array<vk::ClearValue, 2> clearValues;
        clearValues[0] = vk::ClearValue(targetColor);
        clearValues[1] = { 1.0f };

        vk::RenderPassBeginInfo renderPassInfo;
        renderPassInfo.setRenderPass(mRenderPass);
        renderPassInfo.setFramebuffer(renderingResource.framebuffer);
        renderPassInfo.setRenderArea(vk::Rect2D(vk::Offset2D(0, 0), mFramebufferExtents));
        renderPassInfo.setClearValueCount(static_cast<uint32_t>(clearValues.size()));
        renderPassInfo.setPClearValues(&clearValues[0]);
        cmdBuffer->beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Update matrixes
        mPosition = Ogre::Vector3(0.0f, 0.0f, -3.0f);
        Ogre::Quaternion currentOrientation = mRotationY * mRotationX * mDefaultOrientation;
        mMatrixes.modelView.makeTransform(mPosition, Ogre::Vector3::UNIT_SCALE, currentOrientation);
        mMatrixes.modelView = mMatrixes.modelView.transpose();

        uint8_t* devicePtr = static_cast<uint8_t*>(mDevice->mapMemory(mMatrixesMemory, 0, sizeof(mMatrixes)));
        if (devicePtr == nullptr) {
            throw std::runtime_error("Failed to map memory for vertex buffer");
        }
        std::memcpy(devicePtr, &mMatrixes, sizeof(mMatrixes));
        mDevice->unmapMemory(mMatrixesMemory);

        // Could be static, but let's try dynamic approach
        vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(mFramebufferExtents.width), static_cast<float>(mFramebufferExtents.height), 0.0f, 1.0f);
        vk::Rect2D   scissor(vk::Offset2D(0, 0), mFramebufferExtents);
        cmdBuffer->setViewport(0, 1, &viewport);
        cmdBuffer->setScissor(0, 1, &scissor);

        cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mPipelineLayout, 0, 1, mDescriptorSet.get(), 0, nullptr);

        vk::DeviceSize offset = 0;
        cmdBuffer->bindVertexBuffers(0, 1, mVertexBuffer.get(), &offset);
        cmdBuffer->bindIndexBuffer(mIndexesBuffer, 0, vk::IndexType::eUint16);

        // Primary pipeline
        cmdBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, mPipelinePrimary);
        cmdBuffer->drawIndexed(mIndexesNumber, 1, 0, 0, 0);

        // Secondary pipeline
        cmdBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, mPipelineSecondary);
        cmdBuffer->drawIndexed(mIndexesNumber, 1, 0, 0, 0);

        cmdBuffer->endRenderPass();

        vk::ImageMemoryBarrier barrierFromDrawToPresent;
        barrierFromDrawToPresent.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        barrierFromDrawToPresent.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
        barrierFromDrawToPresent.oldLayout = vk::ImageLayout::ePresentSrcKHR;
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

        mFirstDraw = false;
        return true;
    }

    void OnMouseEvent(ApiWithoutSecrets::OS::MouseEvent event, int x, int y) override
    {
        switch (event) {
        case ApiWithoutSecrets::OS::MouseEvent::Down:
            mMousePosition = Ogre::Vector2(static_cast<float>(x), static_cast<float>(y));
            mIsMouseDown = true;
            break;
        case ApiWithoutSecrets::OS::MouseEvent::Move:
            if (mIsMouseDown) {
                Ogre::Vector2 newPos = Ogre::Vector2(static_cast<float>(x), static_cast<float>(y));
                mRotationX.FromAngleAxis(-Ogre::Radian((newPos.x - mMousePosition.x) / 180.0f), Ogre::Vector3::UNIT_Y);
                mRotationY.FromAngleAxis( Ogre::Radian((newPos.y - mMousePosition.y) / 180.0f), Ogre::Vector3::UNIT_X);
            }
            break;
        case ApiWithoutSecrets::OS::MouseEvent::Up:
            mDefaultOrientation = mRotationY * mRotationX * mDefaultOrientation;
            mRotationX = Ogre::Quaternion::IDENTITY;
            mRotationY = Ogre::Quaternion::IDENTITY;
            mIsMouseDown = false;
            break;
        }
    }

    void Shutdown() override
    {
        if (*mDevice) {
            mDevice->waitIdle();
        }
    }

};

int main()
{
    try {
        ApiWithoutSecrets::OS::Window window;
        // Window creation
        if (!window.Create("13 - Geometry shader", 512, 512)) {
            return -1;
        }

        // Render loop
        Sample_03_Window application(window.GetParameters(), 512, 512);
        window.SetMouseListener(&application);
        if (!window.RenderingLoop(application)) {
            return -1;
        }
    }
    catch (std::runtime_error & err) {
        std::cout << "Error!" << std::endl;
        std::cout << err.what() << std::endl;
    }
}
