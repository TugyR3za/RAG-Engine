#pragma once

#include "Rag/Scene/Entity.h"
#include "Rag/Scene/Scene.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace rag::editor
{
    // Developer-only editor state that deliberately does NOT live in the Scene
    // core: which entity is currently selected, plus the human-readable display
    // names the sandbox assigns so entities are identifiable in the Hierarchy
    // panel. Future panels (inspector, gizmos) read and set the selection through
    // this one object, so selection has a single owner outside the Scene.
    class EditorContext
    {
    public:
        // --- Selection ---------------------------------------------------------

        [[nodiscard]] scene::EntityId Selected() const { return selected_; }
        [[nodiscard]] bool HasSelection() const { return selected_.IsValid(); }

        [[nodiscard]] bool IsSelected(scene::EntityId entity) const
        {
            return selected_.IsValid() && selected_ == entity;
        }

        void Select(scene::EntityId entity) { selected_ = entity; }
        void ClearSelection() { selected_ = scene::EntityId{}; }

        // Drops the selection when the entity it points at is gone (destroyed, or
        // its index recycled under a new generation) so the editor never keeps a
        // stale id. Cheap enough to call every frame.
        void RefreshValidity(const scene::Scene& scene)
        {
            if (selected_.IsValid() && !scene.IsAlive(selected_))
            {
                ClearSelection();
            }
        }

        // --- Display names -----------------------------------------------------

        // Keyed by the full entity id (index + generation) so a recycled index can
        // never inherit a previous entity's name.
        void SetName(scene::EntityId entity, std::string_view name)
        {
            names_[entity.ToU64()] = std::string(name);
        }

        [[nodiscard]] const std::string* Name(scene::EntityId entity) const
        {
            const auto it = names_.find(entity.ToU64());
            return it != names_.end() ? &it->second : nullptr;
        }

    private:
        scene::EntityId selected_{};
        std::unordered_map<u64, std::string> names_;
    };
}
