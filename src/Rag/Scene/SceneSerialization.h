#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Core/Math.h"
#include "Rag/Renderer/Renderer.h"
#include "Rag/Scene/Components.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Backend-agnostic scene (de)serialization: a neutral, plain-data description of a
// whole scene plus the JSON <-> struct <-> file plumbing. It deliberately depends
// only on the ECS core, math and the renderer's small value types (mesh id lives
// as a string, not a MeshHandle), so it pulls in no Vulkan, no physics headers and
// no ImGui. The sandbox owns the mapping between this neutral model and the live
// engine (mesh-id resolution, physics BodyDesc rebuild, editor names); this layer
// only knows how to read and write the format.
namespace rag::scene
{
    // Bump when the on-disk format changes in a way older/newer readers must know
    // about. LoadSceneFromFile rejects a file whose version falls outside
    // [kMinimumSupportedSceneVersion, kCurrentSceneVersion] so the format can
    // evolve without silently misreading an incompatible file.
    inline constexpr int kCurrentSceneVersion = 1;
    inline constexpr int kMinimumSupportedSceneVersion = 1;

    // Authoring transform: position, rotation (kept as BOTH an Euler-radians and a
    // quaternion form, with a flag selecting which the runtime composes from, to
    // match TransformComponent exactly) and scale.
    struct SerializedTransform
    {
        math::Vec3 position{};
        math::Vec3 rotation_euler_radians{};
        math::Quat rotation_quat{};
        bool use_quaternion_rotation = false;
        math::Vec3 scale{1.0f, 1.0f, 1.0f};
    };

    struct SerializedCamera
    {
        CameraProjectionMode projection_mode = CameraProjectionMode::Perspective;
        f32 vertical_fov_radians = math::Pi / 3.0f;
        f32 aspect_ratio = 16.0f / 9.0f;
        f32 near_plane = 0.1f;
        f32 far_plane = 1000.0f;
        f32 orthographic_height = 10.0f;
        bool active = false;
    };

    struct SerializedRenderable
    {
        // Stable, re-resolvable mesh identifier: a model file path such as
        // "models/Duck.glb" for file-based assets, or a "builtin:<name>" keyword
        // for procedural meshes ("builtin:cube", "builtin:ground"). The sandbox
        // maps this back onto the renderer's mesh registry on load.
        std::string mesh = "builtin:cube";

        // Human-readable texture identifier for the material this renderable draws
        // with (e.g. "assets/texture.png"). In this engine the visible texture is
        // determined by the mesh, so this is descriptive metadata that round-trips;
        // the mesh id above is what actually re-resolves geometry AND texture.
        std::string texture;

        // Material handle passthrough (authoring state; currently unused by the
        // renderer, which derives the texture from the mesh, but preserved so the
        // round-trip is lossless if per-object materials are wired up later).
        u32 material_index = UINT32_MAX;
        u32 material_generation = 0;

        renderer::Bounds local_bounds{};
        u32 visibility_layer = 1;
        bool visible = true;
    };

    struct SerializedLight
    {
        renderer::RenderLightType type = renderer::RenderLightType::Directional;
        math::Vec3 color{1.0f, 1.0f, 1.0f};
        f32 intensity = 1.0f;
        math::Vec3 local_direction{0.0f, -1.0f, 0.0f};
        f32 range = 10.0f;
        f32 inner_cone_radians = 0.35f;
        f32 outer_cone_radians = 0.75f;
        u32 visibility_layer = 1;
        bool visible = true;

        // The Euler angles the sandbox's light-editing widget drives (its sliders
        // rotate a base direction by these). The live direction is recomputed from
        // this every frame, so persisting it restores the editable light exactly.
        // Zero for lights that are not driven by that widget.
        math::Vec3 editor_euler_radians{};
    };

    // Per-object physics authoring state. Mode/shape are strings for a readable
    // file and so this model needs no physics headers; the sandbox converts them
    // to the physics module's enums. The collider extents/offset are the UNSCALED
    // base (the entity transform's scale is applied on top at body-creation time),
    // exactly as physics::BodyDesc stores them; live pose/velocity are NOT stored.
    struct SerializedPhysics
    {
        std::string mode = "None";  // "None" | "Static" | "Dynamic"
        std::string shape = "Box";  // "Box" | "Sphere"
        math::Vec3 box_half_extents{0.5f, 0.5f, 0.5f};
        f32 sphere_radius = 0.5f;
        math::Vec3 offset{};
        bool gravity_enabled = true;
        bool continuous_collision = true;
    };

    // One entity: its display name plus whichever components it carries. Absent
    // components stay unset so the writer emits only the keys that apply.
    struct SerializedEntity
    {
        std::string name;
        std::optional<SerializedTransform> transform;
        std::optional<SerializedCamera> camera;
        std::optional<SerializedRenderable> renderable;
        std::optional<SerializedLight> light;
        std::optional<SerializedPhysics> physics;
    };

    struct SerializedScene
    {
        int version = kCurrentSceneVersion;
        renderer::EnvironmentSettings environment{};
        std::vector<SerializedEntity> entities;
    };

    // Serializes a scene to a pretty-printed JSON string (always writes the current
    // version). Pure and allocation-only; never touches the filesystem.
    [[nodiscard]] std::string WriteSceneToJson(const SerializedScene& scene);

    // Parses a JSON string into a scene, validating "sceneVersion". Returns false
    // and fills out_error (leaving out_scene unspecified) on malformed JSON, a
    // missing/invalid version, or an unsupported version.
    [[nodiscard]] bool ParseSceneFromJson(
        const std::string& text,
        SerializedScene& out_scene,
        std::string& out_error);

    // Writes a scene to path (creating parent directories). Returns false and fills
    // out_error if the directory cannot be created or the file cannot be written.
    [[nodiscard]] bool SaveSceneToFile(
        const SerializedScene& scene,
        const std::filesystem::path& path,
        std::string& out_error);

    // Reads and parses a scene file. Returns false and fills out_error if the file
    // is missing/unreadable or fails ParseSceneFromJson (bad JSON / bad version).
    [[nodiscard]] bool LoadSceneFromFile(
        const std::filesystem::path& path,
        SerializedScene& out_scene,
        std::string& out_error);
}
