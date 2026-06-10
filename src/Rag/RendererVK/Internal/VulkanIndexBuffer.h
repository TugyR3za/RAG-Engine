#pragma once

#include "Rag/RendererVK/Internal/VulkanDevice.h"

#include <span>

namespace rag::renderer::vk
{
    class VulkanIndexBuffer final
    {
    public:
        VulkanIndexBuffer(VulkanDevice& device, std::span<const u32> indices);
        ~VulkanIndexBuffer();

        VulkanIndexBuffer(const VulkanIndexBuffer&) = delete;
        VulkanIndexBuffer& operator=(const VulkanIndexBuffer&) = delete;

        void Bind(VkCommandBuffer command_buffer) const;

        [[nodiscard]] u32 IndexCount() const;

    private:
        void Create(std::span<const u32> indices);
        void Cleanup();

        VulkanDevice& device_;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        VkDeviceMemory memory_ = VK_NULL_HANDLE;
        u32 index_count_ = 0;
    };
}
