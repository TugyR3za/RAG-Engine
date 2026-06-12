#include "Rag/RendererVK/Internal/VulkanShadowDebugPipeline.h"

#include "Rag/Core/Log.h"
#include "Rag/RendererVK/Internal/VulkanShaderModule.h"

#include <array>
#include <string_view>

namespace rag::renderer::vk
{
    namespace
    {
        constexpr std::string_view DebugVertexShaderFilename = "shadow_debug.vert.spv";
        constexpr std::string_view DebugFragmentShaderFilename = "shadow_debug.frag.spv";
    }

    VulkanShadowDebugPipeline::VulkanShadowDebugPipeline(
        VkDevice device,
        VkRenderPass render_pass,
        VkDescriptorSetLayout descriptor_set_layout)
        : device_(device)
    {
        try
        {
            Create(render_pass, descriptor_set_layout);
        }
        catch (...)
        {
            Cleanup();
            throw;
        }
    }

    VulkanShadowDebugPipeline::~VulkanShadowDebugPipeline()
    {
        Cleanup();
    }

    void VulkanShadowDebugPipeline::Bind(VkCommandBuffer command_buffer, VkExtent2D extent) const
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

    void VulkanShadowDebugPipeline::BindDescriptorSet(
        VkCommandBuffer command_buffer,
        VkDescriptorSet descriptor_set) const
    {
        vkCmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            layout_,
            0,
            1,
            &descriptor_set,
            0,
            nullptr);
    }

    void VulkanShadowDebugPipeline::Draw(VkCommandBuffer command_buffer) const
    {
        vkCmdDraw(command_buffer, 6, 1, 0, 0);
    }

    void VulkanShadowDebugPipeline::Create(
        VkRenderPass render_pass,
        VkDescriptorSetLayout descriptor_set_layout)
    {
        const ScopedShaderModule vertex_module = LoadShaderModule(device_, DebugVertexShaderFilename);
        const ScopedShaderModule fragment_module = LoadShaderModule(device_, DebugFragmentShaderFilename);

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

        // The quad is generated from gl_VertexIndex, so there is no vertex input.
        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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

        // Overlay draws on top of the scene; ignore the depth buffer entirely.
        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_FALSE;
        depth_stencil.depthWriteEnable = VK_FALSE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
        depth_stencil.stencilTestEnable = VK_FALSE;

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
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &descriptor_set_layout;
        layout_info.pushConstantRangeCount = 0;
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
        pipeline_info.pDepthStencilState = &depth_stencil;
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

        RAG_LOG_INFO("Created Vulkan shadow-map debug overlay pipeline (RAG_SHADOW_DEBUG).");
    }

    void VulkanShadowDebugPipeline::Cleanup()
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
}
