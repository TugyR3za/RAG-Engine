#include "Rag/RendererVK/Internal/VulkanGraphicsPipeline.h"

#include "Rag/Core/Log.h"
#include "Rag/RendererVK/Internal/VulkanVertexBuffer.h"

#include <array>
#include <fstream>
#include <system_error>

namespace rag::renderer::vk
{
    namespace
    {
        constexpr std::string_view VertexShaderFilename = "triangle.vert.spv";
        constexpr std::string_view FragmentShaderFilename = "triangle.frag.spv";

        class ScopedShaderModule final
        {
        public:
            ScopedShaderModule(VkDevice device, VkShaderModule module)
                : device_(device),
                  module_(module)
            {
            }

            ~ScopedShaderModule()
            {
                if (module_ != VK_NULL_HANDLE)
                {
                    vkDestroyShaderModule(device_, module_, nullptr);
                }
            }

            ScopedShaderModule(const ScopedShaderModule&) = delete;
            ScopedShaderModule& operator=(const ScopedShaderModule&) = delete;

            [[nodiscard]] VkShaderModule Get() const
            {
                return module_;
            }

        private:
            VkDevice device_ = VK_NULL_HANDLE;
            VkShaderModule module_ = VK_NULL_HANDLE;
        };

        std::filesystem::path ExecutableDirectory()
        {
#if defined(RAG_PLATFORM_WINDOWS)
            std::vector<wchar_t> buffer(512);

            for (;;)
            {
                const DWORD length = GetModuleFileNameW(
                    nullptr,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()));

                if (length == 0)
                {
                    throw VulkanError("Failed to resolve the RAG Sandbox executable path.");
                }

                if (length < buffer.size())
                {
                    return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
                }

                buffer.resize(buffer.size() * 2);
            }
#else
            return std::filesystem::current_path();
#endif
        }
    }

    VulkanGraphicsPipeline::VulkanGraphicsPipeline(VkDevice device, VkRenderPass render_pass)
        : device_(device)
    {
        try
        {
            Create(render_pass);
        }
        catch (...)
        {
            Cleanup();
            throw;
        }
    }

    VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
    {
        Cleanup();
    }

    void VulkanGraphicsPipeline::Bind(VkCommandBuffer command_buffer, VkExtent2D extent) const
    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<f32>(extent.width);
        viewport.height = static_cast<f32>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent;
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    }

    void VulkanGraphicsPipeline::Create(VkRenderPass render_pass)
    {
        const std::filesystem::path vertex_path = ResolveShaderPath(VertexShaderFilename);
        const std::filesystem::path fragment_path = ResolveShaderPath(FragmentShaderFilename);
        const std::vector<u32> vertex_code = LoadSpirv(vertex_path);
        const std::vector<u32> fragment_code = LoadSpirv(fragment_path);

        const ScopedShaderModule vertex_module(device_, CreateShaderModule(vertex_code));
        const ScopedShaderModule fragment_module(device_, CreateShaderModule(fragment_code));

        const VkPipelineShaderStageCreateInfo shader_stages[] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertex_module.Get(),
                .pName = "main",
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragment_module.Get(),
                .pName = "main",
            },
        };

        const VkVertexInputBindingDescription binding_description = Vertex::BindingDescription();
        const std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions =
            Vertex::AttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding_description;
        vertex_input.vertexAttributeDescriptionCount = static_cast<u32>(attribute_descriptions.size());
        vertex_input.pVertexAttributeDescriptions = attribute_descriptions.data();

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable = VK_FALSE;
        color_blend_attachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.logicOpEnable = VK_FALSE;
        color_blending.attachmentCount = 1;
        color_blending.pAttachments = &color_blend_attachment;

        const VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = static_cast<u32>(std::size(dynamic_states));
        dynamic_state.pDynamicStates = dynamic_states;

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        RAG_VK_CHECK(vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout_));

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = static_cast<u32>(std::size(shader_stages));
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = layout_;
        pipeline_info.renderPass = render_pass;
        pipeline_info.subpass = 0;

        RAG_VK_CHECK(vkCreateGraphicsPipelines(
            device_,
            VK_NULL_HANDLE,
            1,
            &pipeline_info,
            nullptr,
            &pipeline_));

        RAG_LOG_INFO(
            "Created Phase 2D Vulkan indexed graphics pipeline using shaders ",
            vertex_path.string(),
            " and ",
            fragment_path.string(),
            ".");
    }

    void VulkanGraphicsPipeline::Cleanup()
    {
        if (pipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }

        if (layout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
        }
    }

    std::filesystem::path VulkanGraphicsPipeline::ResolveShaderPath(std::string_view filename)
    {
        const std::filesystem::path executable_directory = ExecutableDirectory();
        const std::filesystem::path current_directory = std::filesystem::current_path();
        const std::array candidates = {
            executable_directory / "shaders" / filename,
            current_directory / "shaders" / filename,
            current_directory / filename,
        };

        std::error_code error;
        for (const std::filesystem::path& candidate : candidates)
        {
            if (std::filesystem::is_regular_file(candidate, error))
            {
                return candidate;
            }
            error.clear();
        }

        const std::string message =
            "Failed to locate Vulkan shader '" +
            std::string(filename) +
            "'. Expected it at '" +
            candidates.front().string() +
            "'. Rebuild RagSandbox so CMake can compile and copy the SPIR-V files.";
        RAG_LOG_ERROR(message);
        throw VulkanError(message);
    }

    std::vector<u32> VulkanGraphicsPipeline::LoadSpirv(const std::filesystem::path& path)
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

    VkShaderModule VulkanGraphicsPipeline::CreateShaderModule(const std::vector<u32>& code) const
    {
        VkShaderModuleCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = code.size() * sizeof(u32);
        create_info.pCode = code.data();

        VkShaderModule module = VK_NULL_HANDLE;
        RAG_VK_CHECK(vkCreateShaderModule(device_, &create_info, nullptr, &module));
        return module;
    }
}
