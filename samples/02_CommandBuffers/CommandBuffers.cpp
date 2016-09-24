/**
 * Vulkan samples
 * 
 * CommandBuffers
 * Creates few command buffers on the first device and first command queue
 * Only to show and debug code for buffers allocation
 *
 * The MIT License (MIT)
 * Copyright (c) 2016 Alexey Gruzdev
 */

#include <iostream>

#include <vulkan\vulkan.hpp>

#include <VulkanUtility.h>

int main() {
    try {
        vk::ApplicationInfo applicationInfo;
        applicationInfo.pApplicationName = "Vulkan sample: CommandBuffers";
        applicationInfo.pEngineName = "Vulkan";
        applicationInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

        std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
        std::cout << "Check extensions...";
        CheckExtensions(extensions);
        std::cout << "OK" << std::endl;

        std::cout << "Create Vulkan Instance...";
        vk::InstanceCreateInfo instanceCreateInfo;
        instanceCreateInfo.pApplicationInfo = &applicationInfo;
        instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instanceCreateInfo.ppEnabledExtensionNames = &extensions[0];
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

        std::cout << "Check device extensions...";
        std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        CheckDeviceExtensions(*physicalDevice, deviceExtensions);
        std::cout << "OK" << std::endl;

        std::cout << "Create logical device...";
        const uint32_t queueFamilyIndex = 0;
        std::vector<float> queuePriorities = { 1.0f };
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.queueFamilyIndex = static_cast<uint32_t>(queueFamilyIndex);
        queueCreateInfo.queueCount = static_cast<uint32_t>(queuePriorities.size());
        queueCreateInfo.pQueuePriorities = &queuePriorities[0];
        vk::DeviceCreateInfo deviceCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = &deviceExtensions[0];
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        VulkanHolder<vk::Device> logicalDevice = physicalDevice->createDevice(deviceCreateInfo);
        std::cout << "OK" << std::endl;

        std::cout << "Prepare commands buffers...";
        constexpr uint32_t buffersCount = 3;
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
    }
    catch (std::runtime_error & err) {
        std::cout << "Error!" << std::endl;
        std::cout << err.what() << std::endl;
    }
}
