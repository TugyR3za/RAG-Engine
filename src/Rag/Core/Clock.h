#pragma once

#include "Rag/Core/Base.h"

#include <chrono>

namespace rag::core
{
    struct FrameTime
    {
        f64 delta_seconds = 0.0;
        f64 total_seconds = 0.0;
        u64 frame_index = 0;
    };

    class Clock
    {
    public:
        Clock();

        void Reset();
        FrameTime Tick();

    private:
        using NativeClock = std::chrono::steady_clock;

        NativeClock::time_point start_time_;
        NativeClock::time_point previous_time_;
        u64 frame_index_ = 0;
    };
}
