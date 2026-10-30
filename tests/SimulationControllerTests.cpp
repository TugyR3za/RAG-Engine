#include "SimulationController.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    using rag::runtime::SimulationController;
    using rag::runtime::SimulationState;
    using rag::runtime::TransitionResult;

    constexpr rag::f32 Tick = 1.0f / 60.0f;
    constexpr rag::f32 MaxDelta = 0.033f;

    // Stand-in for the sandbox's scene snapshot: a fake "scene" the callbacks
    // capture from and restore into, so the tests can observe exactly what the
    // controller asked for and when.
    struct FakeScene
    {
        int entity_count = 0;
        int restore_calls = 0;
    };

    struct FakeSnapshot
    {
        int entity_count = -1;
    };

    using Controller = SimulationController<FakeSnapshot>;

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Simulation controller test failed: " << message << '\n';
        }
        return condition;
    }

    bool Near(rag::f64 left, rag::f64 right)
    {
        return std::abs(left - right) <= 1e-6;
    }

    auto CaptureFrom(const FakeScene& scene)
    {
        return [&scene](FakeSnapshot& snapshot, std::string&) {
            snapshot.entity_count = scene.entity_count;
            return true;
        };
    }

    auto RestoreInto(FakeScene& scene)
    {
        return [&scene](const FakeSnapshot& snapshot, std::string&) {
            ++scene.restore_calls;
            scene.entity_count = snapshot.entity_count;
            return true;
        };
    }

    bool TransitionTests()
    {
        Controller controller(Tick, MaxDelta);
        FakeScene scene{3};
        std::string error;
        bool passed = true;

        passed = Expect(controller.State() == SimulationState::Edit, "Controller must start in Edit.") && passed;
        passed = Expect(controller.PrePlaySnapshot() == nullptr, "Edit must hold no snapshot.") && passed;
        passed = Expect(controller.CanModifyDocument(), "Document operations must be allowed in Edit.") && passed;

        // Only Play is legal from Edit.
        passed = Expect(controller.Pause(error) == TransitionResult::Rejected, "Pause from Edit must be rejected.") && passed;
        passed = Expect(!error.empty(), "A rejected transition must explain why.") && passed;
        passed = Expect(controller.Resume(error) == TransitionResult::Rejected, "Resume from Edit must be rejected.") && passed;
        passed = Expect(controller.RequestStep(error) == TransitionResult::Rejected, "Step from Edit must be rejected.") && passed;
        passed = Expect(controller.Stop(RestoreInto(scene), error) == TransitionResult::Rejected, "Stop from Edit must be rejected.") && passed;
        passed = Expect(scene.restore_calls == 0, "A rejected Stop must not run the restore callback.") && passed;
        passed = Expect(controller.State() == SimulationState::Edit, "Rejected transitions must not change state.") && passed;

        passed = Expect(controller.Play(CaptureFrom(scene), error) == TransitionResult::Applied, "Play from Edit must apply.") && passed;
        passed = Expect(controller.State() == SimulationState::Playing, "Play must enter Playing.") && passed;
        passed = Expect(controller.PrePlaySnapshot() != nullptr && controller.PrePlaySnapshot()->entity_count == 3,
            "Play must keep the captured snapshot.") && passed;
        passed = Expect(!controller.CanModifyDocument(), "Document operations must be blocked while Playing.") && passed;
        passed = Expect(controller.Play(CaptureFrom(scene), error) == TransitionResult::Rejected, "Play while Playing must be rejected.") && passed;
        passed = Expect(controller.Resume(error) == TransitionResult::Rejected, "Resume while Playing must be rejected.") && passed;
        passed = Expect(controller.RequestStep(error) == TransitionResult::Rejected, "Step while Playing must be rejected.") && passed;

        passed = Expect(controller.Pause(error) == TransitionResult::Applied, "Pause while Playing must apply.") && passed;
        passed = Expect(controller.State() == SimulationState::Paused, "Pause must enter Paused.") && passed;
        passed = Expect(!controller.CanModifyDocument(), "Document operations must be blocked while Paused.") && passed;
        passed = Expect(controller.Pause(error) == TransitionResult::Rejected, "Pause while Paused must be rejected.") && passed;
        passed = Expect(controller.Play(CaptureFrom(scene), error) == TransitionResult::Rejected, "Play while Paused must be rejected.") && passed;
        passed = Expect(controller.Resume(error) == TransitionResult::Applied, "Resume while Paused must apply.") && passed;
        passed = Expect(controller.State() == SimulationState::Playing, "Resume must re-enter Playing.") && passed;

        // Runtime changes, then Stop restores the captured value.
        scene.entity_count = 9;
        passed = Expect(controller.Stop(RestoreInto(scene), error) == TransitionResult::Applied, "Stop while Playing must apply.") && passed;
        passed = Expect(scene.entity_count == 3, "Stop must restore from the pre-play snapshot.") && passed;
        passed = Expect(controller.State() == SimulationState::Edit, "Stop must return to Edit.") && passed;
        passed = Expect(controller.PrePlaySnapshot() == nullptr, "Stop must discard the snapshot after restoring.") && passed;

        // Stop is also legal from Paused.
        (void)controller.Play(CaptureFrom(scene), error);
        (void)controller.Pause(error);
        scene.entity_count = 0;
        passed = Expect(controller.Stop(RestoreInto(scene), error) == TransitionResult::Applied, "Stop while Paused must apply.") && passed;
        passed = Expect(scene.entity_count == 3 && controller.State() == SimulationState::Edit, "Stop from Paused must restore and enter Edit.") && passed;
        return passed;
    }

    bool FailureTests()
    {
        Controller controller(Tick, MaxDelta);
        FakeScene scene{5};
        std::string error;
        bool passed = true;

        // A failed capture keeps Edit and stores nothing; the scene is untouched.
        const auto failing_capture = [](FakeSnapshot&, std::string& out_error) {
            out_error = "mesh cannot be captured";
            return false;
        };
        passed = Expect(controller.Play(failing_capture, error) == TransitionResult::Failed, "A failed capture must fail Play.") && passed;
        passed = Expect(controller.State() == SimulationState::Edit, "A failed Play entry must stay in Edit.") && passed;
        passed = Expect(controller.PrePlaySnapshot() == nullptr, "A failed Play entry must not keep a snapshot.") && passed;
        passed = Expect(error == "mesh cannot be captured", "A failed Play entry must report the capture error.") && passed;
        passed = Expect(scene.entity_count == 5, "A failed Play entry must preserve the scene.") && passed;

        const auto throwing_capture = [](FakeSnapshot&, std::string&) -> bool {
            throw std::runtime_error("out of memory");
        };
        passed = Expect(controller.Play(throwing_capture, error) == TransitionResult::Failed && error == "out of memory",
            "A throwing capture must fail Play with its message.") && passed;
        passed = Expect(controller.State() == SimulationState::Edit, "A throwing capture must stay in Edit.") && passed;

        // A failed restore keeps the snapshot and parks the session in Paused, so
        // a retry can still restore and the document guards stay closed.
        passed = Expect(controller.Play(CaptureFrom(scene), error) == TransitionResult::Applied, "Play must apply after a failure.") && passed;
        scene.entity_count = 12;
        int attempts = 0;
        const auto flaky_restore = [&](const FakeSnapshot& snapshot, std::string& out_error) {
            if (++attempts == 1)
            {
                out_error = "physics body creation failed";
                return false;
            }
            scene.entity_count = snapshot.entity_count;
            return true;
        };
        passed = Expect(controller.Stop(flaky_restore, error) == TransitionResult::Failed, "A failed restore must fail Stop.") && passed;
        passed = Expect(controller.State() == SimulationState::Paused, "A failed restore must leave the session Paused.") && passed;
        passed = Expect(controller.PrePlaySnapshot() != nullptr && controller.PrePlaySnapshot()->entity_count == 5,
            "A failed restore must retain the snapshot.") && passed;
        passed = Expect(!controller.CanModifyDocument(), "A failed restore must keep document operations blocked.") && passed;
        passed = Expect(controller.AdvanceFrame(0.016) == 0.0f, "A failed restore must not resume simulation.") && passed;
        passed = Expect(controller.Stop(flaky_restore, error) == TransitionResult::Applied, "Stop must be retryable after a failed restore.") && passed;
        passed = Expect(scene.entity_count == 5 && controller.State() == SimulationState::Edit, "The retried Stop must restore and enter Edit.") && passed;
        return passed;
    }

    bool TimingTests()
    {
        Controller controller(Tick, MaxDelta);
        FakeScene scene{1};
        std::string error;
        bool passed = true;

        // Edit never advances, and a Step cannot be queued there.
        for (int frame = 0; frame < 10; ++frame)
        {
            passed = Expect(controller.AdvanceFrame(0.016) == 0.0f, "Edit must not advance the simulation.") && passed;
        }
        passed = Expect(controller.SimulatedSeconds() == 0.0, "Edit must accumulate no simulated time.") && passed;

        (void)controller.Play(CaptureFrom(scene), error);
        // The first Playing frame is capped to one tick even after a long frame.
        passed = Expect(controller.AdvanceFrame(0.5) == Tick, "The first frame after Play must be capped to one tick.") && passed;
        passed = Expect(controller.AdvanceFrame(0.010) == 0.010f, "Playing must pass a normal frame delta through.") && passed;
        passed = Expect(controller.AdvanceFrame(0.5) == MaxDelta, "Playing must clamp a long frame to the max delta.") && passed;
        passed = Expect(controller.AdvanceFrame(-1.0) == 0.0f, "A negative frame delta must not advance.") && passed;
        passed = Expect(controller.AdvanceFrame(std::nan("")) == 0.0f, "A NaN frame delta must not advance.") && passed;

        // Paused frames never advance, however long they are.
        (void)controller.Pause(error);
        const rag::f64 paused_at = controller.SimulatedSeconds();
        const rag::u64 updates_at_pause = controller.SimulationUpdates();
        for (int frame = 0; frame < 600; ++frame)
        {
            passed = Expect(controller.AdvanceFrame(0.016) == 0.0f, "Paused must not advance the simulation.") && passed;
        }
        passed = Expect(controller.SimulatedSeconds() == paused_at, "Paused must accumulate no simulated time.") && passed;

        // Step advances exactly one tick, exactly once.
        passed = Expect(controller.RequestStep(error) == TransitionResult::Applied, "Step while Paused must apply.") && passed;
        passed = Expect(controller.RequestStep(error) == TransitionResult::Applied, "A repeated Step request must be accepted.") && passed;
        passed = Expect(controller.AdvanceFrame(0.25) == Tick, "Step must advance exactly one tick regardless of frame delta.") && passed;
        passed = Expect(controller.AdvanceFrame(0.016) == 0.0f, "Step must advance only once per request frame.") && passed;
        passed = Expect(controller.State() == SimulationState::Paused, "Step must remain Paused.") && passed;
        passed = Expect(Near(controller.SimulatedSeconds() - paused_at, Tick), "Step must add exactly one tick of simulated time.") && passed;
        passed = Expect(controller.SimulationUpdates() == updates_at_pause + 1, "Step must count as exactly one simulation update.") && passed;

        // Resume after a long pause (600 frames ~ 10 s of wall time, then a hitch)
        // must not catch up: the first frame is capped to one tick.
        (void)controller.Resume(error);
        passed = Expect(controller.AdvanceFrame(10.0) == Tick, "The first frame after Resume must not catch up paused time.") && passed;
        passed = Expect(controller.AdvanceFrame(0.016) == 0.016f, "Frames after Resume must use the normal frame delta.") && passed;

        // A Step requested and then overtaken by Resume is dropped, not replayed.
        (void)controller.Pause(error);
        (void)controller.RequestStep(error);
        (void)controller.Resume(error);
        passed = Expect(controller.AdvanceFrame(0.005) == 0.005f, "A transition must drop a queued Step.") && passed;

        // Stop resets the lifecycle; a later Play starts its time from zero.
        (void)controller.Stop(RestoreInto(scene), error);
        passed = Expect(controller.AdvanceFrame(0.016) == 0.0f, "Edit after Stop must not advance.") && passed;
        (void)controller.Play(CaptureFrom(scene), error);
        passed = Expect(controller.SimulatedSeconds() == 0.0 && controller.SimulationUpdates() == 0,
            "Play must reset simulated time bookkeeping.") && passed;
        passed = Expect(controller.AdvanceFrame(0.5) == Tick, "The first frame of a new Play must be capped again.") && passed;
        return passed;
    }

    bool RepeatedCycleTests()
    {
        Controller controller(Tick, MaxDelta);
        FakeScene scene{4};
        std::string error;
        bool passed = true;

        for (int cycle = 0; cycle < 25; ++cycle)
        {
            passed = Expect(controller.Play(CaptureFrom(scene), error) == TransitionResult::Applied, "Each cycle must enter Play.") && passed;
            scene.entity_count += cycle + 1; // runtime spawns
            for (int frame = 0; frame < 5; ++frame)
            {
                (void)controller.AdvanceFrame(0.016);
            }
            passed = Expect(controller.Stop(RestoreInto(scene), error) == TransitionResult::Applied, "Each cycle must Stop.") && passed;
            passed = Expect(scene.entity_count == 4, "Repeated cycles must always restore the authored scene.") && passed;
        }
        passed = Expect(scene.restore_calls == 25, "Each Stop must restore exactly once.") && passed;
        return passed;
    }
}

int main()
{
    bool passed = TransitionTests();
    passed = FailureTests() && passed;
    passed = TimingTests() && passed;
    passed = RepeatedCycleTests() && passed;
    if (!passed)
    {
        return 1;
    }

    std::cout << "Simulation controller tests passed.\n";
    return 0;
}
