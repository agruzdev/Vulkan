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
#include <math/OgreAxisAlignedBox.h>

class Sample_03_Window
    : public ApiWithoutSecrets::OS::TutorialBase
    , public ApiWithoutSecrets::OS::MouseListener
{

    struct GeometryBuffer
    {
        Ogre::Vector4 position;
        Ogre::Vector4 scale;
    };

    struct CameraProperties
    {
        Ogre::Matrix4 viewInverse;
        Ogre::Matrix4 projInverse;
    };

    struct VkGeometryInstance {
        float transform[12];
        uint32_t instanceId : 24;
        uint32_t mask : 8;
        uint32_t instanceOffset : 24;
        uint32_t flags : 8;
        uint64_t accelerationStructureHandle;
    };

    struct RenderingResource
    {
        vk::Image imageHandle;
        VulkanHolder<vk::ImageView> imageView;
        VulkanHolder<vk::DescriptorSet> descriptorSet;
        VulkanHolder<vk::CommandBuffer> initCommand;
        VulkanHolder<vk::CommandBuffer> drawCommand;
        VulkanHolder<vk::Semaphore> initializedSemaphore;
        VulkanHolder<vk::Semaphore> readyToPresentSemaphore;
        VulkanHolder<vk::Fence> fence;

        VulkanHolder<vk::Buffer> cameraPropsBuffer;
        VulkanHolder<vk::DeviceMemory> cameraPropsBufferMemory;

        bool inited;
    };

    ApiWithoutSecrets::OS::LibraryHandle mVkLib{};
    vk::DispatchLoaderDynamic mVkDispatcher;

    VulkanHolder<vk::Instance> mVulkan;
    vk::PhysicalDevice mPhysicalDevice;
    VulkanHolder<vk::Device> mDevice;
    VulkanHolder<vk::SurfaceKHR> mSurface;
    VulkanHolder<vk::SwapchainKHR> mSwapChain;
    VulkanHolder<vk::CommandPool> mCommandPool;

    VulkanHolder<vk::Buffer> mVertexBuffer;
    VulkanHolder<vk::DeviceMemory> mVertexMemory;

    VulkanHolder<vk::Buffer> mAabbBuffer;
    VulkanHolder<vk::DeviceMemory> mAabbMemory;

    VulkanHolder<vk::AccelerationStructureNV> mAccelerationStructureBottom;
    VulkanHolder<vk::DeviceMemory> mAccelerationStructureBottomMemory;

    VulkanHolder<vk::AccelerationStructureNV> mAccelerationStructureTop;
    VulkanHolder<vk::DeviceMemory> mAccelerationStructureTopMemory;

    VulkanHolder<vk::Buffer> mInstancesBuffer;
    VulkanHolder<vk::DeviceMemory> mInstancesMemory;

    VulkanHolder<vk::ShaderModule> mRayGenShader;
    VulkanHolder<vk::ShaderModule> mRayIntersectShader;
    VulkanHolder<vk::ShaderModule> mRayMissShader;
    VulkanHolder<vk::ShaderModule> mRayCloseHitShader;
    VulkanHolder<vk::ShaderModule> mRayShadowMissShader;

    VulkanHolder<vk::DescriptorSetLayout> mDescriptorsSetLayout;
    VulkanHolder<vk::DescriptorPool> mDescriptorsPool;

    VulkanHolder<vk::PipelineLayout> mPipelineLayout;
    VulkanHolder<vk::Pipeline> mPipeline;

    VulkanHolder<vk::Buffer> mShaderBindingTable;
    VulkanHolder<vk::DeviceMemory> mShaderBindingTableMemory;

    std::vector<RenderingResource> mRenderingResources;

    VulkanHolder<vk::Semaphore> mSemaphoreAvailable;


    vk::Queue mGraphicsQueue;
    vk::Queue mPresentQueue;

    uint32_t mQueueFamilyGraphics = std::numeric_limits<uint32_t>::max();
    uint32_t mQueueFamilyPresent = std::numeric_limits<uint32_t>::max();

    Ogre::Matrix4 mProjectionMatrix = Ogre::Matrix4::IDENTITY;

    vk::Extent2D mFramebufferExtents;


    Ogre::Vector2 mMousePosition;
    bool mIsMouseDown = false;
    Ogre::Quaternion mDefaultOrientation;
    Ogre::Quaternion mRotationX;
    Ogre::Quaternion mRotationY;


    static void MakePerspectiveProjectionMatrix(Ogre::Matrix4 & dst, const float aspectRatio, const float fieldOfView, const float nearClip, const float farClip)
    {
        const float f = 1.0f / std::tan(fieldOfView * 0.5f * 0.01745329251994329576923690768489f);

        dst[0][0] = f / aspectRatio;
        dst[0][1] = 0.0f;
        dst[0][2] = 0.0f;
        dst[0][3] = 0.0f;

        dst[1][0] = 0.0f;
        dst[1][1] = f;
        dst[1][2] = 0.0f;
        dst[1][3] = 0.0f;

        dst[2][0] = 0.0f;
        dst[2][1] = 0.0f;
        dst[2][2] = (nearClip + farClip) / (nearClip - farClip);
        dst[2][3] = -1.0f;

        dst[3][0] = 0.0f;
        dst[3][1] = 0.0f;
        dst[3][2] = (2.0f * nearClip * farClip) / (nearClip - farClip);
        dst[3][3] = 0.0f;
    }

    void BuildAccelerationStructures(vk::AccelerationStructureInfoNV topASInfo, vk::AccelerationStructureInfoNV bottomASInfo, std::vector<VkGeometryInstance>& instances)
    {
        vk::AccelerationStructureCreateInfoNV accelerationStructureCreateInfoTop{};
        accelerationStructureCreateInfoTop.setInfo(topASInfo);

        mAccelerationStructureTop = MakeHolder(mDevice->createAccelerationStructureNV(accelerationStructureCreateInfoTop, nullptr, mVkDispatcher), [this](vk::AccelerationStructureNV& s) { mDevice->destroyAccelerationStructureNV(s, nullptr, mVkDispatcher); });
        if (!mAccelerationStructureTop) {
            throw std::runtime_error("Failed to create NV top-level acceleration structure.");
        }

        vk::AccelerationStructureCreateInfoNV accelerationStructureCreateInfoBottom{};
        accelerationStructureCreateInfoBottom.setInfo(bottomASInfo);

        mAccelerationStructureBottom = MakeHolder(mDevice->createAccelerationStructureNV(accelerationStructureCreateInfoBottom, nullptr, mVkDispatcher), [this](vk::AccelerationStructureNV& s) { mDevice->destroyAccelerationStructureNV(s, nullptr, mVkDispatcher); });
        if (!mAccelerationStructureBottom) {
            throw std::runtime_error("Failed to create NV bottom-level acceleration structure.");
        }

        vk::AccelerationStructureMemoryRequirementsInfoNV asMemInfo{};

        asMemInfo.setAccelerationStructure(mAccelerationStructureTop);
        vk::MemoryRequirements2 memRequiementsTop = mDevice->getAccelerationStructureMemoryRequirementsNV(asMemInfo, mVkDispatcher);

        asMemInfo.setAccelerationStructure(mAccelerationStructureBottom);
        vk::MemoryRequirements2 memRequiementsBottom = mDevice->getAccelerationStructureMemoryRequirementsNV(asMemInfo, mVkDispatcher);

        vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties(mVkDispatcher);

        for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
            if ((memRequiementsBottom.memoryRequirements.memoryTypeBits & (1 << i)) &&
                (memroProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {

                vk::MemoryAllocateInfo allocateInfo;
                allocateInfo.setAllocationSize(memRequiementsBottom.memoryRequirements.size);
                allocateInfo.setMemoryTypeIndex(i);
                mAccelerationStructureBottomMemory = MakeHolder(mDevice->allocateMemory(allocateInfo, nullptr, mVkDispatcher), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory, nullptr, mVkDispatcher); });
                break;
            }
        }
        if (!mAccelerationStructureBottomMemory) {
            throw std::runtime_error("Failed to allocate a scratch memory for bottom AS");
        }

        for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
            if ((memRequiementsTop.memoryRequirements.memoryTypeBits & (1 << i)) &&
                (memroProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {

                vk::MemoryAllocateInfo allocateInfo;
                allocateInfo.setAllocationSize(memRequiementsTop.memoryRequirements.size);
                allocateInfo.setMemoryTypeIndex(i);
                mAccelerationStructureTopMemory = MakeHolder(mDevice->allocateMemory(allocateInfo, nullptr, mVkDispatcher), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory, nullptr, mVkDispatcher); });
                break;
            }
        }
        if (!mAccelerationStructureTopMemory) {
            throw std::runtime_error("Failed to allocate a scratch memory for top AS");
        }

        std::array<vk::BindAccelerationStructureMemoryInfoNV, 2> asBindInfos = {};
        
        asBindInfos[0].setAccelerationStructure(mAccelerationStructureBottom);
        asBindInfos[0].setMemory(mAccelerationStructureBottomMemory);
        asBindInfos[0].setMemoryOffset(0);
        
        asBindInfos[1].setAccelerationStructure(mAccelerationStructureTop);
        asBindInfos[1].setMemory(mAccelerationStructureTopMemory);
        asBindInfos[1].setMemoryOffset(0);

        mDevice->bindAccelerationStructureMemoryNV(static_cast<uint32_t>(asBindInfos.size()), asBindInfos.data(), mVkDispatcher);


        const vk::DeviceSize scratchMemorySize = std::max(memRequiementsTop.memoryRequirements.size, memRequiementsBottom.memoryRequirements.size);

        vk::BufferCreateInfo sratchBufferInfo{};
        sratchBufferInfo.setSize(scratchMemorySize);
        sratchBufferInfo.setUsage(vk::BufferUsageFlagBits::eRayTracingNV);
        sratchBufferInfo.setSharingMode(vk::SharingMode::eExclusive);

        VulkanHolder<vk::Buffer> scratchBuffer = MakeHolder(mDevice->createBuffer(sratchBufferInfo, nullptr, mVkDispatcher), [this](vk::Buffer& b){
            mDevice->destroyBuffer(b, nullptr, mVkDispatcher);
        });
        if (!scratchBuffer) {
            throw std::runtime_error("Failed to create a scratch buffer for AS");
        }

        VulkanHolder<vk::DeviceMemory> scratchMemory;

        for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
            if ((memRequiementsBottom.memoryRequirements.memoryTypeBits & (1 << i)) &&
                (memroProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {

                vk::MemoryAllocateInfo allocateInfo;
                allocateInfo.setAllocationSize(scratchMemorySize);
                allocateInfo.setMemoryTypeIndex(i);
                scratchMemory = MakeHolder(mDevice->allocateMemory(allocateInfo, nullptr, mVkDispatcher), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory, nullptr, mVkDispatcher); });
                break;
            }
        }
        if (!scratchMemory) {
            throw std::runtime_error("Failed to allocate a scratch memory for AS");
        }
        mDevice->bindBufferMemory(scratchBuffer, scratchMemory, 0, mVkDispatcher);

        vk::CommandPoolCreateInfo commandsPoolInfo;
        commandsPoolInfo.setQueueFamilyIndex(mQueueFamilyGraphics);
        commandsPoolInfo.setFlags(vk::CommandPoolCreateFlagBits::eTransient);
        auto commandPool = MakeHolder(mDevice->createCommandPool(commandsPoolInfo, nullptr, mVkDispatcher), [this](vk::CommandPool & p) { mDevice->destroyCommandPool(p, nullptr, mVkDispatcher); });
        if (!commandPool) {
            throw std::runtime_error("Failed to create a command pool for AS");
        }

        vk::CommandBufferAllocateInfo commandAllocateInfo;
        commandAllocateInfo.setCommandPool(commandPool);
        commandAllocateInfo.setLevel(vk::CommandBufferLevel::ePrimary);
        commandAllocateInfo.setCommandBufferCount(1);

        VulkanHolder<vk::CommandBuffer> buildCommand = MakeHolder<vk::CommandBuffer>(nullptr, [this, &commandPool](vk::CommandBuffer& c){ mDevice->freeCommandBuffers(commandPool, 1, &c, mVkDispatcher); });
        auto vkError = mDevice->allocateCommandBuffers(&commandAllocateInfo, buildCommand.get(), mVkDispatcher);
        if (vkError != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to create a command buffer for AS: " + to_string(vkError));
        }

        //vkGetAccelerationStructureHandleNV

        uint64_t bottomASHandle = 0;
        if (vk::Result::eSuccess != mDevice->getAccelerationStructureHandleNV(mAccelerationStructureBottom, sizeof(bottomASHandle), &bottomASHandle, mVkDispatcher)) {
            throw std::runtime_error("Failed to get an AS handle");
        }

        for (auto& inst : instances) {
            inst.accelerationStructureHandle = bottomASHandle;
        }

        vk::BufferCreateInfo instancesBufferInfo{};
        instancesBufferInfo.setSize(instances.size() * sizeof(VkGeometryInstance));
        instancesBufferInfo.setUsage(vk::BufferUsageFlagBits::eRayTracingNV);
        instancesBufferInfo.setSharingMode(vk::SharingMode::eExclusive);

        mInstancesBuffer = MakeHolder(mDevice->createBuffer(instancesBufferInfo, nullptr, mVkDispatcher), [this](vk::Buffer& b){
            mDevice->destroyBuffer(b, nullptr, mVkDispatcher);
        });
        if(!mInstancesBuffer) {
            throw std::runtime_error("Failed to create an instances buffer for AS");
        }

        vk::MemoryRequirements instancesMemoryRequirements = mDevice->getBufferMemoryRequirements(mInstancesBuffer, mVkDispatcher);

        for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
            if ((instancesMemoryRequirements.memoryTypeBits & (1 << i)) &&
                (memroProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {

                vk::MemoryAllocateInfo allocateInfo;
                allocateInfo.setAllocationSize(instancesMemoryRequirements.size);
                allocateInfo.setMemoryTypeIndex(i);
                mInstancesMemory = MakeHolder(mDevice->allocateMemory(allocateInfo, nullptr, mVkDispatcher), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory, nullptr, mVkDispatcher); });
                break;
            }
        }
        if (!mInstancesMemory) {
            throw std::runtime_error("Failed to allocate an instances memory for AS");
        }


        mDevice->bindBufferMemory(mInstancesBuffer, mInstancesMemory, 0, mVkDispatcher);

        void* instancesDataPtr = mDevice->mapMemory(mInstancesMemory, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, mVkDispatcher);
        if (!instancesDataPtr) {
            throw std::runtime_error("Failed to map the instances memory for AS");
        }

        std::memcpy(instancesDataPtr, instances.data(), instances.size() * sizeof(VkGeometryInstance));

        vk::MappedMemoryRange flushRange{};
        flushRange.setMemory(mInstancesMemory);
        flushRange.setOffset(0);
        flushRange.setSize(VK_WHOLE_SIZE);

        mDevice->flushMappedMemoryRanges(1, &flushRange, mVkDispatcher);

        mDevice->unmapMemory(mInstancesMemory, mVkDispatcher);


        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

        buildCommand->begin(beginInfo, mVkDispatcher);

        vk::MemoryBarrier barrierBuilding{};
        barrierBuilding.setSrcAccessMask(vk::AccessFlagBits::eAccelerationStructureReadNV | vk::AccessFlagBits::eAccelerationStructureWriteNV);
        barrierBuilding.setDstAccessMask(vk::AccessFlagBits::eAccelerationStructureReadNV | vk::AccessFlagBits::eAccelerationStructureWriteNV);

        vk::MemoryBarrier barrierFinish{};
        barrierFinish.setSrcAccessMask(vk::AccessFlagBits::eAccelerationStructureReadNV | vk::AccessFlagBits::eAccelerationStructureWriteNV);
        barrierFinish.setDstAccessMask(vk::AccessFlagBits::eAccelerationStructureReadNV);

        buildCommand->buildAccelerationStructureNV(bottomASInfo, nullptr, 0, false, mAccelerationStructureBottom, nullptr, scratchBuffer, 0, mVkDispatcher);

        buildCommand->pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, vk::DependencyFlags{}, 1, &barrierBuilding, 0, nullptr, 0, nullptr, mVkDispatcher);

        buildCommand->buildAccelerationStructureNV(topASInfo, mInstancesBuffer, 0, false, mAccelerationStructureTop, nullptr, scratchBuffer, 0, mVkDispatcher);

        buildCommand->pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::DependencyFlags{}, 1, &barrierFinish, 0, nullptr, 0, nullptr, mVkDispatcher);

        buildCommand->end();

        VulkanHolder<vk::Fence> fence = MakeHolder(mDevice->createFence(vk::FenceCreateInfo{}, nullptr, mVkDispatcher), [this](vk::Fence& f){ mDevice->destroyFence(f, nullptr, mVkDispatcher); });

        vk::SubmitInfo submitInfo{};
        submitInfo.setCommandBufferCount(1);
        submitInfo.setPCommandBuffers(buildCommand.get());
        
        vkError = mGraphicsQueue.submit(1, &submitInfo, *fence, mVkDispatcher);
        if (vkError != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to submit build command for AS");
        }

        constexpr uint64_t TIMEOUT = static_cast<uint64_t>(10) * 1000 * 1000 * 1000; // 10 second in nanos

        vkError = mDevice->waitForFences(1, fence.get(), true, TIMEOUT, mVkDispatcher);
        if (vkError != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait for build command for AS");
        }

        mGraphicsQueue.waitIdle(mVkDispatcher);
    }

