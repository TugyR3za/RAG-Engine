#include "Rag/RendererVK/Internal/VulkanIndexBuffer.h"

#include "Rag/Core/Log.h"

#include <cstring>
#include <limits>

namespace rag::renderer::vk
{
    static_assert(sizeof(u32) == 4);

    VulkanIndexBuffer::VulkanIndexBuffer(VulkanDevice& device, std::span<const u32> indices)
        : device_(device)
    {
        try
        {
            Create(indices);
        }
        catch (...)
        {
            Cleanup();
            throw;
        }
    }

    VulkanIndexBuffer::~VulkanIndexBuffer()
    {
        Cleanup();
    }

    void VulkanIndexBuffer::Bind(VkCommandBuffer command_buffer) const
    {
        vkCmdBindIndexBuffer(command_buffer, buffer_, 0, VK_INDEX_TYPE_UINT32);
    }

    u32 VulkanIndexBuffer::IndexCount() const
    {
        return index_count_;
    }

    void VulkanIndexBuffer::Create(std::span<const u32> indices)
    {
        if (indices.empty())
        {
            throw VulkanError("Cannot create a Vulkan index buffer without indices.");
        }

        if (indices.size() > std::numeric_limits<u32>::max())
        {
            throw VulkanError("Vulkan index buffer exceeds the supported index count.");
        }

        const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(indices.size_bytes());

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = buffer_size;
        buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
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
        std::memcpy(mapped_memory, indices.data(), static_cast<std::size_t>(buffer_size));
        vkUnmapMemory(device_.Device(), memory_);

        index_count_ = static_cast<u32>(indices.size());
        RAG_LOG_INFO(
            "Created Phase 2D host-visible Vulkan index buffer: indices=",
            index_count_,
            ", bytes=",
            buffer_size,
            ".");
    }

    void VulkanIndexBuffer::Cleanup()
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

        index_count_ = 0;
    }
}
