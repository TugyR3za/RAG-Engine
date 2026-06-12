#pragma once

#include "Rag/RendererVK/Internal/VulkanCommon.h"

namespace rag::renderer::vk
{
    // Draws the RAG_SHADOW_DEBUG overlay: a corner quad that visualizes the raw
    // shadow depth map. Renders inside the main (swapchain) render pass on top of
    // the scene, with depth test disabled. Sampling uses descriptor binding 2.
    class VulkanShadowDebugPipeline final
    {
    public:
        VulkanShadowDebugPipeline(
            VkDevice device,
            VkRenderPass render_pass,
            VkDescriptorSetLayout descriptor_set_layout);
        ~VulkanShadowDebugPipeline();

        VulkanShadowDebugPipeline(const VulkanShadowDebugPipeline&) = delete;
        VulkanShadowDebugPipeline& operator=(const VulkanShadowDebugPipeline&) = delete;

        void Bind(VkCommandBuffer command_buffer, VkExtent2D extent) const;
        void BindDescriptorSet(VkCommandBuffer command_buffer, VkDescriptorSet descriptor_set) const;
        void Draw(VkCommandBuffer command_buffer) const;

    private:
        void Create(VkRenderPass render_pass, VkDescriptorSetLayout descriptor_set_layout);
        void Cleanup();

        VkDevice device_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };
}
