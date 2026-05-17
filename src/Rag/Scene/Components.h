#pragma once

#include "Rag/Core/Math.h"
#include "Rag/Renderer/Renderer.h"
#include "Rag/Scene/Entity.h"

namespace rag::scene
{
    enum class CameraProjectionMode : u8
    {
        Perspective,
        Orthographic
    };

    struct TransformComponent
    {
        math::Vec3 local_position{};
        math::Vec3 local_rotation_radians{};
        math::Vec3 local_scale{1.0f, 1.0f, 1.0f};

        math::Mat4 local_matrix = math::Identity();
        math::Mat4 world_matrix = math::Identity();

        EntityId parent{};
        EntityId first_child{};
        EntityId next_sibling{};
        EntityId previous_sibling{};
        bool dirty = true;
    };

    struct CameraComponent
    {
        CameraProjectionMode projection_mode = CameraProjectionMode::Perspective;
        bool active = false;
        f32 vertical_fov_radians = math::Pi / 3.0f;
        f32 aspect_ratio = 16.0f / 9.0f;
        f32 near_plane = 0.1f;
        f32 far_plane = 1000.0f;
        f32 orthographic_height = 10.0f;
    };

    struct RenderableComponent
    {
        renderer::MeshHandle mesh{};
        renderer::MaterialHandle material{};
        renderer::Bounds local_bounds{};
        u32 visibility_layer = 1;
        bool visible = true;
    };

    struct LightComponent
    {
        renderer::RenderLightType type = renderer::RenderLightType::Directional;
        math::Vec3 color{1.0f, 1.0f, 1.0f};
        f32 intensity = 1.0f;
        f32 range = 10.0f;
        f32 inner_cone_radians = 0.35f;
        f32 outer_cone_radians = 0.75f;
        u32 visibility_layer = 1;
        bool visible = true;
    };
}
