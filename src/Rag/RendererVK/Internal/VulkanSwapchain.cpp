#include "Rag/RendererVK/Internal/VulkanSwapchain.h"

#include "Rag/Core/Log.h"

#include <algorithm>
#include <limits>

namespace rag::renderer::vk
{
    namespace
    {
        const char* FormatName(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_B8G8R8A8_SRGB:
                return "VK_FORMAT_B8G8R8A8_SRGB";
            case VK_FORMAT_B8G8R8A8_UNORM:
                return "VK_FORMAT_B8G8R8A8_UNORM";
            case VK_FORMAT_R8G8B8A8_SRGB:
                return "VK_FORMAT_R8G8B8A8_SRGB";
            case VK_FORMAT_R8G8B8A8_UNORM:
                return "VK_FORMAT_R8G8B8A8_UNORM";
            default:
                return "VK_FORMAT_OTHER";
            }
        }

        const char* PresentModeName(VkPresentModeKHR present_mode)
        {
            switch (present_mode)
            {
            case VK_PRESENT_MODE_IMMEDIATE_KHR:
                return "VK_PRESENT_MODE_IMMEDIATE_KHR";
            case VK_PRESENT_MODE_MAILBOX_KHR:
                return "VK_PRESENT_MODE_MAILBOX_KHR";
            case VK_PRESENT_MODE_FIFO_KHR:
                return "VK_PRESENT_MODE_FIFO_KHR";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
                return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
            default:
                return "VK_PRESENT_MODE_OTHER";
            }
        }
    }

    VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, VkSurfaceKHR surface, u32 width, u32 height)
        : device_(device),
          surface_(surface)
    {
        Create(width, height);
    }

    VulkanSwapchain::~VulkanSwapchain()
    {
        Cleanup();
    }

    void VulkanSwapchain::Recreate(u32 width, u32 height)
    {
        RAG_LOG_INFO("Recreating Vulkan swapchain for ", width, "x", height, ".");
        Cleanup();
        Create(width, height);
    }

    void VulkanSwapchain::BeginRenderPass(
        VkCommandBuffer command_buffer,
        u32 image_index,
        const std::array<f32, 4>& clear_color) const
    {
        VkClearValue clear_value{};
        clear_value.color.float32[0] = clear_color[0];
        clear_value.color.float32[1] = clear_color[1];
        clear_value.color.float32[2] = clear_color[2];
        clear_value.color.float32[3] = clear_color[3];

        VkRenderPassBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin_info.renderPass = render_pass_;
        begin_info.framebuffer = framebuffers_[image_index];
        begin_info.renderArea.offset = {0, 0};
        begin_info.renderArea.extent = extent_;
        begin_info.clearValueCount = 1;
        begin_info.pClearValues = &clear_value;

        vkCmdBeginRenderPass(command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    }

    void VulkanSwapchain::EndRenderPass(VkCommandBuffer command_buffer) const
    {
        vkCmdEndRenderPass(command_buffer);
    }

    VkSwapchainKHR VulkanSwapchain::Handle() const
    {
        return swapchain_;
    }

    VkRenderPass VulkanSwapchain::RenderPass() const
    {
        return render_pass_;
    }

    VkExtent2D VulkanSwapchain::Extent() const
    {
        return extent_;
    }

    VkFormat VulkanSwapchain::ImageFormat() const
    {
        return image_format_;
    }

    u32 VulkanSwapchain::ImageCount() const
    {
        return static_cast<u32>(images_.size());
    }

    void VulkanSwapchain::Create(u32 width, u32 height)
    {
        if (width == 0 || height == 0)
        {
            throw VulkanError("Cannot create a Vulkan swapchain with zero extent.");
        }

        const SwapchainSupportDetails support = device_.QuerySwapchainSupport();
        if (support.formats.empty() || support.present_modes.empty())
        {
            throw VulkanError("Selected Vulkan device does not provide usable swapchain formats or present modes.");
        }

        const VkSurfaceFormatKHR surface_format = ChooseSurfaceFormat(support.formats);
        const VkPresentModeKHR present_mode = ChoosePresentMode(support.present_modes);
        extent_ = ChooseExtent(support.capabilities, width, height);
        if (extent_.width == 0 || extent_.height == 0)
        {
            throw VulkanError("Cannot create a Vulkan swapchain because the resolved surface extent is zero.");
        }

        u32 image_count = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0)
        {
            image_count = std::min(image_count, support.capabilities.maxImageCount);
        }

        VkSwapchainCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface_;
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = extent_;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        const QueueFamilyIndices& indices = device_.Families();
        const u32 queue_family_indices[] = {
            indices.graphics_family.value(),
            indices.present_family.value(),
        };

        if (indices.graphics_family != indices.present_family)
        {
            create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices = queue_family_indices;
        }
        else
        {
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        create_info.preTransform = support.capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        create_info.presentMode = present_mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = VK_NULL_HANDLE;

        RAG_VK_CHECK(vkCreateSwapchainKHR(device_.Device(), &create_info, nullptr, &swapchain_));

        RAG_VK_CHECK(vkGetSwapchainImagesKHR(device_.Device(), swapchain_, &image_count, nullptr));
        images_.resize(image_count);
        RAG_VK_CHECK(vkGetSwapchainImagesKHR(device_.Device(), swapchain_, &image_count, images_.data()));

        image_format_ = surface_format.format;

        CreateImageViews();
        CreateRenderPass();
        CreateFramebuffers();

        RAG_LOG_INFO(
            "Created Vulkan swapchain: ",
            extent_.width,
            "x",
            extent_.height,
            ", images=",
            images_.size(),
            ", format=",
            FormatName(image_format_),
            ", present=",
            PresentModeName(present_mode));
    }

    void VulkanSwapchain::Cleanup()
    {
        const VkDevice device = device_.Device();

        for (VkFramebuffer framebuffer : framebuffers_)
        {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        framebuffers_.clear();

        if (render_pass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, render_pass_, nullptr);
            render_pass_ = VK_NULL_HANDLE;
        }

        for (VkImageView image_view : image_views_)
        {
            vkDestroyImageView(device, image_view, nullptr);
        }
        image_views_.clear();
        images_.clear();

        if (swapchain_ != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void VulkanSwapchain::CreateImageViews()
    {
        image_views_.resize(images_.size());

        for (std::size_t index = 0; index < images_.size(); ++index)
        {
            VkImageViewCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            create_info.image = images_[index];
            create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            create_info.format = image_format_;
            create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            create_info.subresourceRange.baseMipLevel = 0;
            create_info.subresourceRange.levelCount = 1;
            create_info.subresourceRange.baseArrayLayer = 0;
            create_info.subresourceRange.layerCount = 1;

            RAG_VK_CHECK(vkCreateImageView(device_.Device(), &create_info, nullptr, &image_views_[index]));
        }
    }

    void VulkanSwapchain::CreateRenderPass()
    {
        VkAttachmentDescription color_attachment{};
        color_attachment.format = image_format_;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_ref{};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        create_info.attachmentCount = 1;
        create_info.pAttachments = &color_attachment;
        create_info.subpassCount = 1;
        create_info.pSubpasses = &subpass;
        create_info.dependencyCount = 1;
        create_info.pDependencies = &dependency;

        RAG_VK_CHECK(vkCreateRenderPass(device_.Device(), &create_info, nullptr, &render_pass_));
    }

    void VulkanSwapchain::CreateFramebuffers()
    {
        framebuffers_.resize(image_views_.size());

        for (std::size_t index = 0; index < image_views_.size(); ++index)
        {
            const VkImageView attachments[] = {image_views_[index]};

            VkFramebufferCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            create_info.renderPass = render_pass_;
            create_info.attachmentCount = 1;
            create_info.pAttachments = attachments;
            create_info.width = extent_.width;
            create_info.height = extent_.height;
            create_info.layers = 1;

            RAG_VK_CHECK(vkCreateFramebuffer(device_.Device(), &create_info, nullptr, &framebuffers_[index]));
        }
    }

    VkSurfaceFormatKHR VulkanSwapchain::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
    {
        const auto preferred = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
            return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                   format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });

        return preferred != formats.end() ? *preferred : formats.front();
    }

    VkPresentModeKHR VulkanSwapchain::ChoosePresentMode(const std::vector<VkPresentModeKHR>& present_modes) const
    {
        const auto fifo = std::find(present_modes.begin(), present_modes.end(), VK_PRESENT_MODE_FIFO_KHR);
        return fifo != present_modes.end() ? VK_PRESENT_MODE_FIFO_KHR : present_modes.front();
    }

    VkExtent2D VulkanSwapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, u32 width, u32 height) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<u32>::max())
        {
            return capabilities.currentExtent;
        }

        VkExtent2D actual_extent{};
        actual_extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actual_extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actual_extent;
    }
}
