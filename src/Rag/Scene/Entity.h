#pragma once

#include "Rag/Core/Base.h"

namespace rag::scene
{
    inline constexpr u32 InvalidEntityIndex = UINT32_MAX;

    struct EntityId
    {
        u32 index = InvalidEntityIndex;
        u32 generation = 0;

        [[nodiscard]] bool IsValid() const
        {
            return index != InvalidEntityIndex;
        }

        [[nodiscard]] u64 ToU64() const
        {
            return (static_cast<u64>(generation) << 32u) | static_cast<u64>(index);
        }
    };

    [[nodiscard]] inline bool operator==(EntityId left, EntityId right)
    {
        return left.index == right.index && left.generation == right.generation;
    }

    [[nodiscard]] inline bool operator!=(EntityId left, EntityId right)
    {
        return !(left == right);
    }
}
