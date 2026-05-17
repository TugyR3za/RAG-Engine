#pragma once

#include "Rag/RendererVK/Internal/VulkanDevice.h"

#include <array>
#include <vector>

namespace rag::renderer::vk
{
    class VulkanSwapchain final
    {
    public:
        VulkanSwapchain(VulkanDevice& device, VkSurfaceKHR surface, u32 width, u32 height);
        ~VulkanSwapchain();

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        void Recreate(u32 width, u32 height);

        void BeginRenderPass(VkCommandBuffer command_buffer, u32 image_index, const std::array<f32, 4>& clear_color) const;
        void EndRenderPass(VkCommandBuffer command_buffer) const;

        [[nodiscard]] VkSwapchainKHR Handle() const;
        [[nodiscard]] VkExtent2D Extent() const;
        [[nodiscard]] VkFormat ImageFormat() const;
        [[nodiscard]] u32 ImageCount() const;

    private:
        void Create(u32 width, u32 height);
        void Cleanup();
        void CreateImageViews();
        void CreateRenderPass();
        void CreateFramebuffers();

        [[nodiscard]] VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
        [[nodiscard]] VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& present_modes) const;
        [[nodiscard]] VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, u32 width, u32 height) const;

        VulkanDevice& device_;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
        VkFormat image_format_ = VK_FORMAT_UNDEFINED;
        VkExtent2D extent_{};
        std::vector<VkImage> images_;
        std::vector<VkImageView> image_views_;
        VkRenderPass render_pass_ = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> framebuffers_;
    };
}
