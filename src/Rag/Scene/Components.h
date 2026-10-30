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

        // When use_quaternion_rotation is true the transform's rotation is taken
        // from local_rotation_quat instead of local_rotation_radians. Physics
        // drives bodies this way to avoid Euler gimbal lock on tumbling objects.
        math::Quat local_rotation_quat{};
        bool use_quaternion_rotation = false;

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
        // Legacy handle passthrough; the renderer does not read it. Per-object
        // appearance lives in material_settings below.
        renderer::MaterialHandle material{};
        // Owned by value, so entities sharing a mesh/texture stay independent.
        renderer::MaterialSettings material_settings{};
        renderer::Bounds local_bounds{};
        u32 visibility_layer = 1;
        bool visible = true;
    };

    struct LightComponent
    {
        renderer::RenderLightType type = renderer::RenderLightType::Directional;
        // Direction in the light entity's local space. Keeping this separate
        // from position lets tools aim a light from an arbitrary base vector
        // while the entity transform still supplies hierarchy/world rotation.
        math::Vec3 local_direction{0.0f, -1.0f, 0.0f};
        math::Vec3 color{1.0f, 1.0f, 1.0f};
        f32 intensity = 1.0f;
        f32 range = 10.0f;
        f32 inner_cone_radians = 0.35f;
        f32 outer_cone_radians = 0.75f;
        u32 visibility_layer = 1;
        bool visible = true;
    };

    // Authoring configuration for the first-person player spawned when Play
    // starts. Pure data: no runtime velocity, camera or input state lives here.
    // The spawn point is the entity's transform position (the player's FEET);
    // transform rotation and scale are ignored -- facing comes from yaw_radians.
    // Units: metres, seconds, radians; see PlayerStart.h for valid ranges and
    // SanitizePlayerStart for how out-of-range values are corrected.
    struct PlayerStartComponent
    {
        // Initial facing about the world up axis, same convention as the editor
        // camera: 0 looks down -Z, positive turns left (counter-clockwise from above).
        f32 yaw_radians = 0.0f;

        // Ground walking speed, m/s (diagonal input is normalized to this).
        f32 move_speed = 4.5f;

        // Degrees of view rotation per raw mouse count.
        f32 mouse_sensitivity_degrees = 0.1f;

        // Apex height of a standing jump in metres, under the current world
        // gravity (the take-off speed is derived from it). 0 disables jumping.
        f32 jump_height = 1.0f;

        // Capsule radius and TOTAL height (hemispheres included), metres, and
        // the camera height above the feet.
        f32 capsule_radius = 0.3f;
        f32 capsule_height = 1.8f;
        f32 eye_height = 1.6f;

        // World-space Y (metres). Feet falling below it return the player to
        // this start with motion cleared.
        f32 fall_reset_height = -25.0f;
    };
}
