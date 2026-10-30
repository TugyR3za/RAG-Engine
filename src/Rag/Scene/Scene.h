#pragma once

#include "Rag/Renderer/Renderer.h"
#include "Rag/Scene/ComponentStorage.h"
#include "Rag/Scene/Components.h"

#include <cstddef>
#include <vector>

namespace rag::scene
{
    class Scene final
    {
    public:
        Scene() = default;
        ~Scene() = default;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        [[nodiscard]] EntityId CreateEntity();
        void DestroyEntity(EntityId entity);

        // Destroys every live entity (each id becomes stale, as with DestroyEntity)
        // and resets slot reuse so the following CreateEntity calls hand out slots
        // 0, 1, 2, ... in order. A scene rebuilt from a description after this
        // therefore iterates in the same order the description lists entities.
        void DestroyAllEntities();
        [[nodiscard]] bool IsAlive(EntityId entity) const;
        [[nodiscard]] std::size_t EntityCount() const;

        // Invokes visit(EntityId) once for every live entity, in slot (creation)
        // order. Header-only and allocation-free so tools and the editor can walk
        // the whole scene each frame; the core scene keeps no separate entity list.
        template <typename Visitor>
        void ForEachEntity(Visitor&& visit) const
        {
            for (u32 index = 0; index < static_cast<u32>(entity_slots_.size()); ++index)
            {
                if (entity_slots_[index].alive)
                {
                    visit(EntityId{index, entity_slots_[index].generation});
                }
            }
        }

        TransformComponent& AddTransform(EntityId entity);
        [[nodiscard]] bool RemoveTransform(EntityId entity);
        [[nodiscard]] TransformComponent* GetTransform(EntityId entity);
        [[nodiscard]] const TransformComponent* GetTransform(EntityId entity) const;

        CameraComponent& AddCamera(EntityId entity);
        [[nodiscard]] bool RemoveCamera(EntityId entity);
        [[nodiscard]] CameraComponent* GetCamera(EntityId entity);
        [[nodiscard]] const CameraComponent* GetCamera(EntityId entity) const;
        void SetActiveCamera(EntityId entity);
        [[nodiscard]] EntityId ActiveCamera() const;

        RenderableComponent& AddRenderable(EntityId entity);
        [[nodiscard]] bool RemoveRenderable(EntityId entity);
        [[nodiscard]] RenderableComponent* GetRenderable(EntityId entity);
        [[nodiscard]] const RenderableComponent* GetRenderable(EntityId entity) const;

        LightComponent& AddLight(EntityId entity);
        [[nodiscard]] bool RemoveLight(EntityId entity);
        [[nodiscard]] LightComponent* GetLight(EntityId entity);
        [[nodiscard]] const LightComponent* GetLight(EntityId entity) const;

        PlayerStartComponent& AddPlayerStart(EntityId entity);
        [[nodiscard]] bool RemovePlayerStart(EntityId entity);
        [[nodiscard]] PlayerStartComponent* GetPlayerStart(EntityId entity);
        [[nodiscard]] const PlayerStartComponent* GetPlayerStart(EntityId entity) const;
        [[nodiscard]] std::size_t PlayerStartCount() const;

        // The Player Start that Play uses when several exist: the first one in
        // entity slot order -- the order ForEachEntity and the Hierarchy list
        // entities, which after a load or Stop is the scene file's own order.
        // Invalid when there is none.
        [[nodiscard]] EntityId FirstPlayerStart() const;

        [[nodiscard]] bool SetParent(EntityId child, EntityId parent);
        void ClearParent(EntityId child);
        void MarkTransformDirty(EntityId entity);

        void Update();
        void ExtractRenderWorld(renderer::RenderWorld& out_world) const;

        [[nodiscard]] renderer::EnvironmentSettings& Environment() { return environment_; }
        [[nodiscard]] const renderer::EnvironmentSettings& Environment() const { return environment_; }

    private:
        struct EntitySlot
        {
            u32 generation = 1;
            bool alive = false;
        };

        void DestroyEntityInternal(EntityId entity);
        void CollectTransformSubtree(EntityId entity, std::vector<EntityId>& out_entities) const;
        void DetachFromParent(EntityId child);
        [[nodiscard]] bool WouldCreateCycle(EntityId child, EntityId parent) const;
        void MarkTransformSubtreeDirty(EntityId entity);
        void UpdateTransformSubtree(EntityId entity, const math::Mat4& parent_world);
        [[nodiscard]] renderer::RenderCamera BuildRenderCamera(EntityId camera_entity, const CameraComponent& camera) const;

        std::vector<EntitySlot> entity_slots_;
        std::vector<u32> free_entities_;
        std::size_t alive_entity_count_ = 0;

        ComponentStorage<TransformComponent> transforms_;
        ComponentStorage<CameraComponent> cameras_;
        ComponentStorage<RenderableComponent> renderables_;
        ComponentStorage<LightComponent> lights_;
        ComponentStorage<PlayerStartComponent> player_starts_;

        EntityId active_camera_{};
        renderer::EnvironmentSettings environment_{};
    };
}
