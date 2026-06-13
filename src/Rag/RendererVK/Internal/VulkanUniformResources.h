#pragma once

#include "Rag/Core/Math.h"
#include "Rag/RendererVK/Internal/VulkanDevice.h"

#include <vector>

namespace rag::renderer::vk
{
    struct alignas(16) UniformBufferObject
    {
        math::Mat4 view;
        math::Mat4 projection;
        math::Mat4 light_space;
        alignas(16) math::Vec3 light_direction;
        f32 light_direction_padding = 0.0f;
        alignas(16) math::Vec3 light_color;
        f32 light_intensity = 1.0f;
        alignas(16) math::Vec3 camera_position;
        f32 camera_position_padding = 0.0f;
        i32 shadow_pcf_kernel_radius = 2;
        f32 shadow_ambient_floor = 0.25f;
        f32 shadow_tuning_padding_0 = 0.0f;
        f32 shadow_tuning_padding_1 = 0.0f;
    };

    class VulkanUniformResources final
    {
    public:
        VulkanUniformResources(VulkanDevice& device, u32 frames_in_flight);
        ~VulkanUniformResources();

        VulkanUniformResources(const VulkanUniformResources&) = delete;
        VulkanUniformResources& operator=(const VulkanUniformResources&) = delete;

        void Update(u32 frame_index, const UniformBufferObject& uniform_data);

        // Points a frame's descriptor set at the shadow map: binding 1 is the
        // depth-comparison sampler used by the main lighting pass, binding 2 is a
        // plain sampler used only by the RAG_SHADOW_DEBUG overlay. The shadow map
        // is fixed size, so this is written once at startup rather than per frame.
        void BindShadowMap(
            u32 frame_index,
            VkImageView shadow_view,
            VkSampler compare_sampler,
            VkSampler depth_sampler);
        void BindTexture(
            u32 frame_index,
            VkImageView texture_view,
            VkSampler texture_sampler);

        [[nodiscard]] VkDescriptorSetLayout DescriptorSetLayout() const;
        [[nodiscard]] VkDescriptorSet DescriptorSet(u32 frame_index) const;

    private:
        struct FrameUniform
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            void* mapped_memory = nullptr;
            VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        };

        void CreateDescriptorSetLayout();
        void CreateUniformBuffers(u32 frames_in_flight);
        void CreateDescriptorPool(u32 frames_in_flight);
        void AllocateDescriptorSets();
        void Cleanup();

        VulkanDevice& device_;
        VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
        std::vector<FrameUniform> frames_;
    };
}
