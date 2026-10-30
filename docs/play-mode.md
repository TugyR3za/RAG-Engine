# Play / Pause / Stop scene testing

The editor separates **authoring** a scene from **test-running** it. Arrange
objects in Edit, press Play to simulate, pause and inspect mid-run, step single
ticks, then Stop to throw the run away and get the arranged scene back.

| State | Simulation | Editor | Scene file operations |
|---|---|---|---|
| **Edit** (startup) | does not advance | camera, selection, Inspector, gizmos | New / Load / Save / Save As |
| **Playing** | advances every frame | all still usable | disabled ("Stop simulation first") |
| **Paused** | frozen; **Step** advances one tick | all still usable | disabled |

The simulation state is independent of the editor UI visibility (F1).

## Controls

| Action | Toolbar (menu bar) | Simulation menu | Key | Legal from |
|---|---|---|---|---|
| Play | **Play** | Play | F5 | Edit |
| Pause / Resume | **Pause** / **Resume** | Pause, Resume | F6 | Playing / Paused |
| Step one tick | **Step** | Step One Tick | F7 | Paused |
| Stop | **Stop** | Stop | F5 | Playing, Paused |
| Delete selected entity | Inspector **Delete Entity** | | Delete | any (not the active camera) |

- Illegal transitions are greyed out, and shortcuts only request legal ones.
- The state label next to the buttons reads `EDIT`, `PLAYING 1.23s` or
  `PAUSED 1.23s`. The whole menu bar turns green while Playing and amber while
  Paused. A red `(!)` appears with a tooltip if a transition failed.
- The Debug panel's **Simulation** section shows the state, simulated time,
  update count, the last advance, the tick and the size of the in-memory snapshot.
- **Shortcut rules.** F5/F6/F7/Delete are ignored while ImGui is consuming
  keystrokes: while a text field is being edited (so typing, including Delete
  inside a field, never triggers them) or while a widget is held or dragged.
  Otherwise they work even if a panel has focus, and with the editor UI hidden.
  WASD camera movement and the W/E/R gizmo hotkeys keep their stricter rule:
  they also wait until no panel holds keyboard focus (left-click the 3D view).
- **Applied at the next frame.** Buttons and keys only *request* a transition.
  It is applied at the start of the next frame, before anything reads the
  scene, never inside the UI callback. A Stop restore therefore never swaps the
  scene out from under a panel or a draw call.

### Editor UI visibility (F1)

**F1**, or View > **Show Editor UI**, hides or shows the dockspace, panels and
menu bar. That is the bare game view.

- It changes only what is drawn. Edit/Playing/Paused are untouched.
- F5/F6/F7 still work with the UI hidden.
- Press **F1** again to get the editor back. Every toggle logs this reminder.
- Toggling also cancels a half-finished gizmo drag.
- Mouse look (hold right mouse) works in every state and in both views,
  except while a first-person player has the view (below).

### First-person player

If the scene has a **Player Start**, Play spawns a first-person player and the
viewport shows its eyes while Playing. Click the game view to take the mouse;
Escape, Pause, Stop and focus loss give it back. While Paused, the editor camera
inspects the scene, starting from the player's eyes. Resume returns to the
player unchanged. Without a Player Start, everything on this page works exactly
as before. See [first-person player](player-controller.md).

## What Play captures and Stop restores

On Play, the editor captures the current scene **in memory**. No save is
needed, and nothing is written to disk. The capture uses the same neutral
description `Save` writes (`CaptureScene`), plus sandbox state that the scene
file does not carry:

- every entity's name and components: transform, camera, renderable (stable
  mesh id and per-object material), light;
- per-object physics settings: mode, shape, extents, offset, gravity, CCD;
- the environment (skybox on/off and faces);
- world gravity (the Debug panel slider), the light sliders, and the spawn
  counter.

On Stop, the scene is rebuilt through the same `ApplyScene` path that Load uses.
The scene file on disk is never involved. The restore has three stages:

1. **Validate.** The snapshot is checked again: known mesh ids, valid physics
   settings, consistent counts. If that fails, nothing is destroyed. The run
   stays Paused with the snapshot kept, and an error is shown.
2. **Rebuild.**
   - Every Jolt body is destroyed and every entity removed. Slot reuse is reset
     (`Scene::DestroyAllEntities`), so the rebuilt scene keeps its Hierarchy
     order on every cycle.
   - Entities are then recreated in snapshot order. Mesh ids resolve to the
     already-loaded GPU meshes, so nothing is re-uploaded.
   - The skybox reloads only if its settings changed during the run.
3. **Verify.** Entity count, physics record count, a live body for each
   Static/Dynamic record, and Jolt's own `BodyCount()` (no ghost bodies) must
   all match. On a mismatch the run stays Paused with the snapshot kept, and
   Stop can be retried. Saving stays disabled throughout, so a partial scene can
   never be written over the document.

As a result:

- **Runtime changes are all discarded.** That covers moved, rescaled or
  recolored objects, physics mode and gravity edits, skybox toggles, light and
  gravity sliders, and camera movement.
- **Runtime spawns disappear**, and **entities deleted during the run come back**.
- **The document identity is unchanged.** A scene opened from or saved to a
  file is still that file after Stop, and "Save Scene" writes the restored,
  authored scene to it. An Untitled scene stays Untitled, so Save still asks
  for a file name. Nothing can change the identity during a run, because
  New/Load/Save/Save As are refused outside Edit. The editor has no dirty-state
  flag. See [scene documents](scene-documents.md).
