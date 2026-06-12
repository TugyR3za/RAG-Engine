#pragma once

#include "Rag/RendererVK/Internal/VulkanCommon.h"

#include <string_view>

namespace rag::renderer::vk
{
    // RAII wrapper around a VkShaderModule. Pipelines only need the module while
    // they call vkCreateGraphicsPipelines, so this keeps cleanup local.
    class ScopedShaderModule final
    {
    public:
        ScopedShaderModule(VkDevice device, VkShaderModule module);
        ~ScopedShaderModule();

        ScopedShaderModule(const ScopedShaderModule&) = delete;
        ScopedShaderModule& operator=(const ScopedShaderModule&) = delete;

        [[nodiscard]] VkShaderModule Get() const;

    private:
        VkDevice device_ = VK_NULL_HANDLE;
        VkShaderModule module_ = VK_NULL_HANDLE;
    };

    // Locates the named SPIR-V file next to the executable (or the working
    // directory), loads it, and creates a shader module. Throws VulkanError if
    // the file is missing or malformed.
    [[nodiscard]] ScopedShaderModule LoadShaderModule(VkDevice device, std::string_view spirv_filename);
}
