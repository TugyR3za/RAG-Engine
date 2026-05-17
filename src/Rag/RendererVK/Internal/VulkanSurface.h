#pragma once

#include "Rag/Platform/Window.h"
#include "Rag/RendererVK/Internal/VulkanCommon.h"

namespace rag::renderer::vk
{
    class VulkanSurface final
    {
    public:
        VulkanSurface(VkInstance instance, platform::NativeWindowHandle native_window);
        ~VulkanSurface();

        VulkanSurface(const VulkanSurface&) = delete;
        VulkanSurface& operator=(const VulkanSurface&) = delete;

        [[nodiscard]] VkSurfaceKHR Get() const;

    private:
        VkInstance instance_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    };
}
