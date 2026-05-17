#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Core/Math.h"
#include "Rag/Platform/Window.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace rag::renderer
{
    enum class RendererBackend : u8
    {
        Vulkan,
        OpenGL,
        DirectX12
    };

    struct RendererDesc
    {
        RendererBackend backend = RendererBackend::Vulkan;
        platform::IWindow* window = nullptr;
        std::string application_name = "RAG Engine";
        bool enable_validation = false;
        u32 frames_in_flight = 2;
    };

    struct RenderFrameContext
    {
        std::array<f32, 4> clear_color = {0.02f, 0.025f, 0.035f, 1.0f};
        const struct RenderWorld* render_world = nullptr;
    };

    struct MeshHandle
    {
        u32 index = UINT32_MAX;
        u32 generation = 0;

        [[nodiscard]] bool IsValid() const
        {
            return index != UINT32_MAX;
        }
    };

    struct MaterialHandle
    {
        u32 index = UINT32_MAX;
        u32 generation = 0;

        [[nodiscard]] bool IsValid() const
        {
            return index != UINT32_MAX;
        }
    };

    struct Bounds
    {
        math::Vec3 center{};
        math::Vec3 extents{0.5f, 0.5f, 0.5f};
    };

    enum class RenderLightType : u8
    {
        Directional,
        Point,
        Spot
    };

    struct RenderCamera
    {
        bool valid = false;
        math::Mat4 view = math::Identity();
        math::Mat4 projection = math::Identity();
        math::Vec3 position{};
        f32 near_plane = 0.1f;
        f32 far_plane = 1000.0f;
    };

    struct RenderObject
    {
        u64 object_id = 0;
        math::Mat4 world_transform = math::Identity();
        MeshHandle mesh{};
        MaterialHandle material{};
        Bounds local_bounds{};
        u32 visibility_layer = 1;
    };

    struct RenderLight
    {
        u64 object_id = 0;
        RenderLightType type = RenderLightType::Directional;
        math::Vec3 position{};
        math::Vec3 direction{0.0f, -1.0f, 0.0f};
        math::Vec3 color{1.0f, 1.0f, 1.0f};
        f32 intensity = 1.0f;
        f32 range = 10.0f;
        f32 inner_cone_radians = 0.35f;
        f32 outer_cone_radians = 0.75f;
        u32 visibility_layer = 1;
    };

    struct RenderWorld
    {
        RenderCamera camera{};
        std::vector<RenderObject> objects;
        std::vector<RenderLight> lights;

        void Clear()
        {
            camera = RenderCamera{};
            objects.clear();
            lights.clear();
        }
    };

    struct RendererStats
    {
        RendererBackend backend = RendererBackend::Vulkan;
        u64 frame_index = 0;
        u32 swapchain_width = 0;
        u32 swapchain_height = 0;
        u32 swapchain_image_count = 0;
    };

    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        virtual void RenderFrame(const RenderFrameContext& context) = 0;
        virtual void OnWindowResized(u32 width, u32 height) = 0;
        virtual void WaitIdle() = 0;

        [[nodiscard]] virtual RendererBackend Backend() const = 0;
        [[nodiscard]] virtual RendererStats GetStats() const = 0;
    };

    using RendererPtr = std::unique_ptr<IRenderer>;
}
