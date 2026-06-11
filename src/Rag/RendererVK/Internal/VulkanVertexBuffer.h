#pragma once

#include "Rag/RendererVK/Internal/VulkanDevice.h"

#include <array>
#include <span>

namespace rag::renderer::vk
{
    struct Vertex
    {
        std::array<f32, 3> position;
        std::array<f32, 3> normal;
        std::array<f32, 3> color;

        [[nodiscard]] static VkVertexInputBindingDescription BindingDescription();
        [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 3> AttributeDescriptions();
    };

    class VulkanVertexBuffer final
    {
    public:
        VulkanVertexBuffer(VulkanDevice& device, std::span<const Vertex> vertices);
        ~VulkanVertexBuffer();

        VulkanVertexBuffer(const VulkanVertexBuffer&) = delete;
        VulkanVertexBuffer& operator=(const VulkanVertexBuffer&) = delete;

        void Bind(VkCommandBuffer command_buffer) const;

        [[nodiscard]] u32 VertexCount() const;

    private:
        void Create(std::span<const Vertex> vertices);
        void Cleanup();

        VulkanDevice& device_;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        VkDeviceMemory memory_ = VK_NULL_HANDLE;
        u32 vertex_count_ = 0;
    };
}
