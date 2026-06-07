#include "Rag/RendererVK/VulkanRenderer.h"

#include "Rag/Core/Log.h"
#include "Rag/RendererVK/Internal/VulkanDevice.h"
#include "Rag/RendererVK/Internal/VulkanFrameResources.h"
#include "Rag/RendererVK/Internal/VulkanInstance.h"
#include "Rag/RendererVK/Internal/VulkanSurface.h"
#include "Rag/RendererVK/Internal/VulkanSwapchain.h"

#include <chrono>
#include <limits>
#include <vector>

namespace rag::renderer::vk
{
    class VulkanRenderer::Impl final
    {
    public:
        explicit Impl(const RendererDesc& desc)
            : desc_(desc)
        {
            if (desc_.window == nullptr)
            {
                throw VulkanError("VulkanRenderer requires a valid platform window.");
            }

            if (desc_.frames_in_flight == 0)
            {
                desc_.frames_in_flight = 2;
            }

            instance_ = std::make_unique<VulkanInstance>(desc_.application_name, desc_.enable_validation);
            surface_ = std::make_unique<VulkanSurface>(instance_->Get(), desc_.window->GetNativeHandle());
            device_ = std::make_unique<VulkanDevice>(instance_->Get(), surface_->Get());
            swapchain_ = std::make_unique<VulkanSwapchain>(
                *device_,
                surface_->Get(),
                desc_.window->Width(),
                desc_.window->Height());
            frames_ = std::make_unique<VulkanFrameResources>(
                device_->Device(),
                device_->Families().graphics_family.value(),
                desc_.frames_in_flight,
                swapchain_->ImageCount());

            image_in_flight_.assign(swapchain_->ImageCount(), VK_NULL_HANDLE);
            UpdateStats();

            RAG_LOG_INFO(
                "Initialized Vulkan renderer with ",
                stats_.swapchain_image_count,
                " swapchain images.");
        }

        ~Impl()
        {
            WaitIdle();
            RAG_LOG_INFO("Shut down Vulkan renderer.");
        }

        void RenderFrame(const RenderFrameContext& context)
        {
            const auto frame_start = std::chrono::steady_clock::now();
            f64 frame_interval_seconds = 0.0;
            if (has_previous_frame_time_)
            {
                frame_interval_seconds = std::chrono::duration<f64>(frame_start - previous_frame_time_).count();
                stats_.last_frame_ms = frame_interval_seconds * 1000.0;
            }
            previous_frame_time_ = frame_start;
            has_previous_frame_time_ = true;

            if (desc_.window->Width() == 0 || desc_.window->Height() == 0)
            {
                surface_suspended_ = true;
                return;
            }

            if (resize_requested_)
            {
                if (!RecreateSwapchain())
                {
                    return;
                }
            }

            VulkanFrameData& frame = frames_->CurrentFrame();
            RAG_VK_CHECK(vkWaitForFences(
                device_->Device(),
                1,
                &frame.in_flight,
                VK_TRUE,
                std::numeric_limits<u64>::max()));

            u32 image_index = 0;
            const VkResult acquire_result = vkAcquireNextImageKHR(
                device_->Device(),
                swapchain_->Handle(),
                std::numeric_limits<u64>::max(),
                frame.image_available,
                VK_NULL_HANDLE,
                &image_index);

            if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                resize_requested_ = true;
                (void)RecreateSwapchain();
                return;
            }

            if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR)
            {
                CheckVk(acquire_result, "vkAcquireNextImageKHR");
            }

            if (image_in_flight_[image_index] != VK_NULL_HANDLE)
            {
                RAG_VK_CHECK(vkWaitForFences(
                    device_->Device(),
                    1,
                    &image_in_flight_[image_index],
                    VK_TRUE,
                    std::numeric_limits<u64>::max()));
            }

            image_in_flight_[image_index] = frame.in_flight;

            RAG_VK_CHECK(vkResetFences(device_->Device(), 1, &frame.in_flight));
            RAG_VK_CHECK(vkResetCommandPool(device_->Device(), frame.command_pool, 0));

            RecordCommandBuffer(frame.command_buffer, image_index, context);

            const VkSemaphore wait_semaphores[] = {frame.image_available};
            const VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            const VkSemaphore render_finished = frames_->RenderFinishedSemaphore(image_index);
            const VkSemaphore signal_semaphores[] = {render_finished};

            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = wait_semaphores;
            submit_info.pWaitDstStageMask = wait_stages;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &frame.command_buffer;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = signal_semaphores;

            RAG_VK_CHECK(vkQueueSubmit(device_->GraphicsQueue(), 1, &submit_info, frame.in_flight));

            const VkSwapchainKHR swapchains[] = {swapchain_->Handle()};
            VkPresentInfoKHR present_info{};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = signal_semaphores;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = swapchains;
            present_info.pImageIndices = &image_index;

