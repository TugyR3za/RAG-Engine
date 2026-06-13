#pragma once

#include "Rag/RendererVK/Internal/VulkanDevice.h"

#include <span>
#include <string_view>

namespace rag::renderer::vk
{
    // One immutable RGBA8 color texture uploaded through a host-visible staging
    // buffer into device-local optimal-tiling image memory.
    class VulkanTexture final
    {
    public:
        VulkanTexture(VulkanDevice& device, std::string_view filename);
        VulkanTexture(
            VulkanDevice& device,
            std::string_view debug_name,
            u32 width,
            u32 height,
            std::span<const u8> rgba_pixels);
        ~VulkanTexture();

        VulkanTexture(const VulkanTexture&) = delete;
        VulkanTexture& operator=(const VulkanTexture&) = delete;

        [[nodiscard]] VkImageView ImageView() const;
        [[nodiscard]] VkSampler Sampler() const;
        [[nodiscard]] u32 Width() const;
        [[nodiscard]] u32 Height() const;
        [[nodiscard]] VkFormat Format() const;

    private:
        void CreateImage();
        void UploadPixels(const void* pixels, VkDeviceSize byte_count);
        void CreateImageView();
        void CreateSampler();
        void Initialize(std::span<const u8> rgba_pixels);
        void Cleanup();

        VulkanDevice& device_;
        VkImage image_ = VK_NULL_HANDLE;
        VkDeviceMemory memory_ = VK_NULL_HANDLE;
        VkImageView image_view_ = VK_NULL_HANDLE;
        VkSampler sampler_ = VK_NULL_HANDLE;
        u32 width_ = 0;
        u32 height_ = 0;
        VkFormat format_ = VK_FORMAT_R8G8B8A8_SRGB;
    };
}
