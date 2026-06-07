#pragma once

#include "Rag/RendererVK/Internal/VulkanCommon.h"

#include <vector>

namespace rag::renderer::vk
{
    struct VulkanFrameData
    {
        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkFence in_flight = VK_NULL_HANDLE;
    };

    class VulkanFrameResources final
    {
    public:
        VulkanFrameResources(
            VkDevice device,
            u32 graphics_queue_family,
            u32 frames_in_flight,
            u32 swapchain_image_count);
        ~VulkanFrameResources();

        VulkanFrameResources(const VulkanFrameResources&) = delete;
        VulkanFrameResources& operator=(const VulkanFrameResources&) = delete;

        [[nodiscard]] VulkanFrameData& CurrentFrame();
        [[nodiscard]] VkSemaphore RenderFinishedSemaphore(u32 image_index) const;
        [[nodiscard]] u32 CurrentFrameIndex() const;
        [[nodiscard]] u32 FrameCount() const;
        void RecreateRenderFinishedSemaphores(u32 swapchain_image_count);
        void Advance();

    private:
        void CreateFrameResources(u32 graphics_queue_family, u32 frames_in_flight);
        [[nodiscard]] std::vector<VkSemaphore> CreateSemaphores(u32 count) const;
        void DestroySemaphores(std::vector<VkSemaphore>& semaphores);
        void Cleanup();

        VkDevice device_ = VK_NULL_HANDLE;
        u32 current_frame_ = 0;
        std::vector<VulkanFrameData> frames_;
        std::vector<VkSemaphore> render_finished_by_image_;
    };
}
