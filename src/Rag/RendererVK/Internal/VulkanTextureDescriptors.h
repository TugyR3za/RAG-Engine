#pragma once

#include "Rag/RendererVK/Internal/VulkanDevice.h"

#include <span>
#include <vector>

namespace rag::renderer::vk
{
    class VulkanTexture;

    // Owns descriptor set 1: one combined-image-sampler set per immutable
    // texture. Per-frame camera/light/shadow state remains in descriptor set 0.
    class VulkanTextureDescriptors final
    {
    public:
        VulkanTextureDescriptors(
            VulkanDevice& device,
            std::span<VulkanTexture* const> textures);
        ~VulkanTextureDescriptors();

        VulkanTextureDescriptors(const VulkanTextureDescriptors&) = delete;
        VulkanTextureDescriptors& operator=(const VulkanTextureDescriptors&) = delete;

        [[nodiscard]] VkDescriptorSetLayout DescriptorSetLayout() const;
        [[nodiscard]] VkDescriptorSet DescriptorSet(u32 texture_index) const;
        [[nodiscard]] u32 Count() const;

    private:
        void CreateDescriptorSetLayout();
        void CreateDescriptorPool(u32 texture_count);
        void AllocateAndWrite(std::span<VulkanTexture* const> textures);
        void Cleanup();

        VulkanDevice& device_;
        VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptor_sets_;
    };
}