- **The selection is remapped.** A selected entity that existed before Play is
  re-selected on its rebuilt counterpart. A selected runtime spawn clears. Any
  gizmo drag is cancelled.

**Entity identity is not preserved.** Rebuilt entities get new ids (same slot,
new generation), and any id held from before Stop is stale. Only the editor
selection is remapped, as described above.

If the scene cannot be captured losslessly (a renderable whose mesh handle has
no stable id), **Play is refused**. The editor stays in Edit with the scene
untouched, and the reason is shown.

## Timing, Pause and Step

The engine has no fixed-step accumulator. Each frame, Jolt is advanced by the
frame delta, which it splits into `ceil(delta / tick)` collision steps. Play
mode keeps that design and only decides how much time to hand over
(`SimulationController::AdvanceFrame`):

| Situation | Seconds handed to physics this frame |
|---|---|
| Edit | 0 |
| Paused | 0 |
| Paused, the frame after **Step** | exactly one tick: 1/60 s ≈ 16.67 ms, i.e. one collision step |
| Playing | frame delta, clamped to 33 ms |
| First frame after Play or Resume | frame delta, clamped to **one tick** |

- **No catch-up after a pause.** Paused frames add nothing, and the first frame
  after Resume is capped at one tick. A long pause, or a hitch on the Resume
  frame, can never become a catch-up step.
- **Transitions reset the bookkeeping.** Every transition drops a queued Step.
  Play also resets the simulated-time counters.
- **Physics stays in sync in every state.** Even in Edit and Paused, bodies of
  destroyed entities are released and collider scale follows transform scale.
  Inspector and gizmo edits push the new pose into the body and clear its
  velocity, exactly as before.
- **Play starts from the authored pose.** Every body is moved to its entity's
  current transform with zero velocity. That pose becomes the target of
  **Reset bodies**, which is available only during a run.
- **Restoration never steps.** The rebuild itself never advances physics, and
  the frame after Stop is already Edit.

The tick and cap are `SimulationTickSeconds` and
`MaxSimulationFrameDeltaSeconds` in `sandbox/SandboxMain.cpp`. The same values
are passed to `PhysicsWorldDesc`.

## Game builds

With `RAG_EDITOR` off there is no editor to press Play in. The sandbox starts
Playing on the first frame, through the same lifecycle path.

## Tests

| Test | Kind | What it covers |
|---|---|---|
| `SimulationController` (`tests/SimulationControllerTests.cpp`) | unit | legal and illegal transitions; failed capture stays Edit; failed restore keeps the snapshot and stays Paused; Edit/Paused advance 0; Step = exactly one tick; no catch-up after Resume; 25 repeated cycles |
| `SceneDocument` (`tests/SceneDocumentTests.cpp`) | unit, real files in a temp folder | Open A -> New -> Save writes nothing; cancelled / empty-name Save As writes nothing; Save As B leaves A byte-identical and makes B the document; failed Save As / Save / Open keep the previous identity, file bytes and scene |
| `SceneRuntime` | unit | `DestroyAllEntities`: stale ids, ascending slot reuse, creation-order iteration |
| `PhysicsWorld` | unit | `BodyCount` follows create/destroy/shutdown |
| `PlayerController`, `PlayerPhysics` | unit / Jolt integration | first-person movement and collision; see [first-person player](player-controller.md#tests) |
| `SandboxPlayMode` (label `gpu`) | automated, in the real editor (window + Vulkan + Jolt) | the full lifecycle on in-memory scenes (see below) |
| `tools\build-and-run.ps1 -SmokeTest` | automated smoke | renders one frame each in Edit, Playing, Edit; fails if a transition does not happen |

`SandboxPlayMode` runs `RagSandbox --play-mode-self-test`, which drives the real
request and safe-point path frame by frame. It checks:

- Edit and Pause leave the scene and Jolt untouched;
- a refused Play leaves the scene intact;
- New, Save and Load are refused while Playing or Paused;
- Step advances one tick;
- Resume after a 250 ms paused frame advances at most one tick;
- Stop restores everything after a spawn, a deletion, and material, physics,
  transform, scale, camera, environment, gravity and light edits;
- three further cycles leave no accumulated entities or bodies;
- an empty scene and a scene of non-renderable entities both restore;
- scene documents, with disposable files in a fresh temp folder: Open A, then
  New, then Save opens the Save As prompt and leaves A byte-identical. Cancel
  writes nothing. A failed Save As keeps Untitled or B. Save As B makes B the
  document, and a later Save rewrites B, not A. Failed Loads keep the scene
  and B. Play/Stop keeps both a saved and an Untitled document, and Save is
  refused while Playing;
- the first-person player on an in-memory level: spawn, view, capture and
  release, walking, jumping, fall reset, Pause/Step/Resume camera policy, Stop
  teardown and repeated cycles (see [first-person player](player-controller.md#tests)).

It never writes to `scenes/`. It reads `scenes/default.ragscene` twice: in the
normal startup load, and in a byte comparison proving the file is unchanged at
every document check. Its temp folder is removed at exit. Skip it with
`ctest -LE gpu`.

## Known limitations

- **Parent/child links are not captured.** Neither the scene format nor the
  editor authors them yet.
- **The camera pose is restored too.** The editor camera is a scene entity, so
  Stop also moves the view back to where it was when Play was pressed.
- **World gravity is not saved in scene files.** It is restored on Stop but is
  otherwise session state.
- **Deleting in Edit is permanent.** There is no undo.
- **Escape quits the application immediately**, even while a text field is
  being edited. There are two exceptions. While the Save As prompt is open,
  Escape cancels the prompt. While a first-person player has the mouse, Escape
  only releases the mouse.