public:

    bool CheckPhysicalDeviceProperties(const vk::PhysicalDevice & physicalDevice, uint32_t &selected_graphics_queue_family_index, uint32_t &selected_present_queue_family_index)
    {
        auto deviceProperties = physicalDevice.getProperties(mVkDispatcher);
        auto deviceFeatures = physicalDevice.getFeatures(mVkDispatcher);

        if ((VK_VERSION_MAJOR(deviceProperties.apiVersion) < 1) || (deviceProperties.limits.maxImageDimension2D < 4096)) {
            std::cout << "Physical device " << physicalDevice << " doesn't support required parameters!" << std::endl;
            return false;
        }

        auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties(mVkDispatcher);
        std::vector<vk::Bool32> queuePresentSupport(queueFamilyProperties.size());

        uint32_t graphics_queue_family_index = UINT32_MAX;
        uint32_t present_queue_family_index = UINT32_MAX;

        for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
            queuePresentSupport[i] = physicalDevice.getSurfaceSupportKHR(i, mSurface, mVkDispatcher);

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
        mVkLib = LoadLibrary("vulkan-1.dll");
        if(!mVkLib) {
            throw std::runtime_error("Failed to open Vulkan.dll");
        }
        mVkDispatcher.init(reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(mVkLib, "vkGetInstanceProcAddr")));

        uint32_t vkVersion;
        vk::enumerateInstanceVersion(&vkVersion, mVkDispatcher);
        std::cout << "Vulkan version " << VK_VERSION_MAJOR(vkVersion) << "." << VK_VERSION_MINOR(vkVersion) << "." << VK_VERSION_PATCH(vkVersion) << std::endl;

        vk::ApplicationInfo applicationInfo;
        applicationInfo.pApplicationName = "Vulkan sample: Window";
        applicationInfo.pEngineName = "Vulkan";
        applicationInfo.apiVersion = vkVersion;
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
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
        };
        std::cout << "Check extensions...";
        CheckExtensions(extensions, mVkDispatcher);
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
        CheckLayers(layers, mVkDispatcher);
        instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        instanceCreateInfo.ppEnabledLayerNames = &layers[0];
