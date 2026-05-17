#pragma once

#include "Rag/Core/Log.h"

#include <cstdlib>

namespace rag::core
{
    [[noreturn]] inline void Abort()
    {
#if defined(RAG_COMPILER_MSVC)
        __debugbreak();
#elif defined(RAG_COMPILER_CLANG) || defined(RAG_COMPILER_GCC)
        __builtin_trap();
#endif
        std::abort();
    }
}

#if defined(RAG_ENABLE_ASSERTS)
    #define RAG_ASSERT(condition, ...)                                                                 \
        do                                                                                             \
        {                                                                                              \
            if (!(condition))                                                                          \
            {                                                                                          \
                RAG_LOG_FATAL("Assertion failed: ", #condition __VA_OPT__(, ". ", __VA_ARGS__));      \
                ::rag::core::Abort();                                                                  \
            }                                                                                          \
        } while (false)
#else
    #define RAG_ASSERT(condition, ...) ((void)0)
#endif
