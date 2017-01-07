/**
* Vulkan samples
*
* The MIT License (MIT)
* Copyright (c) 2016 Alexey Gruzdev
*/

#include <iostream>
#include <numeric>
#include <vector>
#include <iterator>

#include <vulkan\vulkan.hpp>

#include <VulkanUtility.h>

int main()
{
    try {
        vk::ApplicationInfo applicationInfo;
        applicationInfo.pApplicationName = "Vulkan sample: Compute pipeline";
        applicationInfo.pEngineName = "Vulkan";
        applicationInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

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
        VulkanHolder<vk::Instance> vulkan = vk::createInstance(instanceCreateInfo);
        if (!vulkan) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }
        std::cout << "OK" << std::endl;

        std::cout << "Find Vulkan physical device...";
        std::vector<vk::PhysicalDevice> devices = vulkan->enumeratePhysicalDevices();
        if (devices.empty()) {
            throw std::runtime_error("Physical device was not found");
        }
        vk::PhysicalDevice* const physicalDevice = &devices.front();
        std::cout << "OK" << std::endl;

        std::cout << "Create logical device...";
        const uint32_t queueFamilyIndex = 0;
        std::vector<float> queuePriorities = { 1.0f };
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.queueFamilyIndex = static_cast<uint32_t>(queueFamilyIndex);
        queueCreateInfo.queueCount = static_cast<uint32_t>(queuePriorities.size());
        queueCreateInfo.pQueuePriorities = &queuePriorities[0];
        vk::DeviceCreateInfo deviceCreateInfo;
        deviceCreateInfo.enabledExtensionCount = 0;
        deviceCreateInfo.ppEnabledExtensionNames = nullptr;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        VulkanHolder<vk::Device> logicalDevice = physicalDevice->createDevice(deviceCreateInfo);
        std::cout << "OK" << std::endl;

        const uint32_t bufferElements = 1024;
        const VkDeviceSize bufferSize = bufferElements * sizeof(int32_t);

        std::cout << "Allocate buffers...";
        
        vk::BufferCreateInfo bufferInfo;
        bufferInfo.setSize(bufferSize);
        bufferInfo.setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
        bufferInfo.setSharingMode(vk::SharingMode::eExclusive);
        VulkanHolder<vk::Buffer> inBuffer  = MakeHolder(logicalDevice->createBuffer(bufferInfo), [&logicalDevice](vk::Buffer & buffer) { logicalDevice->destroyBuffer(buffer); });
        VulkanHolder<vk::Buffer> outBuffer = MakeHolder(logicalDevice->createBuffer(bufferInfo), [&logicalDevice](vk::Buffer & buffer) { logicalDevice->destroyBuffer(buffer); });

        VulkanHolder<vk::DeviceMemory> inBufferMemory;
        VulkanHolder<vk::DeviceMemory> outBufferMemory;

        /**
         * if propertyFlags has the VK_MEMORY_PROPERTY_HOST_COHERENT_BIT bit set, host cache management commands
         * vkFlushMappedMemoryRanges and vkInvalidateMappedMemoryRanges are not needed to make host writes visible
         * to the device or device writes visible to the host, respectively.
         */
        {
            vk::MemoryRequirements bufferRequirements = logicalDevice->getBufferMemoryRequirements(inBuffer);

            vk::PhysicalDeviceMemoryProperties memroProperties = physicalDevice->getMemoryProperties();
            for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                if ((bufferRequirements.memoryTypeBits & (1 << i)) &&
                    (memroProperties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))) {

                    vk::MemoryAllocateInfo allocateInfo;
                    allocateInfo.setAllocationSize(bufferRequirements.size);
                    allocateInfo.setMemoryTypeIndex(i);
                    inBufferMemory = MakeHolder(logicalDevice->allocateMemory(allocateInfo), [&logicalDevice](vk::DeviceMemory & memory) { logicalDevice->freeMemory(memory); });
                }
            }
            if (!inBufferMemory) {
                throw std::runtime_error("Failed to allocate memory for matrix buffers");
            }
        }

        {
            vk::MemoryRequirements bufferRequirements = logicalDevice->getBufferMemoryRequirements(outBuffer);

            vk::PhysicalDeviceMemoryProperties memroProperties = physicalDevice->getMemoryProperties();
            for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                if ((bufferRequirements.memoryTypeBits & (1 << i)) &&
                    (memroProperties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))) {

                    vk::MemoryAllocateInfo allocateInfo;
                    allocateInfo.setAllocationSize(bufferRequirements.size);
                    allocateInfo.setMemoryTypeIndex(i);
                    outBufferMemory = MakeHolder(logicalDevice->allocateMemory(allocateInfo), [&logicalDevice](vk::DeviceMemory & memory) { logicalDevice->freeMemory(memory); });
                }
            }
            if (!outBufferMemory) {
                throw std::runtime_error("Failed to allocate memory for matrix buffers");
            }
        }

        logicalDevice->bindBufferMemory(inBuffer,  inBufferMemory,  0);
        logicalDevice->bindBufferMemory(outBuffer, outBufferMemory, 0);

        std::cout << "OK" << std::endl;


        std::cout << "Loading shader... ";
        auto code = GetBinaryFileContents(QUOTE(SHADERS_DIR) "/spv/10.comp.spv");
        if (code.empty()) {
            throw std::runtime_error("LoadShader: Failed to read shader file!");
        }
        vk::ShaderModuleCreateInfo shaderInfo;
        shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
        shaderInfo.setCodeSize(code.size());

        auto computeShader = MakeHolder(logicalDevice->createShaderModule(shaderInfo), [&logicalDevice](vk::ShaderModule & shader) { logicalDevice->destroyShaderModule(shader); });

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings;
        bindings[0].setBinding(0);
        bindings[0].setDescriptorType(vk::DescriptorType::eStorageBuffer);
        bindings[0].setDescriptorCount(1);
        bindings[0].setStageFlags(vk::ShaderStageFlagBits::eCompute);

        bindings[1].setBinding(1);
        bindings[1].setDescriptorType(vk::DescriptorType::eStorageBuffer);
        bindings[1].setDescriptorCount(1);
        bindings[1].setStageFlags(vk::ShaderStageFlagBits::eCompute);

        vk::DescriptorSetLayoutCreateInfo descriptorSetInfo;
        descriptorSetInfo.setBindingCount(static_cast<uint32_t>(bindings.size()));
        descriptorSetInfo.setPBindings(&bindings[0]);
        auto descriptorSetLayout = MakeHolder(logicalDevice->createDescriptorSetLayout(descriptorSetInfo), [&logicalDevice](vk::DescriptorSetLayout & layout) { logicalDevice->destroyDescriptorSetLayout(layout); });

        std::array<vk::DescriptorPoolSize, 1> poolSize;
        poolSize[0].setType(vk::DescriptorType::eStorageBuffer);
        poolSize[0].setDescriptorCount(2);

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
        poolInfo.setMaxSets(1);
        poolInfo.setPoolSizeCount(static_cast<uint32_t>(poolSize.size()));
        poolInfo.setPPoolSizes(&poolSize[0]);

        auto descriptorPool = MakeHolder(logicalDevice->createDescriptorPool(poolInfo), [&logicalDevice](vk::DescriptorPool & pool) { logicalDevice->destroyDescriptorPool(pool); });
        
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.setDescriptorPool(descriptorPool);
        allocInfo.setDescriptorSetCount(1);
        allocInfo.setPSetLayouts(descriptorSetLayout.get());

        vk::DescriptorSet decriptorSetTmp;
        if (vk::Result::eSuccess != logicalDevice->allocateDescriptorSets(&allocInfo, &decriptorSetTmp)) {
            throw std::runtime_error("Failed to allocate descriptors set");
        }
        auto descriptorSet = MakeHolder(decriptorSetTmp, [&logicalDevice, &descriptorPool](vk::DescriptorSet & set) { logicalDevice->freeDescriptorSets(descriptorPool, set); });
        std::cout << "OK" << std::endl;

        std::cout << "Create pipeline...";

        vk::PipelineShaderStageCreateInfo stageInfos[1];
        stageInfos[0].setStage(vk::ShaderStageFlagBits::eCompute);
        stageInfos[0].setModule(computeShader);
        stageInfos[0].setPName("main"); // Shader entry point

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayoutInfo.setSetLayoutCount(1);
        pipelineLayoutInfo.setPSetLayouts(descriptorSetLayout.get());
        auto pipelineLayout = MakeHolder(logicalDevice->createPipelineLayout(pipelineLayoutInfo), [&logicalDevice](vk::PipelineLayout & layout) { logicalDevice->destroyPipelineLayout(layout); });

        vk::ComputePipelineCreateInfo computePipelineInfo;
        computePipelineInfo.setStage(stageInfos[0]);
        computePipelineInfo.setLayout(pipelineLayout);
        auto pipeline = MakeHolder(logicalDevice->createComputePipeline(vk::PipelineCache(), computePipelineInfo), [&logicalDevice](vk::Pipeline & pipeline) {logicalDevice->destroyPipeline(pipeline); });

        std::cout << "OK" << std::endl;


        std::cout << "Prepare commands buffers...";
        constexpr uint32_t buffersCount = 1;
        vk::CommandPoolCreateInfo poolCreateInfo;
        poolCreateInfo.queueFamilyIndex = queueFamilyIndex;
        auto commandPool = MakeHolder(logicalDevice->createCommandPool(poolCreateInfo),
            [&logicalDevice](vk::CommandPool & pool) { logicalDevice->destroyCommandPool(pool); });
        auto commandBuffers = MakeHolder(logicalDevice->allocateCommandBuffers(vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, buffersCount)),
            [&commandPool, &logicalDevice](std::vector<vk::CommandBuffer> & buffers) { logicalDevice->freeCommandBuffers(commandPool, buffers); });
        if (commandBuffers->size() != buffersCount) {
            throw std::runtime_error("Failed to create all command buffers");
        }
        std::cout << "OK" << std::endl;


        std::cout << "Prepare descriptors set...";
        std::array<vk::WriteDescriptorSet, 2> writeDescriptorsInfo;
        std::array<vk::DescriptorBufferInfo, 2> descriptorBufferInfos;
        descriptorBufferInfos[0].setBuffer(inBuffer);
        descriptorBufferInfos[0].setOffset(0);
        descriptorBufferInfos[0].setRange(bufferSize);

        descriptorBufferInfos[1].setBuffer(outBuffer);
        descriptorBufferInfos[1].setOffset(0);
        descriptorBufferInfos[1].setRange(bufferSize);

        writeDescriptorsInfo[0].setDescriptorType(vk::DescriptorType::eStorageBuffer);
        writeDescriptorsInfo[0].setDstSet(descriptorSet);
        writeDescriptorsInfo[0].setDstBinding(0);
        writeDescriptorsInfo[0].setDstArrayElement(0);
        writeDescriptorsInfo[0].setDescriptorCount(1);
        writeDescriptorsInfo[0].setPBufferInfo(&descriptorBufferInfos[0]);

        writeDescriptorsInfo[1].setDescriptorType(vk::DescriptorType::eStorageBuffer);
        writeDescriptorsInfo[1].setDstSet(descriptorSet);
        writeDescriptorsInfo[1].setDstBinding(1);
        writeDescriptorsInfo[1].setDstArrayElement(0);
        writeDescriptorsInfo[1].setDescriptorCount(1);
        writeDescriptorsInfo[1].setPBufferInfo(&descriptorBufferInfos[1]);

        logicalDevice->updateDescriptorSets(static_cast<uint32_t>(writeDescriptorsInfo.size()), &writeDescriptorsInfo[0], 0, nullptr);

        std::cout << "OK" << std::endl;

        std::cout << "Upload input data...";

        std::vector<int32_t> hostData(bufferElements);
        std::iota(hostData.begin(), hostData.end(), 0);

        int32_t* devicePtr = static_cast<int32_t*>(logicalDevice->mapMemory(inBufferMemory, 0, bufferSize));
        if (devicePtr == nullptr) {
            throw std::runtime_error("Failed to map memory for input buffer");
        }
        std::copy(hostData.cbegin(), hostData.cend(), devicePtr);
        logicalDevice->unmapMemory(inBufferMemory);

        std::cout << "OK" << std::endl;

        std::cout << "Input data:" << std::endl;
        for (int32_t i = 0; i < 10; ++i) {
            std::cout << hostData[i] << ", ";
        }
        std::cout << "..., " << hostData[bufferElements - 1] << std::endl;

        std::cout << "Run computations...";

        auto* cmdBuffer = &(*commandBuffers)[0];
        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuffer->begin(beginInfo);

        cmdBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);

        cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, 1, descriptorSet.get(), 0, nullptr);

        cmdBuffer->dispatch(bufferElements, 1, 1);

        cmdBuffer->end();

        vk::Queue queue = logicalDevice->getQueue(queueFamilyIndex, 0);
        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = cmdBuffer;

        queue.submit(submitInfo, vk::Fence());
        queue.waitIdle();

        std::cout << "OK" << std::endl;

        std::cout << "Read results...";

        std::vector<int32_t> result(bufferElements);
        devicePtr = static_cast<int32_t*>(logicalDevice->mapMemory(outBufferMemory, 0, bufferSize));
        if (devicePtr == nullptr) {
            throw std::runtime_error("Failed to map memory for input buffer");
        }
        std::copy_n(devicePtr, bufferElements, result.begin());
        logicalDevice->unmapMemory(outBufferMemory);


        if (std::equal(result.begin(), result.end(), hostData.begin(), [](int32_t r, int32_t h) { return r == h + 1; })) {
            std::cout << "OK" << std::endl;
        }
        else {
            std::cout << "Fail. Invalid result" << std::endl;
        }

        std::cout << "Output data:" << std::endl;
        for (int32_t i = 0; i < 10; ++i) {
            std::cout << result[i] << ", ";
        }
        std::cout << "..., " << result[bufferElements - 1] << std::endl;
        std::cout << std::endl;
    }
    catch (std::runtime_error & err) {
        std::cout << "Error!" << std::endl;
        std::cout << err.what() << std::endl;
    }
}
