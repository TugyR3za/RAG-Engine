#include "Rag/RendererVK/Internal/VulkanUniformResources.h"

#include "Rag/Core/Log.h"

#include <cstring>

namespace rag::renderer::vk
{
    static_assert(sizeof(math::Mat4) == sizeof(f32) * 16);
    static_assert(sizeof(UniformBufferObject) == sizeof(math::Mat4) * 3);

    VulkanUniformResources::VulkanUniformResources(VulkanDevice& device, u32 frames_in_flight)
        : device_(device)
    {
        if (frames_in_flight == 0)
        {
            throw VulkanError("Vulkan uniform frame count must be greater than zero.");
        }

        try
        {
            CreateDescriptorSetLayout();
            CreateUniformBuffers(frames_in_flight);
            CreateDescriptorPool(frames_in_flight);
            AllocateDescriptorSets();
        }
        catch (...)
        {
            Cleanup();
            throw;
        }

        RAG_LOG_INFO(
            "Created Vulkan per-frame uniform resources: frames=",
            frames_in_flight,
            ", bytes_per_frame=",
            sizeof(UniformBufferObject),
            ".");
    }

    VulkanUniformResources::~VulkanUniformResources()
    {
        Cleanup();
    }

    void VulkanUniformResources::Update(u32 frame_index, const UniformBufferObject& uniform_data)
    {
        if (frame_index >= frames_.size())
        {
            throw VulkanError("Vulkan uniform frame index is outside the per-frame buffer array.");
        }

        std::memcpy(frames_[frame_index].mapped_memory, &uniform_data, sizeof(uniform_data));
    }

    VkDescriptorSetLayout VulkanUniformResources::DescriptorSetLayout() const
    {
        return descriptor_set_layout_;
    }

    VkDescriptorSet VulkanUniformResources::DescriptorSet(u32 frame_index) const
    {
        if (frame_index >= frames_.size())
        {
            throw VulkanError("Vulkan descriptor frame index is outside the per-frame descriptor array.");
        }

        return frames_[frame_index].descriptor_set;
    }

    void VulkanUniformResources::CreateDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding uniform_binding{};
        uniform_binding.binding = 0;
        uniform_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniform_binding.descriptorCount = 1;
        uniform_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        create_info.bindingCount = 1;
        create_info.pBindings = &uniform_binding;

        RAG_VK_CHECK(vkCreateDescriptorSetLayout(
            device_.Device(),
            &create_info,
            nullptr,
            &descriptor_set_layout_));
    }

    void VulkanUniformResources::CreateUniformBuffers(u32 frames_in_flight)
    {
        frames_.resize(frames_in_flight);

        for (FrameUniform& frame : frames_)
        {
            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = sizeof(UniformBufferObject);
            buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            RAG_VK_CHECK(vkCreateBuffer(device_.Device(), &buffer_info, nullptr, &frame.buffer));

            VkMemoryRequirements memory_requirements{};
            vkGetBufferMemoryRequirements(device_.Device(), frame.buffer, &memory_requirements);

            VkMemoryAllocateInfo allocation_info{};
            allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation_info.allocationSize = memory_requirements.size;
            allocation_info.memoryTypeIndex = device_.FindMemoryType(
                memory_requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            RAG_VK_CHECK(vkAllocateMemory(device_.Device(), &allocation_info, nullptr, &frame.memory));
            RAG_VK_CHECK(vkBindBufferMemory(device_.Device(), frame.buffer, frame.memory, 0));
            RAG_VK_CHECK(vkMapMemory(
                device_.Device(),
                frame.memory,
                0,
                sizeof(UniformBufferObject),
                0,
                &frame.mapped_memory));
        }
    }

    void VulkanUniformResources::CreateDescriptorPool(u32 frames_in_flight)
    {
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = frames_in_flight;

        VkDescriptorPoolCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        create_info.maxSets = frames_in_flight;
        create_info.poolSizeCount = 1;
        create_info.pPoolSizes = &pool_size;

        RAG_VK_CHECK(vkCreateDescriptorPool(
            device_.Device(),
            &create_info,
            nullptr,
            &descriptor_pool_));
    }

    void VulkanUniformResources::AllocateDescriptorSets()
    {
        std::vector<VkDescriptorSetLayout> layouts(frames_.size(), descriptor_set_layout_);
        std::vector<VkDescriptorSet> descriptor_sets(frames_.size(), VK_NULL_HANDLE);

        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = descriptor_pool_;
        allocate_info.descriptorSetCount = static_cast<u32>(layouts.size());
        allocate_info.pSetLayouts = layouts.data();

        RAG_VK_CHECK(vkAllocateDescriptorSets(
            device_.Device(),
            &allocate_info,
            descriptor_sets.data()));

        for (std::size_t index = 0; index < frames_.size(); ++index)
        {
            FrameUniform& frame = frames_[index];
            frame.descriptor_set = descriptor_sets[index];

            VkDescriptorBufferInfo buffer_info{};
            buffer_info.buffer = frame.buffer;
            buffer_info.offset = 0;
            buffer_info.range = sizeof(UniformBufferObject);

            VkWriteDescriptorSet descriptor_write{};
            descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_write.dstSet = frame.descriptor_set;
            descriptor_write.dstBinding = 0;
            descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptor_write.descriptorCount = 1;
            descriptor_write.pBufferInfo = &buffer_info;

            vkUpdateDescriptorSets(device_.Device(), 1, &descriptor_write, 0, nullptr);
        }
    }

    void VulkanUniformResources::Cleanup()
    {
        if (descriptor_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device_.Device(), descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }

        for (FrameUniform& frame : frames_)
        {
            if (frame.mapped_memory != nullptr)
            {
                vkUnmapMemory(device_.Device(), frame.memory);
                frame.mapped_memory = nullptr;
            }

            if (frame.buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(device_.Device(), frame.buffer, nullptr);
                frame.buffer = VK_NULL_HANDLE;
            }

            if (frame.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device_.Device(), frame.memory, nullptr);
                frame.memory = VK_NULL_HANDLE;
            }

            frame.descriptor_set = VK_NULL_HANDLE;
        }
        frames_.clear();

        if (descriptor_set_layout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device_.Device(), descriptor_set_layout_, nullptr);
            descriptor_set_layout_ = VK_NULL_HANDLE;
        }
    }
}
