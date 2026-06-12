#include "Rag/RendererVK/Internal/VulkanShadowMap.h"

#include "Rag/Core/Log.h"

#include <array>

namespace rag::renderer::vk
{
    VulkanShadowMap::VulkanShadowMap(VulkanDevice& device, u32 frame_count, u32 size)
        : device_(device)
    {
        if (frame_count == 0)
        {
            throw VulkanError("Vulkan shadow map frame count must be greater than zero.");
        }

        if (size == 0)
        {
            throw VulkanError("Vulkan shadow map size must be greater than zero.");
        }

        extent_ = VkExtent2D{size, size};

        try
        {
            CreateRenderPass();
            CreateSamplers();
            CreateFrameImages(frame_count);
        }
        catch (...)
        {
            Cleanup();
            throw;
        }

        RAG_LOG_INFO(
            "Created Vulkan directional shadow map: ",
            extent_.width,
            "x",
            extent_.height,
            ", format=VK_FORMAT_D32_SFLOAT, frames=",
            frame_count,
            ".");
    }

    VulkanShadowMap::~VulkanShadowMap()
    {
        Cleanup();
    }

    void VulkanShadowMap::BeginPass(VkCommandBuffer command_buffer, u32 frame_index) const
    {
        if (frame_index >= frames_.size())
        {
            throw VulkanError("Vulkan shadow map frame index is outside the per-frame image array.");
        }

        VkClearValue clear_value{};
        clear_value.depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin_info.renderPass = render_pass_;
        begin_info.framebuffer = frames_[frame_index].framebuffer;
        begin_info.renderArea.offset = {0, 0};
        begin_info.renderArea.extent = extent_;
        begin_info.clearValueCount = 1;
        begin_info.pClearValues = &clear_value;

        vkCmdBeginRenderPass(command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    }

    void VulkanShadowMap::EndPass(VkCommandBuffer command_buffer) const
    {
        vkCmdEndRenderPass(command_buffer);
    }

    VkRenderPass VulkanShadowMap::RenderPass() const
    {
        return render_pass_;
    }

    VkExtent2D VulkanShadowMap::Extent() const
    {
        return extent_;
    }

    VkImageView VulkanShadowMap::ImageView(u32 frame_index) const
    {
        if (frame_index >= frames_.size())
        {
            throw VulkanError("Vulkan shadow map frame index is outside the per-frame image array.");
        }

        return frames_[frame_index].view;
    }

    VkSampler VulkanShadowMap::CompareSampler() const
    {
        return compare_sampler_;
    }

    VkSampler VulkanShadowMap::DepthSampler() const
    {
        return depth_sampler_;
    }

    u32 VulkanShadowMap::FrameCount() const
    {
        return static_cast<u32>(frames_.size());
    }

    void VulkanShadowMap::CreateRenderPass()
    {
        VkAttachmentDescription depth_attachment{};
        depth_attachment.format = format_;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // The render pass leaves the image ready to be sampled by the lit pass.
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference depth_attachment_ref{};
        depth_attachment_ref.attachment = 0;
        depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;

        // Order the new depth writes after any prior sampling of a reused image,
        // and make the finished depth buffer available/visible (and transitioned
        // to SHADER_READ_ONLY) for the lit pass's fragment-shader reads.
        constexpr VkPipelineStageFlags depth_stages =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = depth_stages;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = depth_stages;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        create_info.attachmentCount = 1;
        create_info.pAttachments = &depth_attachment;
        create_info.subpassCount = 1;
        create_info.pSubpasses = &subpass;
        create_info.dependencyCount = static_cast<u32>(dependencies.size());
        create_info.pDependencies = dependencies.data();

        RAG_VK_CHECK(vkCreateRenderPass(device_.Device(), &create_info, nullptr, &render_pass_));
    }

    void VulkanShadowMap::CreateSamplers()
    {
        // Hardware 2x2 PCF needs linear filtering of the depth format; fall back
        // to point sampling if the device does not advertise it.
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(device_.PhysicalDevice(), format_, &format_properties);
        const bool linear_filter_supported =
            (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
        const VkFilter compare_filter = linear_filter_supported ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

        VkSamplerCreateInfo compare_info{};
        compare_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        compare_info.magFilter = compare_filter;
        compare_info.minFilter = compare_filter;
        compare_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        // Clamp to a white (depth = 1.0) border so fragments outside the light
        // frustum compare as lit instead of picking up spurious shadows.
        compare_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        compare_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        compare_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        compare_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        compare_info.compareEnable = VK_TRUE;
        compare_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        compare_info.minLod = 0.0f;
        compare_info.maxLod = 0.0f;
        RAG_VK_CHECK(vkCreateSampler(device_.Device(), &compare_info, nullptr, &compare_sampler_));

        VkSamplerCreateInfo depth_info{};
        depth_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        depth_info.magFilter = VK_FILTER_NEAREST;
        depth_info.minFilter = VK_FILTER_NEAREST;
        depth_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        depth_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        depth_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        depth_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        depth_info.compareEnable = VK_FALSE;
        depth_info.minLod = 0.0f;
        depth_info.maxLod = 0.0f;
        RAG_VK_CHECK(vkCreateSampler(device_.Device(), &depth_info, nullptr, &depth_sampler_));
    }

    void VulkanShadowMap::CreateFrameImages(u32 frame_count)
    {
        frames_.resize(frame_count);

        for (FrameImage& frame : frames_)
        {
            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.extent.width = extent_.width;
            image_info.extent.height = extent_.height;
            image_info.extent.depth = 1;
            image_info.mipLevels = 1;
            image_info.arrayLayers = 1;
            image_info.format = format_;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.usage =
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;
            image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            RAG_VK_CHECK(vkCreateImage(device_.Device(), &image_info, nullptr, &frame.image));

            VkMemoryRequirements memory_requirements{};
            vkGetImageMemoryRequirements(device_.Device(), frame.image, &memory_requirements);

            VkMemoryAllocateInfo allocation_info{};
            allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation_info.allocationSize = memory_requirements.size;
            allocation_info.memoryTypeIndex = device_.FindMemoryType(
                memory_requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            RAG_VK_CHECK(vkAllocateMemory(device_.Device(), &allocation_info, nullptr, &frame.memory));
            RAG_VK_CHECK(vkBindImageMemory(device_.Device(), frame.image, frame.memory, 0));

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = frame.image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format_;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;
            RAG_VK_CHECK(vkCreateImageView(device_.Device(), &view_info, nullptr, &frame.view));

            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = render_pass_;
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments = &frame.view;
            framebuffer_info.width = extent_.width;
            framebuffer_info.height = extent_.height;
            framebuffer_info.layers = 1;
            RAG_VK_CHECK(vkCreateFramebuffer(device_.Device(), &framebuffer_info, nullptr, &frame.framebuffer));
        }
    }

    void VulkanShadowMap::Cleanup()
    {
        const VkDevice device = device_.Device();

        for (FrameImage& frame : frames_)
        {
            if (frame.framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(device, frame.framebuffer, nullptr);
                frame.framebuffer = VK_NULL_HANDLE;
            }

            if (frame.view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device, frame.view, nullptr);
                frame.view = VK_NULL_HANDLE;
            }

            if (frame.image != VK_NULL_HANDLE)
            {
                vkDestroyImage(device, frame.image, nullptr);
                frame.image = VK_NULL_HANDLE;
            }

            if (frame.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, frame.memory, nullptr);
                frame.memory = VK_NULL_HANDLE;
            }
        }
        frames_.clear();

        if (depth_sampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, depth_sampler_, nullptr);
            depth_sampler_ = VK_NULL_HANDLE;
        }

        if (compare_sampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, compare_sampler_, nullptr);
            compare_sampler_ = VK_NULL_HANDLE;
        }

        if (render_pass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, render_pass_, nullptr);
            render_pass_ = VK_NULL_HANDLE;
        }
    }
}
