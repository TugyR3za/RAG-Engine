#include "Rag/RendererVK/Internal/VulkanSurface.h"

#include "Rag/Core/Log.h"

#if defined(RAG_PLATFORM_WINDOWS)
    #include <Windows.h>
#endif

namespace rag::renderer::vk
{
    VulkanSurface::VulkanSurface(VkInstance instance, platform::NativeWindowHandle native_window)
        : instance_(instance)
    {
#if defined(RAG_PLATFORM_WINDOWS)
        if (native_window.instance == nullptr || native_window.window == nullptr)
        {
            throw VulkanError("Cannot create Vulkan Win32 surface from a null native window handle.");
        }

        VkWin32SurfaceCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        create_info.hinstance = static_cast<HINSTANCE>(native_window.instance);
        create_info.hwnd = static_cast<HWND>(native_window.window);

        RAG_VK_CHECK(vkCreateWin32SurfaceKHR(instance_, &create_info, nullptr, &surface_));
        RAG_LOG_INFO("Created Vulkan Win32 surface.");
#else
        (void)native_window;
        throw VulkanError("Vulkan surface creation is not implemented for this platform.");
#endif
    }

    VulkanSurface::~VulkanSurface()
    {
        if (surface_ != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
    }

    VkSurfaceKHR VulkanSurface::Get() const
    {
        return surface_;
    }
}
