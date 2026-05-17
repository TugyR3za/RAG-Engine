#include "Rag/Core/Clock.h"

namespace rag::core
{
    Clock::Clock()
    {
        Reset();
    }

    void Clock::Reset()
    {
        start_time_ = NativeClock::now();
        previous_time_ = start_time_;
        frame_index_ = 0;
    }

    FrameTime Clock::Tick()
    {
        const auto now = NativeClock::now();
        const std::chrono::duration<f64> delta = now - previous_time_;
        const std::chrono::duration<f64> total = now - start_time_;

        previous_time_ = now;

        FrameTime frame_time{};
        frame_time.delta_seconds = delta.count();
        frame_time.total_seconds = total.count();
        frame_time.frame_index = frame_index_++;
        return frame_time;
    }
}
