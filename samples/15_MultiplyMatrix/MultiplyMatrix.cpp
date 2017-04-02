/**
* Vulkan samples
*
* The MIT License (MIT)
* Copyright (c) 2016 Alexey Gruzdev
*/

#include <iostream>
#include <numeric>
#include <vector>
#include <chrono>

#include <vulkan\vulkan.hpp>

#include <VulkanUtility.h>


namespace
{

    constexpr size_t MATRIX_COLS = 512;
    constexpr size_t MATRIX_ROWS = 256;

    template <size_t Cols_, size_t Rows_>
    using Matrix  = std::array<std::array<float, Cols_>, Rows_>;

    template <size_t Cols_, size_t Rows_>
    void MultiplyMatrix(const Matrix<Cols_, Rows_>* A, const Matrix<Rows_, Cols_>* B, Matrix<Rows_, Rows_>* C) {
        #pragma omp parallel for
        for(int j = 0; j < static_cast<int>(Rows_); ++j) {
            for(size_t i = 0; i < Rows_; ++i) {
                float res = 0.0f;
                for(size_t k = 0; k < Cols_; ++k) {
                    res += (*A)[j][k] * (*B)[k][i];
                }
                (*C)[j][i] = res;
            }
        }
    }

    struct Constants
    {
        uint32_t columns;
        uint32_t rows;
    };
}

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

        auto matA = std::make_unique<Matrix<MATRIX_COLS, MATRIX_ROWS>>();
        auto matB = std::make_unique<Matrix<MATRIX_ROWS, MATRIX_COLS>>();
        auto matC = std::make_unique<Matrix<MATRIX_ROWS, MATRIX_ROWS>>(); // C = A * B
        std::cout << "Prepare data...";
        {
            auto random = []() { return std::rand() / static_cast<float>(RAND_MAX); };
            for(auto & row : *matA) {
                std::generate(row.begin(), row.end(), random);
            }
            for (auto & row : *matB) {
                std::generate(row.begin(), row.end(), random);
            }
            MultiplyMatrix(matA.get(), matB.get(), matC.get());
            std::cout << "OK" << std::endl;
        }

        //const uint32_t bufferElements = 1024;
        const vk::DeviceSize bufferSize = MATRIX_ROWS * MATRIX_COLS * sizeof(float);

        std::cout << "Allocate buffers...";
        
        vk::BufferCreateInfo bufferInfo;
        bufferInfo.setSize(bufferSize);
        bufferInfo.setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
        bufferInfo.setSharingMode(vk::SharingMode::eExclusive);
        VulkanHolder<vk::Buffer> bufferA = MakeHolder(logicalDevice->createBuffer(bufferInfo), [&logicalDevice](vk::Buffer & buffer) { logicalDevice->destroyBuffer(buffer); });
        VulkanHolder<vk::Buffer> bufferB = MakeHolder(logicalDevice->createBuffer(bufferInfo), [&logicalDevice](vk::Buffer & buffer) { logicalDevice->destroyBuffer(buffer); });
        VulkanHolder<vk::Buffer> bufferC = MakeHolder(logicalDevice->createBuffer(bufferInfo), [&logicalDevice](vk::Buffer & buffer) { logicalDevice->destroyBuffer(buffer); });

        auto allocateMemoryForBuffer = [&logicalDevice, &physicalDevice](vk::Buffer buffer)
        {
            VulkanHolder<vk::DeviceMemory> memory;

            vk::MemoryRequirements bufferRequirements = logicalDevice->getBufferMemoryRequirements(buffer);

            vk::PhysicalDeviceMemoryProperties memroProperties = physicalDevice->getMemoryProperties();
            for (uint32_t i = 0; i < memroProperties.memoryTypeCount; ++i) {
                if ((bufferRequirements.memoryTypeBits & (1 << i)) &&
                    (memroProperties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))) {

                    vk::MemoryAllocateInfo allocateInfo;
                    allocateInfo.setAllocationSize(bufferRequirements.size);
                    allocateInfo.setMemoryTypeIndex(i);
                    memory = MakeHolder(logicalDevice->allocateMemory(allocateInfo), [&logicalDevice](vk::DeviceMemory & mem) { logicalDevice->freeMemory(mem); });
                }
            }
            if (!memory) {
                throw std::runtime_error("Failed to allocate memory for matrix buffers");
            }
            logicalDevice->bindBufferMemory(buffer, memory, 0);
            return memory;
        };

        VulkanHolder<vk::DeviceMemory> bufferMemoryA = allocateMemoryForBuffer(bufferA);
        VulkanHolder<vk::DeviceMemory> bufferMemoryB = allocateMemoryForBuffer(bufferB);
        VulkanHolder<vk::DeviceMemory> bufferMemoryC = allocateMemoryForBuffer(bufferC);

        std::cout << "OK" << std::endl;


        std::cout << "Loading shader... ";
        auto code = GetBinaryShaderFromSourceFile(QUOTE(SHADERS_DIR) "/glsl/15.comp");
        if (code.empty()) {
            throw std::runtime_error("LoadShader: Failed to read shader file!");
        }
        vk::ShaderModuleCreateInfo shaderInfo;
        shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(&code[0]));
        shaderInfo.setCodeSize(code.size());

        auto computeShader = MakeHolder(logicalDevice->createShaderModule(shaderInfo), [&logicalDevice](vk::ShaderModule & shader) { logicalDevice->destroyShaderModule(shader); });

        std::array<vk::DescriptorSetLayoutBinding, 3> bindings;
        bindings[0].setBinding(0);
        bindings[0].setDescriptorType(vk::DescriptorType::eStorageBuffer);
        bindings[0].setDescriptorCount(1);
        bindings[0].setStageFlags(vk::ShaderStageFlagBits::eCompute);

        bindings[1].setBinding(1);
        bindings[1].setDescriptorType(vk::DescriptorType::eStorageBuffer);
        bindings[1].setDescriptorCount(1);
        bindings[1].setStageFlags(vk::ShaderStageFlagBits::eCompute);

        bindings[2].setBinding(2);
        bindings[2].setDescriptorType(vk::DescriptorType::eStorageBuffer);
        bindings[2].setDescriptorCount(1);
        bindings[2].setStageFlags(vk::ShaderStageFlagBits::eCompute);

        vk::DescriptorSetLayoutCreateInfo descriptorSetInfo;
        descriptorSetInfo.setBindingCount(static_cast<uint32_t>(bindings.size()));
        descriptorSetInfo.setPBindings(&bindings[0]);
        auto descriptorSetLayout = MakeHolder(logicalDevice->createDescriptorSetLayout(descriptorSetInfo), [&logicalDevice](vk::DescriptorSetLayout & layout) { logicalDevice->destroyDescriptorSetLayout(layout); });

        std::array<vk::DescriptorPoolSize, 1> poolSize;
        poolSize[0].setType(vk::DescriptorType::eStorageBuffer);
        poolSize[0].setDescriptorCount(3);

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

        std::array<vk::PushConstantRange, 1> pushConstants;
        pushConstants[0].setStageFlags(vk::ShaderStageFlagBits::eCompute);
        pushConstants[0].setSize(sizeof(Constants));
        pushConstants[0].setOffset(0);

        vk::PipelineShaderStageCreateInfo stageInfos[1];
        stageInfos[0].setStage(vk::ShaderStageFlagBits::eCompute);
        stageInfos[0].setModule(computeShader);
        stageInfos[0].setPName("main"); // Shader entry point

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayoutInfo.setSetLayoutCount(1);
        pipelineLayoutInfo.setPSetLayouts(descriptorSetLayout.get());
        pipelineLayoutInfo.setPushConstantRangeCount(static_cast<uint32_t>(pushConstants.size()));
        pipelineLayoutInfo.setPPushConstantRanges(&pushConstants[0]);
        auto pipelineLayout = MakeHolder(logicalDevice->createPipelineLayout(pipelineLayoutInfo), [&logicalDevice](vk::PipelineLayout & layout) { logicalDevice->destroyPipelineLayout(layout); });

        vk::ComputePipelineCreateInfo computePipelineInfo;
        computePipelineInfo.setStage(stageInfos[0]);
        computePipelineInfo.setLayout(pipelineLayout);
        auto pipeline = MakeHolder(logicalDevice->createComputePipeline(vk::PipelineCache(), computePipelineInfo), [&logicalDevice](vk::Pipeline & pipeline) {logicalDevice->destroyPipeline(pipeline); });

        vk::QueryPoolCreateInfo queryPoolInfo;
        queryPoolInfo.setQueryCount(2);
        queryPoolInfo.setQueryType(vk::QueryType::eTimestamp);
        auto queryPool = MakeHolder(logicalDevice->createQueryPool(queryPoolInfo), [&logicalDevice](vk::QueryPool & pool) {logicalDevice->destroyQueryPool(pool); });

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
        std::array<vk::WriteDescriptorSet, 3> writeDescriptorsInfo;
        std::array<vk::DescriptorBufferInfo, 3> descriptorBufferInfos;
        descriptorBufferInfos[0].setBuffer(bufferA);
        descriptorBufferInfos[0].setOffset(0);
        descriptorBufferInfos[0].setRange(bufferSize);

        descriptorBufferInfos[1].setBuffer(bufferB);
        descriptorBufferInfos[1].setOffset(0);
        descriptorBufferInfos[1].setRange(bufferSize);

        descriptorBufferInfos[2].setBuffer(bufferC);
        descriptorBufferInfos[2].setOffset(0);
        descriptorBufferInfos[2].setRange(bufferSize);

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

        writeDescriptorsInfo[2].setDescriptorType(vk::DescriptorType::eStorageBuffer);
        writeDescriptorsInfo[2].setDstSet(descriptorSet);
        writeDescriptorsInfo[2].setDstBinding(2);
        writeDescriptorsInfo[2].setDstArrayElement(0);
        writeDescriptorsInfo[2].setDescriptorCount(1);
        writeDescriptorsInfo[2].setPBufferInfo(&descriptorBufferInfos[2]);

        logicalDevice->updateDescriptorSets(static_cast<uint32_t>(writeDescriptorsInfo.size()), &writeDescriptorsInfo[0], 0, nullptr);

        std::cout << "OK" << std::endl;

        std::cout << "Upload input data...";
        {
            float* devicePtr = static_cast<float*>(logicalDevice->mapMemory(bufferMemoryA, 0, bufferSize));
            if (devicePtr == nullptr) {
                throw std::runtime_error("Failed to map memory for matrix A buffer");
            }
            for (const auto & row : *matA) {
                for (const float & x : row) {
                    *devicePtr++ = x;
                }
            }
            logicalDevice->unmapMemory(bufferMemoryA);
        }
        {
            // Load transposed
            float* devicePtr = static_cast<float*>(logicalDevice->mapMemory(bufferMemoryB, 0, bufferSize));
            if (devicePtr == nullptr) {
                throw std::runtime_error("Failed to map memory for matrix B buffer");
            }
            for(size_t i = 0; i < MATRIX_ROWS; ++i) {
                for(size_t j = 0; j < MATRIX_COLS; ++j) {
                    *devicePtr++ = (*matB)[j][i];
                }
            }
            logicalDevice->unmapMemory(bufferMemoryB);
        }
        std::cout << "OK" << std::endl;

        std::cout << "Run computations..." << std::endl;

        auto* cmdBuffer = &(*commandBuffers)[0];
        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuffer->begin(beginInfo);

        cmdBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);

        cmdBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, 1, descriptorSet.get(), 0, nullptr);

        Constants constants;
        constants.columns = static_cast<uint32_t>(MATRIX_COLS);
        constants.rows    = static_cast<uint32_t>(MATRIX_ROWS);
        cmdBuffer->pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(Constants), &constants);

        cmdBuffer->resetQueryPool(queryPool, 0, 2);

        cmdBuffer->writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, queryPool, 0);

        const size_t BLOCK_SIZE = 32;
        assert(MATRIX_ROWS % BLOCK_SIZE == 0);
        cmdBuffer->dispatch(MATRIX_ROWS / BLOCK_SIZE, MATRIX_ROWS / BLOCK_SIZE, 1);

        cmdBuffer->writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, queryPool, 1);

        cmdBuffer->end();

        vk::Queue queue = logicalDevice->getQueue(queueFamilyIndex, 0);
        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = cmdBuffer;

        queue.submit(submitInfo, vk::Fence());
        queue.waitIdle();

        std::array<uint64_t, 2> timestamps = { 0, 0 }; // nanoseconds 
        logicalDevice->getQueryPoolResults(queryPool, 0, 2, sizeof(timestamps), &timestamps[0], sizeof(uint64_t), vk::QueryResultFlagBits::e64);

        std::cout << "Executions time = " << (timestamps[1] - timestamps[0]) / 1e6 << " ms" << std::endl;

        //std::cout << "Read results..." << std::endl;
        {
            float* devicePtr = static_cast<float*>(logicalDevice->mapMemory(bufferMemoryC, 0, bufferSize));
            if (devicePtr == nullptr) {
                throw std::runtime_error("Failed to map memory for matrix B buffer");
            }
            float error = 0.0f;
            for(size_t j = 0; j < MATRIX_ROWS; ++j) {
                for(size_t i = 0; i < MATRIX_ROWS; ++i) {
                    error += std::fabs((*devicePtr++) - (*matC)[j][i]);
                }
            }
            logicalDevice->unmapMemory(bufferMemoryC);

            error = error / (MATRIX_ROWS * MATRIX_ROWS);
            std::cout << "Mean error = " << error << std::endl;
            if(error < 0.0001f) {
                std::cout << "OK" << std::endl;
            } else {
                std::cout << "Error!" << std::endl;
            }
        }

    }
    catch (std::runtime_error & err) {
        std::cout << "Error!" << std::endl;
        std::cout << err.what() << std::endl;
    }
}
