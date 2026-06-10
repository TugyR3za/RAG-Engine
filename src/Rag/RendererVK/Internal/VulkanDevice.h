#pragma once

#include "Rag/RendererVK/Internal/VulkanCommon.h"

#include <optional>
#include <vector>

namespace rag::renderer::vk
{
    struct QueueFamilyIndices
    {
        std::optional<u32> graphics_family;
        std::optional<u32> present_family;

        [[nodiscard]] bool IsComplete() const;
    };

    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> present_modes;
    };

    class VulkanDevice final
    {
    public:
        VulkanDevice(VkInstance instance, VkSurfaceKHR surface);
        ~VulkanDevice();

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        [[nodiscard]] VkPhysicalDevice PhysicalDevice() const;
        [[nodiscard]] VkDevice Device() const;
        [[nodiscard]] VkQueue GraphicsQueue() const;
        [[nodiscard]] VkQueue PresentQueue() const;
        [[nodiscard]] const QueueFamilyIndices& Families() const;
        [[nodiscard]] SwapchainSupportDetails QuerySwapchainSupport() const;
        [[nodiscard]] const VkPhysicalDeviceProperties& Properties() const;
        [[nodiscard]] u32 FindMemoryType(u32 type_filter, VkMemoryPropertyFlags properties) const;

    private:
        void PickPhysicalDevice();
        void CreateLogicalDevice();

        [[nodiscard]] bool IsDeviceSuitable(VkPhysicalDevice device) const;
        [[nodiscard]] i32 ScoreDevice(VkPhysicalDevice device) const;
        [[nodiscard]] QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
        [[nodiscard]] bool RequiredDeviceExtensionsSupported(VkPhysicalDevice device) const;
        [[nodiscard]] SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device) const;

        VkInstance instance_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties properties_{};
        VkDevice device_ = VK_NULL_HANDLE;
        VkQueue graphics_queue_ = VK_NULL_HANDLE;
        VkQueue present_queue_ = VK_NULL_HANDLE;
        QueueFamilyIndices families_{};
    };
}
