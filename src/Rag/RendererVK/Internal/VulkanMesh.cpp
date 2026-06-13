#include "Rag/RendererVK/Internal/VulkanMesh.h"

namespace rag::renderer::vk
{
    VulkanMesh::VulkanMesh(
        VulkanDevice& device,
        std::span<const Vertex> vertices,
        std::span<const u32> indices)
        : vertex_buffer_(device, vertices),
          index_buffer_(device, indices)
    {
    }

    void VulkanMesh::Bind(VkCommandBuffer command_buffer) const
    {
        vertex_buffer_.Bind(command_buffer);
        index_buffer_.Bind(command_buffer);
    }

    u32 VulkanMesh::IndexCount() const
    {
        return index_buffer_.IndexCount();
    }
}
