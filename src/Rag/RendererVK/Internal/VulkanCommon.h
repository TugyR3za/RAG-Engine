#pragma once

#include "Rag/Core/Base.h"

#if defined(VK_USE_PLATFORM_WIN32_KHR)
    #include <Windows.h>
#endif

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace rag::renderer::vk
{
    class VulkanError final : public std::runtime_error
    {
    public:
        explicit VulkanError(const std::string& message);
    };

    [[nodiscard]] const char* VkResultToString(VkResult result);
    void CheckVk(VkResult result, std::string_view operation);
}

#define RAG_VK_CHECK(expression) ::rag::renderer::vk::CheckVk((expression), #expression)
