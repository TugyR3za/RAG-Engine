#pragma once

#include "Rag/Scene/Entity.h"

#include <utility>
#include <vector>

namespace rag::scene
{
    template <typename TComponent>
    class ComponentStorage
    {
    public:
        template <typename... Args>
        TComponent& Add(EntityId entity, Args&&... args)
        {
            if (TComponent* existing = Get(entity))
            {
                return *existing;
            }

            EnsureSparseSize(entity.index);
            sparse_[entity.index] = static_cast<u32>(components_.size());
            entities_.push_back(entity);
            components_.emplace_back(std::forward<Args>(args)...);
            return components_.back();
        }

        bool Remove(EntityId entity)
        {
            if (!Contains(entity))
            {
                return false;
            }

            const u32 dense_index = sparse_[entity.index];
            const u32 last_index = static_cast<u32>(components_.size() - 1u);

            if (dense_index != last_index)
            {
                components_[dense_index] = std::move(components_[last_index]);
                entities_[dense_index] = entities_[last_index];
                sparse_[entities_[dense_index].index] = dense_index;
            }

            components_.pop_back();
            entities_.pop_back();
            sparse_[entity.index] = InvalidEntityIndex;
            return true;
        }

        [[nodiscard]] bool Contains(EntityId entity) const
        {
            if (!entity.IsValid() || entity.index >= sparse_.size())
            {
                return false;
            }

            const u32 dense_index = sparse_[entity.index];
            return dense_index != InvalidEntityIndex &&
                   dense_index < entities_.size() &&
                   entities_[dense_index] == entity;
        }

        [[nodiscard]] TComponent* Get(EntityId entity)
        {
            if (!Contains(entity))
            {
                return nullptr;
            }

            return &components_[sparse_[entity.index]];
        }

        [[nodiscard]] const TComponent* Get(EntityId entity) const
        {
            if (!Contains(entity))
            {
                return nullptr;
            }

            return &components_[sparse_[entity.index]];
        }

        [[nodiscard]] std::size_t Size() const
        {
            return components_.size();
        }

        [[nodiscard]] bool Empty() const
        {
            return components_.empty();
        }

        [[nodiscard]] EntityId EntityAt(std::size_t index) const
        {
            return entities_[index];
        }

        [[nodiscard]] TComponent& ComponentAt(std::size_t index)
        {
            return components_[index];
        }

        [[nodiscard]] const TComponent& ComponentAt(std::size_t index) const
        {
            return components_[index];
        }

        void Clear()
        {
            components_.clear();
            entities_.clear();
            sparse_.clear();
        }

    private:
        void EnsureSparseSize(u32 entity_index)
        {
            if (entity_index >= sparse_.size())
            {
                sparse_.resize(static_cast<std::size_t>(entity_index) + 1u, InvalidEntityIndex);
            }
        }

        std::vector<EntityId> entities_;
        std::vector<TComponent> components_;
        std::vector<u32> sparse_;
    };
}
