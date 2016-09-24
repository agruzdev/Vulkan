/**
* Vulkan samples
*
* Common utilities
*
* The MIT License (MIT)
* Copyright (c) 2016 Alexey Gruzdev
*/

#ifndef _VULKAN_UTILITY_H_
#define _VULKAN_UTILITY_H_

#include <cstdint>
#include <initializer_list>
#include <string>
#include <memory>
#include <functional>
#include <type_traits>
#include <utility>

#define NOMINMAX
#include <vulkan\vulkan.hpp>

namespace details
{
    template <typename _VkType>
    struct VulkanDefaultDeleter final
    {
        void operator()(_VkType & vkObject) {
            vkObject.destroy();
        }
    };
}

template <typename _VkType>
class VulkanHolder
{
    using MyType = VulkanHolder< _VkType>;
    using StoredType  = _VkType;
    using DeleterType = std::function<void(_VkType&)>;

    _VkType mInstance;
    DeleterType mDeleter;
    bool mDetached = false;

public:
    VulkanHolder()
        : mDetached(true)
    { }

    VulkanHolder(const _VkType & instance, DeleterType && deleter = details::VulkanDefaultDeleter<_VkType>()):
        mInstance(instance), mDeleter(std::move(deleter))
    { }

    VulkanHolder(_VkType && instance, DeleterType && deleter = details::VulkanDefaultDeleter<_VkType>()):
        mInstance(std::move(instance)), mDeleter(std::move(deleter))
    { }

    ~VulkanHolder()
    {
        if (!mDetached) {
            mDeleter(mInstance);
        }
    }

    VulkanHolder(const VulkanHolder&) = delete;
    VulkanHolder& operator= (const VulkanHolder&) = delete;

    VulkanHolder(VulkanHolder && other)
        : mInstance(std::move(other.mInstance)), mDeleter(std::move(other.mDeleter))
    {
        other.mDetached = true;
    }

    VulkanHolder& operator= (VulkanHolder && other)
    {
        if (this != &other) {
            if (!mDetached) {
                mDeleter(mInstance);
            }
            mInstance = std::move(other.mInstance);
            mDeleter = std::move(other.mDeleter);
            mDetached = false;
            other.mDetached = true;
        }
        return *this;
    }

    operator _VkType()
    {
        return mInstance;
    }

    operator _VkType() const
    {
        return mInstance;
    }

    _VkType* get()
    {
        return &mInstance;
    }

    const _VkType* get() const
    {
        return const_cast<MyType*>(this).get();
    }

    _VkType* operator->()
    {
        return get();
    }

    const _VkType* operator->() const
    {
        return get();
    }

    _VkType & operator*()
    {
        return mInstance;
    }

    const _VkType & operator*() const
    {
        return const_cast<MyType*>(this).operator*();
    }

    bool operator!()
    {
        return !mInstance;
    }
    
    bool operator!() const
    {
        return const_cast<MyType*>(this).operator!();
    }

    /**
     * Detach holder from the instance. Don't delete it on destroy
     */
    _VkType & detach()
    {
        mDetached = true;
        return mInstance;
    }
};

template <typename _VkType, typename _VkDeleter>
inline 
auto MakeHolder(_VkType && instance, _VkDeleter && deleter)
{
    return VulkanHolder<std::decay_t<_VkType>>(std::forward<_VkType>(instance), std::forward<_VkDeleter>(deleter));
}

inline
const char* DeviceTypeToString(const vk::PhysicalDeviceType & type)
{
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

inline 
void CheckExtensions(std::vector<const char*> & names)
{
    auto extensions = vk::enumerateInstanceExtensionProperties();
    for (const auto & requestedExt : names) {
        if (extensions.cend() == std::find_if(extensions.cbegin(), extensions.cend(), [&requestedExt](const vk::ExtensionProperties & prop) {
            return (0 == std::strcmp(prop.extensionName, requestedExt));
        })) {
            throw std::runtime_error(std::string("Extendion is not supported ") + requestedExt);
        }
    }
}

inline
void CheckDeviceExtensions(const vk::PhysicalDevice & physDevice, std::vector<const char*> & names)
{
    auto extensions = physDevice.enumerateDeviceExtensionProperties();
    for (const auto & requestedExt : names) {
        if (extensions.cend() == std::find_if(extensions.cbegin(), extensions.cend(), [&requestedExt](const vk::ExtensionProperties & prop) {
            return (0 == std::strcmp(prop.extensionName, requestedExt));
        })) {
            throw std::runtime_error(std::string("Extendion is not supported ") + requestedExt);
        }
    }
}

inline
boolean CheckFormat(const std::vector<vk::SurfaceFormatKHR> & formats, const std::pair<vk::Format, vk::ColorSpaceKHR> & request)
{
    return (formats.cend() != std::find_if(formats.cbegin(), formats.cend(), [&request](const vk::SurfaceFormatKHR & surfaceFormat) {
        // If the list contains only one entry with undefined format
        // it means that there are no preferred surface formats and any can be chosen
        if (surfaceFormat.format == vk::Format::eUndefined) {
            return true;
        }
        if (surfaceFormat.format == request.first && surfaceFormat.colorSpace == request.second) {
            return true;
        }
        return false;
    }));
}


#endif
