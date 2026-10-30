# First-person player

A scene with a **Player Start** spawns a first-person player when Play starts.
You walk with WASD, look with the mouse, and jump with Space. The capsule
collides with static geometry and dynamic props. Stop returns the authored scene.
A scene without a Player Start plays exactly as before, with the free-fly camera.

## Adding and configuring a Player Start

1. In **Edit**, click **Add Player Start** at the top of the Hierarchy panel.
   The start is placed 4 m ahead of the editor view, at about eye level, and
   faces the same way. It is then selected.
   - The button is disabled while a run is active, and when the scene already
     has a Player Start.
2. Place it with the move gizmo or the Inspector **Position**. The position is
   the player's **feet**. Rotation and scale are not used, so a Player Start
   only gets the move handles.
3. Set the rest in the Inspector's **Player Start** section.
4. Save the scene to keep it (File > Save Scene / Save Scene As). Nothing is
   written automatically.

In the 3D view a Player Start is drawn as a line from the feet to the capsule
top, a ring at the feet, an arrow showing its facing, and a label. Green means
Play uses it; grey means it is ignored. Click it in the view or the Hierarchy to
select it. The selection box shows the capsule.

| Field | Unit | Range | Default | Meaning |
|---|---|---|---|---|
| Position (Transform) | m | any | — | world position of the feet |
| Yaw | degrees (stored as radians) | -180..180 | 0 | initial facing. 0 looks down -Z; positive turns left, like the editor camera |
| Move Speed | m/s | 0.1..50 | 4.5 | ground speed. Diagonals are normalized to the same speed |
| Mouse Sensitivity | degrees per raw mouse count | 0.005..2 | 0.1 | view rotation per count of mouse motion |
| Jump Height | m | 0..10 | 1.0 | apex height of a standing jump under the current world gravity. 0 disables jumping |
| Capsule Radius | m | 0.1..1 | 0.3 | |
| Capsule Height | m | 2 × radius + 0.1 .. 4 | 1.8 | total height, both rounded ends included |
| Eye Height | m | 0.1 .. capsule height | 1.6 | camera height above the feet |
| Fall Reset Y | m (world Y) | ±100000 | -25 | falling below it returns the player to the start |

- **Invalid values are corrected, never fatal.** The same `SanitizePlayerStart`
  rule runs on scene load, after every Inspector edit, and on Play.
  - A non-number or non-finite value takes its default.
  - Everything else is clamped, in dependency order: radius, then height,
    then eye height.
  - Load logs what it corrected.
- **The scene file.** The Player Start is an optional `"playerStart"` object on
  its entity, with fields `yawRadians`, `moveSpeed`, `mouseSensitivityDegrees`,
  `jumpHeight`, `capsuleRadius`, `capsuleHeight`, `eyeHeight` and
  `fallResetHeight`.
  - It is additive within `sceneVersion` 1, so older scenes load unchanged and
    simply have no player.
  - Only authoring values are saved: no velocity, view angles, or mouse-capture
    state.

### Several Player Starts

The editor adds at most one. A hand-edited file may contain more. Play then uses
the **first one in the Hierarchy** (entity slot order, which is the file's
order after a load) and logs a warning naming it. The others are labelled
`- not used` in the Hierarchy and the Inspector says which start wins.

## Controls

| State | Mouse / keys |
|---|---|
| Playing, player view, **not captured** | The view shows the player. A "Click the view to control the player" hint is centred. Left-click the game view (not a panel) to capture the mouse. The capture click does nothing else: no selection, no gizmo, no movement. |
| Playing, **captured** | WASD walks relative to the horizontal view direction. The mouse looks (pitch is limited to just under ±90°). **Space** jumps. A crosshair is shown. The OS cursor is hidden and confined to the window, and editor panels ignore mouse and keyboard. |
| Captured, **Escape** | Releases the mouse **only**; it does not quit. A second Escape (with nothing captured) quits as before. |
| Any state | F5 / F6 / F7 and F1 keep working while captured. |

The mouse is released by:

- Escape;
- Pause and Stop (F6 / F5 or the toolbar);
- the window losing focus (Alt+Tab, clicking another window, minimizing);
- the player being removed (Stop restore).

Held keys are dropped when the window loses focus, so nothing stays stuck
"down" after Alt+Tab. A new capture starts clean:

- Mouse motion is read per frame, and the capture frame itself feeds nothing.
- A Space that was already held does not jump; a jump needs a new press.

The menu bar shows `PLAYER (Esc frees mouse)` while captured,
`PLAYER (click view)` while waiting for a click, and `EDITOR VIEW` while
Paused. The Debug panel's **Player** section shows the player's position,
speeds, ground state, view angles, jump and fall-reset counts, and why a
start produced no player.

## Camera policy

