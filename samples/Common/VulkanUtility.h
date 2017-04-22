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
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#define NOMINMAX
#pragma warning(push, 0)
#include <vulkan/vulkan.hpp>
#pragma warning(pop)

#define Q_IMPL(X) #X
#define QUOTE(X) Q_IMPL(X)

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

    void destory()
    {
        if (!mDetached) {
            mDeleter(mInstance);
            mDetached = true;
        }
    }

    ~VulkanHolder()
    {
        destory();
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
void CheckLayers(std::vector<const char*> & names)
{
    auto extensions = vk::enumerateInstanceLayerProperties();
    for (const auto & requestedExt : names) {
        if (extensions.cend() == std::find_if(extensions.cbegin(), extensions.cend(), [&requestedExt](const vk::LayerProperties & prop) {
            return (0 == std::strcmp(prop.layerName, requestedExt));
        })) {
            throw std::runtime_error(std::string("Layer is not supported ") + requestedExt);
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
bool CheckFormat(const std::vector<vk::SurfaceFormatKHR> & formats, const std::pair<vk::Format, vk::ColorSpaceKHR> & request)
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

// from https://github.com/GameTechDev/IntroductionToVulkan/blob/master/Project/Common/Tools.cpp
inline
std::vector<char> GetBinaryFileContents(const std::string & filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (file.fail()) {
        std::cout << "Could not open \"" << filename << "\" file!" << std::endl;
        return std::vector<char>{};
    }

    std::streampos begin, end;
    begin = file.tellg();
    file.seekg(0, std::ios::end);
    end = file.tellg();

    std::vector<char> result(static_cast<size_t>(end - begin));
    file.seekg(0, std::ios::beg);
    file.read(&result[0], end - begin);
    file.close();

    return result;
}


inline 
std::vector<char> GetBinaryShaderFromSourceFile(const std::string & filename)
{
    const std::string cmd = "%VULKAN_SDK%/Bin/glslangValidator.exe -V -o ./tmp_shader.spv \"" + filename + "\"";
    if (0 != std::system(cmd.c_str())) {
        std::cout << "Failed to compile \"" << filename << "\"!" << std::endl;
        return std::vector<char>{};
    }
    return GetBinaryFileContents("./tmp_shader.spv");
}


struct RgbaImage
{
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> pixels;
};

#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4996)
#endif
inline
RgbaImage LoadBmpImage(const std::string & filename)
{
    RgbaImage image = { 0, 0, {} };

    FILE* f = fopen(filename.c_str(), "rb");

    if (f == NULL) {
        std::cout << "Invalid input file" << std::endl;
        return image;
    }

    unsigned char info[54];
    fread(info, sizeof(unsigned char), 54, f); // read the 54-byte header

    // extract image height and width from header
    image.width  = *reinterpret_cast<uint32_t*>(&info[18]);
    image.height = *reinterpret_cast<uint32_t*>(&info[22]);

    int row_padded = (image.width * 3 + 3) & (~3);
    image.pixels.resize(image.width * image.height * 4);
    uint8_t* dstLine = &image.pixels[0];

    std::vector<uint8_t> row(row_padded);
    for (uint32_t i = 0; i < image.height; i++, dstLine += image.width * 4) {
        fread(&row[0], sizeof(unsigned char), row_padded, f);

        int iSrc = 0;
        int iDst = 0;
        for (uint32_t j = 0; j < image.width; ++j, iSrc += 3, iDst += 4) {
            // Convert (B, G, R) to (R, G, B)
            dstLine[iDst + 0] = row[iSrc + 2];
            dstLine[iDst + 1] = row[iSrc + 1];
            dstLine[iDst + 2] = row[iSrc + 0];
            dstLine[iDst + 3] = 255;
        }
    }
    fclose(f);
    return image;
}
#ifdef _MSC_VER
# pragma warning(pop)
#endif

#endif
