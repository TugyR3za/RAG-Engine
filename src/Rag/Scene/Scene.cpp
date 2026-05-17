#include "Rag/Scene/Scene.h"

#include "Rag/Core/Assert.h"

#include <algorithm>

namespace rag::scene
{
    EntityId Scene::CreateEntity()
    {
        if (!free_entities_.empty())
        {
            const u32 index = free_entities_.back();
            free_entities_.pop_back();

            EntitySlot& slot = entity_slots_[index];
            slot.alive = true;
            ++alive_entity_count_;
            return EntityId{index, slot.generation};
        }

        const u32 index = static_cast<u32>(entity_slots_.size());
        entity_slots_.push_back(EntitySlot{});
        entity_slots_.back().alive = true;
        ++alive_entity_count_;
        return EntityId{index, entity_slots_.back().generation};
    }

    void Scene::DestroyEntity(EntityId entity)
    {
        if (!IsAlive(entity))
        {
            return;
        }

        std::vector<EntityId> entities_to_destroy;
        CollectTransformSubtree(entity, entities_to_destroy);
        if (entities_to_destroy.empty())
        {
            entities_to_destroy.push_back(entity);
        }

        DetachFromParent(entity);

        for (auto it = entities_to_destroy.rbegin(); it != entities_to_destroy.rend(); ++it)
        {
            DestroyEntityInternal(*it);
        }
    }

    bool Scene::IsAlive(EntityId entity) const
    {
        return entity.IsValid() &&
               entity.index < entity_slots_.size() &&
               entity_slots_[entity.index].alive &&
               entity_slots_[entity.index].generation == entity.generation;
    }

    std::size_t Scene::EntityCount() const
    {
        return alive_entity_count_;
    }

    TransformComponent& Scene::AddTransform(EntityId entity)
    {
        RAG_ASSERT(IsAlive(entity), "Cannot add TransformComponent to a dead entity.");
        TransformComponent& transform = transforms_.Add(entity);
        transform.dirty = true;
        return transform;
    }

    bool Scene::RemoveTransform(EntityId entity)
    {
        TransformComponent* transform = transforms_.Get(entity);
        if (transform == nullptr)
        {
            return false;
        }

        DetachFromParent(entity);

        EntityId child = transform->first_child;
        while (child.IsValid())
        {
            TransformComponent* child_transform = transforms_.Get(child);
            if (child_transform == nullptr)
            {
                break;
            }

            const EntityId next_child = child_transform->next_sibling;
            child_transform->parent = EntityId{};
            child_transform->previous_sibling = EntityId{};
            child_transform->next_sibling = EntityId{};
            child_transform->dirty = true;
            child = next_child;
        }

        return transforms_.Remove(entity);
    }

    TransformComponent* Scene::GetTransform(EntityId entity)
    {
        return transforms_.Get(entity);
    }

    const TransformComponent* Scene::GetTransform(EntityId entity) const
    {
        return transforms_.Get(entity);
    }

    CameraComponent& Scene::AddCamera(EntityId entity)
    {
        RAG_ASSERT(IsAlive(entity), "Cannot add CameraComponent to a dead entity.");
        return cameras_.Add(entity);
    }

    bool Scene::RemoveCamera(EntityId entity)
    {
        if (active_camera_ == entity)
        {
            active_camera_ = EntityId{};
        }

        return cameras_.Remove(entity);
    }

    CameraComponent* Scene::GetCamera(EntityId entity)
    {
        return cameras_.Get(entity);
    }

    const CameraComponent* Scene::GetCamera(EntityId entity) const
    {
        return cameras_.Get(entity);
    }

    void Scene::SetActiveCamera(EntityId entity)
    {
        if (!IsAlive(entity) || cameras_.Get(entity) == nullptr)
        {
            active_camera_ = EntityId{};
            return;
        }

        for (std::size_t index = 0; index < cameras_.Size(); ++index)
        {
            cameras_.ComponentAt(index).active = cameras_.EntityAt(index) == entity;
        }

        active_camera_ = entity;
    }

    EntityId Scene::ActiveCamera() const
    {
        return active_camera_;
    }

    RenderableComponent& Scene::AddRenderable(EntityId entity)
    {
        RAG_ASSERT(IsAlive(entity), "Cannot add RenderableComponent to a dead entity.");
        return renderables_.Add(entity);
    }

    bool Scene::RemoveRenderable(EntityId entity)
    {
        return renderables_.Remove(entity);
    }

    RenderableComponent* Scene::GetRenderable(EntityId entity)
    {
        return renderables_.Get(entity);
    }

    const RenderableComponent* Scene::GetRenderable(EntityId entity) const
    {
        return renderables_.Get(entity);
    }

    LightComponent& Scene::AddLight(EntityId entity)
    {
        RAG_ASSERT(IsAlive(entity), "Cannot add LightComponent to a dead entity.");
        return lights_.Add(entity);
    }

