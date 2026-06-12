#include "Rag/RendererVK/Internal/VulkanUniformResources.h"

#include "Rag/Core/Log.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace rag::renderer::vk
{
    static_assert(sizeof(math::Mat4) == sizeof(f32) * 16);
    static_assert(sizeof(UniformBufferObject) == 256);
    static_assert(offsetof(UniformBufferObject, view) == 0);
    static_assert(offsetof(UniformBufferObject, projection) == 64);
    static_assert(offsetof(UniformBufferObject, light_space) == 128);
    static_assert(offsetof(UniformBufferObject, light_direction) == 192);
    static_assert(offsetof(UniformBufferObject, light_direction_padding) == 204);
    static_assert(offsetof(UniformBufferObject, light_color) == 208);
    static_assert(offsetof(UniformBufferObject, light_intensity) == 220);
    static_assert(offsetof(UniformBufferObject, camera_position) == 224);
    static_assert(offsetof(UniformBufferObject, camera_position_padding) == 236);
    static_assert(offsetof(UniformBufferObject, shadow_pcf_kernel_radius) == 240);
    static_assert(offsetof(UniformBufferObject, shadow_ambient_floor) == 244);
    static_assert(offsetof(UniformBufferObject, shadow_tuning_padding_0) == 248);
    static_assert(offsetof(UniformBufferObject, shadow_tuning_padding_1) == 252);

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

    void VulkanUniformResources::BindShadowMap(
        u32 frame_index,
        VkImageView shadow_view,
        VkSampler compare_sampler,
        VkSampler depth_sampler)
    {
        if (frame_index >= frames_.size())
        {
            throw VulkanError("Vulkan shadow bind frame index is outside the per-frame descriptor array.");
        }

        const VkDescriptorSet descriptor_set = frames_[frame_index].descriptor_set;

        VkDescriptorImageInfo compare_info{};
        compare_info.sampler = compare_sampler;
        compare_info.imageView = shadow_view;
        compare_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo depth_info{};
        depth_info.sampler = depth_sampler;
        depth_info.imageView = shadow_view;
        depth_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptor_set;
        writes[0].dstBinding = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &compare_info;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptor_set;
        writes[1].dstBinding = 2;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &depth_info;

        vkUpdateDescriptorSets(
            device_.Device(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0,
            nullptr);
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
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

        // binding 0: camera/light/light-space uniform block (read by both stages).
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        // binding 1: shadow map sampled through a depth-comparison sampler
        // (sampler2DShadow) for the lit pass's PCF shadow test.
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // binding 2: same shadow image through a plain sampler so the
        // RAG_SHADOW_DEBUG overlay can read raw depth. Always declared so the
        // layout is identical regardless of the debug toggle.
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        create_info.bindingCount = static_cast<u32>(bindings.size());
        create_info.pBindings = bindings.data();

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
        std::array<VkDescriptorPoolSize, 2> pool_sizes{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = frames_in_flight;
        // Two combined image samplers per frame: the comparison sampler for the
        // lit pass and the plain sampler for the debug overlay.
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = frames_in_flight * 2;

        VkDescriptorPoolCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        create_info.maxSets = frames_in_flight;
        create_info.poolSizeCount = static_cast<u32>(pool_sizes.size());
        create_info.pPoolSizes = pool_sizes.data();

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
