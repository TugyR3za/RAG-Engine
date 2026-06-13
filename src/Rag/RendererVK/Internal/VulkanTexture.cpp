#include "Rag/RendererVK/Internal/VulkanTexture.h"

#include "Rag/Core/Log.h"
#include "Rag/RendererVK/Internal/VulkanAssetPath.h"

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

#include <cstring>
#include <limits>
#include <memory>
#include <string>

namespace rag::renderer::vk
{
    namespace
    {
        struct StbiImageDeleter
        {
            void operator()(stbi_uc* pixels) const
            {
                stbi_image_free(pixels);
            }
        };

        void DestroyUploadResources(
            VkDevice device,
            VkCommandPool& command_pool,
            VkBuffer& staging_buffer,
            VkDeviceMemory& staging_memory)
        {
            if (command_pool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(device, command_pool, nullptr);
                command_pool = VK_NULL_HANDLE;
            }

            if (staging_buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(device, staging_buffer, nullptr);
                staging_buffer = VK_NULL_HANDLE;
            }

            if (staging_memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, staging_memory, nullptr);
                staging_memory = VK_NULL_HANDLE;
            }
        }
    }

    VulkanTexture::VulkanTexture(VulkanDevice& device, std::string_view filename)
        : device_(device)
    {
        const std::filesystem::path path = ResolveRuntimeAssetPath(
            "assets",
            filename,
            "Rebuild RagSandbox so CMake can copy the texture assets next to the executable.");

        int image_width = 0;
        int image_height = 0;
        int source_channels = 0;
        std::unique_ptr<stbi_uc, StbiImageDeleter> pixels(
            stbi_load(
                path.string().c_str(),
                &image_width,
                &image_height,
                &source_channels,
                STBI_rgb_alpha));

        if (pixels == nullptr)
        {
            const char* reason = stbi_failure_reason();
            const std::string message =
                "Failed to load texture '" +
                path.string() +
                "' with stb_image: " +
                (reason != nullptr ? std::string(reason) : std::string("unknown decode error."));
            RAG_LOG_ERROR(message);
            throw VulkanError(message);
        }

        if (image_width <= 0 || image_height <= 0)
        {
            const std::string message =
                "Texture '" + path.string() + "' has invalid dimensions.";
            RAG_LOG_ERROR(message);
            throw VulkanError(message);
        }

        const u64 pixel_count =
            static_cast<u64>(image_width) * static_cast<u64>(image_height);
        constexpr u64 BytesPerPixel = 4;
        if (pixel_count > (std::numeric_limits<u64>::max() / BytesPerPixel))
        {
            throw VulkanError("Texture pixel data size overflowed.");
        }

        const u64 byte_count = pixel_count * BytesPerPixel;
        width_ = static_cast<u32>(image_width);
        height_ = static_cast<u32>(image_height);

        try
        {
            CreateImage();
            UploadPixels(pixels.get(), static_cast<VkDeviceSize>(byte_count));
            CreateImageView();
            CreateSampler();
        }
        catch (...)
        {
            Cleanup();
            throw;
        }

        RAG_LOG_INFO(
            "Loaded texture: assets/",
            filename,
            ", ",
            width_,
            "x",
            height_,
            ", RGBA8 sRGB (VK_FORMAT_R8G8B8A8_SRGB), staged and uploaded to GPU.");
    }

    VulkanTexture::~VulkanTexture()
    {
        Cleanup();
    }

    VkImageView VulkanTexture::ImageView() const
    {
        return image_view_;
    }

    VkSampler VulkanTexture::Sampler() const
    {
        return sampler_;
    }

    u32 VulkanTexture::Width() const
    {
        return width_;
    }

    u32 VulkanTexture::Height() const
    {
        return height_;
    }

    VkFormat VulkanTexture::Format() const
    {
        return format_;
    }

    void VulkanTexture::CreateImage()
    {
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent = VkExtent3D{width_, height_, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = format_;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        RAG_VK_CHECK(vkCreateImage(device_.Device(), &image_info, nullptr, &image_));

        VkMemoryRequirements memory_requirements{};
        vkGetImageMemoryRequirements(device_.Device(), image_, &memory_requirements);

        VkMemoryAllocateInfo allocation_info{};
        allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation_info.allocationSize = memory_requirements.size;
        allocation_info.memoryTypeIndex = device_.FindMemoryType(
            memory_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RAG_VK_CHECK(vkAllocateMemory(device_.Device(), &allocation_info, nullptr, &memory_));
        RAG_VK_CHECK(vkBindImageMemory(device_.Device(), image_, memory_, 0));
    }

    void VulkanTexture::UploadPixels(const void* pixels, VkDeviceSize byte_count)
    {
        VkBuffer staging_buffer = VK_NULL_HANDLE;
        VkDeviceMemory staging_memory = VK_NULL_HANDLE;
        VkCommandPool command_pool = VK_NULL_HANDLE;

        try
        {
            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = byte_count;
            buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            RAG_VK_CHECK(vkCreateBuffer(
                device_.Device(),
                &buffer_info,
                nullptr,
                &staging_buffer));

            VkMemoryRequirements memory_requirements{};
            vkGetBufferMemoryRequirements(
                device_.Device(),
                staging_buffer,
                &memory_requirements);

            VkMemoryAllocateInfo allocation_info{};
            allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation_info.allocationSize = memory_requirements.size;
            allocation_info.memoryTypeIndex = device_.FindMemoryType(
                memory_requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            RAG_VK_CHECK(vkAllocateMemory(
                device_.Device(),
                &allocation_info,
                nullptr,
                &staging_memory));
            RAG_VK_CHECK(vkBindBufferMemory(
                device_.Device(),
                staging_buffer,
                staging_memory,
                0));

            void* mapped_memory = nullptr;
            RAG_VK_CHECK(vkMapMemory(
                device_.Device(),
                staging_memory,
                0,
                byte_count,
                0,
                &mapped_memory));
            std::memcpy(mapped_memory, pixels, static_cast<std::size_t>(byte_count));
            vkUnmapMemory(device_.Device(), staging_memory);

            VkCommandPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pool_info.queueFamilyIndex = device_.Families().graphics_family.value();
            RAG_VK_CHECK(vkCreateCommandPool(
                device_.Device(),
                &pool_info,
                nullptr,
                &command_pool));

            VkCommandBufferAllocateInfo allocate_command{};
            allocate_command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_command.commandPool = command_pool;
            allocate_command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_command.commandBufferCount = 1;

            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            RAG_VK_CHECK(vkAllocateCommandBuffers(
                device_.Device(),
                &allocate_command,
                &command_buffer));

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            RAG_VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

            VkImageMemoryBarrier to_transfer{};
            to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_transfer.srcAccessMask = 0;
            to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.image = image_;
            to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_transfer.subresourceRange.baseMipLevel = 0;
            to_transfer.subresourceRange.levelCount = 1;
            to_transfer.subresourceRange.baseArrayLayer = 0;
            to_transfer.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(
                command_buffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &to_transfer);

            VkBufferImageCopy copy_region{};
            copy_region.bufferOffset = 0;
            copy_region.bufferRowLength = 0;
            copy_region.bufferImageHeight = 0;
            copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.mipLevel = 0;
            copy_region.imageSubresource.baseArrayLayer = 0;
            copy_region.imageSubresource.layerCount = 1;
            copy_region.imageOffset = VkOffset3D{0, 0, 0};
            copy_region.imageExtent = VkExtent3D{width_, height_, 1};
            vkCmdCopyBufferToImage(
                command_buffer,
                staging_buffer,
                image_,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy_region);

            VkImageMemoryBarrier to_shader_read{};
            to_shader_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_shader_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_shader_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            to_shader_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_shader_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_shader_read.image = image_;
            to_shader_read.subresourceRange = to_transfer.subresourceRange;
            vkCmdPipelineBarrier(
                command_buffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &to_shader_read);

            RAG_VK_CHECK(vkEndCommandBuffer(command_buffer));

            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer;
            RAG_VK_CHECK(vkQueueSubmit(
                device_.GraphicsQueue(),
                1,
                &submit_info,
                VK_NULL_HANDLE));
            RAG_VK_CHECK(vkQueueWaitIdle(device_.GraphicsQueue()));

            DestroyUploadResources(
                device_.Device(),
                command_pool,
                staging_buffer,
                staging_memory);
        }
        catch (...)
        {
            (void)vkDeviceWaitIdle(device_.Device());
            DestroyUploadResources(
                device_.Device(),
                command_pool,
                staging_buffer,
                staging_memory);
            throw;
        }
    }

    void VulkanTexture::CreateImageView()
    {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        RAG_VK_CHECK(vkCreateImageView(
            device_.Device(),
            &view_info,
            nullptr,
            &image_view_));
    }

    void VulkanTexture::CreateSampler()
    {
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.anisotropyEnable = VK_FALSE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 0.0f;
        RAG_VK_CHECK(vkCreateSampler(
            device_.Device(),
            &sampler_info,
            nullptr,
            &sampler_));
    }

    void VulkanTexture::Cleanup()
    {
        if (sampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(device_.Device(), sampler_, nullptr);
            sampler_ = VK_NULL_HANDLE;
        }

        if (image_view_ != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_.Device(), image_view_, nullptr);
            image_view_ = VK_NULL_HANDLE;
        }

        if (image_ != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_.Device(), image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }

        if (memory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_.Device(), memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }

        width_ = 0;
        height_ = 0;
    }
}