    bool Scene::RemoveLight(EntityId entity)
    {
        return lights_.Remove(entity);
    }

    LightComponent* Scene::GetLight(EntityId entity)
    {
        return lights_.Get(entity);
    }

    const LightComponent* Scene::GetLight(EntityId entity) const
    {
        return lights_.Get(entity);
    }

    bool Scene::SetParent(EntityId child, EntityId parent)
    {
        if (!IsAlive(child) || !IsAlive(parent) || child == parent || WouldCreateCycle(child, parent))
        {
            return false;
        }

        TransformComponent* child_transform = transforms_.Get(child);
        TransformComponent* parent_transform = transforms_.Get(parent);
        if (child_transform == nullptr || parent_transform == nullptr)
        {
            return false;
        }

        DetachFromParent(child);

        child_transform = transforms_.Get(child);
        parent_transform = transforms_.Get(parent);
        child_transform->parent = parent;
        child_transform->previous_sibling = EntityId{};
        child_transform->next_sibling = parent_transform->first_child;

        if (parent_transform->first_child.IsValid())
        {
            TransformComponent* old_first_child = transforms_.Get(parent_transform->first_child);
            if (old_first_child != nullptr)
            {
                old_first_child->previous_sibling = child;
            }
        }

        parent_transform->first_child = child;
        MarkTransformSubtreeDirty(child);
        return true;
    }

    void Scene::ClearParent(EntityId child)
    {
        DetachFromParent(child);
        MarkTransformSubtreeDirty(child);
    }

    void Scene::MarkTransformDirty(EntityId entity)
    {
        MarkTransformSubtreeDirty(entity);
    }

    void Scene::Update()
    {
        for (std::size_t index = 0; index < transforms_.Size(); ++index)
        {
            const EntityId entity = transforms_.EntityAt(index);
            const TransformComponent& transform = transforms_.ComponentAt(index);
            if (!transform.parent.IsValid() || transforms_.Get(transform.parent) == nullptr)
            {
                UpdateTransformSubtree(entity, math::Identity());
            }
        }

        if (!IsAlive(active_camera_) || cameras_.Get(active_camera_) == nullptr)
        {
            active_camera_ = EntityId{};
            for (std::size_t index = 0; index < cameras_.Size(); ++index)
            {
                if (cameras_.ComponentAt(index).active)
                {
                    active_camera_ = cameras_.EntityAt(index);
                    break;
                }
            }
        }
    }

    void Scene::ExtractRenderWorld(renderer::RenderWorld& out_world) const
    {
        out_world.Clear();
        out_world.objects.reserve(renderables_.Size());
        out_world.lights.reserve(lights_.Size());

        if (const CameraComponent* camera = cameras_.Get(active_camera_))
        {
            out_world.camera = BuildRenderCamera(active_camera_, *camera);
        }

        for (std::size_t index = 0; index < renderables_.Size(); ++index)
        {
            const RenderableComponent& renderable = renderables_.ComponentAt(index);
            if (!renderable.visible)
            {
                continue;
            }

            const EntityId entity = renderables_.EntityAt(index);
            const TransformComponent* transform = transforms_.Get(entity);
            if (transform == nullptr)
            {
                continue;
            }

            renderer::RenderObject object{};
            object.object_id = entity.ToU64();
            object.world_transform = transform->world_matrix;
            object.mesh = renderable.mesh;
            object.material = renderable.material;
            object.local_bounds = renderable.local_bounds;
            object.visibility_layer = renderable.visibility_layer;
            out_world.objects.push_back(object);
        }

        for (std::size_t index = 0; index < lights_.Size(); ++index)
        {
            const LightComponent& light = lights_.ComponentAt(index);
            if (!light.visible)
            {
                continue;
            }

            const EntityId entity = lights_.EntityAt(index);
            const TransformComponent* transform = transforms_.Get(entity);
            if (transform == nullptr)
            {
                continue;
            }

            renderer::RenderLight render_light{};
            render_light.object_id = entity.ToU64();
            render_light.type = light.type;
            render_light.position = math::ExtractTranslation(transform->world_matrix);
            render_light.direction = math::TransformDirection(transform->world_matrix, math::Vec3{0.0f, -1.0f, 0.0f});
            render_light.color = light.color;
            render_light.intensity = light.intensity;
            render_light.range = light.range;
            render_light.inner_cone_radians = light.inner_cone_radians;
            render_light.outer_cone_radians = light.outer_cone_radians;
            render_light.visibility_layer = light.visibility_layer;
            out_world.lights.push_back(render_light);
        }
    }

