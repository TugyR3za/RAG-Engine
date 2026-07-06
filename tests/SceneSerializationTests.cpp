#include "Rag/Core/Log.h"
#include "Rag/Scene/SceneSerialization.h"

#include <cmath>
#include <filesystem>
#include <string>

namespace
{
    bool NearlyEqual(rag::f32 left, rag::f32 right)
    {
        return std::fabs(left - right) <= 0.0001f;
    }

    bool Vec3Equal(const rag::math::Vec3& a, const rag::math::Vec3& b)
    {
        return NearlyEqual(a.x, b.x) && NearlyEqual(a.y, b.y) && NearlyEqual(a.z, b.z);
    }

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            RAG_LOG_ERROR("Scene serialization test failed: ", message);
        }
        return condition;
    }

    // A representative scene exercising every component kind plus both rotation
    // representations and both collider shapes.
    rag::scene::SerializedScene MakeSampleScene()
    {
        using namespace rag::scene;
        SerializedScene scene;

        SerializedEntity cube;
        cube.name = "Center Cube";
        SerializedTransform cube_transform;
        cube_transform.position = rag::math::Vec3{1.0f, 12.0f, -3.0f};
        cube_transform.rotation_quat = rag::math::Quat{0.1f, 0.2f, 0.3f, 0.9f};
        cube_transform.use_quaternion_rotation = true;
        cube_transform.scale = rag::math::Vec3{1.5f, 1.5f, 1.5f};
        cube.transform = cube_transform;
        SerializedRenderable cube_renderable;
        cube_renderable.mesh = "builtin:cube";
        cube_renderable.texture = "assets/texture.png";
        cube.renderable = cube_renderable;
        SerializedPhysics cube_physics;
        cube_physics.mode = "Dynamic";
        cube_physics.shape = "Box";
        cube_physics.box_half_extents = rag::math::Vec3{0.5f, 0.5f, 0.5f};
        cube_physics.gravity_enabled = true;
        cube.physics = cube_physics;
        scene.entities.push_back(cube);

        SerializedEntity sphere;
        sphere.name = "Sphere";
        SerializedTransform sphere_transform;
        sphere_transform.position = rag::math::Vec3{6.5f, 9.0f, 0.0f};
        sphere_transform.rotation_euler_radians = rag::math::Vec3{0.25f, -0.5f, 0.75f};
        sphere.transform = sphere_transform;
        SerializedRenderable sphere_renderable;
        sphere_renderable.mesh = "models/sphere.obj";
        sphere_renderable.local_bounds.extents = rag::math::Vec3{1.0f, 1.0f, 1.0f};
        sphere.renderable = sphere_renderable;
        SerializedPhysics sphere_physics;
        sphere_physics.mode = "Dynamic";
        sphere_physics.shape = "Sphere";
        sphere_physics.sphere_radius = 1.0f;
        sphere.physics = sphere_physics;
        scene.entities.push_back(sphere);

        SerializedEntity camera;
        camera.name = "Camera";
        SerializedTransform camera_transform;
        camera_transform.position = rag::math::Vec3{0.0f, 3.0f, 9.0f};
        camera.transform = camera_transform;
        SerializedCamera camera_component;
        camera_component.vertical_fov_radians = rag::math::Pi / 3.0f;
        camera_component.near_plane = 0.1f;
        camera_component.far_plane = 100.0f;
        camera_component.active = true;
        camera.camera = camera_component;
        scene.entities.push_back(camera);

        SerializedEntity light;
        light.name = "Sun";
        light.transform = SerializedTransform{};
        SerializedLight light_component;
        light_component.type = rag::renderer::RenderLightType::Directional;
        light_component.color = rag::math::Vec3{1.0f, 0.92f, 0.78f};
        light_component.intensity = 1.25f;
        light_component.editor_euler_radians = rag::math::Vec3{0.3f, 0.1f, -0.2f};
        light.light = light_component;
        scene.entities.push_back(light);

        return scene;
    }

    bool ScenesMatch(const rag::scene::SerializedScene& a, const rag::scene::SerializedScene& b)
    {
        if (a.entities.size() != b.entities.size())
        {
            return false;
        }

        for (std::size_t index = 0; index < a.entities.size(); ++index)
        {
            const rag::scene::SerializedEntity& left = a.entities[index];
            const rag::scene::SerializedEntity& right = b.entities[index];

            if (left.name != right.name ||
                left.transform.has_value() != right.transform.has_value() ||
                left.camera.has_value() != right.camera.has_value() ||
                left.renderable.has_value() != right.renderable.has_value() ||
                left.light.has_value() != right.light.has_value() ||
                left.physics.has_value() != right.physics.has_value())
            {
                return false;
            }

            if (left.transform.has_value())
            {
                if (!Vec3Equal(left.transform->position, right.transform->position) ||
                    !Vec3Equal(left.transform->rotation_euler_radians, right.transform->rotation_euler_radians) ||
                    !Vec3Equal(left.transform->scale, right.transform->scale) ||
                    left.transform->use_quaternion_rotation != right.transform->use_quaternion_rotation)
                {
                    return false;
                }
            }
            if (left.renderable.has_value())
            {
                if (left.renderable->mesh != right.renderable->mesh ||
                    left.renderable->texture != right.renderable->texture)
                {
                    return false;
                }
            }
            if (left.light.has_value())
            {
                if (!Vec3Equal(left.light->color, right.light->color) ||
                    !NearlyEqual(left.light->intensity, right.light->intensity) ||
                    !Vec3Equal(left.light->editor_euler_radians, right.light->editor_euler_radians))
                {
                    return false;
                }
            }
            if (left.physics.has_value())
            {
                if (left.physics->mode != right.physics->mode ||
                    left.physics->shape != right.physics->shape ||
                    !NearlyEqual(left.physics->sphere_radius, right.physics->sphere_radius) ||
                    !Vec3Equal(left.physics->box_half_extents, right.physics->box_half_extents))
                {
                    return false;
                }
            }
            if (left.camera.has_value())
            {
                if (left.camera->active != right.camera->active ||
                    !NearlyEqual(left.camera->far_plane, right.camera->far_plane))
                {
                    return false;
                }
            }
        }

        return true;
    }
}

