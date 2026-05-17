#pragma once

#include "Rag/Renderer/Renderer.h"

#include <memory>

namespace rag::renderer::vk
{
    class VulkanRenderer final : public IRenderer
    {
    public:
        explicit VulkanRenderer(const RendererDesc& desc);
        ~VulkanRenderer() override;

        VulkanRenderer(const VulkanRenderer&) = delete;
        VulkanRenderer& operator=(const VulkanRenderer&) = delete;

        void RenderFrame(const RenderFrameContext& context) override;
        void OnWindowResized(u32 width, u32 height) override;
        void WaitIdle() override;

        [[nodiscard]] RendererBackend Backend() const override;
        [[nodiscard]] RendererStats GetStats() const override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    RendererPtr CreateVulkanRenderer(const RendererDesc& desc);
}
