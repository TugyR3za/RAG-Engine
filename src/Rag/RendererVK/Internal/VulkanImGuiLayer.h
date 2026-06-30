#pragma once

#include "Rag/RendererVK/Internal/VulkanCommon.h"

namespace rag::renderer::vk
{
    // Plain Vulkan handles the ImGui layer needs to stand up its backends. The
    // renderer fills this in from its instance/device/swapchain wrappers so the
    // layer stays decoupled from them.
    struct VulkanImGuiInit
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        u32 graphics_queue_family = 0;
        VkQueue graphics_queue = VK_NULL_HANDLE;
        VkRenderPass render_pass = VK_NULL_HANDLE;
        u32 min_image_count = 2;
        u32 image_count = 2;
        void* window_handle = nullptr; // HWND
    };

    // Owns the Dear ImGui context plus its official Win32 + Vulkan backends and
    // renders the UI on top of the scene inside the renderer's existing render
    // pass. Every ImGui/backend header is confined to the .cpp, so this header
    // stays a thin Vulkan-handle interface and nothing else needs ImGui to build.
    class VulkanImGuiLayer final
    {
    public:
        explicit VulkanImGuiLayer(const VulkanImGuiInit& init);
        ~VulkanImGuiLayer();

        VulkanImGuiLayer(const VulkanImGuiLayer&) = delete;
        VulkanImGuiLayer& operator=(const VulkanImGuiLayer&) = delete;

        // Begins a new ImGui frame (both backends + core). Call once per rendered
        // frame before issuing any ImGui widget calls.
        void BeginFrame();

        // Finalizes the ImGui frame and records its draw data into command_buffer.
        // Must be called inside an active render pass, after the scene is drawn so
        // the UI lands on top.
        void RenderInto(VkCommandBuffer command_buffer);

        // Keeps the backend in sync after the swapchain (and its render pass) are
        // recreated on resize/fullscreen toggles.
        void OnSwapchainRecreated(VkRenderPass render_pass, u32 image_count);

        // Forwards a raw native Win32 message to the ImGui backend. Returns true
        // if ImGui consumed it. Shaped to match platform::NativeMessageHook.
        bool ProcessWin32Message(void* native_window, u32 message, u64 w_param, i64 l_param, i64& out_result);

    private:
        void CreateDescriptorPool();

        VkDevice device_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
        VkRenderPass render_pass_ = VK_NULL_HANDLE;
        u32 min_image_count_ = 2;
        bool frame_started_ = false;
    };
}
