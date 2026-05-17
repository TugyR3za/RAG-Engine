#pragma once

#include "Rag/RendererVK/Internal/VulkanCommon.h"

#include <string>
#include <vector>

namespace rag::renderer::vk
{
    class VulkanInstance final
    {
    public:
        VulkanInstance(std::string application_name, bool enable_validation);
        ~VulkanInstance();

        VulkanInstance(const VulkanInstance&) = delete;
        VulkanInstance& operator=(const VulkanInstance&) = delete;

        [[nodiscard]] VkInstance Get() const;
        [[nodiscard]] bool ValidationEnabled() const;

    private:
        void CreateInstance();
        void CreateDebugMessenger();
        void DestroyDebugMessenger();

        [[nodiscard]] bool ValidationLayersSupported() const;
        [[nodiscard]] std::vector<const char*> RequiredExtensions() const;

        std::string application_name_;
        bool enable_validation_ = false;
        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    };
}
