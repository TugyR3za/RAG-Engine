#pragma once

#include "Rag/RendererVK/Internal/VulkanCommon.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace rag::renderer::vk
{
    class VulkanGraphicsPipeline final
    {
    public:
        VulkanGraphicsPipeline(
            VkDevice device,
            VkRenderPass render_pass,
            VkDescriptorSetLayout descriptor_set_layout);
        ~VulkanGraphicsPipeline();

        VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
        VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;

        void Bind(VkCommandBuffer command_buffer, VkExtent2D extent) const;
        void BindDescriptorSet(VkCommandBuffer command_buffer, VkDescriptorSet descriptor_set) const;

    private:
        void Create(VkRenderPass render_pass, VkDescriptorSetLayout descriptor_set_layout);
        void Cleanup();

        [[nodiscard]] static std::filesystem::path ResolveShaderPath(std::string_view filename);
        [[nodiscard]] static std::vector<u32> LoadSpirv(const std::filesystem::path& path);
        [[nodiscard]] VkShaderModule CreateShaderModule(const std::vector<u32>& code) const;

        VkDevice device_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };
}