int main()
{
    using namespace rag::scene;

    const SerializedScene original = MakeSampleScene();

    // 1. String round-trip: write -> parse -> equal.
    const std::string json_text = WriteSceneToJson(original);
    SerializedScene parsed;
    std::string error;
    if (!Expect(ParseSceneFromJson(json_text, parsed, error), "String round-trip should parse.") ||
        !Expect(error.empty(), "Successful parse should leave no error message.") ||
        !Expect(parsed.version == kCurrentSceneVersion, "Parsed scene should carry the current version.") ||
        !Expect(ScenesMatch(original, parsed), "String round-trip should preserve the scene."))
    {
        return 1;
    }

    // 2. File round-trip through a temp path.
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "rag_scene_serialization_test.ragscene";
    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);

    if (!Expect(SaveSceneToFile(original, temp_path, error), "SaveSceneToFile should succeed.") ||
        !Expect(std::filesystem::exists(temp_path), "Saved scene file should exist on disk."))
    {
        return 1;
    }

    SerializedScene loaded;
    if (!Expect(LoadSceneFromFile(temp_path, loaded, error), "LoadSceneFromFile should succeed.") ||
        !Expect(ScenesMatch(original, loaded), "File round-trip should preserve the scene."))
    {
        return 1;
    }
    std::filesystem::remove(temp_path, remove_error);

    // 3. A future/unsupported version must be rejected, not silently misread.
    SerializedScene rejected;
    const std::string future_version =
        R"({"sceneVersion": 999999, "entities": []})";
    if (!Expect(!ParseSceneFromJson(future_version, rejected, error), "A newer sceneVersion must be rejected.") ||
        !Expect(!error.empty(), "A rejected version should report an error."))
    {
        return 1;
    }

    // 4. A missing version field must be rejected.
    SerializedScene no_version;
    const std::string missing_version = R"({"entities": []})";
    if (!Expect(!ParseSceneFromJson(missing_version, no_version, error), "A missing sceneVersion must be rejected."))
    {
        return 1;
    }

    // 5. Malformed JSON must be reported, not crash.
    SerializedScene malformed;
    if (!Expect(!ParseSceneFromJson("{ this is not json", malformed, error), "Malformed JSON must be rejected."))
    {
        return 1;
    }

    RAG_LOG_INFO("Scene serialization tests passed.");
    return 0;
}
