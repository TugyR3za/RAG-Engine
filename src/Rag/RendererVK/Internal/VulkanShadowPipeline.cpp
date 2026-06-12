#include "Rag/RendererVK/Internal/VulkanShadowPipeline.h"

#include "Rag/Core/Log.h"
#include "Rag/RendererVK/Internal/VulkanShaderModule.h"
#include "Rag/RendererVK/Internal/VulkanVertexBuffer.h"

#include <array>
#include <string_view>

namespace rag::renderer::vk
{
    namespace
    {
        constexpr std::string_view ShadowVertexShaderFilename = "shadow_depth.vert.spv";
    }

    VulkanShadowPipeline::VulkanShadowPipeline(
        VkDevice device,
        VkRenderPass shadow_render_pass,
        VkDescriptorSetLayout descriptor_set_layout)
        : device_(device)
    {
        try
        {
            Create(shadow_render_pass, descriptor_set_layout);
        }
        catch (...)
        {
            Cleanup();
            throw;
        }
    }

    VulkanShadowPipeline::~VulkanShadowPipeline()
    {
        Cleanup();
    }

    void VulkanShadowPipeline::Bind(VkCommandBuffer command_buffer, VkExtent2D extent) const
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

    void VulkanShadowPipeline::BindDescriptorSet(
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

    void VulkanShadowPipeline::PushModelMatrix(
        VkCommandBuffer command_buffer,
        const math::Mat4& model) const
    {
        static_assert(sizeof(math::Mat4) == 64);
        vkCmdPushConstants(
            command_buffer,
            layout_,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(math::Mat4),
            model.elements.data());
    }

    void VulkanShadowPipeline::Create(
        VkRenderPass shadow_render_pass,
        VkDescriptorSetLayout descriptor_set_layout)
    {
        const ScopedShaderModule vertex_module = LoadShaderModule(device_, ShadowVertexShaderFilename);

        VkPipelineShaderStageCreateInfo vertex_stage{};
        vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertex_stage.module = vertex_module.Get();
        vertex_stage.pName = "main";

        // Reuse the scene vertex layout; only the position attribute is consumed.
        const VkVertexInputBindingDescription binding_description = Vertex::BindingDescription();
        const std::array<VkVertexInputAttributeDescription, 3> attribute_descriptions =
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
        // No culling so thin geometry (the ground slab) still occludes correctly;
        // acne is handled with a slope-scaled bias in the lit fragment shader.
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
        depth_stencil.stencilTestEnable = VK_FALSE;

        // Depth-only subpass: no color attachments, so no blend attachments.
        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.logicOpEnable = VK_FALSE;
        color_blending.attachmentCount = 0;
        color_blending.pAttachments = nullptr;

        const VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = static_cast<u32>(std::size(dynamic_states));
        dynamic_state.pDynamicStates = dynamic_states;

        VkPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(math::Mat4);

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &descriptor_set_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_constant_range;
        RAG_VK_CHECK(vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout_));

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = 1;
        pipeline_info.pStages = &vertex_stage;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = layout_;
        pipeline_info.renderPass = shadow_render_pass;
        pipeline_info.subpass = 0;

        RAG_VK_CHECK(vkCreateGraphicsPipelines(
            device_,
            VK_NULL_HANDLE,
            1,
            &pipeline_info,
            nullptr,
            &pipeline_));

        RAG_LOG_INFO("Created Vulkan depth-only shadow pipeline using shader ", ShadowVertexShaderFilename, ".");
    }

    void VulkanShadowPipeline::Cleanup()
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
