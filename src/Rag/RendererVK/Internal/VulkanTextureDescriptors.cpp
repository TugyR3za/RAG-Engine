#include "Rag/RendererVK/Internal/VulkanTextureDescriptors.h"

#include "Rag/Core/Log.h"
#include "Rag/RendererVK/Internal/VulkanTexture.h"

namespace rag::renderer::vk
{
    VulkanTextureDescriptors::VulkanTextureDescriptors(
        VulkanDevice& device,
        std::span<VulkanTexture* const> textures)
        : device_(device)
    {
        if (textures.empty())
        {
            throw VulkanError("Vulkan texture descriptor registry requires at least one texture.");
        }

        try
        {
            CreateDescriptorSetLayout();
            CreateDescriptorPool(static_cast<u32>(textures.size()));
            AllocateAndWrite(textures);
        }
        catch (...)
        {
            Cleanup();
            throw;
        }

        RAG_LOG_INFO(
            "Created Vulkan per-texture descriptor sets: textures=",
            textures.size(),
            ", set=1, binding=0.");
    }

    VulkanTextureDescriptors::~VulkanTextureDescriptors()
    {
        Cleanup();
    }

    VkDescriptorSetLayout VulkanTextureDescriptors::DescriptorSetLayout() const
    {
        return descriptor_set_layout_;
    }

    VkDescriptorSet VulkanTextureDescriptors::DescriptorSet(u32 texture_index) const
    {
        if (texture_index >= descriptor_sets_.size())
        {
            throw VulkanError("Vulkan texture descriptor index is outside the texture registry.");
        }
        return descriptor_sets_[texture_index];
    }

    u32 VulkanTextureDescriptors::Count() const
    {
        return static_cast<u32>(descriptor_sets_.size());
    }

    void VulkanTextureDescriptors::CreateDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding texture_binding{};
        texture_binding.binding = 0;
        texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texture_binding.descriptorCount = 1;
        texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        create_info.bindingCount = 1;
        create_info.pBindings = &texture_binding;
        RAG_VK_CHECK(vkCreateDescriptorSetLayout(
            device_.Device(),
            &create_info,
            nullptr,
            &descriptor_set_layout_));
    }

    void VulkanTextureDescriptors::CreateDescriptorPool(u32 texture_count)
    {
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = texture_count;

        VkDescriptorPoolCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        create_info.maxSets = texture_count;
        create_info.poolSizeCount = 1;
        create_info.pPoolSizes = &pool_size;
        RAG_VK_CHECK(vkCreateDescriptorPool(
            device_.Device(),
            &create_info,
            nullptr,
            &descriptor_pool_));
    }

    void VulkanTextureDescriptors::AllocateAndWrite(
        std::span<VulkanTexture* const> textures)
    {
        std::vector<VkDescriptorSetLayout> layouts(
            textures.size(),
            descriptor_set_layout_);
        descriptor_sets_.resize(textures.size(), VK_NULL_HANDLE);

        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = descriptor_pool_;
        allocate_info.descriptorSetCount = static_cast<u32>(layouts.size());
        allocate_info.pSetLayouts = layouts.data();
        RAG_VK_CHECK(vkAllocateDescriptorSets(
            device_.Device(),
            &allocate_info,
            descriptor_sets_.data()));

        for (std::size_t index = 0; index < textures.size(); ++index)
        {
            if (textures[index] == nullptr)
            {
                throw VulkanError("Vulkan texture descriptor registry contains a null texture.");
            }

            VkDescriptorImageInfo image_info{};
            image_info.sampler = textures[index]->Sampler();
            image_info.imageView = textures[index]->ImageView();
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptor_sets_[index];
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &image_info;
            vkUpdateDescriptorSets(device_.Device(), 1, &write, 0, nullptr);
        }
    }

    void VulkanTextureDescriptors::Cleanup()
    {
        descriptor_sets_.clear();

        if (descriptor_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device_.Device(), descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }

        if (descriptor_set_layout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(
                device_.Device(),
                descriptor_set_layout_,
                nullptr);
            descriptor_set_layout_ = VK_NULL_HANDLE;
        }
    }
}