    void Scene::DestroyEntityInternal(EntityId entity)
    {
        if (!IsAlive(entity))
        {
            return;
        }

        (void)RemoveCamera(entity);
        (void)RemoveRenderable(entity);
        (void)RemoveLight(entity);
        transforms_.Remove(entity);

        EntitySlot& slot = entity_slots_[entity.index];
        slot.alive = false;
        ++slot.generation;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }

        free_entities_.push_back(entity.index);
        --alive_entity_count_;
    }

    void Scene::CollectTransformSubtree(EntityId entity, std::vector<EntityId>& out_entities) const
    {
        if (!IsAlive(entity))
        {
            return;
        }

        out_entities.push_back(entity);

        const TransformComponent* transform = transforms_.Get(entity);
        if (transform == nullptr)
        {
            return;
        }

        EntityId child = transform->first_child;
        while (child.IsValid())
        {
            const TransformComponent* child_transform = transforms_.Get(child);
            const EntityId next_child = child_transform != nullptr ? child_transform->next_sibling : EntityId{};
            CollectTransformSubtree(child, out_entities);
            child = next_child;
        }
    }

    void Scene::DetachFromParent(EntityId child)
    {
        TransformComponent* child_transform = transforms_.Get(child);
        if (child_transform == nullptr || !child_transform->parent.IsValid())
        {
            return;
        }

        TransformComponent* parent_transform = transforms_.Get(child_transform->parent);
        if (parent_transform != nullptr && parent_transform->first_child == child)
        {
            parent_transform->first_child = child_transform->next_sibling;
        }

        if (child_transform->previous_sibling.IsValid())
        {
            TransformComponent* previous = transforms_.Get(child_transform->previous_sibling);
            if (previous != nullptr)
            {
                previous->next_sibling = child_transform->next_sibling;
            }
        }

        if (child_transform->next_sibling.IsValid())
        {
            TransformComponent* next = transforms_.Get(child_transform->next_sibling);
            if (next != nullptr)
            {
                next->previous_sibling = child_transform->previous_sibling;
            }
        }

        child_transform->parent = EntityId{};
        child_transform->previous_sibling = EntityId{};
        child_transform->next_sibling = EntityId{};
    }

    bool Scene::WouldCreateCycle(EntityId child, EntityId parent) const
    {
        EntityId current = parent;
        while (current.IsValid())
        {
            if (current == child)
            {
                return true;
            }

            const TransformComponent* transform = transforms_.Get(current);
            current = transform != nullptr ? transform->parent : EntityId{};
        }

        return false;
    }

    void Scene::MarkTransformSubtreeDirty(EntityId entity)
    {
        TransformComponent* transform = transforms_.Get(entity);
        if (transform == nullptr)
        {
            return;
        }

        transform->dirty = true;

        EntityId child = transform->first_child;
        while (child.IsValid())
        {
            TransformComponent* child_transform = transforms_.Get(child);
            const EntityId next_child = child_transform != nullptr ? child_transform->next_sibling : EntityId{};
            MarkTransformSubtreeDirty(child);
            child = next_child;
        }
    }

    void Scene::UpdateTransformSubtree(EntityId entity, const math::Mat4& parent_world)
    {
        TransformComponent* transform = transforms_.Get(entity);
        if (transform == nullptr)
        {
            return;
        }

        transform->local_matrix = math::ComposeTransform(
            transform->local_position,
            transform->local_rotation_radians,
            transform->local_scale);
        transform->world_matrix = math::Multiply(parent_world, transform->local_matrix);
        transform->dirty = false;

        EntityId child = transform->first_child;
        while (child.IsValid())
        {
            TransformComponent* child_transform = transforms_.Get(child);
            const EntityId next_child = child_transform != nullptr ? child_transform->next_sibling : EntityId{};
            UpdateTransformSubtree(child, transform->world_matrix);
            child = next_child;
        }
    }

    renderer::RenderCamera Scene::BuildRenderCamera(EntityId camera_entity, const CameraComponent& camera) const
    {
        renderer::RenderCamera render_camera{};
        const TransformComponent* transform = transforms_.Get(camera_entity);
        if (transform == nullptr)
        {
            return render_camera;
        }

        render_camera.valid = true;
        render_camera.view = math::InverseRigidTransform(transform->world_matrix);
        render_camera.position = math::ExtractTranslation(transform->world_matrix);
        render_camera.near_plane = camera.near_plane;
        render_camera.far_plane = camera.far_plane;

        if (camera.projection_mode == CameraProjectionMode::Perspective)
        {
            render_camera.projection = math::PerspectiveRH_ZO(
                camera.vertical_fov_radians,
                camera.aspect_ratio,
                camera.near_plane,
                camera.far_plane);
        }
        else
        {
            render_camera.projection = math::OrthographicRH_ZO(
                camera.orthographic_height * camera.aspect_ratio,
                camera.orthographic_height,
                camera.near_plane,
                camera.far_plane);
        }

        return render_camera;
    }
}
