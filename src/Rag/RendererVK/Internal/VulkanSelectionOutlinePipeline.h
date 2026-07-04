#pragma once

#include "Rag/Core/Math.h"
#include "Rag/RendererVK/Internal/VulkanCommon.h"

namespace rag::renderer::vk
{
    // Draws the editor's selection highlight: the 12 edges of a world-space,
    // axis-aligned bounding box as a colored line list. It reads the shared camera
    // UBO (set 0, binding 0) and takes the box center/extents/color as a push
    // constant, so no vertex buffer is bound. Depth testing is disabled so the box
    // stays visible on top of the shaded scene. Rendered inside the main swapchain
    // render pass, after the scene meshes and before the ImGui overlay.
    class VulkanSelectionOutlinePipeline final
    {
    public:
        VulkanSelectionOutlinePipeline(
            VkDevice device,
            VkRenderPass render_pass,
            VkDescriptorSetLayout frame_descriptor_set_layout);
        ~VulkanSelectionOutlinePipeline();

        VulkanSelectionOutlinePipeline(const VulkanSelectionOutlinePipeline&) = delete;
        VulkanSelectionOutlinePipeline& operator=(const VulkanSelectionOutlinePipeline&) = delete;

        void Bind(VkCommandBuffer command_buffer, VkExtent2D extent) const;
        void BindDescriptorSet(VkCommandBuffer command_buffer, VkDescriptorSet descriptor_set) const;

        // Pushes the box (world-space center, half-extents and line color) and
        // emits the 24 line-list vertices that make up its 12 edges.
        void Draw(
            VkCommandBuffer command_buffer,
            const math::Vec3& center,
            const math::Vec3& extents,
            const math::Vec3& color) const;

    private:
        void Create(VkRenderPass render_pass, VkDescriptorSetLayout frame_descriptor_set_layout);
        void Cleanup();

        VkDevice device_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };
}
