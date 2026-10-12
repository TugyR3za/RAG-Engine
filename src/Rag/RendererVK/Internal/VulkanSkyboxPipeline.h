#pragma once

#include "Rag/RendererVK/Internal/VulkanCommon.h"

namespace rag::renderer::vk
{
    // Full-screen background at far depth, using the active camera's rotation.
    class VulkanSkyboxPipeline final
    {
    public:
        VulkanSkyboxPipeline(VkDevice device, VkRenderPass render_pass,
            VkDescriptorSetLayout frame_layout, VkDescriptorSetLayout texture_layout);
        ~VulkanSkyboxPipeline();
        VulkanSkyboxPipeline(const VulkanSkyboxPipeline&) = delete;
        VulkanSkyboxPipeline& operator=(const VulkanSkyboxPipeline&) = delete;

        void Bind(VkCommandBuffer command_buffer, VkExtent2D extent) const;
        void BindDescriptorSets(VkCommandBuffer command_buffer,
            VkDescriptorSet frame_set, VkDescriptorSet texture_set) const;
        void Draw(VkCommandBuffer command_buffer) const;

    private:
        void Create(VkRenderPass render_pass, VkDescriptorSetLayout frame_layout,
            VkDescriptorSetLayout texture_layout);
        void Cleanup();

        VkDevice device_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };
}
