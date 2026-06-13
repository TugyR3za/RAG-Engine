#pragma once

#include "Rag/RendererVK/Internal/VulkanIndexBuffer.h"
#include "Rag/RendererVK/Internal/VulkanVertexBuffer.h"

#include <span>

namespace rag::renderer::vk
{
    // One drawable mesh: a vertex buffer + index buffer pair uploaded once at
    // creation. The renderer keeps a small registry of these and binds the one
    // each RenderObject's MeshHandle selects.
    class VulkanMesh final
    {
    public:
        VulkanMesh(VulkanDevice& device, std::span<const Vertex> vertices, std::span<const u32> indices);

        VulkanMesh(const VulkanMesh&) = delete;
        VulkanMesh& operator=(const VulkanMesh&) = delete;

        // Binds both buffers for subsequent vkCmdDrawIndexed calls.
        void Bind(VkCommandBuffer command_buffer) const;

        [[nodiscard]] u32 IndexCount() const;

    private:
        VulkanVertexBuffer vertex_buffer_;
        VulkanIndexBuffer index_buffer_;
    };
}