#endif
        mVulkan = MakeHolder(vk::createInstance(instanceCreateInfo, nullptr, mVkDispatcher), [this](vk::Instance& i){ i.destroy(nullptr, mVkDispatcher); });
        if (!mVulkan) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }
        std::cout << "OK" << std::endl;

        mVkDispatcher.init(*mVulkan);

        std::cout << "Find Vulkan physical device...";
        std::vector<vk::PhysicalDevice> devices = mVulkan->enumeratePhysicalDevices(mVkDispatcher);
        if (devices.empty()) {
            throw std::runtime_error("Physical device was not found");
        }
        mPhysicalDevice = devices.front();
        for (const auto& pd: devices) {
            if (pd.getProperties(mVkDispatcher).deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                mPhysicalDevice = pd;
            }
        }
        std::cout << "OK" << std::endl;
        std::cout << "Using device: " << mPhysicalDevice.getProperties(mVkDispatcher).deviceName << std::endl;

        /*
        * Create surface for the created window
        * Requires VK_KHR_SURFACE_EXTENSION_NAME and VK_KHR_WIN32_SURFACE_EXTENSION_NAME
        */

        mSurface = MakeHolder(mVulkan->createWin32SurfaceKHR(vk::Win32SurfaceCreateInfoKHR(vk::Win32SurfaceCreateFlagsKHR(), window.Instance, window.Handle), nullptr, mVkDispatcher),
            [this](vk::SurfaceKHR & surface) { mVulkan->destroySurfaceKHR(surface, nullptr, mVkDispatcher); });

        /*
        * Choose a queue with supports creating swapchain
        */
        auto queueProperties = mPhysicalDevice.getQueueFamilyProperties(mVkDispatcher);
        CheckPhysicalDeviceProperties(mPhysicalDevice, mQueueFamilyGraphics, mQueueFamilyPresent);
        if (mQueueFamilyGraphics >= queueProperties.size()) {
            throw std::runtime_error("Device doesn't support rendering to VkSurface");
        }

        std::cout << "Check device extensions...";
        std::vector<const char*> deviceExtensions = { 
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
            VK_NV_RAY_TRACING_EXTENSION_NAME
        };
        CheckDeviceExtensions(mPhysicalDevice, deviceExtensions, mVkDispatcher);
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
        //features.setGeometryShader(VK_TRUE);
        features.setFillModeNonSolid(VK_TRUE);
        features.setSamplerAnisotropy(VK_TRUE);

        vk::DeviceCreateInfo deviceCreateInfo;
        deviceCreateInfo.setEnabledExtensionCount(static_cast<uint32_t>(deviceExtensions.size()));
        deviceCreateInfo.setPpEnabledExtensionNames(&deviceExtensions[0]);
        deviceCreateInfo.setQueueCreateInfoCount(1);
        deviceCreateInfo.setPQueueCreateInfos(&queueCreateInfo);
        deviceCreateInfo.setPEnabledFeatures(&features);

        mDevice = MakeHolder(mPhysicalDevice.createDevice(deviceCreateInfo, nullptr, mVkDispatcher), [this](vk::Device& d){ d.destroy(nullptr, mVkDispatcher); });
        mDevice->waitIdle();
        std::cout << "OK" << std::endl;

        mVkDispatcher.init(*mDevice);

        vk::PhysicalDeviceRayTracingPropertiesNV rayTacingProps{};
        vk::PhysicalDeviceProperties2 props2{};
        props2.pNext = &rayTacingProps;
        mPhysicalDevice.getProperties2(&props2, mVkDispatcher);

        std::cout << "RayTraicing maxRecursionDepth = " << rayTacingProps.maxRecursionDepth << std::endl;
        std::cout << "RayTraicing shaderGroupHandleSize = " << rayTacingProps.shaderGroupHandleSize << std::endl;
        std::cout << "RayTraicing maxShaderGroupStride = " << rayTacingProps.maxShaderGroupStride << std::endl;
        std::cout << "RayTraicing shaderGroupBaseAlignment = " << rayTacingProps.shaderGroupBaseAlignment << std::endl;
        std::cout << "RayTraicing maxGeometryCount = " << rayTacingProps.maxGeometryCount << std::endl;
        std::cout << "RayTraicing maxInstanceCount = " << rayTacingProps.maxInstanceCount << std::endl;
        std::cout << "RayTraicing maxTriangleCount = " << rayTacingProps.maxTriangleCount << std::endl;
        std::cout << "RayTraicing maxDescriptorSetAccelerationStructures = " << rayTacingProps.maxDescriptorSetAccelerationStructures << std::endl;

        /*
        * Retrieve a command queue
        */
        mGraphicsQueue = mDevice->getQueue(static_cast<uint32_t>(mQueueFamilyGraphics), 0, mVkDispatcher);
        mPresentQueue  = mDevice->getQueue(static_cast<uint32_t>(mQueueFamilyPresent), 0, mVkDispatcher);

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

        auto surfaceCapabilities = mPhysicalDevice.getSurfaceCapabilitiesKHR(mSurface, mVkDispatcher);
        if (surfaceCapabilities.maxImageCount < 1) {
            throw std::runtime_error("Invalid capabilities");
        }
        const uint32_t imagesCount = std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount);
        vk::Extent2D imageSize = surfaceCapabilities.currentExtent; // Use current surface size

        auto supportedFormats = mPhysicalDevice.getSurfaceFormatsKHR(mSurface, mVkDispatcher);
        if (supportedFormats.empty()) {
            throw std::runtime_error("Failed to get supported surface formats");
        }
        const auto format = std::make_pair(vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear);
        if (!CheckFormat(supportedFormats, format)) {
            throw std::runtime_error("Format BGRA_Unorm/SrgbNonlinear is not supported");
        }

        auto presentModes = mPhysicalDevice.getSurfacePresentModesKHR(mSurface, mVkDispatcher);
        if (presentModes.empty()) {
            throw std::runtime_error("Failed to get supported surface present modes");
        }

        std::cout << "Create Swapchain...";
        vk::SwapchainCreateInfoKHR swapchainInfo;
        swapchainInfo.surface = mSurface;
        swapchainInfo.imageExtent = imageSize;
        swapchainInfo.imageFormat = format.first;
        swapchainInfo.imageColorSpace = format.second;
        swapchainInfo.minImageCount = imagesCount;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = vk::ImageUsageFlags(vk::ImageUsageFlagBits::eStorage);
        swapchainInfo.presentMode = vk::PresentModeKHR::eMailbox;
        swapchainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchainInfo.preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        swapchainInfo.clipped = true;
        mSwapChain = MakeHolder(mDevice->createSwapchainKHR(swapchainInfo, nullptr, mVkDispatcher), [this](vk::SwapchainKHR & swapchain) { mDevice->destroySwapchainKHR(swapchain, nullptr, mVkDispatcher); });
        std::cout << "OK" << std::endl;

        auto swapchainImages = mDevice->getSwapchainImagesKHR(mSwapChain, mVkDispatcher);

        mRenderingResources.resize(swapchainImages.size());
        mFramebufferExtents = imageSize;

        std::cout << "Create mesh and acceleration structure...";

        mDefaultOrientation.FromAngleAxis(Ogre::Radian(-1.0f), Ogre::Vector3::UNIT_Y);
        mRotationX = Ogre::Quaternion::IDENTITY;
        mRotationY = Ogre::Quaternion::IDENTITY;
        MakePerspectiveProjectionMatrix(mProjectionMatrix, static_cast<float>(width) / height, 45.0f, 0.01f, 1000.0f);


        std::array<GeometryBuffer, 1> geometries = {};
        geometries[0].position = Ogre::Vector4::ZERO;
        geometries[0].scale = 0.75f;

        std::array<Ogre::AxisAlignedBox, 1> aabbs = {};
        aabbs[0].setExtents(geometries[0].position.xyz() + Ogre::Vector3(-1.25f, -1.25f, -1.25f), geometries[0].position.xyz() + Ogre::Vector3(1.25f, 1.25f, 1.25f));
        aabbs[0].scale(geometries[0].scale.xyz());

        const uint32_t vertexBufferSize = static_cast<uint32_t>(geometries.size() * sizeof(GeometryBuffer));
        const uint32_t aabbBufferSize   = aabbs.size() * sizeof(Ogre::AxisAlignedBox);

        {
            vk::BufferCreateInfo bufferInfo;
            bufferInfo.setSize(vertexBufferSize);
            bufferInfo.setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
            bufferInfo.setSharingMode(vk::SharingMode::eExclusive);
            mVertexBuffer = MakeHolder(mDevice->createBuffer(bufferInfo, nullptr, mVkDispatcher), [this](vk::Buffer & buffer) { mDevice->destroyBuffer(buffer, nullptr, mVkDispatcher); });
        }

        {
            vk::BufferCreateInfo bufferInfo;
            bufferInfo.setSize(aabbBufferSize);
            bufferInfo.setUsage(vk::BufferUsageFlagBits::eRayTracingNV);
            bufferInfo.setSharingMode(vk::SharingMode::eExclusive);
            mAabbBuffer = MakeHolder(mDevice->createBuffer(bufferInfo, nullptr, mVkDispatcher), [this](vk::Buffer & buffer) { mDevice->destroyBuffer(buffer, nullptr, mVkDispatcher); });
        }

        // Upload vertex data
        {
            vk::MemoryRequirements vertexMemoryRequirements = mDevice->getBufferMemoryRequirements(mVertexBuffer, mVkDispatcher);

            vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties(mVkDispatcher);
            for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                if ((vertexMemoryRequirements.memoryTypeBits & (1 << i)) &&
                    (memroProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {

                    vk::MemoryAllocateInfo allocateInfo;
                    allocateInfo.setAllocationSize(vertexMemoryRequirements.size);
                    allocateInfo.setMemoryTypeIndex(i);
                    mVertexMemory = MakeHolder(mDevice->allocateMemory(allocateInfo, nullptr, mVkDispatcher), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory, nullptr, mVkDispatcher); });
                    break;
                }
            }
            if (!mVertexMemory) {
                throw std::runtime_error("Failed to allocate memory for vertex buffer");
            }
            mDevice->bindBufferMemory(mVertexBuffer, mVertexMemory, 0, mVkDispatcher);

            void* devicePtr = mDevice->mapMemory(mVertexMemory, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, mVkDispatcher);
            if (devicePtr == nullptr) {
                throw std::runtime_error("Failed to map memory for vertex buffer");
            }
            std::memcpy(devicePtr, geometries.data(), vertexBufferSize);

            vk::MappedMemoryRange mappedRange;
            mappedRange.setMemory(mVertexMemory);
            mappedRange.setOffset(0);
            mappedRange.setSize(VK_WHOLE_SIZE);
            mDevice->flushMappedMemoryRanges(mappedRange, mVkDispatcher);

            mDevice->unmapMemory(mVertexMemory, mVkDispatcher);
        }

        // Upload AABB data
        {
            vk::MemoryRequirements vertexMemoryRequirements = mDevice->getBufferMemoryRequirements(mAabbBuffer, mVkDispatcher);

            vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties(mVkDispatcher);
            for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                if ((vertexMemoryRequirements.memoryTypeBits & (1 << i)) &&
                    (memroProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {

                    vk::MemoryAllocateInfo allocateInfo;
                    allocateInfo.setAllocationSize(vertexMemoryRequirements.size);
                    allocateInfo.setMemoryTypeIndex(i);
                    mAabbMemory = MakeHolder(mDevice->allocateMemory(allocateInfo, nullptr, mVkDispatcher), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory, nullptr, mVkDispatcher); });
                    break;
                }
            }
            if (!mAabbMemory) {
                throw std::runtime_error("Failed to allocate memory for AABB buffer");
            }
            mDevice->bindBufferMemory(mAabbBuffer, mAabbMemory, 0, mVkDispatcher);

            void* devicePtr = mDevice->mapMemory(mAabbMemory, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, mVkDispatcher);
            if (devicePtr == nullptr) {
                throw std::runtime_error("Failed to map memory for vertex buffer");
            }
            std::memcpy(devicePtr, aabbs.data(), aabbBufferSize);

            vk::MappedMemoryRange mappedRange;
            mappedRange.setMemory(mAabbMemory);
            mappedRange.setOffset(0);
            mappedRange.setSize(VK_WHOLE_SIZE);
            mDevice->flushMappedMemoryRanges(mappedRange, mVkDispatcher);

            mDevice->unmapMemory(mAabbMemory, mVkDispatcher);
        }

        // Bottom-level AS

        vk::GeometryAABBNV meshAabs{};
        meshAabs.setNumAABBs(static_cast<uint32_t>(aabbs.size()));
        meshAabs.setAabbData(mAabbBuffer);
        meshAabs.setOffset(0);
        meshAabs.setStride(sizeof(Ogre::AxisAlignedBox));

        std::array<vk::GeometryNV, 1> meshGeometry = {};
        meshGeometry[0].geometry.setAabbs(meshAabs);
        meshGeometry[0].setGeometryType(vk::GeometryTypeNV::eAabbs);
        meshGeometry[0].setFlags(vk::GeometryFlagBitsNV::eOpaque);

        vk::AccelerationStructureInfoNV accelerationStructureInfoBottom{};
        accelerationStructureInfoBottom.setType(vk::AccelerationStructureTypeNV::eBottomLevel);
        accelerationStructureInfoBottom.setInstanceCount(0);
        accelerationStructureInfoBottom.setGeometryCount(static_cast<uint32_t>(meshGeometry.size()));
        accelerationStructureInfoBottom.setPGeometries(meshGeometry.data());
        accelerationStructureInfoBottom.setFlags(vk::BuildAccelerationStructureFlagBitsNV::ePreferFastTrace);

        vk::AccelerationStructureCreateInfoNV accelerationStructureCreateInfoBottom{};
        accelerationStructureCreateInfoBottom.setInfo(accelerationStructureInfoBottom);




        // Top-level AS

        const float transform[12] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
        };

        std::vector<VkGeometryInstance> instances(1);
        std::memcpy(instances[0].transform, transform, sizeof(transform));
        instances[0].instanceId = 0;
        instances[0].mask = 0xff;
        instances[0].instanceOffset = 0;
        instances[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;

        vk::AccelerationStructureInfoNV accelerationStructureInfoTop{};
        accelerationStructureInfoTop.setType(vk::AccelerationStructureTypeNV::eTopLevel);
        accelerationStructureInfoTop.setInstanceCount(static_cast<uint32_t>(instances.size()));


        // Build

        BuildAccelerationStructures(accelerationStructureInfoTop, accelerationStructureInfoBottom, instances);

        std::cout << "OK" << std::endl;


        // Compile shaders

        std::cout << "Compile shaders...";

        {
            auto code = GetBinaryShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/18.rgen");
            if (code.empty()) {
                throw std::runtime_error("LoadShader: Failed to read shader file!");
            }
            vk::ShaderModuleCreateInfo shaderInfo;
            shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
            shaderInfo.setCodeSize(code.size());

            mRayGenShader = MakeHolder(mDevice->createShaderModule(shaderInfo, nullptr, mVkDispatcher), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader, nullptr, mVkDispatcher); });
        }
        {
            auto code = GetBinaryShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/18.rint");
            if (code.empty()) {
                throw std::runtime_error("LoadShader: Failed to read shader file!");
            }
            vk::ShaderModuleCreateInfo shaderInfo;
            shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
            shaderInfo.setCodeSize(code.size());

            mRayIntersectShader = MakeHolder(mDevice->createShaderModule(shaderInfo, nullptr, mVkDispatcher), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader, nullptr, mVkDispatcher); });
        }
        {
            auto code = GetBinaryShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/18.rmiss");
            if (code.empty()) {
                throw std::runtime_error("LoadShader: Failed to read shader file!");
            }
            vk::ShaderModuleCreateInfo shaderInfo;
            shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
            shaderInfo.setCodeSize(code.size());

            mRayMissShader = MakeHolder(mDevice->createShaderModule(shaderInfo, nullptr, mVkDispatcher), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader, nullptr, mVkDispatcher); });
        }
        {
            auto code = GetBinaryShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/18.rchit");
            if (code.empty()) {
                throw std::runtime_error("LoadShader: Failed to read shader file!");
            }
            vk::ShaderModuleCreateInfo shaderInfo;
            shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
            shaderInfo.setCodeSize(code.size());

            mRayCloseHitShader = MakeHolder(mDevice->createShaderModule(shaderInfo, nullptr, mVkDispatcher), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader, nullptr, mVkDispatcher); });
        }
        {
            auto code = GetBinaryShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/18.shdw.rmiss");
            if (code.empty()) {
                throw std::runtime_error("LoadShader: Failed to read shader file!");
            }
            vk::ShaderModuleCreateInfo shaderInfo;
            shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
            shaderInfo.setCodeSize(code.size());

            mRayShadowMissShader = MakeHolder(mDevice->createShaderModule(shaderInfo, nullptr, mVkDispatcher), [this](vk::ShaderModule & shader) { mDevice->destroyShaderModule(shader, nullptr, mVkDispatcher); });
        }

        std::cout << "OK" << std::endl;


        // Descriptros layout

        std::cout << "Create pipeline...";

        std::array<vk::DescriptorSetLayoutBinding, 4> descriptosBindings;

        // AS
        descriptosBindings[0].setDescriptorType(vk::DescriptorType::eAccelerationStructureNV);
        descriptosBindings[0].setBinding(0);
        descriptosBindings[0].setDescriptorCount(1);
        descriptosBindings[0].setStageFlags(vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV);

        // Framebuffer
        descriptosBindings[1].setDescriptorType(vk::DescriptorType::eStorageImage);
        descriptosBindings[1].setBinding(1);
        descriptosBindings[1].setDescriptorCount(1);
        descriptosBindings[1].setStageFlags(vk::ShaderStageFlagBits::eRaygenNV);

        // Vertex buffer
        descriptosBindings[2].setDescriptorType(vk::DescriptorType::eStorageBuffer);
        descriptosBindings[2].setBinding(2);
        descriptosBindings[2].setDescriptorCount(1);
        descriptosBindings[2].setStageFlags(vk::ShaderStageFlagBits::eIntersectionNV | vk::ShaderStageFlagBits::eClosestHitNV);

        // Camera buffer
        descriptosBindings[3].setDescriptorType(vk::DescriptorType::eUniformBuffer);
        descriptosBindings[3].setBinding(4);
        descriptosBindings[3].setDescriptorCount(1);
        descriptosBindings[3].setStageFlags(vk::ShaderStageFlagBits::eRaygenNV | vk::ShaderStageFlagBits::eClosestHitNV);

        vk::DescriptorSetLayoutCreateInfo descriptosLayoutInfo{};
        descriptosLayoutInfo.setBindingCount(static_cast<uint32_t>(descriptosBindings.size()));
        descriptosLayoutInfo.setPBindings(descriptosBindings.data());

        mDescriptorsSetLayout = MakeHolder(mDevice->createDescriptorSetLayout(descriptosLayoutInfo, nullptr, mVkDispatcher), [this](vk::DescriptorSetLayout& l) { mDevice->destroyDescriptorSetLayout(l, nullptr, mVkDispatcher); });
        if (!mDescriptorsSetLayout) {
            throw std::runtime_error("Failed to create a descriptors set layout.");
        }

        std::array<vk::DescriptorPoolSize, 4> descriptorPoolSizes = {};

        descriptorPoolSizes[0].setType(vk::DescriptorType::eAccelerationStructureNV);
        descriptorPoolSizes[0].setDescriptorCount(static_cast<uint32_t>(mRenderingResources.size()));

        descriptorPoolSizes[1].setType(vk::DescriptorType::eStorageImage);
        descriptorPoolSizes[1].setDescriptorCount(static_cast<uint32_t>(mRenderingResources.size()));

        descriptorPoolSizes[2].setType(vk::DescriptorType::eStorageBuffer);
        descriptorPoolSizes[2].setDescriptorCount(static_cast<uint32_t>(mRenderingResources.size()));

        descriptorPoolSizes[3].setType(vk::DescriptorType::eUniformBuffer);
        descriptorPoolSizes[3].setDescriptorCount(static_cast<uint32_t>(mRenderingResources.size()));

        vk::DescriptorPoolCreateInfo descriptorPoolInfo{};
        descriptorPoolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
        descriptorPoolInfo.setMaxSets(static_cast<uint32_t>(mRenderingResources.size()));
        descriptorPoolInfo.setPoolSizeCount(static_cast<uint32_t>(descriptorPoolSizes.size()));
        descriptorPoolInfo.setPPoolSizes(descriptorPoolSizes.data());

        mDescriptorsPool = MakeHolder(mDevice->createDescriptorPool(descriptorPoolInfo, nullptr, mVkDispatcher), [this](vk::DescriptorPool& p){ mDevice->destroyDescriptorPool(p, nullptr, mVkDispatcher); });
        if (!mDescriptorsPool) {
            throw std::runtime_error("Failed to create a descriptors pool.");
        }

        // Pipeline

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setSetLayoutCount(1);
        pipelineLayoutInfo.setPSetLayouts(mDescriptorsSetLayout.get());

        mPipelineLayout = MakeHolder(mDevice->createPipelineLayout(pipelineLayoutInfo, nullptr, mVkDispatcher), [this](vk::PipelineLayout& l) { mDevice->destroyPipelineLayout(l, nullptr, mVkDispatcher); });
        if (!mPipelineLayout ) {
            throw std::runtime_error("Failed to create a pipeline layout.");
        }

        std::array<vk::PipelineShaderStageCreateInfo, 5> shaderStages = {};
        shaderStages[0].setStage(vk::ShaderStageFlagBits::eRaygenNV);
        shaderStages[0].setModule(mRayGenShader);
        shaderStages[0].setPName("main");

        shaderStages[1].setStage(vk::ShaderStageFlagBits::eIntersectionNV);
        shaderStages[1].setModule(mRayIntersectShader);
        shaderStages[1].setPName("main");

        shaderStages[2].setStage(vk::ShaderStageFlagBits::eClosestHitNV);
        shaderStages[2].setModule(mRayCloseHitShader);
        shaderStages[2].setPName("main");

        shaderStages[3].setStage(vk::ShaderStageFlagBits::eMissNV);
        shaderStages[3].setModule(mRayMissShader);
        shaderStages[3].setPName("main");

        shaderStages[4].setStage(vk::ShaderStageFlagBits::eMissNV);
        shaderStages[4].setModule(mRayShadowMissShader);
        shaderStages[4].setPName("main");

        std::array<vk::RayTracingShaderGroupCreateInfoNV, 4> shaderGroups = {};
        // Gen
        shaderGroups[0].setType(vk::RayTracingShaderGroupTypeNV::eGeneral);
        shaderGroups[0].setGeneralShader(0);
        shaderGroups[0].setIntersectionShader(VK_SHADER_UNUSED_NV);
        shaderGroups[0].setAnyHitShader(VK_SHADER_UNUSED_NV);
        shaderGroups[0].setClosestHitShader(VK_SHADER_UNUSED_NV);

        // Hit
        shaderGroups[1].setType(vk::RayTracingShaderGroupTypeNV::eProceduralHitGroup);
        shaderGroups[1].setGeneralShader(VK_SHADER_UNUSED_NV);
        shaderGroups[1].setIntersectionShader(1);
        shaderGroups[1].setAnyHitShader(VK_SHADER_UNUSED_NV);
        shaderGroups[1].setClosestHitShader(2);

        // Miss
        shaderGroups[2].setType(vk::RayTracingShaderGroupTypeNV::eGeneral);
        shaderGroups[2].setGeneralShader(3);
        shaderGroups[2].setIntersectionShader(VK_SHADER_UNUSED_NV);
        shaderGroups[2].setAnyHitShader(VK_SHADER_UNUSED_NV);
        shaderGroups[2].setClosestHitShader(VK_SHADER_UNUSED_NV);

        // Miss shadow ray
        shaderGroups[3].setType(vk::RayTracingShaderGroupTypeNV::eGeneral);
        shaderGroups[3].setGeneralShader(4);
        shaderGroups[3].setIntersectionShader(VK_SHADER_UNUSED_NV);
        shaderGroups[3].setAnyHitShader(VK_SHADER_UNUSED_NV);
        shaderGroups[3].setClosestHitShader(VK_SHADER_UNUSED_NV);

        vk::RayTracingPipelineCreateInfoNV pipelineInfo{};
        pipelineInfo.setLayout(mPipelineLayout);
        pipelineInfo.setStageCount(static_cast<uint32_t>(shaderStages.size()));
        pipelineInfo.setPStages(shaderStages.data());
        pipelineInfo.setGroupCount(static_cast<uint32_t>(shaderGroups.size()));
        pipelineInfo.setPGroups(shaderGroups.data());
        pipelineInfo.setMaxRecursionDepth(2);

        mPipeline = MakeHolder(mDevice->createRayTracingPipelineNV(nullptr, pipelineInfo, nullptr, mVkDispatcher), [this](vk::Pipeline& p) { mDevice->destroyPipeline(p, nullptr, mVkDispatcher); });
        if (!mPipeline) {
            throw std::runtime_error("Failed to create a RT pipeline.");
        }

        std::cout << "OK" << std::endl;

        // Shader binding table

        std::cout << "Create shader binding table...";

        {
            vk::BufferCreateInfo sbtBufferInfo{};
            sbtBufferInfo.setSize(shaderGroups.size() * rayTacingProps.shaderGroupHandleSize);
            sbtBufferInfo.setUsage(vk::BufferUsageFlagBits::eRayTracingNV);
            sbtBufferInfo.setSharingMode(vk::SharingMode::eExclusive);
            sbtBufferInfo.setQueueFamilyIndexCount(1);
            sbtBufferInfo.setPQueueFamilyIndices(&mQueueFamilyGraphics);

            mShaderBindingTable = MakeHolder(mDevice->createBuffer(sbtBufferInfo, nullptr, mVkDispatcher), [this](vk::Buffer& b) { mDevice->destroyBuffer(b, nullptr, mVkDispatcher); });
            if (!mShaderBindingTable) {
                throw std::runtime_error("Failed to create an SBT buffer.");
            }

            vk::MemoryRequirements memoryRequirements = mDevice->getBufferMemoryRequirements(mShaderBindingTable, mVkDispatcher);

            vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties(mVkDispatcher);
            for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                if ((memoryRequirements.memoryTypeBits & (1 << i)) &&
                    (memroProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {

                    vk::MemoryAllocateInfo allocateInfo;
                    allocateInfo.setAllocationSize(memoryRequirements.size);
                    allocateInfo.setMemoryTypeIndex(i);
                    mShaderBindingTableMemory = MakeHolder(mDevice->allocateMemory(allocateInfo, nullptr, mVkDispatcher), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory, nullptr, mVkDispatcher); });
                    break;
                }
            }
            if (!mShaderBindingTableMemory) {
                throw std::runtime_error("Failed to allocate memory for the SBT buffer");
            }

            mDevice->bindBufferMemory(mShaderBindingTable, mShaderBindingTableMemory, 0, mVkDispatcher);

            void* sbtDataPtr = mDevice->mapMemory(mShaderBindingTableMemory, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, mVkDispatcher);
            if (!sbtDataPtr) {
                throw std::runtime_error("Failed to map the SBT memory");
            }

            auto vkError = mDevice->getRayTracingShaderGroupHandlesNV(mPipeline, 0, static_cast<uint32_t>(shaderGroups.size()), sbtBufferInfo.size, sbtDataPtr, mVkDispatcher);
            if (vkError != vk::Result::eSuccess) {
                throw std::runtime_error("Failed to query SBT data: " + to_string(vkError));
            }

            vk::MappedMemoryRange flushRange{};
            flushRange.setMemory(mShaderBindingTableMemory);
            flushRange.setOffset(0);
            flushRange.setSize(VK_WHOLE_SIZE);

            mDevice->flushMappedMemoryRanges(1, &flushRange, mVkDispatcher);

            mDevice->unmapMemory(mShaderBindingTableMemory, mVkDispatcher);
        }

        std::cout << "OK" << std::endl;

        // Rendering resources

        std::cout << "Create command buffers...";

        vk::CommandPoolCreateInfo commandsPoolInfo{};
        commandsPoolInfo.setQueueFamilyIndex(mQueueFamilyGraphics);
        commandsPoolInfo.setFlags(vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
        mCommandPool = MakeHolder(mDevice->createCommandPool(commandsPoolInfo, nullptr, mVkDispatcher), [this](vk::CommandPool& p) { mDevice->destroyCommandPool(p, nullptr, mVkDispatcher); });
        if (!mCommandPool) {
            throw std::runtime_error("Failed to create command buffers pool");
        }

        for (size_t i = 0; i < mRenderingResources.size(); ++i) {

            mRenderingResources[i].imageHandle = swapchainImages[i];

            vk::ImageViewCreateInfo imageViewInfo;
            imageViewInfo.setImage(swapchainImages[i]);
            imageViewInfo.setViewType(vk::ImageViewType::e2D);
            imageViewInfo.setFormat(format.first);
            imageViewInfo.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity));
            imageViewInfo.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

            mRenderingResources[i].imageView = MakeHolder(mDevice->createImageView(imageViewInfo, nullptr, mVkDispatcher), [this](vk::ImageView & view) { mDevice->destroyImageView(view, nullptr, mVkDispatcher); });
            if (!mRenderingResources[i].imageView) {
                throw std::runtime_error("Failed to create an image view.");
            }

            {
                vk::BufferCreateInfo cameraBufferInfo{};
                cameraBufferInfo.setSize(sizeof(CameraProperties));
                cameraBufferInfo.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
                cameraBufferInfo.setSharingMode(vk::SharingMode::eExclusive);
                cameraBufferInfo.setQueueFamilyIndexCount(1);
                cameraBufferInfo.setPQueueFamilyIndices(&mQueueFamilyGraphics);

                mRenderingResources[i].cameraPropsBuffer = MakeHolder(mDevice->createBuffer(cameraBufferInfo, nullptr, mVkDispatcher), [this](vk::Buffer& b) { mDevice->destroyBuffer(b, nullptr, mVkDispatcher); });

                vk::MemoryRequirements memoryRequirements = mDevice->getBufferMemoryRequirements(mRenderingResources[i].cameraPropsBuffer, mVkDispatcher);

                vk::PhysicalDeviceMemoryProperties memroProperties = mPhysicalDevice.getMemoryProperties(mVkDispatcher);
                for (uint32_t j = 0; j < memroProperties.memoryTypeCount; ++j) {
                    if ((memoryRequirements.memoryTypeBits & (1 << j)) &&
                        (memroProperties.memoryTypes[j].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {

                        vk::MemoryAllocateInfo allocateInfo;
                        allocateInfo.setAllocationSize(memoryRequirements.size);
                        allocateInfo.setMemoryTypeIndex(j);
                        mRenderingResources[i].cameraPropsBufferMemory = MakeHolder(mDevice->allocateMemory(allocateInfo, nullptr, mVkDispatcher), [this](vk::DeviceMemory & memory) { mDevice->freeMemory(memory, nullptr, mVkDispatcher); });
                        break;
                    }
                }
                if (!mRenderingResources[i].cameraPropsBufferMemory) {
                    throw std::runtime_error("Failed to allocate memory for index buffer");
                }

                mDevice->bindBufferMemory(mRenderingResources[i].cameraPropsBuffer, mRenderingResources[i].cameraPropsBufferMemory, 0, mVkDispatcher);

            }

            vk::DescriptorSetAllocateInfo descriptorsInfo{};
            descriptorsInfo.setDescriptorPool(mDescriptorsPool);
            descriptorsInfo.setDescriptorSetCount(1);
            descriptorsInfo.setPSetLayouts(mDescriptorsSetLayout.get());

            mRenderingResources[i].descriptorSet = MakeHolder<vk::DescriptorSet>(nullptr, [this](vk::DescriptorSet& s) {  mDevice->freeDescriptorSets(mDescriptorsPool, 1, &s, mVkDispatcher); });

            auto vkError = mDevice->allocateDescriptorSets(&descriptorsInfo, mRenderingResources[i].descriptorSet.get(), mVkDispatcher);
            if (vkError != vk::Result::eSuccess) {
                throw std::runtime_error("Failed to allocate a descriptors set.");
            }

            std::array<vk::WriteDescriptorSet, 4> descriptorWrites = {};

            vk::WriteDescriptorSetAccelerationStructureNV asInfo{};
            asInfo.setAccelerationStructureCount(1);
            asInfo.setPAccelerationStructures(mAccelerationStructureTop.get());

            descriptorWrites[0].setDescriptorCount(1);
            descriptorWrites[0].setDescriptorType(vk::DescriptorType::eAccelerationStructureNV);
            descriptorWrites[0].setDstSet(mRenderingResources[i].descriptorSet);
            descriptorWrites[0].setDstBinding(0);
            descriptorWrites[0].setPNext(&asInfo);

            vk::DescriptorImageInfo imageInfo{};
            imageInfo.setImageLayout(vk::ImageLayout::eGeneral);
            imageInfo.setImageView(mRenderingResources[i].imageView);

            descriptorWrites[1].setDescriptorCount(1);
            descriptorWrites[1].setDescriptorType(vk::DescriptorType::eStorageImage);
            descriptorWrites[1].setDstSet(mRenderingResources[i].descriptorSet);
            descriptorWrites[1].setDstBinding(1);
            descriptorWrites[1].setPImageInfo(&imageInfo);

            vk::DescriptorBufferInfo vertexBufferInfo{};
            vertexBufferInfo.setBuffer(mVertexBuffer);
            vertexBufferInfo.setOffset(0);
            vertexBufferInfo.setRange(VK_WHOLE_SIZE);

            descriptorWrites[2].setDescriptorCount(1);
            descriptorWrites[2].setDescriptorType(vk::DescriptorType::eStorageBuffer);
            descriptorWrites[2].setDstSet(mRenderingResources[i].descriptorSet);
            descriptorWrites[2].setDstBinding(2);
            descriptorWrites[2].setPBufferInfo(&vertexBufferInfo);

            vk::DescriptorBufferInfo cameraBufferInfo{};
            cameraBufferInfo.setBuffer(mRenderingResources[i].cameraPropsBuffer);
            cameraBufferInfo.setOffset(0);
            cameraBufferInfo.setRange(VK_WHOLE_SIZE);

            descriptorWrites[3].setDescriptorCount(1);
            descriptorWrites[3].setDescriptorType(vk::DescriptorType::eUniformBuffer);
            descriptorWrites[3].setDstSet(mRenderingResources[i].descriptorSet);
            descriptorWrites[3].setDstBinding(4);
            descriptorWrites[3].setPBufferInfo(&cameraBufferInfo);

            mDevice->updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr, mVkDispatcher);

            // Init
            {
                mRenderingResources[i].initCommand = MakeHolder<vk::CommandBuffer>(nullptr, [this](vk::CommandBuffer& b) {  mDevice->freeCommandBuffers(mCommandPool, 1, &b, mVkDispatcher); });

                vk::CommandBufferAllocateInfo commandAllocInfo{};
                commandAllocInfo.setCommandBufferCount(1);
                commandAllocInfo.setCommandPool(mCommandPool);
                commandAllocInfo.setLevel(vk::CommandBufferLevel::ePrimary);

                vkError = mDevice->allocateCommandBuffers(&commandAllocInfo, mRenderingResources[i].initCommand.get(), mVkDispatcher);
                if (vkError != vk::Result::eSuccess) {
                    throw std::runtime_error("Failed to allocate the init command buffer.");
                }

                auto& command = mRenderingResources[i].initCommand;

                vk::CommandBufferBeginInfo beginInfo{};
                beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
                command->begin(beginInfo, mVkDispatcher);

                vk::ImageSubresourceRange fullImage{};
                fullImage.setAspectMask(vk::ImageAspectFlagBits::eColor);
                fullImage.setBaseArrayLayer(0);
                fullImage.setBaseMipLevel(0);
                fullImage.setLayerCount(1);
                fullImage.setLevelCount(1);

                vk::ImageMemoryBarrier imageBarrier{};
                imageBarrier.setImage(mRenderingResources[i].imageHandle);
                imageBarrier.setSrcAccessMask(vk::AccessFlagBits::eHostWrite);
                imageBarrier.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead);
                imageBarrier.setOldLayout(vk::ImageLayout::eUndefined);
                imageBarrier.setNewLayout(vk::ImageLayout::ePresentSrcKHR);
                imageBarrier.setSrcQueueFamilyIndex(mQueueFamilyPresent);
                imageBarrier.setDstQueueFamilyIndex(mQueueFamilyPresent);
                imageBarrier.setSubresourceRange(fullImage);

                command->pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eAllCommands, vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1, &imageBarrier, mVkDispatcher);

                command->end(mVkDispatcher);
            }

            // Draw
            {
                mRenderingResources[i].drawCommand = MakeHolder<vk::CommandBuffer>(nullptr, [this](vk::CommandBuffer& b) {  mDevice->freeCommandBuffers(mCommandPool, 1, &b, mVkDispatcher); });

                vk::CommandBufferAllocateInfo commandAllocInfo{};
                commandAllocInfo.setCommandBufferCount(1);
                commandAllocInfo.setCommandPool(mCommandPool);
                commandAllocInfo.setLevel(vk::CommandBufferLevel::ePrimary);

                vkError = mDevice->allocateCommandBuffers(&commandAllocInfo, mRenderingResources[i].drawCommand.get(), mVkDispatcher);
                if (vkError != vk::Result::eSuccess) {
                    throw std::runtime_error("Failed to allocate the draw command buffer.");
                }

                auto& command = mRenderingResources[i].drawCommand;

                vk::CommandBufferBeginInfo beginInfo{};
                beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eSimultaneousUse);
                command->begin(beginInfo, mVkDispatcher);

                vk::ImageSubresourceRange fullImage{};
                fullImage.setAspectMask(vk::ImageAspectFlagBits::eColor);
                fullImage.setBaseArrayLayer(0);
                fullImage.setBaseMipLevel(0);
                fullImage.setLayerCount(1);
                fullImage.setLevelCount(1);

                {
                    vk::ImageMemoryBarrier imageBarrier{};
                    imageBarrier.setImage(mRenderingResources[i].imageHandle);
                    imageBarrier.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentRead);
                    imageBarrier.setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
                    imageBarrier.setOldLayout(vk::ImageLayout::ePresentSrcKHR);
                    imageBarrier.setNewLayout(vk::ImageLayout::eGeneral);
                    imageBarrier.setSrcQueueFamilyIndex(mQueueFamilyPresent);
                    imageBarrier.setDstQueueFamilyIndex(mQueueFamilyGraphics);
                    imageBarrier.setSubresourceRange(fullImage);

                    command->pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1, &imageBarrier, mVkDispatcher);
                }

                command->bindPipeline(vk::PipelineBindPoint::eRayTracingNV, mPipeline, mVkDispatcher);

                command->bindDescriptorSets(vk::PipelineBindPoint::eRayTracingNV, mPipelineLayout, 0, 1, mRenderingResources[i].descriptorSet.get(), 0, nullptr, mVkDispatcher);

                command->traceRaysNV(
                    /* Gen  */ mShaderBindingTable, 0,
                    /* Miss */ mShaderBindingTable, 2 * rayTacingProps.shaderGroupHandleSize, rayTacingProps.shaderGroupHandleSize,
                    /* Hit  */ mShaderBindingTable, rayTacingProps.shaderGroupHandleSize, rayTacingProps.shaderGroupHandleSize,
                    /* Call */ nullptr, 0, 0, 
                    mFramebufferExtents.width, mFramebufferExtents.height, 1, mVkDispatcher);

                {
                    vk::ImageMemoryBarrier imageBarrier{};
                    imageBarrier.setImage(mRenderingResources[i].imageHandle);
                    imageBarrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite);
                    imageBarrier.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead);
                    imageBarrier.setOldLayout(vk::ImageLayout::eGeneral);
                    imageBarrier.setNewLayout(vk::ImageLayout::ePresentSrcKHR);
                    imageBarrier.setSrcQueueFamilyIndex(mQueueFamilyGraphics);
                    imageBarrier.setDstQueueFamilyIndex(mQueueFamilyPresent);
                    imageBarrier.setSubresourceRange(fullImage);

                    command->pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderNV, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1, &imageBarrier, mVkDispatcher);
                }

                command->end(mVkDispatcher);

            }

            vk::SemaphoreCreateInfo semaphoreInfo{};

            mRenderingResources[i].initializedSemaphore = MakeHolder(mDevice->createSemaphore(semaphoreInfo, nullptr, mVkDispatcher), [this](vk::Semaphore& s) { mDevice->destroySemaphore(s, nullptr, mVkDispatcher); });
            mRenderingResources[i].readyToPresentSemaphore = MakeHolder(mDevice->createSemaphore(semaphoreInfo, nullptr, mVkDispatcher), [this](vk::Semaphore& s) { mDevice->destroySemaphore(s, nullptr, mVkDispatcher); });

            vk::FenceCreateInfo fenceInfo{};
            fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);

            mRenderingResources[i].fence = MakeHolder(mDevice->createFence(fenceInfo, nullptr, mVkDispatcher), [this](vk::Fence& f){ mDevice->destroyFence(f, nullptr, mVkDispatcher); });

            mRenderingResources[i].inited = false;
        }

        vk::SemaphoreCreateInfo semaphoreAvailableInfo{};

        mSemaphoreAvailable = MakeHolder(mDevice->createSemaphore(semaphoreAvailableInfo, nullptr, mVkDispatcher), [this](vk::Semaphore& s) { mDevice->destroySemaphore(s, nullptr, mVkDispatcher); });

        std::cout << "OK" << std::endl;

        CanRender = true;
    }

    bool OnWindowSizeChanged() override
    {
        return true;
    }

    bool Draw() override
    {
        constexpr uint64_t TIMEOUT = static_cast<uint64_t>(10) * 1000 * 1000 * 1000; // 10 second in nanos

        auto imageIdx = mDevice->acquireNextImageKHR(mSwapChain, TIMEOUT, mSemaphoreAvailable, nullptr, mVkDispatcher);
        if (imageIdx.result != vk::Result::eSuccess) {
            std::cout << "Failed to acquire image! Stoppping." << std::endl;
            return false;
        }

        auto & renderingResource = mRenderingResources[imageIdx.value];

        if (vk::Result::eSuccess != mDevice->waitForFences(1, renderingResource.fence.get(), true, TIMEOUT, mVkDispatcher)) {
            std::cout << "Failed to wait for a fence! Stoppping." << std::endl;
            return false;
        }

        mDevice->resetFences(1, renderingResource.fence.get(), mVkDispatcher);

        // Update matrixes
        const auto position = Ogre::Vector3(0.0f, 0.0f, -3.0f);
        Ogre::Quaternion currentOrientation = mRotationY * mRotationX * mDefaultOrientation;
        Ogre::Matrix4 view;
        view.makeTransform(position, Ogre::Vector3::UNIT_SCALE, currentOrientation);
        view = view.transpose();

        CameraProperties cameraProps{};
        cameraProps.viewInverse = view.inverse();
        cameraProps.projInverse = mProjectionMatrix.inverse();

        void* cameraDataPtr = mDevice->mapMemory(renderingResource.cameraPropsBufferMemory, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, mVkDispatcher);
        if (cameraDataPtr == nullptr) {
            std::cout << "Failed to map memory for camera properties" << std::endl;
            return false;
        }
        std::memcpy(cameraDataPtr, &cameraProps, sizeof(CameraProperties));
        
        vk::MappedMemoryRange flushRange{};
        flushRange.setMemory(renderingResource.cameraPropsBufferMemory);
        flushRange.setOffset(0);
        flushRange.setSize(VK_WHOLE_SIZE);

        mDevice->flushMappedMemoryRanges(1, &flushRange, mVkDispatcher);

        mDevice->unmapMemory(renderingResource.cameraPropsBufferMemory, mVkDispatcher);


        if (!renderingResource.inited) {

            vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eTopOfPipe;

            vk::SubmitInfo initInfo{};
            initInfo.setCommandBufferCount(1);
            initInfo.setPCommandBuffers(renderingResource.initCommand.get());
            initInfo.setWaitSemaphoreCount(1);
            initInfo.setPWaitSemaphores(mSemaphoreAvailable.get());
            initInfo.setPWaitDstStageMask(&waitStage);
            initInfo.setSignalSemaphoreCount(1);
            initInfo.setPSignalSemaphores(renderingResource.initializedSemaphore.get());

            if (vk::Result::eSuccess != mGraphicsQueue.submit(1, &initInfo, nullptr, mVkDispatcher)) {
                std::cout << "Failed to submit an init command! Stoppping." << std::endl;
                return false;
            }


            vk::SubmitInfo drawInfo{};
            drawInfo.setCommandBufferCount(1);
            drawInfo.setPCommandBuffers(renderingResource.drawCommand.get());
            drawInfo.setWaitSemaphoreCount(1);
            drawInfo.setPWaitSemaphores(renderingResource.initializedSemaphore.get());
            drawInfo.setPWaitDstStageMask(&waitStage);
            drawInfo.setSignalSemaphoreCount(1);
            drawInfo.setPSignalSemaphores(renderingResource.readyToPresentSemaphore.get());

            if (vk::Result::eSuccess != mGraphicsQueue.submit(1, &drawInfo, renderingResource.fence, mVkDispatcher)) {
                std::cout << "Failed to submit a draw command! Stoppping." << std::endl;
                return false;
            }

            renderingResource.inited = true;
        }
        else {

            vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eTopOfPipe;

            vk::SubmitInfo drawInfo{};
            drawInfo.setCommandBufferCount(1);
            drawInfo.setPCommandBuffers(renderingResource.drawCommand.get());
            drawInfo.setWaitSemaphoreCount(1);
            drawInfo.setPWaitSemaphores(mSemaphoreAvailable.get());
            drawInfo.setPWaitDstStageMask(&waitStage);
            drawInfo.setSignalSemaphoreCount(1);
            drawInfo.setPSignalSemaphores(renderingResource.readyToPresentSemaphore.get());

            if (vk::Result::eSuccess != mGraphicsQueue.submit(1, &drawInfo, renderingResource.fence, mVkDispatcher)) {
                std::cout << "Failed to submit a draw command! Stoppping." << std::endl;
                return false;
            }
        }

        vk::PresentInfoKHR presentInfo{};
        presentInfo.setWaitSemaphoreCount(1);
        presentInfo.setPWaitSemaphores(renderingResource.readyToPresentSemaphore.get());
        presentInfo.setSwapchainCount(1);
        presentInfo.setPSwapchains(mSwapChain.get());
        presentInfo.setPImageIndices(&imageIdx.value);

        auto result = mPresentQueue.presentKHR(&presentInfo, mVkDispatcher);
        if (result != vk::Result::eSuccess) {
            std::cout << "Failed to present image! Stoppping." << std::endl;
            return false;
        }

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
                mRotationX.FromAngleAxis( Ogre::Radian((newPos.x - mMousePosition.x) / 180.0f), Ogre::Vector3::UNIT_Y);
                mRotationY.FromAngleAxis(-Ogre::Radian((newPos.y - mMousePosition.y) / 180.0f), Ogre::Vector3::UNIT_X);
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
        if (!window.Create("18 - Ray Marching", 512, 512)) {
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
