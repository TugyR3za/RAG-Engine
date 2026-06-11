#include "Rag/RendererVK/Internal/VulkanVertexBuffer.h"

#include "Rag/Core/Log.h"

#include <cstddef>
#include <cstring>
#include <limits>

namespace rag::renderer::vk
{
    static_assert(sizeof(Vertex) == sizeof(f32) * 9);

    VkVertexInputBindingDescription Vertex::BindingDescription()
    {
        VkVertexInputBindingDescription description{};
        description.binding = 0;
        description.stride = sizeof(Vertex);
        description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return description;
    }

    std::array<VkVertexInputAttributeDescription, 3> Vertex::AttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 3> descriptions{};

        descriptions[0].binding = 0;
        descriptions[0].location = 0;
        descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[0].offset = static_cast<u32>(offsetof(Vertex, position));

        descriptions[1].binding = 0;
        descriptions[1].location = 1;
        descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[1].offset = static_cast<u32>(offsetof(Vertex, normal));

        descriptions[2].binding = 0;
        descriptions[2].location = 2;
        descriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[2].offset = static_cast<u32>(offsetof(Vertex, color));

        return descriptions;
    }

    VulkanVertexBuffer::VulkanVertexBuffer(VulkanDevice& device, std::span<const Vertex> vertices)
        : device_(device)
    {
        try
        {
            Create(vertices);
        }
        catch (...)
        {
            Cleanup();
            throw;
        }
    }

    VulkanVertexBuffer::~VulkanVertexBuffer()
    {
        Cleanup();
    }

    void VulkanVertexBuffer::Bind(VkCommandBuffer command_buffer) const
    {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &buffer_, &offset);
    }

    u32 VulkanVertexBuffer::VertexCount() const
    {
        return vertex_count_;
    }

    void VulkanVertexBuffer::Create(std::span<const Vertex> vertices)
    {
        if (vertices.empty())
        {
            throw VulkanError("Cannot create a Vulkan vertex buffer without vertices.");
        }

        if (vertices.size() > std::numeric_limits<u32>::max())
        {
            throw VulkanError("Vulkan vertex buffer exceeds the supported vertex count.");
        }

        const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(vertices.size_bytes());

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = buffer_size;
        buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        RAG_VK_CHECK(vkCreateBuffer(device_.Device(), &buffer_info, nullptr, &buffer_));

        VkMemoryRequirements memory_requirements{};
        vkGetBufferMemoryRequirements(device_.Device(), buffer_, &memory_requirements);

        VkMemoryAllocateInfo allocation_info{};
        allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation_info.allocationSize = memory_requirements.size;
        allocation_info.memoryTypeIndex = device_.FindMemoryType(
            memory_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        RAG_VK_CHECK(vkAllocateMemory(device_.Device(), &allocation_info, nullptr, &memory_));
        RAG_VK_CHECK(vkBindBufferMemory(device_.Device(), buffer_, memory_, 0));

        void* mapped_memory = nullptr;
        RAG_VK_CHECK(vkMapMemory(
            device_.Device(),
            memory_,
            0,
            buffer_size,
            0,
            &mapped_memory));
        std::memcpy(mapped_memory, vertices.data(), static_cast<std::size_t>(buffer_size));
        vkUnmapMemory(device_.Device(), memory_);

        vertex_count_ = static_cast<u32>(vertices.size());
        RAG_LOG_INFO(
            "Created host-visible Vulkan vertex buffer: vertices=",
            vertex_count_,
            ", bytes=",
            buffer_size,
            ".");
    }

    void VulkanVertexBuffer::Cleanup()
    {
        if (buffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_.Device(), buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
        }

        if (memory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_.Device(), memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }

        vertex_count_ = 0;
    }
}
