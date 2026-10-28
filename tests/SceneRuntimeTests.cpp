#include "Rag/Core/Log.h"
#include "Rag/Scene/Scene.h"

#include <cmath>

namespace
{
    bool NearlyEqual(rag::f32 left, rag::f32 right)
    {
        return std::fabs(left - right) <= 0.0001f;
    }

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            RAG_LOG_ERROR("Scene test failed: ", message);
            return false;
        }

        return true;
    }

    bool Vec3Near(rag::math::Vec3 left, rag::math::Vec3 right)
    {
        return NearlyEqual(left.x, right.x) && NearlyEqual(left.y, right.y) && NearlyEqual(left.z, right.z);
    }

    const rag::renderer::RenderObject* FindObject(
        const rag::renderer::RenderWorld& world,
        rag::scene::EntityId entity)
    {
        for (const rag::renderer::RenderObject& object : world.objects)
        {
            if (object.object_id == entity.ToU64())
            {
                return &object;
            }
        }
        return nullptr;
    }

    // Per-object base-color materials: defaults, color conversion, extraction,
    // independence of entities sharing a mesh, and entity lifetime.
    bool MaterialTests()
    {
        using rag::renderer::MaterialSettings;
        using rag::renderer::SrgbToLinear;

        // Defaults reproduce the pre-material look: white tint, texture on.
        const MaterialSettings defaults{};
        const rag::scene::RenderableComponent default_renderable{};
        const rag::renderer::RenderObject default_object{};
        if (!Expect(Vec3Near(defaults.base_color, {1.0f, 1.0f, 1.0f}), "Default base color must be white.") ||
            !Expect(defaults.use_base_color_texture, "Base-color texture must be enabled by default.") ||
            !Expect(default_renderable.material_settings == defaults, "New renderables must carry default material.") ||
            !Expect(Vec3Near(default_object.base_color_linear, {1.0f, 1.0f, 1.0f}) &&
                    default_object.use_base_color_texture, "Default RenderObject must draw the texture untinted."))
        {
            return false;
        }

        // IEC 61966-2-1 sRGB decode: exact endpoints, both curve segments, clamping.
        if (!Expect(SrgbToLinear(0.0f) == 0.0f && SrgbToLinear(1.0f) == 1.0f, "sRGB endpoints must be exact.") ||
            !Expect(NearlyEqual(SrgbToLinear(0.5f), 0.214041f), "sRGB 0.5 must decode to ~0.2140 linear.") ||
            !Expect(NearlyEqual(SrgbToLinear(0.04045f), 0.04045f / 12.92f), "Low sRGB values use the linear segment.") ||
            !Expect(SrgbToLinear(2.0f) == 1.0f && SrgbToLinear(-1.0f) == 0.0f, "sRGB input must be clamped.") ||
            !Expect(SrgbToLinear(std::nanf("")) == 1.0f, "Non-finite sRGB input must fall back to white."))
        {
            return false;
        }

        rag::scene::Scene scene;
        constexpr rag::renderer::MeshHandle CubeMesh{0, 0};
        constexpr rag::renderer::MeshHandle DuckMesh{3, 0};
        const auto make_renderable = [&scene](rag::renderer::MeshHandle mesh) {
            const rag::scene::EntityId entity = scene.CreateEntity();
            scene.AddTransform(entity);
            scene.AddRenderable(entity).mesh = mesh;
            return entity;
        };

        // Three cubes share one mesh (and so one texture); the duck has its own.
        const rag::scene::EntityId red_cube = make_renderable(CubeMesh);
        const rag::scene::EntityId blue_cube = make_renderable(CubeMesh);
        const rag::scene::EntityId plain_cube = make_renderable(CubeMesh);
        const rag::scene::EntityId duck = make_renderable(DuckMesh);
        scene.GetRenderable(red_cube)->material_settings = MaterialSettings{{1.0f, 0.0f, 0.0f}, false};
        scene.GetRenderable(blue_cube)->material_settings = MaterialSettings{{0.0f, 0.0f, 1.0f}, false};
        scene.GetRenderable(duck)->material_settings.base_color = rag::math::Vec3{0.5f, 0.5f, 0.5f};

        rag::renderer::RenderWorld world;
        scene.Update();
        scene.ExtractRenderWorld(world);
        const rag::renderer::RenderObject* red = FindObject(world, red_cube);
        const rag::renderer::RenderObject* blue = FindObject(world, blue_cube);
        const rag::renderer::RenderObject* plain = FindObject(world, plain_cube);
        const rag::renderer::RenderObject* duck_object = FindObject(world, duck);
        if (!Expect(red && blue && plain && duck_object, "Every material test entity must be extracted.") ||
            !Expect(red->mesh.index == blue->mesh.index && blue->mesh.index == plain->mesh.index,
                "The cubes must share one mesh for the independence check to mean anything.") ||
            !Expect(Vec3Near(red->base_color_linear, {1.0f, 0.0f, 0.0f}) && !red->use_base_color_texture,
                "Red cube must extract as solid red without its texture.") ||
            !Expect(Vec3Near(blue->base_color_linear, {0.0f, 0.0f, 1.0f}) && !blue->use_base_color_texture,
                "Blue cube must extract as solid blue without its texture.") ||
            !Expect(Vec3Near(plain->base_color_linear, {1.0f, 1.0f, 1.0f}) && plain->use_base_color_texture,
                "A cube sharing the mesh must keep the default material.") ||
            !Expect(Vec3Near(duck_object->base_color_linear, {0.214041f, 0.214041f, 0.214041f}) &&
                    duck_object->use_base_color_texture,
                "Tinted duck must keep its texture and extract a linear-space tint."))
        {
            return false;
        }

        // Editing one entity after the fact leaves its mesh-mates untouched.
        scene.GetRenderable(red_cube)->material_settings.base_color = rag::math::Vec3{0.0f, 1.0f, 0.0f};
        scene.GetRenderable(duck)->material_settings = MaterialSettings{};
        scene.ExtractRenderWorld(world);
        if (!Expect(Vec3Near(FindObject(world, red_cube)->base_color_linear, {0.0f, 1.0f, 0.0f}),
                "Material edits must appear in the next extraction.") ||
            !Expect(Vec3Near(FindObject(world, blue_cube)->base_color_linear, {0.0f, 0.0f, 1.0f}) &&
                    Vec3Near(FindObject(world, plain_cube)->base_color_linear, {1.0f, 1.0f, 1.0f}),
                "Editing one cube must not change cubes sharing its mesh.") ||
            !Expect(scene.GetRenderable(duck)->material_settings == MaterialSettings{} &&
                    Vec3Near(FindObject(world, duck)->base_color_linear, {1.0f, 1.0f, 1.0f}),
                "Resetting the duck must restore its default appearance."))
        {
            return false;
        }

        // A recycled entity slot must not inherit the destroyed entity's material.
        scene.DestroyEntity(red_cube);
        const rag::scene::EntityId recycled = scene.CreateEntity();
        scene.AddTransform(recycled);
        const rag::scene::RenderableComponent& recycled_renderable = scene.AddRenderable(recycled);
        if (!Expect(recycled.index == red_cube.index, "Test expects the destroyed slot to be recycled.") ||
            !Expect(recycled_renderable.material_settings == MaterialSettings{},
                "A recycled entity must start with the default material."))
        {
            return false;
        }
        scene.ExtractRenderWorld(world);
        return Expect(FindObject(world, red_cube) == nullptr && world.objects.size() == 4,
            "Destroyed entities must leave no render object behind.");
    }
}

