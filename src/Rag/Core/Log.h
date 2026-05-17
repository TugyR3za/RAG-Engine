#pragma once

#include "Rag/Core/Base.h"

#include <mutex>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>

namespace rag::core
{
    enum class LogLevel : u8
    {
        Trace,
        Info,
        Warning,
        Error,
        Fatal
    };

    class Log
    {
    public:
        static void SetMinimumLevel(LogLevel level);
        static LogLevel GetMinimumLevel();

        static void Write(
            LogLevel level,
            std::string_view message,
            const std::source_location& location = std::source_location::current());

    private:
        static const char* LevelName(LogLevel level);
        static std::mutex& Mutex();
        static LogLevel& MinimumLevel();
    };

    namespace detail
    {
        template <typename... Args>
        std::string BuildLogMessage(Args&&... args)
        {
            std::ostringstream stream;
            (stream << ... << std::forward<Args>(args));
            return stream.str();
        }
    }
}

#define RAG_LOG_TRACE(...) ::rag::core::Log::Write(::rag::core::LogLevel::Trace, ::rag::core::detail::BuildLogMessage(__VA_ARGS__), std::source_location::current())
#define RAG_LOG_INFO(...) ::rag::core::Log::Write(::rag::core::LogLevel::Info, ::rag::core::detail::BuildLogMessage(__VA_ARGS__), std::source_location::current())
#define RAG_LOG_WARNING(...) ::rag::core::Log::Write(::rag::core::LogLevel::Warning, ::rag::core::detail::BuildLogMessage(__VA_ARGS__), std::source_location::current())
#define RAG_LOG_ERROR(...) ::rag::core::Log::Write(::rag::core::LogLevel::Error, ::rag::core::detail::BuildLogMessage(__VA_ARGS__), std::source_location::current())
#define RAG_LOG_FATAL(...) ::rag::core::Log::Write(::rag::core::LogLevel::Fatal, ::rag::core::detail::BuildLogMessage(__VA_ARGS__), std::source_location::current())
