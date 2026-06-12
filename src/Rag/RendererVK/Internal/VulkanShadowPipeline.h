#pragma once

#include "Rag/Core/Math.h"
#include "Rag/RendererVK/Internal/VulkanCommon.h"

namespace rag::renderer::vk
{
    // Depth-only pipeline for the shadow pass. It transforms vertices by the
    // light-space view-projection (read from the shared UBO) times the model
    // push constant and writes depth only; there is no color attachment.
    class VulkanShadowPipeline final
    {
    public:
        VulkanShadowPipeline(
            VkDevice device,
            VkRenderPass shadow_render_pass,
            VkDescriptorSetLayout descriptor_set_layout);
        ~VulkanShadowPipeline();

        VulkanShadowPipeline(const VulkanShadowPipeline&) = delete;
        VulkanShadowPipeline& operator=(const VulkanShadowPipeline&) = delete;

        void Bind(VkCommandBuffer command_buffer, VkExtent2D extent) const;
        void BindDescriptorSet(VkCommandBuffer command_buffer, VkDescriptorSet descriptor_set) const;
        void PushModelMatrix(VkCommandBuffer command_buffer, const math::Mat4& model) const;

    private:
        void Create(VkRenderPass shadow_render_pass, VkDescriptorSetLayout descriptor_set_layout);
        void Cleanup();

        VkDevice device_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };
}
