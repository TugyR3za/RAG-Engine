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
}

int main()
{
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
    scene.ExtractRenderWorld(render_world);
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