| State | Viewport camera | Editor free-fly camera |
|---|---|---|
| Edit | editor camera | flies (hold right mouse + WASD) |
| Playing, no Player Start | editor camera | flies, exactly as before |
| Playing, with a player | **player's eyes** (editor camera's FOV/clip planes) | parked; ignores input |
| Paused, with a player | **editor camera**, moved to the player's eyes when Pause is pressed | flies freely; this never moves the player |
| Resume | back to the player's eyes, at the exact pose it was paused at | parked again |
| Stop | editor camera, restored to its pre-Play pose with the rest of the scene | — |

The player view is not a scene entity. Each frame the sandbox replaces the
extracted render camera with one built from the player's eye position and
yaw/pitch. The renderer only ever receives an ordinary camera.

## Movement rules

- **Walking.** On walkable ground, horizontal velocity is exactly
  `move_speed × wish direction`. The player stops at once when the keys are
  released.
- **Air control.** In the air the player keeps its horizontal momentum. Input
  steers it at most 8 m/s² (`PlayerAirAcceleration`), so walking off a ledge
  carries you forward.
- **Gravity and jumping.**
  - The world gravity (Debug panel slider) applies.
  - A jump needs a **new press** while on walkable ground and not already
    rising. The press is spent on the first simulation substep of its frame,
    whether or not a jump happened.
    - Holding Space never relaunches.
    - A press in the air is not buffered until landing.
  - Gravity is integrated at each substep's midpoint, so the apex matches
    `jump_height` at any frame rate.
- **Hitting things.** After every substep the velocity is cut back to what the
  capsule actually achieved:
  - a head bump ends the rise;
  - landing ends the fall;
  - a wall ends airborne momentum into it.
- **Slopes** up to 50° are walkable. Steeper ones act as walls: the player
  slides down and cannot jump from them.
- **Stick to floor.** Walking down slopes or off small lips (up to 0.5 m) keeps
  the player on the ground instead of hopping.

## Dynamic props

The player is a Jolt `CharacterVirtual`: a capsule moved by collision sweeps,
not a rigid body.

- **It never passes through props.** It slides along static and dynamic
  colliders.
- **It pushes what it walks into**, with at most **600 N**.
  - Dynamic bodies use Jolt's default density (1000 kg/m³) and friction (0.2).
    So a 0.5 m crate (125 kg) slides along in front of the player, while the
    default scene's 1 m cubes (~1 t) do not move and block it like a wall.
  - Pushing is a simple force limit, not a production-quality interaction.
- **Its weight (70 kg)** presses down on the dynamic body it stands on.
- **Rigid bodies do not see the player.** A prop falling onto the player passes
  through it, and props cannot push the player. If a prop ends up overlapping
  the capsule, the capsule's penetration recovery pushes the player out.

## Lifecycle

- **Edit.** There is no player. Nothing moves.
- **Play.** Bodies are put at their authored poses first, then the player
  spawns at the start.
  - The spawn is probed for overlap with every static and dynamic collider.
  - An obstructed start is lifted to the first clear spot, probing every
    0.25 m up to 2 m above it, with a warning.
  - With no clear spot, **no player is spawned**. The run continues with the
    free-fly camera and the reason is shown (`(no player)` in the menu bar and
    the Debug panel).
  - Play itself never fails because of the Player Start.
- **Playing.** Each frame the lifecycle decides the advance, exactly as before
  (see [play mode](play-mode.md)). With a player, that advance is split into
  `ceil(advance / tick)` equal substeps, the same split Jolt already uses for
  its collision steps. Each substep moves the character first, then steps the
  rigid bodies, so both always share the same simulated time.
  - A 500 ms hitch is still clamped to 33 ms (two substeps).
- **Paused.** Nothing moves, whatever the input.
  - **Step** advances exactly one tick: one character substep and one physics
    step.
  - Step uses no live input. The mouse is not captured while Paused, so no
    movement, jump or mouse look is consumed; gravity and airborne momentum
    still apply.
- **Stop.**
  - The mouse is released.
  - The restore destroys the character together with the runtime scene
    (`ClearSceneState`).
  - Restore verification also checks that no character survived.
  - Repeated Play/Stop cycles do not accumulate characters or bodies (a
    character is never a body), and the document identity is unchanged.

**Live tuning.** During a run the Player Start can be edited like any entity.
Those edits are runtime changes that Stop reverts.

- Speed, sensitivity, jump height, eye height (clamped to the spawned capsule),
  fall-reset height and the start position apply on the next frame.
- The capsule size applies at the next Play.
- If the start is deleted mid-run, the player keeps its last values.

## Fall reset

When the feet drop below **Fall Reset Y**, only the player is reset:

- it returns to the (live) start;
- it faces the start's yaw, with the view levelled;
- all motion and any pending jump are cleared.

The rest of the scene keeps simulating, and nothing else is reset.

