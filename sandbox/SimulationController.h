#pragma once

#include "Rag/Core/Base.h"

#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace rag::runtime
{
    // Whether the scene is being authored or simulated. This is deliberately
    // independent of whether the editor panels are visible (F1): the panels-hidden
    // "game view" can be in any of these states.
    //   Edit    - authoring. No simulation time passes; editor edits are the truth.
    //   Playing - a test run: simulation advances every frame. Entered from Edit,
    //             which first captures a snapshot of the authored scene.
    //   Paused  - the test run frozen mid-flight. Rendering, editor navigation and
    //             inspection continue; Step advances exactly one tick.
    enum class SimulationState : u8
    {
        Edit,
        Playing,
        Paused
    };

    [[nodiscard]] constexpr const char* SimulationStateName(SimulationState state)
    {
        switch (state)
        {
            case SimulationState::Edit: return "Edit";
            case SimulationState::Playing: return "Playing";
            case SimulationState::Paused: return "Paused";
        }
        return "Unknown";
    }

    enum class TransitionResult : u8
    {
        Applied,  // the state changed (or a Step was queued)
        Rejected, // not legal from the current state; nothing changed
        Failed    // legal, but the capture/restore callback reported an error
    };

    // The Edit / Playing / Paused lifecycle and its timing policy, with no engine
    // dependencies so it can be unit-tested in isolation. The owner supplies the
    // capture and restore work as callbacks; this class guarantees the ordering
    // and the invariants around them:
    //
    //   * A pre-play snapshot exists exactly while the state is not Edit.
    //   * Play only leaves Edit after capture succeeded; on failure the state
    //     stays Edit and nothing is stored.
    //   * Stop only discards the snapshot after restore succeeded. A failed
    //     restore leaves the state Paused with the snapshot retained, so Stop can
    //     be retried and the scene-document guards stay closed meanwhile.
    //
    // Timing. The engine has no fixed-step accumulator: Jolt is advanced once per
    // frame by the frame delta and subdivides it into ~tick-sized collision steps.
    // AdvanceFrame keeps that design and only decides how much time to hand over:
    //   Edit             -> 0
    //   Paused           -> 0, or exactly one tick on the frame after RequestStep
    //   Playing          -> the frame delta, clamped to max_frame_delta_seconds
    //   first frame after Play/Resume -> clamped to one tick instead, so the
    //                       transition frame (and any paused wall time) can never
    //                       turn into a catch-up step
    // Every transition resets that bookkeeping and drops any queued Step.
    template <typename Snapshot>
    class SimulationController
    {
    public:
        SimulationController(f32 tick_seconds, f32 max_frame_delta_seconds)
            : tick_seconds_(tick_seconds),
              max_frame_delta_seconds_(max_frame_delta_seconds)
        {
        }

        [[nodiscard]] SimulationState State() const { return state_; }
        [[nodiscard]] const char* StateName() const { return SimulationStateName(state_); }

        // Legal transitions. The editor uses these to enable/disable controls.
        [[nodiscard]] bool CanPlay() const { return state_ == SimulationState::Edit; }
        [[nodiscard]] bool CanPause() const { return state_ == SimulationState::Playing; }
        [[nodiscard]] bool CanResume() const { return state_ == SimulationState::Paused; }
        [[nodiscard]] bool CanStop() const { return state_ != SimulationState::Edit; }
        [[nodiscard]] bool CanStep() const { return state_ == SimulationState::Paused; }

        // New / Open / Save / Save As are only safe on the authored scene. While a
        // test run is active the live scene holds runtime state that must never
        // replace or be written over the document.
        [[nodiscard]] bool CanModifyDocument() const { return state_ == SimulationState::Edit; }

        // The authored scene captured on entering Play, or nullptr in Edit.
        [[nodiscard]] const Snapshot* PrePlaySnapshot() const
        {
            return snapshot_.has_value() ? &*snapshot_ : nullptr;
        }

        [[nodiscard]] f32 TickSeconds() const { return tick_seconds_; }

        // Simulated time and the number of frames that advanced it, since the last
        // Play. Diagnostics only; they never drive the simulation.
        [[nodiscard]] f64 SimulatedSeconds() const { return simulated_seconds_; }
        [[nodiscard]] u64 SimulationUpdates() const { return simulation_updates_; }
        [[nodiscard]] f32 LastAdvanceSeconds() const { return last_advance_seconds_; }

        // Edit -> Playing. capture(Snapshot&, std::string& error) -> bool must not
        // modify the scene; returning false (or throwing) vetoes the transition.
        template <typename Capture>
        TransitionResult Play(Capture&& capture, std::string& out_error)
        {
            if (!CanPlay())
            {
                return Reject("Play", out_error);
            }

            Snapshot snapshot{};
            bool captured = false;
            try
            {
                captured = std::forward<Capture>(capture)(snapshot, out_error);
            }
            catch (const std::exception& exception)
            {
                out_error = exception.what();
                captured = false;
            }

            if (!captured)
            {
                if (out_error.empty())
                {
                    out_error = "The scene snapshot could not be captured.";
                }
                return TransitionResult::Failed;
            }

            snapshot_ = std::move(snapshot);
            simulated_seconds_ = 0.0;
            simulation_updates_ = 0;
            EnterState(SimulationState::Playing);
            return TransitionResult::Applied;
        }

        TransitionResult Pause(std::string& out_error)
        {
            if (!CanPause())
            {
                return Reject("Pause", out_error);
            }
            EnterState(SimulationState::Paused);
            return TransitionResult::Applied;
        }

        TransitionResult Resume(std::string& out_error)
        {
            if (!CanResume())
            {
                return Reject("Resume", out_error);
            }
            EnterState(SimulationState::Playing);
            return TransitionResult::Applied;
        }

        // Playing/Paused -> Edit. restore(const Snapshot&, std::string& error) ->
        // bool rebuilds the authored scene. On failure the snapshot is kept and
        // the state becomes Paused (simulation frozen, document guards closed).
        template <typename Restore>
        TransitionResult Stop(Restore&& restore, std::string& out_error)
        {
            if (!CanStop())
            {
                return Reject("Stop", out_error);
            }

            bool restored = false;
            try
            {
                restored = std::forward<Restore>(restore)(*snapshot_, out_error);
            }
            catch (const std::exception& exception)
            {
                out_error = exception.what();
                restored = false;
            }

            if (!restored)
            {
                if (out_error.empty())
                {
                    out_error = "The pre-play scene could not be restored.";
                }
                EnterState(SimulationState::Paused);
                return TransitionResult::Failed;
            }

            snapshot_.reset();
            EnterState(SimulationState::Edit);
            return TransitionResult::Applied;
        }

        // Queues exactly one tick for the next AdvanceFrame. Paused only; a second
        // request before that frame does not queue a second tick.
        TransitionResult RequestStep(std::string& out_error)
        {
            if (!CanStep())
            {
                return Reject("Step", out_error);
            }
            step_requested_ = true;
            return TransitionResult::Applied;
        }

        // Call once per frame with the measured frame delta. Returns the seconds
        // the simulation should advance this frame (see the class comment).
        f32 AdvanceFrame(f64 frame_delta_seconds)
        {
            f32 advance = 0.0f;
            if (state_ == SimulationState::Playing)
            {
                const f32 limit = first_frame_after_transition_
                    ? tick_seconds_
                    : max_frame_delta_seconds_;
                const f32 delta = static_cast<f32>(frame_delta_seconds);
                // Also rejects NaN and negative deltas.
                advance = delta > 0.0f ? (delta < limit ? delta : limit) : 0.0f;
                first_frame_after_transition_ = false;
            }
            else if (state_ == SimulationState::Paused && step_requested_)
            {
                advance = tick_seconds_;
            }

            step_requested_ = false;
            last_advance_seconds_ = advance;
            if (advance > 0.0f)
            {
                simulated_seconds_ += advance;
                ++simulation_updates_;
            }
            return advance;
        }

    private:
        TransitionResult Reject(const char* action, std::string& out_error) const
        {
            out_error = std::string(action) + " is not available while " + StateName() + ".";
            return TransitionResult::Rejected;
        }

        void EnterState(SimulationState state)
        {
            state_ = state;
            step_requested_ = false;
            first_frame_after_transition_ = true;
        }

        SimulationState state_ = SimulationState::Edit;
        std::optional<Snapshot> snapshot_;
        f32 tick_seconds_;
        f32 max_frame_delta_seconds_;
        bool step_requested_ = false;
        bool first_frame_after_transition_ = true;
        f64 simulated_seconds_ = 0.0;
        u64 simulation_updates_ = 0;
        f32 last_advance_seconds_ = 0.0f;
    };
}
