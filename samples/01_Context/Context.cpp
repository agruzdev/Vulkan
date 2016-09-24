/**
 * Vulkan samples
 * 
 * Context
 * Creates vulkan instance and displays all found physical devices
 * Only devices supporting Vulkan will be displayed
 *
 * The MIT License (MIT)
 * Copyright (c) 2016 Alexey Gruzdev
 */

#include <iostream>

#include <vulkan\vulkan.hpp>

#include <VulkanUtility.h>

int main() {
    try {
        std::cout << "Create Vulkan Instance...";
        vk::ApplicationInfo applicationInfo;
        applicationInfo.pApplicationName = "Vulkan sample: Context";
        applicationInfo.pEngineName = "Vulkan";
        applicationInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        VulkanHolder<vk::Instance> vulkan = vk::createInstance(vk::InstanceCreateInfo(vk::InstanceCreateFlags(), &applicationInfo));
        if (!vulkan) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }
        std::cout << "OK" << std::endl;

        auto extensions = vk::enumerateInstanceExtensionProperties();
        std::cout << "Vulkan Instance extensions:" << std::endl;
        for (const auto & ext : extensions) {
            std::cout << ext.extensionName << " ";
        }
        std::cout << std::endl;
        std::cout << std::endl;

        std::cout << "Found physical devices:" << std::endl;
        std::vector<vk::PhysicalDevice> devices = vulkan->enumeratePhysicalDevices();
        for(const auto & physDevice: devices) {
            std::cout << "Id:      " << physDevice.getProperties().deviceID << std::endl;
            std::cout << "Name:    " << physDevice.getProperties().deviceName << std::endl;
            std::cout << "Type:    " << DeviceTypeToString(physDevice.getProperties().deviceType) << std::endl;
            std::cout << "Vendor:  " << physDevice.getProperties().vendorID << std::endl;
            std::cout << "Driver:  " << physDevice.getProperties().driverVersion << std::endl;
            std::cout << "API ver: " << physDevice.getProperties().apiVersion << std::endl;
            std::cout << "Extensions: " << std::endl;
            auto deviceExtensions = physDevice.enumerateDeviceExtensionProperties();
            for (const auto & ext : deviceExtensions) {
                std::cout << ext.extensionName << " ";
            }
            std::cout << std::endl;
            std::cout << std::endl;
        }
    }
    catch (std::runtime_error & err) {
        std::cout << "Error!" << std::endl;
        std::cout << err.what() << std::endl;
    }
}