            const VkResult present_result = vkQueuePresentKHR(device_->PresentQueue(), &present_info);
            if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR || resize_requested_)
            {
                resize_requested_ = true;
                (void)RecreateSwapchain();
            }
            else
            {
                CheckVk(present_result, "vkQueuePresentKHR");
            }

            frames_->Advance();
            ++stats_.frame_index;
            UpdateFrameDiagnostics(frame_interval_seconds);
        }

        void OnWindowResized(u32 width, u32 height)
        {
            if (width == 0 || height == 0)
            {
                surface_suspended_ = true;
                resize_requested_ = true;
                RAG_LOG_INFO("Vulkan surface suspended because the window is minimized or has zero extent.");
                return;
            }

            if (surface_suspended_)
            {
                RAG_LOG_INFO("Vulkan surface restored at ", width, "x", height, ".");
            }

            surface_suspended_ = false;
            resize_requested_ = true;
        }

        void WaitIdle()
        {
            if (device_ != nullptr && device_->Device() != VK_NULL_HANDLE)
            {
                const VkResult result = vkDeviceWaitIdle(device_->Device());
                if (result != VK_SUCCESS)
                {
                    RAG_LOG_ERROR("vkDeviceWaitIdle failed during Vulkan shutdown: ", VkResultToString(result));
                }
            }
        }

        [[nodiscard]] RendererStats GetStats() const
        {
            return stats_;
        }

    private:
        void RecordCommandBuffer(
            VkCommandBuffer command_buffer,
            u32 image_index,
            const RenderFrameContext& context)
        {
            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            RAG_VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
            swapchain_->BeginRenderPass(command_buffer, image_index, context.clear_color);
            swapchain_->EndRenderPass(command_buffer);
            RAG_VK_CHECK(vkEndCommandBuffer(command_buffer));
        }

        bool RecreateSwapchain()
        {
            if (desc_.window->Width() == 0 || desc_.window->Height() == 0)
            {
                surface_suspended_ = true;
                return false;
            }

            WaitIdle();
            swapchain_->Recreate(desc_.window->Width(), desc_.window->Height());
            frames_->RecreateRenderFinishedSemaphores(swapchain_->ImageCount());
            image_in_flight_.assign(swapchain_->ImageCount(), VK_NULL_HANDLE);
            UpdateStats();
            resize_requested_ = false;
            surface_suspended_ = false;
            return true;
        }

        void UpdateStats()
        {
            const VkExtent2D extent = swapchain_->Extent();
            stats_.backend = RendererBackend::Vulkan;
            stats_.swapchain_width = extent.width;
            stats_.swapchain_height = extent.height;
            stats_.swapchain_image_count = swapchain_->ImageCount();
            stats_.active_gpu_name = device_->Properties().deviceName;
        }

        void UpdateFrameDiagnostics(f64 frame_interval_seconds)
        {
            if (frame_interval_seconds <= 0.0)
            {
                return;
            }

            diagnostics_elapsed_seconds_ += frame_interval_seconds;
            ++diagnostics_frame_count_;

            if (diagnostics_elapsed_seconds_ < 1.0)
            {
                return;
            }

            stats_.average_fps = static_cast<f64>(diagnostics_frame_count_) / diagnostics_elapsed_seconds_;

            RAG_LOG_INFO(
                "Vulkan frame diagnostics: gpu=\"",
                stats_.active_gpu_name,
                "\", fps=",
                stats_.average_fps,
                ", frame_ms=",
                stats_.last_frame_ms,
                ", swapchain=",
                stats_.swapchain_width,
                "x",
                stats_.swapchain_height,
                ", images=",
                stats_.swapchain_image_count);

            diagnostics_elapsed_seconds_ = 0.0;
            diagnostics_frame_count_ = 0;
        }

        RendererDesc desc_{};
        std::unique_ptr<VulkanInstance> instance_;
        std::unique_ptr<VulkanSurface> surface_;
        std::unique_ptr<VulkanDevice> device_;
        std::unique_ptr<VulkanSwapchain> swapchain_;
        std::unique_ptr<VulkanFrameResources> frames_;
        std::vector<VkFence> image_in_flight_;
        RendererStats stats_{};
        std::chrono::steady_clock::time_point previous_frame_time_{};
        f64 diagnostics_elapsed_seconds_ = 0.0;
        u32 diagnostics_frame_count_ = 0;
        bool resize_requested_ = false;
        bool has_previous_frame_time_ = false;
        bool surface_suspended_ = false;
    };

    VulkanRenderer::VulkanRenderer(const RendererDesc& desc)
        : impl_(std::make_unique<Impl>(desc))
    {
    }

    VulkanRenderer::~VulkanRenderer() = default;

    void VulkanRenderer::RenderFrame(const RenderFrameContext& context)
    {
        impl_->RenderFrame(context);
    }

    void VulkanRenderer::OnWindowResized(u32 width, u32 height)
    {
        impl_->OnWindowResized(width, height);
    }

    void VulkanRenderer::WaitIdle()
    {
        impl_->WaitIdle();
    }

    RendererBackend VulkanRenderer::Backend() const
    {
        return RendererBackend::Vulkan;
    }

    RendererStats VulkanRenderer::GetStats() const
    {
        return impl_->GetStats();
    }

    RendererPtr CreateVulkanRenderer(const RendererDesc& desc)
    {
        return std::make_unique<VulkanRenderer>(desc);
    }
}
