
#include <iostream>

#include <vulkan\vulkan.hpp>

inline 
const char* deviceTypeToString(const vk::PhysicalDeviceType & type) {
    switch (type) {
    case vk::PhysicalDeviceType::eCpu:
        return "CPU";
    case vk::PhysicalDeviceType::eDiscreteGpu:
        return "Discrete GPU";
    case vk::PhysicalDeviceType::eIntegratedGpu:
        return "Integrated GPU";
    case vk::PhysicalDeviceType::eVirtualGpu:
        return "Virtual GPU";
    case vk::PhysicalDeviceType::eOther:
        return "Other";
    default:
        return "Unknown";
    }
}

int main() {
    try {
        std::cout << "Create Vulkan Instance...";
        vk::Instance vulkan = vk::createInstance(vk::InstanceCreateInfo());
        if (!vulkan) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }
        std::cout << "OK" << std::endl;

        std::cout << "Found physical devices:" << std::endl;
        std::vector<vk::PhysicalDevice> devices = vulkan.enumeratePhysicalDevices();
        for(const auto & physDevice: devices) {
            std::cout << "Id:      " << physDevice.getProperties().deviceID << std::endl;
            std::cout << "Name:    " << physDevice.getProperties().deviceName << std::endl;
            std::cout << "Type:    " << deviceTypeToString(physDevice.getProperties().deviceType) << std::endl;
            std::cout << "Vendor:  " << physDevice.getProperties().vendorID << std::endl;
            std::cout << "Driver:  " << physDevice.getProperties().driverVersion << std::endl;
            std::cout << "API ver: " << physDevice.getProperties().apiVersion << std::endl;
            std::cout << std::endl;
        }
        vulkan.destroy(); // Why doesn't it happen in destructor?
    }
    catch (std::runtime_error & err) {
        std::cout << "Error!" << std::endl;
        std::cout << err.what() << std::endl;
    }
}
