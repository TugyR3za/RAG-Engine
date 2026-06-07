#include "Rag/RendererVK/Internal/VulkanDevice.h"

#include "Rag/Core/Log.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>

namespace rag::renderer::vk
{
    namespace
    {
        constexpr const char* RequiredDeviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

        std::string VulkanVersionToString(u32 version)
        {
            std::ostringstream stream;
            stream << VK_VERSION_MAJOR(version)
                   << '.'
                   << VK_VERSION_MINOR(version)
                   << '.'
                   << VK_VERSION_PATCH(version);
            return stream.str();
        }

        const char* DeviceTypeName(VkPhysicalDeviceType type)
        {
            switch (type)
            {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                return "Integrated GPU";
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                return "Discrete GPU";
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                return "Virtual GPU";
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                return "CPU";
            default:
                return "Other";
            }
        }
    }

    bool QueueFamilyIndices::IsComplete() const
    {
        return graphics_family.has_value() && present_family.has_value();
    }

    VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface)
        : instance_(instance),
          surface_(surface)
    {
        PickPhysicalDevice();
        CreateLogicalDevice();
    }

    VulkanDevice::~VulkanDevice()
    {
        if (device_ != VK_NULL_HANDLE)
        {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
    }

    VkPhysicalDevice VulkanDevice::PhysicalDevice() const
    {
        return physical_device_;
    }

    VkDevice VulkanDevice::Device() const
    {
        return device_;
    }

    VkQueue VulkanDevice::GraphicsQueue() const
    {
        return graphics_queue_;
    }

    VkQueue VulkanDevice::PresentQueue() const
    {
        return present_queue_;
    }

    const QueueFamilyIndices& VulkanDevice::Families() const
    {
        return families_;
    }

    SwapchainSupportDetails VulkanDevice::QuerySwapchainSupport() const
    {
        return QuerySwapchainSupport(physical_device_);
    }

    const VkPhysicalDeviceProperties& VulkanDevice::Properties() const
    {
        return properties_;
    }

    void VulkanDevice::PickPhysicalDevice()
    {
        u32 device_count = 0;
        RAG_VK_CHECK(vkEnumeratePhysicalDevices(instance_, &device_count, nullptr));

        if (device_count == 0)
        {
            throw VulkanError("No Vulkan physical devices were found.");
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        RAG_VK_CHECK(vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()));

        i32 best_score = std::numeric_limits<i32>::min();
        VkPhysicalDevice best_device = VK_NULL_HANDLE;

        for (VkPhysicalDevice device : devices)
        {
            if (!IsDeviceSuitable(device))
            {
                continue;
            }

            const i32 score = ScoreDevice(device);
            if (score > best_score)
            {
                best_score = score;
                best_device = device;
            }
        }

        if (best_device == VK_NULL_HANDLE)
        {
            throw VulkanError("No suitable Vulkan physical device was found.");
        }

        physical_device_ = best_device;
        families_ = FindQueueFamilies(physical_device_);
        vkGetPhysicalDeviceProperties(physical_device_, &properties_);

        RAG_LOG_INFO(
            "Selected Vulkan device: ",
            properties_.deviceName,
            " (",
            DeviceTypeName(properties_.deviceType),
            ", Vulkan ",
            VulkanVersionToString(properties_.apiVersion),
            ")");
        RAG_LOG_INFO(
            "Vulkan queue families: graphics=",
            families_.graphics_family.value(),
            ", present=",
            families_.present_family.value());
    }

    void VulkanDevice::CreateLogicalDevice()
    {
        const std::set<u32> unique_queue_families = {
            families_.graphics_family.value(),
            families_.present_family.value(),
        };

        const f32 queue_priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
        queue_create_infos.reserve(unique_queue_families.size());

        for (u32 family : unique_queue_families)
        {
            VkDeviceQueueCreateInfo queue_create_info{};
            queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex = family;
            queue_create_info.queueCount = 1;
            queue_create_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }

        VkPhysicalDeviceFeatures features{};

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = static_cast<u32>(queue_create_infos.size());
        create_info.pQueueCreateInfos = queue_create_infos.data();
        create_info.pEnabledFeatures = &features;
        create_info.enabledExtensionCount = static_cast<u32>(
            sizeof(RequiredDeviceExtensions) / sizeof(RequiredDeviceExtensions[0]));
        create_info.ppEnabledExtensionNames = RequiredDeviceExtensions;

        RAG_VK_CHECK(vkCreateDevice(physical_device_, &create_info, nullptr, &device_));

        vkGetDeviceQueue(device_, families_.graphics_family.value(), 0, &graphics_queue_);
        vkGetDeviceQueue(device_, families_.present_family.value(), 0, &present_queue_);

        RAG_LOG_INFO("Created Vulkan logical device and graphics/present queues.");
    }

    bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device) const
    {
        const QueueFamilyIndices indices = FindQueueFamilies(device);
        if (!indices.IsComplete())
        {
            return false;
        }

        if (!RequiredDeviceExtensionsSupported(device))
        {
            return false;
        }

        const SwapchainSupportDetails support = QuerySwapchainSupport(device);
        return !support.formats.empty() && !support.present_modes.empty();
    }

    i32 VulkanDevice::ScoreDevice(VkPhysicalDevice device) const
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);

        i32 score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score += 1000;
        }

        score += static_cast<i32>(properties.limits.maxImageDimension2D);
        return score;
    }

    QueueFamilyIndices VulkanDevice::FindQueueFamilies(VkPhysicalDevice device) const
    {
        QueueFamilyIndices indices{};

        u32 family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);

        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());

        for (u32 index = 0; index < family_count; ++index)
        {
            const VkQueueFamilyProperties& family = families[index];

            if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                indices.graphics_family = index;
            }

            VkBool32 present_supported = VK_FALSE;
            RAG_VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_, &present_supported));
            if (present_supported == VK_TRUE)
            {
                indices.present_family = index;
            }

            if (indices.IsComplete())
            {
                break;
            }
        }

        return indices;
    }

    bool VulkanDevice::RequiredDeviceExtensionsSupported(VkPhysicalDevice device) const
    {
        u32 extension_count = 0;
        RAG_VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr));

        std::vector<VkExtensionProperties> extensions(extension_count);
        RAG_VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, extensions.data()));

        for (const char* required_extension : RequiredDeviceExtensions)
        {
            const bool found = std::any_of(extensions.begin(), extensions.end(), [required_extension](const VkExtensionProperties& extension) {
                return std::strcmp(extension.extensionName, required_extension) == 0;
            });

            if (!found)
            {
                return false;
            }
        }

        return true;
    }

    SwapchainSupportDetails VulkanDevice::QuerySwapchainSupport(VkPhysicalDevice device) const
    {
        SwapchainSupportDetails details{};

        RAG_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities));

        u32 format_count = 0;
        RAG_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, nullptr));
        if (format_count > 0)
        {
            details.formats.resize(format_count);
            RAG_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, details.formats.data()));
        }

        u32 present_mode_count = 0;
        RAG_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &present_mode_count, nullptr));
        if (present_mode_count > 0)
        {
            details.present_modes.resize(present_mode_count);
            RAG_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &present_mode_count, details.present_modes.data()));
        }

        return details;
    }
}
