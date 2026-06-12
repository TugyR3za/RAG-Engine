#pragma once

#include "Rag/RendererVK/Internal/VulkanDevice.h"

#include <vector>

namespace rag::renderer::vk
{
    // Depth-only render target for directional shadow mapping. Owns one depth
    // image (plus view and framebuffer) per frame in flight so the shadow pass
    // and the lit pass that samples it never race across frames, a shared
    // depth-only render pass, and two samplers: a depth-comparison sampler for
    // the PCF shadow test and a plain sampler for the debug overlay.
    //
    // The shadow map is a fixed size and is intentionally independent of the
    // swapchain, so it is created once and never recreated on window resize.
    class VulkanShadowMap final
    {
    public:
        VulkanShadowMap(VulkanDevice& device, u32 frame_count, u32 size);
        ~VulkanShadowMap();

        VulkanShadowMap(const VulkanShadowMap&) = delete;
        VulkanShadowMap& operator=(const VulkanShadowMap&) = delete;

        // Begins the depth render pass for the given frame, clearing depth to 1.0.
        void BeginPass(VkCommandBuffer command_buffer, u32 frame_index) const;
        void EndPass(VkCommandBuffer command_buffer) const;

        [[nodiscard]] VkRenderPass RenderPass() const;
        [[nodiscard]] VkExtent2D Extent() const;
        [[nodiscard]] VkImageView ImageView(u32 frame_index) const;
        [[nodiscard]] VkSampler CompareSampler() const;
        [[nodiscard]] VkSampler DepthSampler() const;
        [[nodiscard]] u32 FrameCount() const;

    private:
        struct FrameImage
        {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
        };

        void CreateRenderPass();
        void CreateSamplers();
        void CreateFrameImages(u32 frame_count);
        void Cleanup();

        VulkanDevice& device_;
        VkFormat format_ = VK_FORMAT_D32_SFLOAT;
        VkExtent2D extent_{};
        VkRenderPass render_pass_ = VK_NULL_HANDLE;
        VkSampler compare_sampler_ = VK_NULL_HANDLE;
        VkSampler depth_sampler_ = VK_NULL_HANDLE;
        std::vector<FrameImage> frames_;
    };
}
