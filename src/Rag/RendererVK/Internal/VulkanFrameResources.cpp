#include "Rag/RendererVK/Internal/VulkanFrameResources.h"

#include <utility>

namespace rag::renderer::vk
{
    VulkanFrameResources::VulkanFrameResources(
        VkDevice device,
        u32 graphics_queue_family,
        u32 frames_in_flight,
        u32 swapchain_image_count)
        : device_(device)
    {
        if (frames_in_flight == 0)
        {
            throw VulkanError("Vulkan frame resource count must be greater than zero.");
        }

        if (swapchain_image_count == 0)
        {
            throw VulkanError("Vulkan swapchain image count must be greater than zero.");
        }

        try
        {
            CreateFrameResources(graphics_queue_family, frames_in_flight);
            render_finished_by_image_ = CreateSemaphores(swapchain_image_count);
        }
        catch (...)
        {
            Cleanup();
            throw;
        }
    }

    VulkanFrameResources::~VulkanFrameResources()
    {
        Cleanup();
    }

    VulkanFrameData& VulkanFrameResources::CurrentFrame()
    {
        return frames_[current_frame_];
    }

    VkSemaphore VulkanFrameResources::RenderFinishedSemaphore(u32 image_index) const
    {
        if (image_index >= render_finished_by_image_.size())
        {
            throw VulkanError("Vulkan swapchain image index is outside the render-finished semaphore array.");
        }

        return render_finished_by_image_[image_index];
    }

    u32 VulkanFrameResources::CurrentFrameIndex() const
    {
        return current_frame_;
    }

    u32 VulkanFrameResources::FrameCount() const
    {
        return static_cast<u32>(frames_.size());
    }

    void VulkanFrameResources::RecreateRenderFinishedSemaphores(u32 swapchain_image_count)
    {
        if (swapchain_image_count == 0)
        {
            throw VulkanError("Vulkan swapchain image count must be greater than zero.");
        }

        std::vector<VkSemaphore> new_semaphores = CreateSemaphores(swapchain_image_count);
        DestroySemaphores(render_finished_by_image_);
        render_finished_by_image_ = std::move(new_semaphores);
    }

    void VulkanFrameResources::Advance()
    {
        current_frame_ = (current_frame_ + 1) % static_cast<u32>(frames_.size());
    }

    void VulkanFrameResources::CreateFrameResources(u32 graphics_queue_family, u32 frames_in_flight)
    {
        frames_.resize(frames_in_flight);

        for (VulkanFrameData& frame : frames_)
        {
            VkCommandPoolCreateInfo pool_create_info{};
            pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool_create_info.queueFamilyIndex = graphics_queue_family;
            RAG_VK_CHECK(vkCreateCommandPool(device_, &pool_create_info, nullptr, &frame.command_pool));

            VkCommandBufferAllocateInfo allocate_info{};
            allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool = frame.command_pool;
            allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_info.commandBufferCount = 1;
            RAG_VK_CHECK(vkAllocateCommandBuffers(device_, &allocate_info, &frame.command_buffer));

            VkSemaphoreCreateInfo semaphore_create_info{};
            semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            RAG_VK_CHECK(vkCreateSemaphore(device_, &semaphore_create_info, nullptr, &frame.image_available));

            VkFenceCreateInfo fence_create_info{};
            fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            RAG_VK_CHECK(vkCreateFence(device_, &fence_create_info, nullptr, &frame.in_flight));
        }
    }

    std::vector<VkSemaphore> VulkanFrameResources::CreateSemaphores(u32 count) const
    {
        std::vector<VkSemaphore> semaphores(count, VK_NULL_HANDLE);

        VkSemaphoreCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        try
        {
            for (VkSemaphore& semaphore : semaphores)
            {
                RAG_VK_CHECK(vkCreateSemaphore(device_, &create_info, nullptr, &semaphore));
            }
        }
        catch (...)
        {
            for (VkSemaphore semaphore : semaphores)
            {
                if (semaphore != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(device_, semaphore, nullptr);
                }
            }
            throw;
        }

        return semaphores;
    }

    void VulkanFrameResources::DestroySemaphores(std::vector<VkSemaphore>& semaphores)
    {
        for (VkSemaphore semaphore : semaphores)
        {
            if (semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
        }

        semaphores.clear();
    }

    void VulkanFrameResources::Cleanup()
    {
        DestroySemaphores(render_finished_by_image_);

        for (VulkanFrameData& frame : frames_)
        {
            if (frame.in_flight != VK_NULL_HANDLE)
            {
                vkDestroyFence(device_, frame.in_flight, nullptr);
                frame.in_flight = VK_NULL_HANDLE;
            }

            if (frame.image_available != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device_, frame.image_available, nullptr);
                frame.image_available = VK_NULL_HANDLE;
            }

            if (frame.command_pool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(device_, frame.command_pool, nullptr);
                frame.command_pool = VK_NULL_HANDLE;
                frame.command_buffer = VK_NULL_HANDLE;
            }
        }

        frames_.clear();
    }
}
