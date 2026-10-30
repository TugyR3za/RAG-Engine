#include "Rag/Core/Log.h"
#include "Rag/Scene/PlayerStart.h"
#include "Rag/Scene/SceneSerialization.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
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
        scene.environment = {true, {"assets/sky/px.png", "assets/sky/nx.png",
            "assets/sky/py.png", "assets/sky/ny.png", "assets/sky/pz.png", "assets/sky/nz.png"}};

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
        cube_renderable.material_settings = rag::renderer::MaterialSettings{{1.0f, 0.0f, 0.0f}, false};
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

        // Shares the cube's mesh but keeps its texture with a non-trivial tint.
        SerializedEntity tinted_cube;
        tinted_cube.name = "Cube 1";
        tinted_cube.transform = SerializedTransform{};
        SerializedRenderable tinted_renderable;
        tinted_renderable.mesh = "builtin:cube";
        tinted_renderable.texture = "assets/texture.png";
        tinted_renderable.material_settings = rag::renderer::MaterialSettings{{0.25f, 0.5f, 0.75f}, true};
        tinted_cube.renderable = tinted_renderable;
        scene.entities.push_back(tinted_cube);

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

        SerializedEntity start;
        start.name = "Player Start";
        start.transform = SerializedTransform{};
        start.transform->position = rag::math::Vec3{2.0f, 0.5f, -4.0f};
        SerializedPlayerStart start_component;
        start_component.config.yaw_radians = 1.25f;
        start_component.config.move_speed = 6.0f;
        start_component.config.mouse_sensitivity_degrees = 0.2f;
        start_component.config.jump_height = 1.4f;
        start_component.config.capsule_radius = 0.4f;
        start_component.config.capsule_height = 2.0f;
        start_component.config.eye_height = 1.7f;
        start_component.config.fall_reset_height = -12.0f;
        start.player_start = start_component;
        scene.entities.push_back(start);

        return scene;
    }

    bool PlayerStartsMatch(const rag::scene::PlayerStartComponent& a, const rag::scene::PlayerStartComponent& b)
    {
        return NearlyEqual(a.yaw_radians, b.yaw_radians) &&
               NearlyEqual(a.move_speed, b.move_speed) &&
               NearlyEqual(a.mouse_sensitivity_degrees, b.mouse_sensitivity_degrees) &&
               NearlyEqual(a.jump_height, b.jump_height) &&
               NearlyEqual(a.capsule_radius, b.capsule_radius) &&
               NearlyEqual(a.capsule_height, b.capsule_height) &&
               NearlyEqual(a.eye_height, b.eye_height) &&
               NearlyEqual(a.fall_reset_height, b.fall_reset_height);
    }

    bool ScenesMatch(const rag::scene::SerializedScene& a, const rag::scene::SerializedScene& b)
    {
        if (a.entities.size() != b.entities.size() || a.environment != b.environment)
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
                left.physics.has_value() != right.physics.has_value() ||
                left.player_start.has_value() != right.player_start.has_value())
            {
                return false;
            }

            if (left.player_start.has_value() &&
                !PlayerStartsMatch(left.player_start->config, right.player_start->config))
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
                    left.renderable->texture != right.renderable->texture ||
                    !Vec3Equal(left.renderable->material_settings.base_color,
                        right.renderable->material_settings.base_color) ||
                    left.renderable->material_settings.use_base_color_texture !=
                        right.renderable->material_settings.use_base_color_texture)
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

    // Optional additive version-1 data; parsing into a reused object must reset it.
    parsed = original;
    if (!Expect(ParseSceneFromJson(R"({"sceneVersion":1,"entities":[]})", parsed, error), "Legacy v1 must load.") ||
        !Expect(parsed.environment == rag::renderer::EnvironmentSettings{}, "Legacy scene must clear prior environment."))
        return 1;

    auto disabled = original;
    disabled.environment.skybox_enabled = false;
    if (!Expect(ParseSceneFromJson(WriteSceneToJson(disabled), parsed, error), "Disabled sky must parse.") ||
        !Expect(ScenesMatch(disabled, parsed), "Disabled sky must retain all face paths."))
        return 1;

    // Malformed optional environment is isolated: entities survive and sky is off.
    const std::string bad_environments[] = {
        "null", "[]", "42", R"({"skyboxEnabled":"yes"})",
        R"({"skyboxEnabled":true})", R"({"skyboxFaces":["a.png"]})",
        R"({"skyboxEnabled":true,"skyboxFaces":[1,2,3,4,5,6]})",
        R"({"skyboxEnabled":true,"skyboxFaces":["a","b","c","d","e","../escape.png"]})"
    };
    for (const std::string& bad : bad_environments)
    {
        parsed = original;
        if (!Expect(ParseSceneFromJson("{\"sceneVersion\":1,\"entities\":[{\"name\":\"kept\"}],\"environment\":" +
                bad + "}", parsed, error), "Invalid environment must not reject the scene.") ||
            !Expect(parsed.environment == rag::renderer::EnvironmentSettings{}, "Invalid environment must reset to disabled.") ||
            !Expect(parsed.entities.size() == 1 && parsed.entities[0].name == "kept", "Entities must survive invalid environment."))
            return 1;
    }

    const std::string bad_paths[] = {"", "/absolute.png", "C:/absolute.png", "C:relative.png",
        "assets\\px.png", "assets/../px.png", "./px.png", "assets//px.png", "assets/", std::string("nul\0.png", 8)};
    for (const std::string& bad : bad_paths)
    {
        auto environment = original.environment;
        environment.skybox_faces[2] = bad;
        if (!Expect(!rag::renderer::ValidateEnvironment(environment, error), "Nonportable/incomplete paths must be rejected.") ||
            !Expect(error.find("+Y") != std::string::npos, "Invalid path must identify the affected face."))
            return 1;
    }
    if (!Expect(rag::renderer::ValidateEnvironment({}, error), "Unconfigured disabled environment is valid.")) return 1;

    // Material authoring values are stored inside the existing "material" object
    // as plain values (no GPU handles) and the version stays at 1.
    if (!Expect(json_text.find("\"baseColor\"") != std::string::npos &&
                json_text.find("\"useBaseColorTexture\"") != std::string::npos,
            "Written scenes must contain the material fields.") ||
        !Expect(kCurrentSceneVersion == 1, "Additive material fields must not bump the scene version."))
    {
        return 1;
    }

    // Older files: a renderable with the legacy handle-only "material" object (as
    // in scenes/default.ragscene) or with no "material" at all loads with defaults.
    const std::string legacy_renderables = R"({"sceneVersion":1,"entities":[
        {"name":"Cube 0","renderable":{"mesh":"builtin:cube","texture":"assets/texture.png",
            "material":{"generation":0,"index":4294967295},"visible":true}},
        {"name":"Duck","renderable":{"mesh":"models/Duck.glb"}}]})";
    if (!Expect(ParseSceneFromJson(legacy_renderables, parsed, error), "Legacy renderables must load.") ||
        !Expect(parsed.entities.size() == 2 && parsed.entities[0].renderable && parsed.entities[1].renderable,
            "Legacy renderables must be kept.") ||
        !Expect(parsed.entities[0].renderable->material_settings == rag::renderer::MaterialSettings{} &&
                parsed.entities[1].renderable->material_settings == rag::renderer::MaterialSettings{},
            "Missing material fields must default to white + texture.") ||
        !Expect(parsed.entities[0].renderable->material_index == UINT32_MAX,
            "The legacy material handle passthrough must still load."))
    {
        return 1;
    }

    // Invalid optional material values fall back per field and never drop the
    // entity; out-of-range colors are clamped to the displayable [0, 1] range.
    const auto parse_material = [&](const std::string& material_json, rag::renderer::MaterialSettings& out) {
        SerializedScene scene;
        const bool ok = ParseSceneFromJson(
            R"({"sceneVersion":1,"entities":[{"name":"kept","renderable":{"material":)" + material_json + "}}]}",
            scene, error);
        if (ok && scene.entities.size() == 1 && scene.entities[0].renderable)
        {
            out = scene.entities[0].renderable->material_settings;
            return true;
        }
        return false;
    };
    const std::string bad_materials[] = {
        R"({"baseColor":"red","useBaseColorTexture":"yes"})",
        R"({"baseColor":[1,0],"useBaseColorTexture":1})",
        R"({"baseColor":[1,"x",0],"useBaseColorTexture":null})",
        R"({"baseColor":null})"};
    for (const std::string& bad : bad_materials)
    {
        rag::renderer::MaterialSettings settings{{0.1f, 0.2f, 0.3f}, false};
        if (!Expect(parse_material(bad, settings), "Invalid material fields must not reject the entity.") ||
            !Expect(settings == rag::renderer::MaterialSettings{}, "Invalid material fields must use defaults."))
        {
            return 1;
        }
    }
    rag::renderer::MaterialSettings clamped;
    if (!Expect(parse_material(R"({"baseColor":[2.0,-1.0,0.5],"useBaseColorTexture":false})", clamped),
            "Out-of-range material must load.") ||
        !Expect(Vec3Equal(clamped.base_color, rag::math::Vec3{1.0f, 0.0f, 0.5f}) && !clamped.use_base_color_texture,
            "Out-of-range base color must be clamped and the toggle kept."))
    {
        return 1;
    }

    // --- Player Start (optional additive version-1 "playerStart" object) ----

    // Written only for entities that have one; the round-trip above already
    // compared every field of the sample's non-default start.
    if (!Expect(json_text.find("\"playerStart\"") != std::string::npos &&
                json_text.find("\"mouseSensitivityDegrees\"") != std::string::npos,
            "Written scenes must contain the Player Start fields.") ||
        !Expect(ParseSceneFromJson(json_text, parsed, error) && parsed.entities.size() == original.entities.size(),
            "The sample must reparse for the Player Start checks.") ||
        !Expect(!parsed.entities.front().player_start.has_value() && parsed.entities.back().player_start.has_value(),
            "Only the entity that had a Player Start has one after the round-trip."))
    {
        return 1;
    }

    // Older files (scenes/default.ragscene has no Player Start) load unchanged.
    if (!Expect(ParseSceneFromJson(legacy_renderables, parsed, error), "Legacy scene must load.") ||
        !Expect(!parsed.entities[0].player_start.has_value() && !parsed.entities[1].player_start.has_value(),
            "A scene without \"playerStart\" has no Player Start."))
    {
        return 1;
    }

    // Defaults: an empty object is a valid default configuration.
    const auto parse_start = [&](const std::string& start_json, std::optional<PlayerStartComponent>& out) {
        SerializedScene scene;
        out.reset();
        const bool ok = ParseSceneFromJson(
            R"({"sceneVersion":1,"entities":[{"name":"kept","playerStart":)" + start_json + "}]}", scene, error);
        if (ok && scene.entities.size() == 1 && scene.entities[0].name == "kept")
        {
            if (scene.entities[0].player_start.has_value())
            {
                out = scene.entities[0].player_start->config;
            }
            return true;
        }
        return false;
    };
    std::optional<PlayerStartComponent> start;
    if (!Expect(parse_start("{}", start), "An empty playerStart must load.") ||
        !Expect(start.has_value() && PlayerStartsMatch(*start, PlayerStartComponent{}),
            "An empty playerStart takes every default."))
    {
        return 1;
    }

    // A non-object never rejects the scene or drops the entity; it just has no start.
    for (const std::string bad : {"5", "null", "\"yes\"", "[1,2]"})
    {
        if (!Expect(parse_start(bad, start), "A malformed playerStart must not reject the scene.") ||
            !Expect(!start.has_value(), "A malformed playerStart is ignored."))
        {
            return 1;
        }
    }

    // Non-numbers take the default; out-of-range numbers are clamped in
    // dependency order (radius, then height >= 2r + 0.1, then eye <= height);
    // yaw is wrapped.
    if (!Expect(parse_start(R"({"moveSpeed":"fast","capsuleRadius":0.5,"capsuleHeight":0.2,
                "eyeHeight":9,"yawRadians":10,"fallResetHeight":null,"jumpHeight":-3,
                "mouseSensitivityDegrees":1e9})", start), "An out-of-range playerStart must load.") ||
        !Expect(start.has_value(), "An out-of-range playerStart is kept (corrected)."))
    {
        return 1;
    }
    const PlayerStartComponent defaults{};
    if (!Expect(NearlyEqual(start->move_speed, defaults.move_speed), "A non-number speed takes the default.") ||
        !Expect(NearlyEqual(start->capsule_radius, 0.5f), "A valid radius is kept.") ||
        !Expect(NearlyEqual(start->capsule_height, 1.1f), "Height is raised to 2 x radius + 0.1.") ||
        !Expect(NearlyEqual(start->eye_height, 1.1f), "Eye height is clamped into the capsule.") ||
        !Expect(NearlyEqual(start->yaw_radians, 10.0f - (4.0f * rag::math::Pi)), "Yaw is wrapped into [-pi, pi].") ||
        !Expect(NearlyEqual(start->fall_reset_height, defaults.fall_reset_height), "A null reset height takes the default.") ||
        !Expect(NearlyEqual(start->jump_height, 0.0f), "A negative jump height is clamped to 0 (no jumping).") ||
        !Expect(NearlyEqual(start->mouse_sensitivity_degrees, PlayerStartLimits::MaxMouseSensitivityDegrees),
            "A huge sensitivity is clamped."))
    {
        return 1;
    }

    // SanitizePlayerStart directly: valid input is untouched and reported as
    // such; non-finite values fall back to defaults; every correction is listed.
    PlayerStartComponent valid = original.entities.back().player_start->config;
    if (!Expect(SanitizePlayerStart(valid).empty() && PlayerStartsMatch(valid, original.entities.back().player_start->config),
            "A valid configuration is not changed."))
    {
        return 1;
    }
    PlayerStartComponent broken;
    broken.move_speed = std::numeric_limits<rag::f32>::quiet_NaN();
    broken.capsule_height = std::numeric_limits<rag::f32>::infinity();
    broken.yaw_radians = -std::numeric_limits<rag::f32>::infinity();
    const std::string report = SanitizePlayerStart(broken);
    if (!Expect(PlayerStartsMatch(broken, PlayerStartComponent{}), "Non-finite values take the defaults.") ||
        !Expect(report.find("moveSpeed") != std::string::npos && report.find("capsuleHeight") != std::string::npos &&
                report.find("yawRadians") != std::string::npos,
            "Every correction is reported."))
    {
        return 1;
    }
    for (rag::f32 radius = PlayerStartLimits::MinCapsuleRadius; radius <= PlayerStartLimits::MaxCapsuleRadius; radius += 0.05f)
    {
        PlayerStartComponent wide;
        wide.capsule_radius = radius;
        wide.capsule_height = 0.0f;
        wide.eye_height = 100.0f;
        (void)SanitizePlayerStart(wide);
        if (!Expect(wide.capsule_height >= (2.0f * wide.capsule_radius) + PlayerStartLimits::MinCylinderHeight - 0.0001f &&
                    wide.capsule_height <= PlayerStartLimits::MaxCapsuleHeight &&
                    wide.eye_height <= wide.capsule_height && wide.eye_height >= PlayerStartLimits::MinEyeHeight,
                "Every allowed radius yields a valid capsule (cylinder > 0, eye inside)."))
        {
            return 1;
        }
    }

    RAG_LOG_INFO("Scene serialization tests passed.");
    return 0;
}
