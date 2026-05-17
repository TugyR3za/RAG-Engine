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
        VkSemaphore render_finished = VK_NULL_HANDLE;
        VkFence in_flight = VK_NULL_HANDLE;
    };

    class VulkanFrameResources final
    {
    public:
        VulkanFrameResources(VkDevice device, u32 graphics_queue_family, u32 frames_in_flight);
        ~VulkanFrameResources();

        VulkanFrameResources(const VulkanFrameResources&) = delete;
        VulkanFrameResources& operator=(const VulkanFrameResources&) = delete;

        [[nodiscard]] VulkanFrameData& CurrentFrame();
        [[nodiscard]] u32 CurrentFrameIndex() const;
        [[nodiscard]] u32 FrameCount() const;
        void Advance();

    private:
        void Cleanup();

        VkDevice device_ = VK_NULL_HANDLE;
        u32 current_frame_ = 0;
        std::vector<VulkanFrameData> frames_;
    };
}
