#include "Rag/RendererVK/Internal/VulkanImGuiLayer.h"

#include "Rag/Core/Log.h"

// VulkanCommon.h (via the header above) already pulls in <Windows.h> and
// <vulkan/vulkan.h>, which the ImGui Win32 backend declarations depend on.
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_win32.h>

#if !defined(IMGUI_HAS_DOCK)
    #error RAG Engine editor requires the Dear ImGui docking branch.
#endif

// imgui_impl_win32.h intentionally hides this declaration behind '#if 0' so the
// header doesn't drag in <windows.h>; the backend expects each consumer to
// forward declare it, exactly as the official example_win32_vulkan does.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace rag::renderer::vk
{
    namespace
    {
        void ImGuiCheckVkResult(VkResult result)
        {
            if (result != VK_SUCCESS)
            {
                RAG_LOG_ERROR("Dear ImGui Vulkan backend error: ", VkResultToString(result));
            }
        }

        int ImGuiCreateWin32VulkanSurface(
            ImGuiViewport* viewport,
            ImU64 instance,
            const void* allocator,
            ImU64* out_surface)
        {
            VkWin32SurfaceCreateInfoKHR create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            create_info.hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
            create_info.hinstance = GetModuleHandleW(nullptr);
            return static_cast<int>(vkCreateWin32SurfaceKHR(
                reinterpret_cast<VkInstance>(instance),
                &create_info,
                static_cast<const VkAllocationCallbacks*>(allocator),
                reinterpret_cast<VkSurfaceKHR*>(out_surface)));
        }
    }

    VulkanImGuiLayer::VulkanImGuiLayer(const VulkanImGuiInit& init)
        : device_(init.device),
          render_pass_(init.render_pass),
          min_image_count_(init.min_image_count)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // don't write an imgui.ini next to the exe
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // Viewports are intentionally left off: they would spawn extra OS windows
        // and need Vulkan platform-window plumbing we do not want here.
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(init.window_handle);
        ImGui::GetPlatformIO().Platform_CreateVkSurface = ImGuiCreateWin32VulkanSurface;

        CreateDescriptorPool();

        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_2;
        info.Instance = init.instance;
        info.PhysicalDevice = init.physical_device;
        info.Device = init.device;
        info.QueueFamily = init.graphics_queue_family;
        info.Queue = init.graphics_queue;
        info.DescriptorPool = descriptor_pool_;
        info.MinImageCount = init.min_image_count;
        info.ImageCount = init.image_count;
        info.PipelineInfoMain.RenderPass = init.render_pass;
        info.PipelineInfoMain.Subpass = 0;
        info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.CheckVkResultFn = ImGuiCheckVkResult;
        ImGui_ImplVulkan_Init(&info);

        RAG_LOG_INFO(
            "Dear ImGui ",
            IMGUI_VERSION,
            " docking build initialized; DockingEnable=",
            (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0 ? "true" : "false",
            ", Win32 + Vulkan backends active");
    }

    VulkanImGuiLayer::~VulkanImGuiLayer()
    {
        // The renderer guarantees the device is idle and still alive here, and
        // that this layer is destroyed before the device. Shut the Vulkan backend
        // down first (it frees descriptor sets from our pool), then the pool.
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (descriptor_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }
    }

    void VulkanImGuiLayer::BeginFrame()
    {
        // Defensive: if a previous frame was started but never rendered (e.g. the
        // frame was aborted), close it so NewFrame does not assert.
        if (frame_started_)
        {
            ImGui::EndFrame();
            frame_started_ = false;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        frame_started_ = true;
    }

    void VulkanImGuiLayer::RenderInto(VkCommandBuffer command_buffer)
    {
        if (!frame_started_)
        {
            return;
        }

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
        frame_started_ = false;
    }

    void VulkanImGuiLayer::OnSwapchainRecreated(VkRenderPass render_pass, u32 image_count)
    {
        // A resize/fullscreen toggle keeps the same attachment formats, so the
        // recreated render pass stays compatible with the pipeline ImGui already
        // built and the draw path keeps working. Only the image count, used to
        // size the backend's per-frame buffer ring, may need refreshing.
        render_pass_ = render_pass;
        if (image_count >= 2 && image_count != min_image_count_)
        {
            ImGui_ImplVulkan_SetMinImageCount(image_count);
            min_image_count_ = image_count;
        }
    }

    bool VulkanImGuiLayer::ProcessWin32Message(
        void* native_window,
        u32 message,
        u64 w_param,
        i64 l_param,
        i64& out_result)
    {
        const LRESULT result = ImGui_ImplWin32_WndProcHandler(
            static_cast<HWND>(native_window),
            static_cast<UINT>(message),
            static_cast<WPARAM>(w_param),
            static_cast<LPARAM>(l_param));
        out_result = static_cast<i64>(result);
        return result != 0;
    }

    void VulkanImGuiLayer::CreateDescriptorPool()
    {
        // Mirrors the official example_win32_vulkan pool: combined image samplers
        // for the font atlas (and any future ImGui::Image textures).
        const VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE},
        };

        u32 max_sets = 0;
        for (const VkDescriptorPoolSize& pool_size : pool_sizes)
        {
            max_sets += pool_size.descriptorCount;
        }

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = max_sets;
        pool_info.poolSizeCount = static_cast<u32>(sizeof(pool_sizes) / sizeof(pool_sizes[0]));
        pool_info.pPoolSizes = pool_sizes;

        RAG_VK_CHECK(vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_));
    }
}