- **Obstructed start.** The reset uses the same lift-and-probe as the spawn.
  If nothing within 2 m is clear, the player is placed at the start anyway and
  the character's penetration recovery pushes it out. This is logged.
- **Start below the line.** A start at or below its own Fall Reset Y would
  reset forever, so fall reset is turned off for that run. A warning is logged
  once and the Inspector says so.
- **Logging.** Resets are logged individually up to 5, then every 25th. They
  are never logged per frame.

## Architecture

| Layer | What it owns |
|---|---|
| `RagScene` | `PlayerStartComponent` (authoring data only), `Scene::FirstPlayerStart`, `SanitizePlayerStart` / `PlayerStartLimits` (`Rag/Scene/PlayerStart.h`), and `"playerStart"` (de)serialization |
| `RagPhysics` | `CharacterHandle` / `CharacterDesc` / `CharacterState` and `PhysicsWorld::CreateCharacter`, `MoveCharacter`, `SetCharacterPosition`, `IsCharacterSpaceFree`, `CharacterCount`. Jolt `CharacterVirtual` stays behind the PIMPL |
| `sandbox/PlayerController.h` | engine-free movement rules (walk, diagonals, look, jump gating, air control, gravity, velocity reconciliation, fall-reset decision), unit-tested with a fake mover |
| `RagCore` / `RagPlatform` | raw mouse motion (`WM_INPUT` -> `MouseRawMotion`), focus events (`WindowFocus`; held keys released on focus loss), and `IWindow::SetCursorCaptured` (hide + confine the cursor; dropped by the platform on focus loss) |
| `sandbox/SandboxMain.cpp` | glue: spawn on Play, teardown in `ClearSceneState`, substep loop in `UpdateSimulation`, input sampling and capture, view override, crosshair, Inspector, Hierarchy and Debug UI |

## Tests

| Test | Kind | What it covers |
|---|---|---|
| `PlayerController` (`tests/PlayerControllerTests.cpp`) | unit, no physics | substep split; walk speed and diagonal normalization; facing; look and pitch clamp; jump gating (hold, airborne press, long frame, repeated presses, jump height 0); jump apex at 30/60/144/240 Hz; ceiling and wall; air momentum and steering; fall-reset detection, reset and start-below-line; no movement while Paused, Step = one substep, hitch clamp |
| `PlayerPhysics` (`tests/PlayerPhysicsTests.cpp`) | integration, real Jolt, no GPU | 100 create/destroy cycles with no leaked character or body; spawn probe; walking on a floor; stopping at a wall; walking off a ledge and landing; jumping into a ceiling; pushing a 125 kg crate and being blocked by a 1 t crate without entering either; 30° slope walkable and 60° not; the same walk and jump at 30/60/144 Hz; a 35 m fall through hitch-clamped frames onto a 10 cm slab; fall-reset teleport |
| `SceneRuntime`, `SceneSerialization` | unit | Player Start storage, removal and first-start choice; JSON round-trip; legacy scenes; malformed and out-of-range values; the sanitizer |
| `SandboxPlayMode` (label `gpu`) | automated, in the real editor | 34 player steps on an in-memory level (listed below) |

The `SandboxPlayMode` player steps check:

- Play spawns one player, with no extra bodies and no mouse capture.
- The render camera is the player's eyes.
- Capture shows the crosshair and blocks the panels.
- Walking stops at the wall without penetration, and never moves the editor
  camera.
- One press gives one jump.
- Escape releases the mouse without quitting; focus loss releases it too.
- Live tuning works.
- A fall reset moves only the player.
- Pause hands the view to the editor camera at the player's eyes.
- Paused frames and editor navigation do not move the player.
- Step is one substep without input.
- Resume returns to the same pose.
- Stop restores everything and removes the player.
- 3 further cycles accumulate nothing.
- A lifted spawn is clear; a buried start plays with the free camera.
- With two starts the first one wins.
- A scene with no start plays with the free camera.
- Add Player Start works, refuses a second, and round-trips.

The self-test captures the mouse **logically only**: it never hides or confines
the real cursor. The OS-level capture, the visual crosshair and real key/mouse
input are manual checks.

## Known limitations

- **One player.** No crouch, sprint, stairs/step climbing (ledges higher than
  the capsule's rounded bottom must be jumped onto), or moving-platform support
  (standing on a moving body does not carry the player).
- **Pushing is basic.** Rigid bodies do not react to the player except through
  its limited push and weight (see above).
- **No jump buffering or coyote time.** A press must happen while on walkable
  ground.
- **Not a vehicle for gameplay code.** No scripting hooks, interaction,
  inventory, animation or third-person camera.
- **Non-editor builds are unverified.** A game build (`RAG_EDITOR` off) uses
  the same player code path and starts Playing immediately, but only the
  canonical editor build (`build/skybox`) has been compiled and tested.
