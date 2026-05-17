#include "Rag/Core/Log.h"

#include <chrono>
#include <iomanip>
#include <iostream>

namespace rag::core
{
    void Log::SetMinimumLevel(LogLevel level)
    {
        std::scoped_lock lock(Mutex());
        MinimumLevel() = level;
    }

    LogLevel Log::GetMinimumLevel()
    {
        std::scoped_lock lock(Mutex());
        return MinimumLevel();
    }

    void Log::Write(LogLevel level, std::string_view message, const std::source_location& location)
    {
        std::scoped_lock lock(Mutex());

        if (static_cast<u8>(level) < static_cast<u8>(MinimumLevel()))
        {
            return;
        }

        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      now.time_since_epoch()) %
                                  1000;

        std::tm local_time{};
#if defined(RAG_PLATFORM_WINDOWS)
        localtime_s(&local_time, &time);
#else
        localtime_r(&time, &local_time);
#endif

        std::ostream& stream = level >= LogLevel::Error ? std::cerr : std::cout;
        stream << '[' << std::put_time(&local_time, "%H:%M:%S")
               << '.' << std::setfill('0') << std::setw(3) << milliseconds.count()
               << std::setfill(' ') << ']'
               << '[' << LevelName(level) << "] "
               << message
               << " (" << location.file_name() << ':' << location.line() << ")\n";
    }

    const char* Log::LevelName(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "Trace";
        case LogLevel::Info:
            return "Info";
        case LogLevel::Warning:
            return "Warning";
        case LogLevel::Error:
            return "Error";
        case LogLevel::Fatal:
            return "Fatal";
        default:
            return "Unknown";
        }
    }

    std::mutex& Log::Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    LogLevel& Log::MinimumLevel()
    {
#if defined(RAG_DEBUG)
        static LogLevel level = LogLevel::Trace;
#else
        static LogLevel level = LogLevel::Info;
#endif
        return level;
    }
}
