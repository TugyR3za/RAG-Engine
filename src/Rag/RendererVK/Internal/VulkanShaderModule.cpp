#include "Rag/RendererVK/Internal/VulkanShaderModule.h"

#include "Rag/Core/Log.h"
#include "Rag/RendererVK/Internal/VulkanAssetPath.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace rag::renderer::vk
{
    namespace
    {
        std::vector<u32> LoadSpirv(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                const std::string message = "Failed to open Vulkan shader file: " + path.string();
                RAG_LOG_ERROR(message);
                throw VulkanError(message);
            }

            const std::streamsize size = file.tellg();
            if (size <= 0 || (size % static_cast<std::streamsize>(sizeof(u32))) != 0)
            {
                const std::string message = "Vulkan shader file is empty or has an invalid SPIR-V size: " + path.string();
                RAG_LOG_ERROR(message);
                throw VulkanError(message);
            }

            std::vector<u32> code(static_cast<std::size_t>(size) / sizeof(u32));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(code.data()), size);

            if (!file)
            {
                const std::string message = "Failed while reading Vulkan shader file: " + path.string();
                RAG_LOG_ERROR(message);
                throw VulkanError(message);
            }

            return code;
        }
    }

    ScopedShaderModule::ScopedShaderModule(VkDevice device, VkShaderModule module)
        : device_(device),
          module_(module)
    {
    }

    ScopedShaderModule::~ScopedShaderModule()
    {
        if (module_ != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device_, module_, nullptr);
        }
    }

    VkShaderModule ScopedShaderModule::Get() const
    {
        return module_;
    }

    ScopedShaderModule LoadShaderModule(VkDevice device, std::string_view spirv_filename)
    {
        const std::filesystem::path path = ResolveRuntimeAssetPath(
            "shaders",
            spirv_filename,
            "Rebuild RagSandbox so CMake can compile and copy the SPIR-V files.");
        const std::vector<u32> code = LoadSpirv(path);

        VkShaderModuleCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = code.size() * sizeof(u32);
        create_info.pCode = code.data();

        VkShaderModule module = VK_NULL_HANDLE;
        RAG_VK_CHECK(vkCreateShaderModule(device, &create_info, nullptr, &module));
        return ScopedShaderModule(device, module);
    }
}