int main()
{
    if (!MaterialTests())
    {
        return 1;
    }

    rag::scene::Scene scene;

    const rag::scene::EntityId parent = scene.CreateEntity();
    rag::scene::TransformComponent& parent_transform = scene.AddTransform(parent);
    parent_transform.local_position = rag::math::Vec3{10.0f, 0.0f, 0.0f};

    const rag::scene::EntityId child = scene.CreateEntity();
    rag::scene::TransformComponent& child_transform = scene.AddTransform(child);
    child_transform.local_position = rag::math::Vec3{0.0f, 2.0f, 0.0f};

    if (!Expect(scene.SetParent(child, parent), "SetParent should succeed for two live transform entities."))
    {
        return 1;
    }

    const rag::scene::EntityId camera_entity = scene.CreateEntity();
    rag::scene::TransformComponent& camera_transform = scene.AddTransform(camera_entity);
    camera_transform.local_position = rag::math::Vec3{0.0f, 0.0f, 5.0f};
    rag::scene::CameraComponent& camera = scene.AddCamera(camera_entity);
    camera.active = true;
    scene.SetActiveCamera(camera_entity);

    rag::scene::RenderableComponent& renderable = scene.AddRenderable(child);
    renderable.mesh = rag::renderer::MeshHandle{1, 1};
    renderable.material = rag::renderer::MaterialHandle{2, 1};

    const rag::scene::EntityId light_entity = scene.CreateEntity();
    scene.AddTransform(light_entity);
    rag::scene::LightComponent& light = scene.AddLight(light_entity);
    light.type = rag::renderer::RenderLightType::Point;

    scene.Update();

    const rag::scene::TransformComponent* updated_child = scene.GetTransform(child);
    const rag::math::Vec3 child_world_position = rag::math::ExtractTranslation(updated_child->world_matrix);
    if (!Expect(NearlyEqual(child_world_position.x, 10.0f), "Child world X should inherit parent translation.") ||
        !Expect(NearlyEqual(child_world_position.y, 2.0f), "Child world Y should keep local offset."))
    {
        return 1;
    }

    rag::renderer::RenderWorld render_world;
    scene.Environment() = {true, {"px", "nx", "py", "ny", "pz", "nz"}};
    scene.ExtractRenderWorld(render_world);
    if (!Expect(render_world.environment == scene.Environment(), "Extraction must carry scene environment.")) return 1;
    scene.Environment() = {};
    scene.ExtractRenderWorld(render_world);
    if (!Expect(!render_world.environment.skybox_enabled && render_world.environment.skybox_faces[0].empty(),
            "Extraction must clear stale environment settings.")) return 1;
    if (!Expect(render_world.camera.valid, "Render extraction should include an active camera.") ||
        !Expect(render_world.objects.size() == 1, "Render extraction should include one render object.") ||
        !Expect(render_world.lights.size() == 1, "Render extraction should include one light."))
    {
        return 1;
    }

    scene.DestroyEntity(parent);
    if (!Expect(!scene.IsAlive(parent), "Destroyed parent should no longer be alive.") ||
        !Expect(!scene.IsAlive(child), "Destroying a parent transform should destroy the child subtree.") ||
        !Expect(scene.GetRenderable(child) == nullptr, "Destroyed child renderable should be removed."))
    {
        return 1;
    }

    const rag::scene::EntityId recycled = scene.CreateEntity();
    if (!Expect(recycled.index == child.index || recycled.index == parent.index, "Entity registry should recycle destroyed indices.") ||
        !Expect(recycled != child && recycled != parent, "Recycled entity should have a new generation."))
    {
        return 1;
    }

    RAG_LOG_INFO("Scene runtime tests passed.");
    return 0;
}
