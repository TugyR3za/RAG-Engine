#pragma once

#include "Rag/Core/Math.h"
#include "Rag/RendererVK/Internal/VulkanCommon.h"

namespace rag::renderer::vk
{
    class VulkanGraphicsPipeline final
    {
    public:
        VulkanGraphicsPipeline(
            VkDevice device,
            VkRenderPass render_pass,
            VkDescriptorSetLayout frame_descriptor_set_layout,
            VkDescriptorSetLayout texture_descriptor_set_layout);
        ~VulkanGraphicsPipeline();

        VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
        VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;

        void Bind(VkCommandBuffer command_buffer, VkExtent2D extent) const;
        void BindDescriptorSets(
            VkCommandBuffer command_buffer,
            VkDescriptorSet frame_descriptor_set,
            VkDescriptorSet texture_descriptor_set) const;
        void PushModelMatrix(VkCommandBuffer command_buffer, const math::Mat4& model) const;

    private:
        void Create(
            VkRenderPass render_pass,
            VkDescriptorSetLayout frame_descriptor_set_layout,
            VkDescriptorSetLayout texture_descriptor_set_layout);
        void Cleanup();

        VkDevice device_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };
}
