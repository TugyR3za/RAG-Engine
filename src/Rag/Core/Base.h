#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
    #define RAG_COMPILER_MSVC 1
#elif defined(__clang__)
    #define RAG_COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define RAG_COMPILER_GCC 1
#else
    #define RAG_COMPILER_UNKNOWN 1
#endif

#if defined(RAG_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

#if !defined(RAG_ENGINE_VERSION)
    #define RAG_ENGINE_VERSION "0.0.0-local"
#endif

namespace rag
{
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    using i8 = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    using f32 = float;
    using f64 = double;
}
