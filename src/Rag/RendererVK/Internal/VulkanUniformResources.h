#pragma once

#include "Rag/Core/Math.h"
#include "Rag/RendererVK/Internal/VulkanDevice.h"

#include <vector>

namespace rag::renderer::vk
{
    struct alignas(16) UniformBufferObject
    {
        math::Mat4 view;
        math::Mat4 projection;
    };

    class VulkanUniformResources final
    {
    public:
        VulkanUniformResources(VulkanDevice& device, u32 frames_in_flight);
        ~VulkanUniformResources();

        VulkanUniformResources(const VulkanUniformResources&) = delete;
        VulkanUniformResources& operator=(const VulkanUniformResources&) = delete;

        void Update(u32 frame_index, const UniformBufferObject& uniform_data);

        [[nodiscard]] VkDescriptorSetLayout DescriptorSetLayout() const;
        [[nodiscard]] VkDescriptorSet DescriptorSet(u32 frame_index) const;

    private:
        struct FrameUniform
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            void* mapped_memory = nullptr;
            VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        };

        void CreateDescriptorSetLayout();
        void CreateUniformBuffers(u32 frames_in_flight);
        void CreateDescriptorPool(u32 frames_in_flight);
        void AllocateDescriptorSets();
        void Cleanup();

        VulkanDevice& device_;
        VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
        std::vector<FrameUniform> frames_;
    };
}
