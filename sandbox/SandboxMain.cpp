#include "Rag/Core/Application.h"
#include "Rag/Core/Event.h"
#include "Rag/Core/Log.h"
#include "Rag/Core/Math.h"
#include "Rag/Platform/SystemPaths.h"
#include "Rag/Scene/PlayerStart.h"
#include "Rag/Scene/Scene.h"
#include "Rag/Scene/SceneSerialization.h"

// Edit / Playing / Paused lifecycle and its timing policy. Header-only and
// engine-free (unit-tested on its own); used by editor and game builds alike.
#include "SimulationController.h"

// First-person movement rules (walking, mouse look, jump gating, air control,
// fall reset). Header-only and engine-free (unit-tested on its own); collision
// comes from PhysicsWorld's character controller.
#include "PlayerController.h"

// Which file the scene belongs to (or Untitled) and the Save / Save As / Open
// identity rules. Header-only and engine-free (unit-tested on its own).
#include "SceneDocument.h"

#if defined(RAG_ENABLE_PHYSICS)
    #include "Rag/Physics/PhysicsWorld.h"
#endif

#if defined(RAG_ENABLE_VULKAN)
    #include "Rag/RendererVK/VulkanRenderer.h"
#endif

#if defined(RAG_ENABLE_IMGUI)
    #include <imgui.h>
#endif

#if defined(RAG_EDITOR)
    // DockBuilder (the one-time default-layout helper) lives in the internal
    // header. RAG_EDITOR is only defined when RAG_ENABLE_IMGUI is on, so imgui.h
    // above is always available first. Silence the third-party header's /W4 noise.
    #if defined(_MSC_VER)
        #pragma warning(push, 0)
    #endif
    #include <imgui_internal.h>
    #if defined(_MSC_VER)
        #pragma warning(pop)
    #endif
#endif

#if defined(RAG_EDITOR)
    // Editor-only selection + display-name state, kept out of the Scene core.
    #include "EditorContext.h"

    // Editor-only Asset Browser model: scans the project's on-disk asset folders
    // (models/, assets/) into a sorted, selectable list. Pure filesystem + state,
    // no Vulkan, so it stays a plain header alongside EditorContext.
    #include "AssetBrowser.h"

    // ImGuizmo draws the in-viewport translate/rotate/scale handles. It only needs
    // imgui.h (pulled in above under RAG_ENABLE_IMGUI, which RAG_EDITOR implies) and
    // shares ImGui's single static context. Silence its third-party /W4 noise.
    #if defined(_MSC_VER)
        #pragma warning(push, 0)
    #endif
    #include <ImGuizmo.h>
    #if defined(_MSC_VER)
        #pragma warning(pop)
    #endif
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if !defined(RAG_BUILD_CONFIGURATION)
    #define RAG_BUILD_CONFIGURATION "unknown"
#endif

#if !defined(RAG_CMAKE_BUILD_DIRECTORY)
    #define RAG_CMAKE_BUILD_DIRECTORY "unknown"
#endif

#if !defined(RAG_PROJECT_VERSION)
    #define RAG_PROJECT_VERSION "unknown"
#endif

namespace
{
    void LogBuildIdentity(int argc, char** argv)
    {
        std::filesystem::path executable_path = rag::platform::ExecutableDirectory();
        if (argc > 0 && argv != nullptr && argv[0] != nullptr)
        {
            executable_path /= std::filesystem::path(argv[0]).filename();
        }
        else
        {
            executable_path /= "RagSandbox.exe";
        }

        std::error_code working_directory_error;
        const std::filesystem::path working_directory =
            std::filesystem::current_path(working_directory_error);

        RAG_LOG_INFO(
            "Build: RAG Engine ", RAG_PROJECT_VERSION, " | ", RAG_BUILD_CONFIGURATION,
            " | compiled ", __DATE__, " ", __TIME__);
        RAG_LOG_INFO("Executable: ", executable_path.lexically_normal().string());
        RAG_LOG_INFO("CMake build directory: ", RAG_CMAKE_BUILD_DIRECTORY);
        if (working_directory_error)
        {
            RAG_LOG_WARNING(
                "Working directory unavailable: ", working_directory_error.message());
        }
        else
        {
            RAG_LOG_INFO("Working directory: ", working_directory.lexically_normal().string());
        }
    }

    class SandboxClient final : public rag::core::IApplicationClient
    {
    public:
        SandboxClient(bool smoke_test, bool play_mode_self_test)
            : smoke_test_(smoke_test),
              play_mode_self_test_requested_(play_mode_self_test)
        {
        }

        // Process exit code once the loop ends: non-zero when the smoke test or
        // the play-mode self-test failed or did not run to completion.
        [[nodiscard]] int ExitCode() const
        {
            if (smoke_test_ && !smoke_test_passed_)
            {
                return 1;
            }
#if defined(RAG_EDITOR)
            if (self_test_.has_value() && (self_test_->failed || !self_test_->completed))
            {
                return 1;
            }
#else
            if (play_mode_self_test_requested_)
            {
                return 1;
            }
#endif
            return 0;
        }

        void OnStart() override
        {
            RAG_LOG_INFO("RAG Engine Vulkan scene sandbox started. Press Escape to quit, F11 to toggle fullscreen.");
            RAG_LOG_INFO(
                "Free-fly camera: WASD move, Space/E up, Left Ctrl/Q down, "
                "hold Right Mouse to look, Left Shift to sprint.");
            RAG_LOG_INFO(
                "First-person player: a scene with a Player Start spawns a player on Play. "
                "Click the view to control it (WASD, mouse look, Space jumps); Esc releases the mouse.");
            InitializePhysics();

            // Load the scene from the default scene file instead of building it in
            // code, so the same save/load path the editor uses is exercised every
            // launch. Missing/unreadable files use an in-memory fallback that is
            // an Untitled document: no file backs it, so Save asks for a name
            // instead of creating or overwriting a default scene.
            RAG_LOG_INFO("Scene files: Save As writes to '", ScenesDirectory().lexically_normal().string(), "'.");
            std::string scene_error;
            bool loaded = false;
            if (const std::optional<std::filesystem::path> existing = FindDefaultSceneFile(scene_error))
            {
                loaded = LoadSceneFromPath(*existing, scene_error);
            }

            if (!loaded)
            {
                RAG_LOG_INFO(
                    "No usable default scene file (", scene_error, "); building the built-in scene in memory "
                    "as an Untitled document. Nothing is written until File > Save Scene As.");
                BuildScene();
                MarkDocumentUntitled();
            }

#if defined(RAG_EDITOR)
            // The editor opens in Edit: the authored scene stays exactly where it
            // was saved until a test run is started explicitly.
            RAG_LOG_INFO(
                "Simulation: Edit. F5 = Play/Stop, F6 = Pause/Resume, F7 = Step one tick "
                "(or use the Play/Pause/Step/Stop buttons in the menu bar). F1 hides/shows the editor UI.");
#else
            // A game build has no editor to press Play in, so it simulates from
            // the first frame (through the same lifecycle path the editor uses).
            RAG_LOG_INFO("Simulation: game build, starting Play immediately.");
            RequestSimulationCommand(SimulationCommand::Play);
#endif

            if (play_mode_self_test_requested_)
            {
#if defined(RAG_EDITOR)
                RAG_LOG_INFO("Play-mode self-test requested; running the scripted lifecycle checks.");
                BuildPlayModeSelfTest();
#else
                RAG_LOG_ERROR("The play-mode self-test needs the editor build (RAG_EDITOR); closing.");
                requested_close_ = true;
#endif
            }

            if (smoke_test_)
            {
#if defined(RAG_ENABLE_VULKAN)
                RAG_LOG_INFO("Smoke test requested; rendering one frame per lifecycle state before shutdown.");
#else
                RAG_LOG_INFO("Smoke test requested; closing after startup.");
                smoke_test_passed_ = true;
                requested_close_ = true;
#endif
            }
        }

        void InitializeRenderer(rag::platform::IWindow& window)
        {
            window_ = &window;

#if defined(RAG_ENABLE_VULKAN)
            rag::renderer::RendererDesc renderer_desc{};
            renderer_desc.backend = rag::renderer::RendererBackend::Vulkan;
            renderer_desc.window = &window;
            renderer_desc.application_name = "RAG Engine Sandbox";
            renderer_desc.frames_in_flight = 2;

#if defined(RAG_DEBUG)
            renderer_desc.enable_validation = true;
#else
            renderer_desc.enable_validation = false;
#endif

            renderer_ = rag::renderer::vk::CreateVulkanRenderer(renderer_desc);
#else
            (void)window;
            RAG_LOG_WARNING("Vulkan renderer is not compiled; sandbox is running the Phase 1 window loop only.");
#endif
        }

        void OnEvent(const rag::core::Event& event) override
        {
            // Alt+Tab, a click on another window or a minimize: the platform has
            // already given the cursor back; drop the player's control too, and
            // the held keys InputState just released can no longer move it.
            if (event.type == rag::core::EventType::WindowFocus &&
                !std::get<rag::core::WindowFocusEvent>(event.payload).focused)
            {
                ReleasePlayerInput("the window lost focus");
            }

            if (event.type == rag::core::EventType::WindowResize)
            {
                const auto& resize = std::get<rag::core::WindowResizeEvent>(event.payload);
                RAG_LOG_INFO("Window resized to ", resize.width, "x", resize.height);

                if (resize.width > 0 && resize.height > 0)
                {
                    if (rag::scene::CameraComponent* camera = scene_.GetCamera(camera_entity_))
                    {
                        camera->aspect_ratio =
                            static_cast<rag::f32>(resize.width) /
                            static_cast<rag::f32>(resize.height);
                    }
                }

#if defined(RAG_ENABLE_VULKAN)
                if (renderer_ != nullptr)
                {
                    renderer_->OnWindowResized(resize.width, resize.height);
                }
#endif
            }
        }

        void OnUpdate(
            const rag::core::FrameTime& frame_time,
            const rag::core::InputState& input) override
        {
            if (input.WasKeyPressed(rag::core::KeyCode::Escape))
            {
                HandleEscape();
            }

            if (input.WasKeyPressed(rag::core::KeyCode::F11) && window_ != nullptr)
            {
                window_->SetFullscreen(!window_->IsFullscreen());
            }

#if defined(RAG_EDITOR)
            // F1 shows/hides the editor UI (dockspace + panels) -- the bare
            // "game view". It only changes what is drawn: the simulation state
            // (Edit/Playing/Paused) is independent and is not touched by F1.
            if (input.WasKeyPressed(rag::core::KeyCode::F1))
            {
                editor_mode_ = !editor_mode_;
                OnEditorUiVisibilityChanged();
            }
#endif

            bool ui_wants_mouse = false;
            bool ui_wants_keyboard = false;
#if defined(RAG_ENABLE_IMGUI)
            // While ImGui is interacting (hovering a window, dragging a slider or
            // typing) it reports that it wants the mouse/keyboard; the free-fly
            // camera then ignores that input so the UI and the view don't fight.
            if (ImGui::GetCurrentContext() != nullptr)
            {
                const ImGuiIO& io = ImGui::GetIO();
                ui_wants_mouse = io.WantCaptureMouse;
                ui_wants_keyboard = io.WantCaptureKeyboard;
            }
#endif

            bool accept_user_input = true;
#if defined(RAG_EDITOR)
            // The self-test drives the editor itself, so live input (camera,
            // shortcuts, picking) is ignored while it runs to keep it deterministic.
            accept_user_input = !self_test_.has_value();
            if (self_test_.has_value())
            {
                RunPlayModeSelfTestFrame(frame_time);
            }

            // F5/F6/F7/Delete are ignored while ImGui is actually consuming
            // keystrokes: a text field is being edited (typing a value, including
            // Delete inside it) or a widget is held/dragged. Unlike WASD they do
            // not also wait for a panel to lose nav focus: ImGui's keyboard
            // navigation never uses these keys, and panels take nav focus whenever
            // they appear or are clicked (startup, F1, the Play button itself), so
            // that rule would make "click Play, press F6" or "select in Hierarchy,
            // press Delete" silently do nothing. They also work with the UI hidden.
            bool ui_consuming_keys = false;
            if (ImGui::GetCurrentContext() != nullptr)
            {
                ui_consuming_keys = ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive();
            }
            if (accept_user_input && !ui_consuming_keys)
            {
                HandleSimulationShortcuts(input);
                // Delete is an editor key: while the mouse is captured for the
                // player the keyboard belongs to the game.
                if (editor_mode_ && !player_input_captured_ && input.WasKeyPressed(rag::core::KeyCode::Delete))
                {
                    RequestDeleteEntity(editor_.Selected());
                }
            }

            // Safe point: deletions and lifecycle requests (from last frame's UI or
            // the keys above) are applied here, before anything reads the scene
            // this frame -- never inside the renderer's UI callback, so a Stop
            // restore cannot swap the scene out from under a panel or a draw.
            ProcessPendingEntityDeletion();
            ProcessPendingPlayerStartCreation();
#endif
            ProcessPendingSimulationCommand();

            // After the lifecycle settled: release the mouse if the player view
            // just ended, take it on a click on the game view, and sample this
            // frame's WASD / jump / mouse-look intent (zero unless captured and
            // Playing). The simulation below consumes it.
            UpdatePlayerInput(input, accept_user_input, ui_wants_mouse);

#if defined(RAG_EDITOR)
            // Gizmo mode hotkeys W/E/R (translate/rotate/scale). These letters are
            // also free-fly movement keys, so only switch modes when the camera is
            // NOT being flown (right mouse up) and ImGui is not typing, leaving the
            // fly controls untouched. The gizmo toolbar buttons work regardless.
            // W/E also walk the player, so they never switch modes while captured.
            if (accept_user_input && editor_mode_ && !ui_wants_keyboard && !player_input_captured_ &&
                !input.IsMouseButtonDown(rag::core::MouseButton::Right))
            {
                if (input.WasKeyPressed(rag::core::KeyCode::W))
                {
                    gizmo_operation_ = ImGuizmo::TRANSLATE;
                }
                if (input.WasKeyPressed(rag::core::KeyCode::E))
                {
                    gizmo_operation_ = ImGuizmo::ROTATE;
                }
                if (input.WasKeyPressed(rag::core::KeyCode::R))
                {
                    gizmo_operation_ = ImGuizmo::SCALE;
                }
            }
#endif

            if (accept_user_input && !PlayerViewActive())
            {
                // Editor navigation, not simulation: the free-fly camera moves in
                // every state, including Edit and Paused -- but not while the
                // viewport shows the player, whose keys and mouse would otherwise
                // also fly the (hidden) editor camera.
                UpdateCamera(frame_time, input, ui_wants_mouse, ui_wants_keyboard);
            }
            else
            {
                // Resume mouse look cleanly later instead of from a stale cursor.
                mouse_look_active_ = false;
            }
            UpdateSimulation(frame_time);
            SyncLightFromUi();
            scene_.Update();
            scene_.ExtractRenderWorld(render_world_);
            if (PlayerViewActive())
            {
                // The renderer just receives a camera: the player's eyes while
                // Playing, the scene's active (editor) camera otherwise.
                ApplyPlayerView(render_world_.camera);
            }

#if defined(RAG_EDITOR)
            // Resolve a viewport left-click into a selection now that the camera and
            // world transforms for this frame are up to date. Editor mode only, and
            // never in the player view, where a click on the view takes control.
            if (editor_mode_ && accept_user_input && !PlayerViewActive())
            {
                UpdateViewportSelection(input, ui_wants_mouse);
            }
#endif

#if defined(RAG_ENABLE_VULKAN)
            if (renderer_ != nullptr)
            {
                rag::renderer::RenderFrameContext render_context{};
                render_context.clear_color = {0.02f, 0.02f, 0.06f, 1.0f};
                render_context.render_world = &render_world_;
#if defined(RAG_EDITOR)
                // Only hand the renderer a UI builder while editor mode is on or the
                // player overlay (crosshair / "click to play" hint) is needed; with
                // neither the overlay frame stays empty and the renderer draws the
                // game alone. (RAG_EDITOR implies RAG_ENABLE_IMGUI.)
                if (editor_mode_ || PlayerViewActive())
                {
                    render_context.on_build_ui = [this]()
                    {
                        if (editor_mode_)
                        {
                            BuildEditorUi();
                        }
                        BuildPlayerOverlay();
                    };
                }
                if (editor_mode_)
                {
                    // Highlight the selected object in the 3D view with a world-space
                    // bounding-box outline that tracks it as physics moves it.
                    render_context.selection_highlight = ComputeSelectionHighlight();
                }
#elif defined(RAG_ENABLE_IMGUI)
                if (PlayerViewActive())
                {
                    render_context.on_build_ui = [this]() { BuildPlayerOverlay(); };
                }
#endif
                renderer_->RenderFrame(render_context);
            }
#endif

            if (smoke_test_)
            {
                AdvanceSmokeTest();
            }

            time_since_title_update_ += frame_time.delta_seconds;
            if (time_since_title_update_ >= 1.0)
            {
                RAG_LOG_TRACE(
                    "Frame ",
                    frame_time.frame_index,
                    " dt=",
                    frame_time.delta_seconds,
                    "s objects=",
                    render_world_.objects.size(),
                    " mouse=(",
                    input.MouseX(),
                    ", ",
                    input.MouseY(),
                    ")");
                time_since_title_update_ = 0.0;
            }
        }

        [[nodiscard]] bool ShouldClose() const override
        {
            return requested_close_;
        }

        void OnStop() override
        {
            // Give the cursor back while ImGui (owned by the renderer) still exists.
            ReleasePlayerInput("shutting down");

#if defined(RAG_ENABLE_VULKAN)
            if (renderer_ != nullptr)
            {
                renderer_->WaitIdle();
                renderer_.reset();
            }
#endif

#if defined(RAG_ENABLE_PHYSICS)
            physics_.Shutdown();
#endif

#if defined(RAG_EDITOR)
            // Also covers a self-test that was closed before it finished.
            RemoveSelfTestDocumentFiles();
#endif
        }

    private:
        // One simulation tick: Jolt's reference collision step, and exactly what a
        // single Step advances while Paused. Playing frames hand Jolt the frame
        // delta clamped to MaxSimulationFrameDeltaSeconds; Jolt subdivides it into
        // ceil(delta / tick) collision steps (there is no separate accumulator).
        static constexpr rag::f32 SimulationTickSeconds = 1.0f / 60.0f;
        static constexpr rag::f32 MaxSimulationFrameDeltaSeconds = 0.033f;

        // Everything Stop needs to put the authored scene back, captured in memory
        // on entering Play and never written to disk. scene is the same neutral
        // description Save writes, so restoration reuses ApplyScene; the other
        // fields are sandbox-level state the scene file does not carry.
        // entities[i] is the live id scene.entities[i] was captured from, used only
        // to re-select that entity after Stop -- entity ids are NOT preserved
        // across Stop (the rebuilt entities get new generations).
        struct PlaySnapshot
        {
            rag::scene::SerializedScene scene;
            std::vector<rag::scene::EntityId> entities;
            std::size_t physics_record_count = 0;
            rag::f32 gravity_y = 0.0f;
            rag::math::Vec3 light_euler{};
            rag::math::Vec3 light_color{};
            rag::f32 light_intensity = 0.0f;
            rag::u32 spawn_counter = 0;
        };

        // Lifecycle requests from the toolbar, menu, shortcuts or tests. They are
        // queued and applied at the start of the next frame (the safe point).
        enum class SimulationCommand : rag::u8
        {
            Play,
            Pause,
            Resume,
            Stop,
            Step
        };

        [[nodiscard]] static const char* SimulationCommandName(SimulationCommand command)
        {
            switch (command)
            {
                case SimulationCommand::Play: return "Play";
                case SimulationCommand::Pause: return "Pause";
                case SimulationCommand::Resume: return "Resume";
                case SimulationCommand::Stop: return "Stop";
                case SimulationCommand::Step: return "Step";
            }
            return "Unknown";
        }

#if defined(RAG_ENABLE_PHYSICS)
        // Links a Jolt body to the scene entity it drives. desc is the source of
        // truth for the collider + simulation settings, so the body can be rebuilt
        // (None <-> Static <-> Dynamic) without losing shape/scale. body is invalid
        // while the entity is in BodyMode::None. start_position/start_rotation let
        // the editor's reset button drop it again.
        struct PhysicsBody
        {
            rag::scene::EntityId entity{};
            rag::physics::BodyHandle body{};
            rag::math::Vec3 start_position{};
            rag::math::Quat start_rotation{};
            rag::math::Vec3 synchronized_scale{1.0f, 1.0f, 1.0f};
            rag::physics::BodyDesc desc{};
        };

        void InitializePhysics()
        {
            rag::physics::PhysicsWorldDesc desc{};
            desc.gravity = rag::math::Vec3{0.0f, -9.81f, 0.0f};
            desc.fixed_timestep_seconds = SimulationTickSeconds;
            desc.max_frame_delta_seconds = MaxSimulationFrameDeltaSeconds;

            physics_ready_ = physics_.Initialize(desc);
            if (!physics_ready_)
            {
                RAG_LOG_ERROR("Physics initialization failed; scene objects will not be simulated.");
            }
        }
#else
        void InitializePhysics() {}
#endif

        // Assigns an editor-side display name so the entity is identifiable in the
        // Hierarchy panel. Names live in the editor context, never in the Scene, so
        // this compiles to a no-op when the editor UI is disabled.
        void NameEntity(rag::scene::EntityId entity, std::string_view name)
        {
#if defined(RAG_EDITOR)
            editor_.SetName(entity, name);
#else
            (void)entity;
            (void)name;
#endif
        }

        // Runtime metadata for meshes already registered by the renderer. Bounds
        // are shared by startup entities, selection, and physics fitting so every
        // creation path agrees on the model's local-space size and pivot.
        [[nodiscard]] static rag::renderer::Bounds BoundsForMesh(
            rag::renderer::MeshHandle mesh)
        {
            rag::renderer::Bounds bounds{};
            if (mesh.index == 1) // models/sphere.obj
            {
                bounds.extents = rag::math::Vec3{1.0f, 1.0f, 1.0f};
            }
            else if (mesh.index == 3) // models/Duck.glb
            {
                bounds.center = rag::math::Vec3{0.0f, 0.7f, 0.0f};
                bounds.extents = rag::math::Vec3{0.7f, 0.6f, 0.7f};
            }
            return bounds;
        }

        [[nodiscard]] static bool MeshUsesSphereCollider(
            rag::renderer::MeshHandle mesh)
        {
            return mesh.index == 1; // models/sphere.obj
        }

        void BuildScene()
        {
            scene_.Environment() = {true, {
                "assets/skyboxes/starter/px.png", "assets/skyboxes/starter/nx.png",
                "assets/skyboxes/starter/py.png", "assets/skyboxes/starter/ny.png",
                "assets/skyboxes/starter/pz.png", "assets/skyboxes/starter/nz.png"}};
            constexpr rag::u32 RingCubeCount = 8;
            constexpr rag::f32 RingRadius = 4.0f;
            constexpr rag::f32 CenterCubeScale = 1.5f;

            // Dynamic bodies start elevated so they visibly fall and settle on
            // the floor at startup. Tweak these to change the drop heights.
            constexpr rag::f32 RingCubeStartHeight = 7.0f;
            constexpr rag::f32 RingCubeStartStagger = 0.6f; // raises each successive cube
            constexpr rag::f32 CenterCubeStartHeight = 12.0f;
            constexpr rag::f32 SphereStartHeight = 9.0f;

            // The static ground is a unit cube mesh scaled to (40, 0.2, 40) centred
            // at y=-0.6. Its collider uses the same unit half-extents as the cubes
            // and inherits that scale through the transform, so the box spans
            // (20, 0.1, 20) with a top surface at y=-0.5 and follows edits to scale.
            constexpr rag::math::Vec3 GroundCenter{0.0f, -0.6f, 0.0f};
            constexpr rag::math::Vec3 GroundScale{40.0f, 0.2f, 40.0f};

            // Duck.glb's bounds are offset upward from its pivot. BoundsForMesh
            // supplies the same center/extents to rendering, selection and physics.
            constexpr rag::f32 DuckDropHeight = 3.0f;

            // Renderer mesh registry convention: slot 0 = built-in cube,
            // slot 1 = OBJ sphere, slot 2 = tiled-UV ground, slot 3 = glTF duck.
            constexpr rag::renderer::MeshHandle CubeMeshHandle{0, 0};
            constexpr rag::renderer::MeshHandle SphereMeshHandle{1, 0};
            constexpr rag::renderer::MeshHandle GroundMeshHandle{2, 0};
            constexpr rag::renderer::MeshHandle DuckMeshHandle{3, 0};

            for (rag::u32 index = 0; index < RingCubeCount; ++index)
            {
                const rag::f32 angle = (2.0f * rag::math::Pi * static_cast<rag::f32>(index)) /
                                       static_cast<rag::f32>(RingCubeCount);

                const rag::math::Vec3 start_position{
                    RingRadius * std::cos(angle),
                    RingCubeStartHeight + (RingCubeStartStagger * static_cast<rag::f32>(index)),
                    RingRadius * std::sin(angle)};

                const rag::scene::EntityId entity = scene_.CreateEntity();
                NameEntity(entity, "Cube " + std::to_string(index));
                scene_.AddTransform(entity).local_position = start_position;
                scene_.AddRenderable(entity).mesh = CubeMeshHandle;
                (void)SpawnDynamicBox(entity);
            }

            const rag::math::Vec3 center_start{0.0f, CenterCubeStartHeight, 0.0f};
            const rag::scene::EntityId center_cube = scene_.CreateEntity();
            NameEntity(center_cube, "Center Cube");
            rag::scene::TransformComponent& center_transform = scene_.AddTransform(center_cube);
            center_transform.local_position = center_start;
            center_transform.local_scale =
                rag::math::Vec3{CenterCubeScale, CenterCubeScale, CenterCubeScale};
            scene_.AddRenderable(center_cube).mesh = CubeMeshHandle;
            (void)SpawnDynamicBox(center_cube);

            const rag::scene::EntityId ground = scene_.CreateEntity();
            NameEntity(ground, "Ground");
            rag::scene::TransformComponent& ground_transform = scene_.AddTransform(ground);
            ground_transform.local_position = GroundCenter;
            ground_transform.local_scale = GroundScale;
            scene_.AddRenderable(ground).mesh = GroundMeshHandle;
            (void)SpawnStaticBox(ground);

            // The OBJ sphere drops onto open floor off to the side of the ring so
            // its shadow stays clearly visible as it falls and lands.
            const rag::math::Vec3 sphere_start{6.5f, SphereStartHeight, 0.0f};
            const rag::scene::EntityId sphere = scene_.CreateEntity();
            NameEntity(sphere, "Sphere");
            scene_.AddTransform(sphere).local_position = sphere_start;
            rag::scene::RenderableComponent& sphere_renderable = scene_.AddRenderable(sphere);
            sphere_renderable.mesh = SphereMeshHandle;
            sphere_renderable.local_bounds = BoundsForMesh(SphereMeshHandle);
            (void)SpawnDynamicSphere(sphere);

            const rag::scene::EntityId duck = scene_.CreateEntity();
            NameEntity(duck, "Duck");
            rag::scene::TransformComponent& duck_transform = scene_.AddTransform(duck);
            duck_transform.local_position =
                rag::math::Vec3{-6.5f, DuckDropHeight, 0.0f};
            rag::scene::RenderableComponent& duck_renderable = scene_.AddRenderable(duck);
            duck_renderable.mesh = DuckMeshHandle;
            duck_renderable.local_bounds = BoundsForMesh(DuckMeshHandle);
            (void)SpawnDynamicBox(duck);

            CreateDefaultCameraEntity();

            // The directional light is kept editable from the debug panel: its
            // rotation, color and intensity live in members the UI mutates and
            // SyncLightFromUi() pushes onto the scene each frame.
            CreateDefaultLightEntity();

            RAG_LOG_INFO(
                "Built sandbox scene: ",
                scene_.EntityCount(),
                " entities (",
                RingCubeCount,
                " ring cubes, 1 center cube, 1 obj sphere, 1 glTF duck, "
                "1 ground, 1 camera, 1 directional light), ",
                DynamicBodyCount(),
                " dynamic bodies falling under gravity.");
        }

#if defined(RAG_ENABLE_PHYSICS)
        // The rotation a transform composes from, as a quaternion: physics-driven
        // transforms store it directly, editor-driven ones as Euler angles.
        [[nodiscard]] static rag::math::Quat TransformRotation(
            const rag::scene::TransformComponent& transform)
        {
            return transform.use_quaternion_rotation
                ? transform.local_rotation_quat
                : rag::math::QuatFromEulerXYZ(transform.local_rotation_radians);
        }

        // Core registration: creates the Jolt body from a descriptor filled with
        // the entity's current transform, then records it so the editor can later
        // rescale, reset, reposition, or switch its mode. desc is kept as the
        // collider source data (shape, base extents, offset) that survives a mode
        // change, so None -> Dynamic rebuilds the correct collider at the right size.
        [[nodiscard]] bool RegisterPhysicsBody(
            rag::scene::EntityId entity,
            rag::physics::BodyDesc desc)
        {
            if (!physics_ready_ || !scene_.IsAlive(entity))
            {
                return false;
            }

            if (FindPhysicsBody(entity) != nullptr)
            {
                RAG_LOG_WARNING(
                    "Physics: entity ",
                    entity.ToU64(),
                    " is already registered; duplicate body creation was skipped.");
                return false;
            }

            rag::math::Vec3 position = desc.position;
            rag::math::Quat rotation = desc.rotation;
            rag::math::Vec3 scale = desc.scale;
            if (const rag::scene::TransformComponent* transform = scene_.GetTransform(entity))
            {
                position = transform->local_position;
                scale = transform->local_scale;
                rotation = TransformRotation(*transform);
            }
            desc.position = position;
            desc.rotation = rotation;
            desc.scale = scale;

            rag::physics::BodyHandle body{};
            if (desc.mode != rag::physics::BodyMode::None)
            {
                body = physics_.CreateBody(desc);
                if (!body.IsValid())
                {
                    RAG_LOG_ERROR(
                        "Physics: failed to register body for entity ",
                        entity.ToU64(),
                        ".");
                    return false;
                }
            }

            physics_bodies_.push_back(
                PhysicsBody{entity, body, position, rotation, scale, desc});

            // Physics owns a dynamic body's orientation, so drive it by quaternion
            // to avoid gimbal issues; static/none entities keep their editor Euler.
            if (body.IsValid() && desc.mode == rag::physics::BodyMode::Dynamic)
            {
                if (rag::scene::TransformComponent* transform = scene_.GetTransform(entity))
                {
                    transform->use_quaternion_rotation = true;
                }
            }
            return true;
        }

        // Fits a primitive collider to the renderable's local bounds, then uses
        // the same BodyDesc registration path for startup and runtime entities.
        [[nodiscard]] bool RegisterRenderablePhysics(
            rag::scene::EntityId entity,
            rag::physics::BodyMode mode,
            rag::physics::ColliderShape shape)
        {
            const rag::scene::RenderableComponent* renderable =
                scene_.GetRenderable(entity);
            if (renderable == nullptr)
            {
                return false;
            }

            constexpr rag::f32 MinimumExtent = 0.01f;
            const rag::renderer::Bounds& bounds = renderable->local_bounds;
            const rag::math::Vec3 extents{
                std::max(std::abs(bounds.extents.x), MinimumExtent),
                std::max(std::abs(bounds.extents.y), MinimumExtent),
                std::max(std::abs(bounds.extents.z), MinimumExtent)};

            rag::physics::BodyDesc desc;
            desc.shape = shape;
            desc.mode = mode;
            desc.offset = bounds.center;
            desc.gravity_enabled = true;
            desc.continuous_collision = true;
            if (shape == rag::physics::ColliderShape::Sphere)
            {
                desc.sphere_radius =
                    std::max(extents.x, std::max(extents.y, extents.z));
            }
            else
            {
                desc.box_half_extents = extents;
            }
            return RegisterPhysicsBody(entity, desc);
        }

        [[nodiscard]] bool SpawnDynamicBox(rag::scene::EntityId entity)
        {
            return RegisterRenderablePhysics(
                entity,
                rag::physics::BodyMode::Dynamic,
                rag::physics::ColliderShape::Box);
        }

        [[nodiscard]] bool SpawnDynamicSphere(rag::scene::EntityId entity)
        {
            return RegisterRenderablePhysics(
                entity,
                rag::physics::BodyMode::Dynamic,
                rag::physics::ColliderShape::Sphere);
        }

        [[nodiscard]] bool SpawnStaticBox(rag::scene::EntityId entity)
        {
            return RegisterRenderablePhysics(
                entity,
                rag::physics::BodyMode::Static,
                rag::physics::ColliderShape::Box);
        }

        [[nodiscard]] bool SpawnDynamicPhysicsForMesh(
            rag::scene::EntityId entity,
            rag::renderer::MeshHandle mesh)
        {
            return MeshUsesSphereCollider(mesh)
                ? SpawnDynamicSphere(entity)
                : SpawnDynamicBox(entity);
        }

        [[nodiscard]] std::size_t DynamicBodyCount() const
        {
            std::size_t count = 0;
            for (const PhysicsBody& physics_body : physics_bodies_)
            {
                if (physics_body.desc.mode == rag::physics::BodyMode::Dynamic)
                {
                    ++count;
                }
            }
            return count;
        }

        // Scene destruction owns entity lifetime. Sweep physics records before
        // each step so any runtime-destroyed entity immediately releases its Jolt
        // body and cannot leave a dangling simulation record.
        void RemoveDestroyedPhysicsBodies()
        {
            for (auto it = physics_bodies_.begin(); it != physics_bodies_.end();)
            {
                if (scene_.IsAlive(it->entity))
                {
                    ++it;
                    continue;
                }

                if (it->body.IsValid())
                {
                    physics_.DestroyBody(it->body);
                }
                it = physics_bodies_.erase(it);
            }
        }

        static bool PhysicsScalesDiffer(
            const rag::math::Vec3& left,
            const rag::math::Vec3& right)
        {
            constexpr rag::f32 Epsilon = 0.0001f;
            return
                std::abs(left.x - right.x) > Epsilon ||
                std::abs(left.y - right.y) > Epsilon ||
                std::abs(left.z - right.z) > Epsilon;
        }

        // Detect transform scale edits from any source before simulation and push
        // them onto the collider. Jolt rebuilds from each body's immutable base
        // shape, and only when this comparison detects a real change. Applies to
        // static and dynamic bodies; entities with no live body (None) are skipped.
        void SynchronizeColliderScales()
        {
            for (PhysicsBody& physics_body : physics_bodies_)
            {
                if (!physics_body.body.IsValid())
                {
                    continue;
                }

                const rag::scene::TransformComponent* transform =
                    scene_.GetTransform(physics_body.entity);
                if (transform == nullptr ||
                    !PhysicsScalesDiffer(
                        transform->local_scale,
                        physics_body.synchronized_scale))
                {
                    continue;
                }

                if (physics_.SetBodyScale(
                        physics_body.body,
                        transform->local_scale))
                {
                    physics_body.synchronized_scale = transform->local_scale;
                }
            }
        }

        // Advances Jolt by seconds (already decided by the lifecycle) and copies
        // the result onto the scene. Never called in Edit, nor while Paused except
        // for a single-tick Step.
        void StepPhysics(rag::f32 seconds)
        {
            physics_.Step(seconds);
            WriteBackDynamicBodies();
        }

        // Write each dynamic body's simulated pose back onto its scene transform
        // so the renderer draws it where physics put it. Static bodies keep the
        // pose the editor set, and None-mode entities have no body to read.
        void WriteBackDynamicBodies()
        {
            for (const PhysicsBody& physics_body : physics_bodies_)
            {
                if (physics_body.desc.mode != rag::physics::BodyMode::Dynamic ||
                    !physics_body.body.IsValid())
                {
                    continue;
                }

                rag::scene::TransformComponent* transform = scene_.GetTransform(physics_body.entity);
                if (transform == nullptr)
                {
                    continue;
                }

                const rag::physics::BodyState state = physics_.GetBodyState(physics_body.body);
                transform->local_position = state.position;
                transform->local_rotation_quat = state.rotation;
                transform->use_quaternion_rotation = true;
                scene_.MarkTransformDirty(physics_body.entity);
            }
        }

        // Pushes the editor's gravity slider value into the live Jolt world.
        void ApplyGravity()
        {
            if (physics_ready_)
            {
                physics_.SetGravity(rag::math::Vec3{0.0f, physics_gravity_y_, 0.0f});
            }
        }

        // Drops every dynamic body back to the pose it had when Play started (or
        // when it was spawned during the run) so they fall again. Static and
        // None-mode entities are left where they are. Only meaningful during a
        // test run: Edit does not simulate, and Stop restores the authored scene.
        void ResetPhysicsBodies()
        {
            if (!physics_ready_ || simulation_.State() == rag::runtime::SimulationState::Edit)
            {
                return;
            }

            for (const PhysicsBody& physics_body : physics_bodies_)
            {
                if (physics_body.desc.mode != rag::physics::BodyMode::Dynamic ||
                    !physics_body.body.IsValid())
                {
                    continue;
                }
                physics_.SetBodyPose(
                    physics_body.body,
                    physics_body.start_position,
                    physics_body.start_rotation);
            }
            // Mirror the reset onto the transforms now, so it is visible (and
            // inspectable) while Paused instead of only after the next step.
            WriteBackDynamicBodies();
            RAG_LOG_INFO("Physics bodies reset to their start positions.");
        }

        // Play starts from exactly the authored arrangement: every body is moved
        // to its entity's current transform with velocity cleared (dynamic bodies
        // are woken), and that pose becomes the "Reset bodies" target for the run.
        // Edit-mode edits already push into bodies, so this is a guarantee, not a
        // fix-up; it never steps the simulation.
        void PrepareBodiesForPlay()
        {
            if (!physics_ready_)
            {
                return;
            }

            RemoveDestroyedPhysicsBodies();
            SynchronizeColliderScales();
            for (PhysicsBody& physics_body : physics_bodies_)
            {
                const rag::scene::TransformComponent* transform =
                    scene_.GetTransform(physics_body.entity);
                if (transform == nullptr)
                {
                    continue;
                }

                physics_body.start_position = transform->local_position;
                physics_body.start_rotation = TransformRotation(*transform);
                if (physics_body.body.IsValid())
                {
                    physics_.SetBodyPose(
                        physics_body.body,
                        physics_body.start_position,
                        physics_body.start_rotation);
                }
            }
        }

        // Returns the physics record tracked for an entity (any mode, including a
        // None entity whose body is currently absent), or nullptr for an entity the
        // physics system does not manage (the camera, the light).
        [[nodiscard]] PhysicsBody* FindPhysicsBody(rag::scene::EntityId entity)
        {
            for (PhysicsBody& physics_body : physics_bodies_)
            {
                if (physics_body.entity == entity)
                {
                    return &physics_body;
                }
            }
            return nullptr;
        }

        // Switches an entity between None/Static/Dynamic live. Destroys the old body
        // and rebuilds from the preserved descriptor + current transform, so the
        // collider keeps its shape, offset and scale across the change.
        void SetEntityBodyMode(PhysicsBody& physics_body, rag::physics::BodyMode new_mode)
        {
            if (physics_body.desc.mode == new_mode)
            {
                return;
            }

            if (physics_body.body.IsValid())
            {
                physics_.DestroyBody(physics_body.body);
                physics_body.body = {};
            }

            physics_body.desc.mode = new_mode;
            if (new_mode == rag::physics::BodyMode::None)
            {
                return;
            }

            // Rebuild at the entity's current pose/scale so nothing jumps.
            if (const rag::scene::TransformComponent* transform =
                    scene_.GetTransform(physics_body.entity))
            {
                physics_body.desc.position = transform->local_position;
                physics_body.desc.scale = transform->local_scale;
                physics_body.desc.rotation = TransformRotation(*transform);
            }

            physics_body.body = physics_.CreateBody(physics_body.desc);
            physics_body.synchronized_scale = physics_body.desc.scale;

            if (physics_body.body.IsValid() &&
                new_mode == rag::physics::BodyMode::Dynamic)
            {
                if (rag::scene::TransformComponent* transform =
                        scene_.GetTransform(physics_body.entity))
                {
                    transform->use_quaternion_rotation = true;
                }
            }
        }

        // Toggles gravity on a dynamic body live (no rebuild needed).
        void SetEntityGravityEnabled(PhysicsBody& physics_body, bool enabled)
        {
            physics_body.desc.gravity_enabled = enabled;
            if (physics_body.body.IsValid() &&
                physics_body.desc.mode == rag::physics::BodyMode::Dynamic)
            {
                physics_.SetBodyGravityEnabled(physics_body.body, enabled);
            }
        }
#else
        [[nodiscard]] bool SpawnDynamicBox(rag::scene::EntityId) { return false; }
        [[nodiscard]] bool SpawnDynamicSphere(rag::scene::EntityId) { return false; }
        [[nodiscard]] bool SpawnStaticBox(rag::scene::EntityId) { return false; }
        [[nodiscard]] bool SpawnDynamicPhysicsForMesh(
            rag::scene::EntityId,
            rag::renderer::MeshHandle)
        {
            return false;
        }
        [[nodiscard]] std::size_t DynamicBodyCount() const { return 0; }
        void PrepareBodiesForPlay() {}
#endif

        // Advances the simulation by whatever the lifecycle allows this frame:
        // the clamped frame delta while Playing, exactly one tick on the frame
        // after a Paused Step, and nothing otherwise. Bookkeeping that keeps Jolt
        // consistent with the scene (releasing bodies of destroyed entities,
        // following scale edits) runs in every state, so Edit/Paused edits are
        // already applied whenever time next advances.
        //
        // With a player, the same advance is split into tick-sized substeps
        // (SimulationSubsteps, the rule Jolt already uses for its collision
        // steps): each substep moves the character first, then steps the rigid
        // bodies, so both always see the same simulated time and a Paused Step
        // (one tick) is exactly one of each. Without a player the bodies are
        // stepped exactly as before.
        void UpdateSimulation(const rag::core::FrameTime& frame_time)
        {
            const rag::f32 advance = simulation_.AdvanceFrame(frame_time.delta_seconds);
#if defined(RAG_ENABLE_PHYSICS)
            if (!physics_ready_)
            {
                return;
            }

            RemoveDestroyedPhysicsBodies();
            SynchronizeColliderScales();
            if (player_.has_value())
            {
                RefreshPlayerFromStart();
                // Look is per frame and only while Playing: the intent is zero
                // unless the mouse is captured, so Paused frames and Steps never
                // turn the view.
                if (simulation_.State() == rag::runtime::SimulationState::Playing)
                {
                    player_->controller.ApplyLook(player_intent_.look_x, player_intent_.look_y);
                }
                player_->controller.BeginFrame(player_intent_);
            }
            if (advance > 0.0f)
            {
                if (player_.has_value())
                {
                    StepPlayerAndPhysics(advance);
                }
                else
                {
                    StepPhysics(advance);
                }
            }
#else
            (void)advance;
#endif
        }

        // --- First-person player ------------------------------------------------
        //
        // Play spawns one capsule character at the scene's Player Start (the first
        // one in the Hierarchy if there are several). Without a Player Start, Play
        // keeps the free-fly camera exactly as before. The player is runtime state
        // only: never captured into the pre-play snapshot or saved, and destroyed
        // with the rest of the runtime scene when Stop restores the snapshot
        // (ClearSceneState), which brings the authored PlayerStartComponent back.
        //
        // Ownership: collision is PhysicsWorld's character (Jolt CharacterVirtual),
        // movement rules are rag::runtime::PlayerController, and this glue samples
        // input, picks the view and drives both through the existing lifecycle.
        //
        // Camera policy: while Playing the viewport shows the player's eyes (the
        // editor camera stays parked and ignores input). Paused hands the viewport
        // back to the editor camera, moved to the player's eyes, for inspection;
        // Resume returns to the player exactly where it was. Stop restores the
        // editor camera with the rest of the authored scene.

        // Spawn probe: the first clear spot at the start or up to 2 m above it.
        static constexpr rag::f32 PlayerSpawnProbeStep = 0.25f;
        static constexpr rag::u32 PlayerSpawnProbeCount = 8;

        // Fall resets are logged one by one up to this count, then every Nth.
        static constexpr rag::u32 PlayerFallResetLogLimit = 5;
        static constexpr rag::u32 PlayerFallResetLogInterval = 25;

        [[nodiscard]] bool HasPlayer() const
        {
#if defined(RAG_ENABLE_PHYSICS)
            return player_.has_value();
#else
            return false;
#endif
        }

        // The viewport shows the player's eyes (and a click takes control).
        [[nodiscard]] bool PlayerViewActive() const
        {
            return HasPlayer() && simulation_.State() == rag::runtime::SimulationState::Playing;
        }

        [[nodiscard]] bool CrosshairVisible() const
        {
            return PlayerViewActive() && player_input_captured_;
        }

        // The self-test exercises capture and release logically but never hides
        // or confines the real cursor of whoever is running it.
        [[nodiscard]] bool OsCursorCaptureAllowed() const
        {
#if defined(RAG_EDITOR)
            return !self_test_.has_value();
#else
            return true;
#endif
        }

        // The spawn point a Player Start describes: its transform position (the
        // feet). Sandbox entities are never parented, so local is world.
        [[nodiscard]] rag::math::Vec3 PlayerStartFeet(rag::scene::EntityId entity) const
        {
            const rag::scene::TransformComponent* transform = scene_.GetTransform(entity);
            return transform != nullptr ? transform->local_position : rag::math::Vec3{};
        }

        void CancelEditorGizmoDrag()
        {
#if defined(RAG_EDITOR)
            CancelGizmoDrag();
#endif
        }

        // Escape releases the player's mouse first and does nothing else; only an
        // Escape with nothing to release (and no Save As prompt) quits.
        void HandleEscape()
        {
#if defined(RAG_EDITOR)
            // While the Save As prompt is up, Escape cancels it instead of
            // quitting, so the natural way out of the prompt cannot discard an
            // Untitled scene that was never written anywhere.
            if (save_as_dialog_open_ && editor_mode_)
            {
                CancelSaveAsDialog();
                return;
            }
#endif
            if (player_input_captured_)
            {
                ReleasePlayerInput("Escape");
                return;
            }
            requested_close_ = true;
        }

        // Called every frame after the safe point. Releases the mouse when the
        // player view ended (Pause, Stop, a failed restore) or the platform lost
        // it (focus), takes it on an explicit click on the game view, and fills
        // player_intent_ for this frame -- all zero unless captured and Playing.
        void UpdatePlayerInput(
            const rag::core::InputState& input,
            bool accept_user_input,
            bool ui_wants_mouse)
        {
            player_intent_ = rag::runtime::PlayerIntent{};
            if (player_input_captured_ && !PlayerViewActive())
            {
                ReleasePlayerInput(HasPlayer() ? "simulation paused" : "the player was removed");
            }
            if (player_input_captured_ && OsCursorCaptureAllowed() && window_ != nullptr &&
                !window_->IsCursorCaptured())
            {
                ReleasePlayerInput("the window lost focus");
            }
            if (!PlayerViewActive())
            {
                return;
            }

#if defined(RAG_EDITOR)
            if (self_test_.has_value())
            {
                if (player_input_captured_ && self_test_player_intent_.has_value())
                {
                    player_intent_ = *self_test_player_intent_;
                }
                return;
            }
#endif
            if (!accept_user_input)
            {
                return;
            }

            if (!player_input_captured_)
            {
                // A click on the game view itself (not on an editor panel) takes
                // control. That click is consumed: picking and the gizmo are off
                // in the player view, and the capture frame feeds no movement.
                if (input.WasMouseButtonPressed(rag::core::MouseButton::Left) && !ui_wants_mouse && input.HasFocus())
                {
                    CapturePlayerInput();
                }
                return;
            }

            const auto axis = [&input](rag::core::KeyCode positive, rag::core::KeyCode negative)
            {
                return (input.IsKeyDown(positive) ? 1.0f : 0.0f) - (input.IsKeyDown(negative) ? 1.0f : 0.0f);
            };
            player_intent_.forward = axis(rag::core::KeyCode::W, rag::core::KeyCode::S);
            player_intent_.right = axis(rag::core::KeyCode::D, rag::core::KeyCode::A);
            // A press edge only: key repeat and a held key never re-trigger it.
            player_intent_.jump_pressed = input.WasKeyPressed(rag::core::KeyCode::Space);
            // Raw device motion accumulated this frame only (InputState resets it
            // every frame), so nothing stale from before the capture leaks in.
            player_intent_.look_x = static_cast<rag::f32>(input.RawMouseDeltaX());
            player_intent_.look_y = static_cast<rag::f32>(input.RawMouseDeltaY());
        }

        void CapturePlayerInput()
        {
            if (player_input_captured_ || !PlayerViewActive())
            {
                return;
            }
            if (OsCursorCaptureAllowed() && window_ != nullptr)
            {
                window_->SetCursorCaptured(true);
                if (!window_->IsCursorCaptured())
                {
                    RAG_LOG_WARNING("Player: the mouse could not be captured (the window has no focus).");
                    return;
                }
            }
            player_input_captured_ = true;
            SetUiInputBlocked(true);
#if defined(RAG_ENABLE_PHYSICS)
            player_->controller.ClearIntent();
#endif
            RAG_LOG_INFO(
                "Player: mouse captured. WASD move, mouse looks, Space jumps; Esc releases the mouse "
                "(F1, F5, F6 and F7 keep working).");
        }

        // Safe to call in any state; logs only when something was released.
        void ReleasePlayerInput(const char* reason)
        {
            if (!player_input_captured_)
            {
                return;
            }
            player_input_captured_ = false;
            if (window_ != nullptr)
            {
                window_->SetCursorCaptured(false);
            }
            SetUiInputBlocked(false);
            player_intent_ = rag::runtime::PlayerIntent{};
#if defined(RAG_ENABLE_PHYSICS)
            if (player_.has_value())
            {
                player_->controller.ClearIntent();
            }
#endif
            RAG_LOG_INFO("Player: mouse released (", reason, "). Click the game view to take control again.");
        }

        // While the player has the mouse, editor panels must not react to the
        // (hidden) cursor or to keys: a click must never press a button under it
        // and WASD/Space must never drive a focused widget.
        void SetUiInputBlocked(bool blocked)
        {
#if defined(RAG_ENABLE_IMGUI)
            if (ImGui::GetCurrentContext() == nullptr)
            {
                return;
            }
            ImGuiIO& io = ImGui::GetIO();
            constexpr ImGuiConfigFlags BlockedInput = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
            if (blocked)
            {
                io.ConfigFlags |= BlockedInput;
            }
            else
            {
                io.ConfigFlags &= ~BlockedInput;
            }
#else
            (void)blocked;
#endif
        }

#if defined(RAG_ENABLE_IMGUI)
        // Crosshair while the player has control; a "click to play" hint while
        // the player view waits for that click. Drawn on top of everything.
        void BuildPlayerOverlay()
        {
            if (!PlayerViewActive())
            {
                return;
            }

            ImDrawList* draw_list = ImGui::GetForegroundDrawList();
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            if (CrosshairVisible())
            {
                constexpr float Gap = 4.0f;
                constexpr float Arm = 8.0f;
                const auto draw_cross = [&](ImU32 color, float thickness)
                {
                    draw_list->AddLine({center.x - Gap - Arm, center.y}, {center.x - Gap, center.y}, color, thickness);
                    draw_list->AddLine({center.x + Gap, center.y}, {center.x + Gap + Arm, center.y}, color, thickness);
                    draw_list->AddLine({center.x, center.y - Gap - Arm}, {center.x, center.y - Gap}, color, thickness);
                    draw_list->AddLine({center.x, center.y + Gap}, {center.x, center.y + Gap + Arm}, color, thickness);
                };
                draw_cross(IM_COL32(0, 0, 0, 170), 4.0f); // outline, readable on bright sky
                draw_cross(IM_COL32(255, 255, 255, 235), 2.0f);
                return;
            }

            const char* const hint = "Click the view to control the player  -  Esc releases the mouse";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            const ImVec2 padding{10.0f, 6.0f};
            const ImVec2 min{center.x - (size.x * 0.5f) - padding.x, center.y - (size.y * 0.5f) - padding.y};
            const ImVec2 max{center.x + (size.x * 0.5f) + padding.x, center.y + (size.y * 0.5f) + padding.y};
            draw_list->AddRectFilled(min, max, IM_COL32(0, 0, 0, 160), 4.0f);
            draw_list->AddText({min.x + padding.x, min.y + padding.y}, IM_COL32(255, 255, 255, 240), hint);
        }
#endif

#if defined(RAG_ENABLE_PHYSICS)
        // The live player while a run has one.
        struct PlayerRuntime
        {
            rag::physics::CharacterHandle character{};
            rag::runtime::PlayerController controller;

            // The Player Start it spawned from. Its config and position are read
            // again every frame (live tuning during a run, restored by Stop like
            // any runtime edit); if it is deleted mid-run the last values stay.
            rag::scene::EntityId start_entity{};
            std::string start_name;
            rag::scene::PlayerStartComponent config{};
            rag::math::Vec3 start_position{};

            // The capsule is built once, at Play.
            rag::f32 capsule_radius = 0.3f;
            rag::f32 capsule_height = 1.8f;

            rag::u32 fall_resets = 0;
            bool fall_reset_disabled_reported = false;
        };

        [[nodiscard]] static rag::runtime::PlayerGround ToPlayerGround(rag::physics::CharacterGround ground)
        {
            switch (ground)
            {
                case rag::physics::CharacterGround::OnGround: return rag::runtime::PlayerGround::OnGround;
                case rag::physics::CharacterGround::OnSteepGround: return rag::runtime::PlayerGround::OnSteepGround;
                case rag::physics::CharacterGround::NotSupported:
                case rag::physics::CharacterGround::InAir: return rag::runtime::PlayerGround::Unsupported;
            }
            return rag::runtime::PlayerGround::Unsupported;
        }

        [[nodiscard]] static const char* PlayerGroundName(rag::runtime::PlayerGround ground)
        {
            switch (ground)
            {
                case rag::runtime::PlayerGround::OnGround: return "on ground";
                case rag::runtime::PlayerGround::OnSteepGround: return "on steep ground (sliding)";
                case rag::runtime::PlayerGround::Unsupported: return "in the air";
            }
            return "unknown";
        }

        [[nodiscard]] static rag::runtime::PlayerMovementSettings MovementSettingsFor(
            const rag::scene::PlayerStartComponent& config,
            rag::f32 capsule_height)
        {
            rag::runtime::PlayerMovementSettings settings;
            settings.move_speed = config.move_speed;
            settings.mouse_sensitivity_degrees = config.mouse_sensitivity_degrees;
            settings.jump_height = config.jump_height;
            // The eye may be tuned live, but never above the capsule built at Play.
            settings.eye_height = std::min(config.eye_height, capsule_height);
            settings.fall_reset_height = config.fall_reset_height;
            return settings;
        }

        // The first spot, from the start upward in PlayerSpawnProbeStep steps,
        // where the capsule overlaps no static or dynamic collider.
        [[nodiscard]] std::optional<rag::math::Vec3> FindClearPlayerSpawn(
            rag::f32 radius,
            rag::f32 height,
            const rag::math::Vec3& start) const
        {
            for (rag::u32 probe = 0; probe <= PlayerSpawnProbeCount; ++probe)
            {
                const rag::math::Vec3 candidate =
                    start + rag::math::Vec3{0.0f, PlayerSpawnProbeStep * static_cast<rag::f32>(probe), 0.0f};
                if (physics_.IsCharacterSpaceFree(radius, height, candidate))
                {
                    return candidate;
                }
            }
            return std::nullopt;
        }

        // Play's spawn, right after the bodies were put at their authored poses.
        // Never fails Play: when no player can be made the run simply keeps the
        // free-fly camera and the reason is shown (player_status_).
        void SpawnPlayerForPlay()
        {
            DestroyPlayer();
            player_status_.clear();

            const rag::scene::EntityId start = scene_.FirstPlayerStart();
            if (!start.IsValid())
            {
                RAG_LOG_INFO("Player: the scene has no Player Start; Play keeps the free-fly camera.");
                return;
            }
            const std::string name = NameForSerialization(start);
            if (!physics_ready_)
            {
                player_status_ = "Physics is unavailable, so no player was spawned.";
                RAG_LOG_WARNING("Player: ", player_status_);
                return;
            }
            if (const std::size_t starts = scene_.PlayerStartCount(); starts > 1)
            {
                RAG_LOG_WARNING(
                    "Player: the scene has ", starts, " Player Starts; using '", name,
                    "', the first in the Hierarchy. The others are ignored.");
            }

            rag::scene::PlayerStartComponent config = *scene_.GetPlayerStart(start);
            if (const std::string corrections = rag::scene::SanitizePlayerStart(config); !corrections.empty())
            {
                RAG_LOG_WARNING("Player: Player Start '", name, "' had out-of-range values, used corrected: ",
                    corrections, ".");
            }

            const rag::math::Vec3 authored = PlayerStartFeet(start);
            const std::optional<rag::math::Vec3> spawn =
                FindClearPlayerSpawn(config.capsule_radius, config.capsule_height, authored);
            if (!spawn.has_value())
            {
                player_status_ = "Player Start '" + name + "' is inside geometry with no clear space up to " +
                    std::to_string(static_cast<int>(PlayerSpawnProbeStep * PlayerSpawnProbeCount)) +
                    " m above it, so no player was spawned; this run uses the free-fly camera. "
                    "Move the start into open space.";
                RAG_LOG_WARNING("Player: ", player_status_);
                return;
            }

            rag::physics::CharacterDesc desc;
            desc.feet_position = *spawn;
            desc.radius = config.capsule_radius;
            desc.height = config.capsule_height;
            const rag::physics::CharacterHandle character = physics_.CreateCharacter(desc);
            if (!character.IsValid())
            {
                player_status_ = "The player character could not be created; this run uses the free-fly camera.";
                RAG_LOG_ERROR("Player: ", player_status_);
                return;
            }

            PlayerRuntime& player = player_.emplace();
            player.character = character;
            player.start_entity = start;
            player.start_name = name;
            player.config = config;
            player.start_position = authored;
            player.capsule_radius = config.capsule_radius;
            player.capsule_height = config.capsule_height;
            player.controller.SetSettings(MovementSettingsFor(config, config.capsule_height));
            player.controller.SetStartPosition(authored);
            player.controller.Reset(
                *spawn,
                config.yaw_radians,
                ToPlayerGround(physics_.GetCharacterState(character).ground));

            if (spawn->y > authored.y)
            {
                RAG_LOG_WARNING(
                    "Player: Player Start '", name, "' overlaps geometry; spawned ", spawn->y - authored.y,
                    " m higher, at the first clear spot above it.");
            }
            RAG_LOG_INFO(
                "Player: spawned from '", name, "' at (", spawn->x, ", ", spawn->y, ", ", spawn->z,
                "). Click the view to control it; Esc releases the mouse.");
            ReportFallResetState();
        }

        void DestroyPlayer()
        {
            ReleasePlayerInput("the player was removed");
            if (player_.has_value())
            {
                physics_.DestroyCharacter(player_->character);
                player_.reset();
            }
            player_intent_ = rag::runtime::PlayerIntent{};
        }

        // Live tuning: movement settings and the start position follow the
        // authored Player Start during a run (Stop reverts those edits like any
        // other runtime change). The capsule keeps the size it was built with.
        void RefreshPlayerFromStart()
        {
            PlayerRuntime& player = *player_;
            if (scene_.IsAlive(player.start_entity))
            {
                if (const rag::scene::PlayerStartComponent* start = scene_.GetPlayerStart(player.start_entity))
                {
                    player.config = *start;
                    (void)rag::scene::SanitizePlayerStart(player.config);
                    player.start_position = PlayerStartFeet(player.start_entity);
                }
            }
            player.controller.SetSettings(MovementSettingsFor(player.config, player.capsule_height));
            player.controller.SetStartPosition(player.start_position);
            ReportFallResetState();
        }

        // A start at or below its own fall-reset height would reset forever, so
        // the controller disables the reset; say so once (not every frame).
        void ReportFallResetState()
        {
            PlayerRuntime& player = *player_;
            const bool disabled = player.controller.FallResetDisabled();
            if (disabled && !player.fall_reset_disabled_reported)
            {
                RAG_LOG_WARNING(
                    "Player: Player Start '", player.start_name, "' (Y=", player.start_position.y,
                    ") is not above its fall-reset height (Y=", player.config.fall_reset_height,
                    "); fall reset is off until that changes.");
            }
            player.fall_reset_disabled_reported = disabled;
        }

        void StepPlayerAndPhysics(rag::f32 advance)
        {
            PlayerRuntime& player = *player_;
            const rag::u32 substeps = rag::runtime::SimulationSubsteps(advance, SimulationTickSeconds);
            const rag::f32 substep_seconds = advance / static_cast<rag::f32>(substeps);
            for (rag::u32 substep = 0; substep < substeps; ++substep)
            {
                player.controller.Step(
                    substep_seconds,
                    physics_gravity_y_,
                    [this, &player](const rag::math::Vec3& velocity, rag::f32 seconds)
                    {
                        const rag::physics::CharacterState state =
                            physics_.MoveCharacter(player.character, velocity, seconds, physics_gravity_y_);
                        return rag::runtime::PlayerMoveResult{state.feet_position, ToPlayerGround(state.ground)};
                    });
                physics_.Step(substep_seconds);
                if (player.controller.NeedsFallReset())
                {
                    ResetPlayerToStart();
                }
            }
            WriteBackDynamicBodies();
        }

        // Fall reset: back to the (live) start, facing its yaw, all motion
        // cleared. Only the player moves; the rest of the scene is untouched. An
        // obstructed start is handled like the spawn (first clear spot up to 2 m
        // above); if there is none the player is put at the start anyway and the
        // character's penetration recovery pushes it out, which is logged.
        void ResetPlayerToStart()
        {
            PlayerRuntime& player = *player_;
            ++player.fall_resets;
            const rag::f32 fell_below = player.config.fall_reset_height;
            const rag::math::Vec3 start = player.start_position;
            const std::optional<rag::math::Vec3> clear =
                FindClearPlayerSpawn(player.capsule_radius, player.capsule_height, start);
            const rag::math::Vec3 target = clear.value_or(start);

            physics_.SetCharacterPosition(player.character, target);
            const rag::physics::CharacterState state = physics_.GetCharacterState(player.character);
            player.controller.Reset(state.feet_position, player.config.yaw_radians, ToPlayerGround(state.ground));

            const bool log_this = player.fall_resets <= PlayerFallResetLogLimit ||
                player.fall_resets % PlayerFallResetLogInterval == 0;
            if (!log_this)
            {
                return;
            }
            const char* const throttled = player.fall_resets == PlayerFallResetLogLimit
                ? " Further resets are logged every 25th."
                : "";
            if (!clear.has_value())
            {
                RAG_LOG_WARNING(
                    "Player: fell below Y=", fell_below, " (reset #", player.fall_resets, "); the start of '",
                    player.start_name, "' is obstructed, so the player was put there anyway and will be pushed out.",
                    throttled);
            }
            else
            {
                RAG_LOG_INFO(
                    "Player: fell below Y=", fell_below, " (reset #", player.fall_resets, "); returned to '",
                    player.start_name, "'", target.y > start.y ? " (lifted above an obstruction)." : ".", throttled);
            }
        }

        // The view the player sees: its eyes, with the editor camera's lens.
        void ApplyPlayerView(rag::renderer::RenderCamera& out_camera) const
        {
            if (!player_.has_value())
            {
                return;
            }
            rag::scene::CameraComponent lens{};
            if (const rag::scene::CameraComponent* camera = scene_.GetCamera(camera_entity_))
            {
                lens = *camera;
            }
            else if (window_ != nullptr && window_->Width() > 0 && window_->Height() > 0)
            {
                lens.aspect_ratio = static_cast<rag::f32>(window_->Width()) / static_cast<rag::f32>(window_->Height());
            }

            const rag::runtime::PlayerController& controller = player_->controller;
            const rag::math::Vec3 eye = controller.EyePosition();
            // Same Euler convention as the editor camera: pitch, then yaw.
            const rag::math::Mat4 world = rag::math::ComposeTransform(
                eye,
                rag::math::Vec3{controller.Pitch(), controller.Yaw(), 0.0f},
                rag::math::Vec3{1.0f, 1.0f, 1.0f});
            out_camera.valid = true;
            out_camera.view = rag::math::InverseRigidTransform(world);
            out_camera.position = eye;
            out_camera.near_plane = lens.near_plane;
            out_camera.far_plane = lens.far_plane;
            out_camera.projection = rag::math::PerspectiveRH_ZO(
                lens.vertical_fov_radians, lens.aspect_ratio, lens.near_plane, lens.far_plane);
        }

        // Pause: the editor camera takes over from the player's eyes, so the
        // view does not jump. Flying it afterwards never moves the player.
        void MoveEditorCameraToPlayerView()
        {
            rag::scene::TransformComponent* transform = scene_.GetTransform(camera_entity_);
            if (!player_.has_value() || transform == nullptr)
            {
                return;
            }
            const rag::runtime::PlayerController& controller = player_->controller;
            camera_yaw_radians_ = controller.Yaw();
            camera_pitch_radians_ = std::clamp(controller.Pitch(), -MaximumPitchRadians, MaximumPitchRadians);
            transform->local_position = controller.EyePosition();
            transform->local_rotation_radians = rag::math::Vec3{camera_pitch_radians_, camera_yaw_radians_, 0.0f};
            transform->use_quaternion_rotation = false;
            scene_.MarkTransformDirty(camera_entity_);
        }
#else
        // Without the physics module there is no character to spawn: a Player
        // Start is still authored and saved, and Play keeps the free camera.
        void SpawnPlayerForPlay() {}
        void ApplyPlayerView(rag::renderer::RenderCamera&) const {}
        void MoveEditorCameraToPlayerView() {}
#endif

        // --- Simulation lifecycle (Edit / Playing / Paused) ---------------------

        void RequestSimulationCommand(SimulationCommand command)
        {
            pending_simulation_command_ = command;
        }

        // Called once per frame at the safe point at the top of OnUpdate.
        void ProcessPendingSimulationCommand()
        {
            if (!pending_simulation_command_.has_value())
            {
                return;
            }

            const SimulationCommand command = *pending_simulation_command_;
            pending_simulation_command_.reset();
            (void)ApplySimulationCommand(command);
        }

        // Performs one lifecycle transition immediately. Only call at the safe
        // point (or from the self-test, which runs there); UI code must use
        // RequestSimulationCommand instead.
        rag::runtime::TransitionResult ApplySimulationCommand(SimulationCommand command)
        {
            using rag::runtime::TransitionResult;
            const char* const previous_state = simulation_.StateName();
            std::string error;
            TransitionResult result = TransitionResult::Rejected;
            switch (command)
            {
                case SimulationCommand::Play:
                    result = simulation_.Play(
                        [this](PlaySnapshot& snapshot, std::string& capture_error)
                        {
                            return CapturePlaySnapshot(snapshot, capture_error);
                        },
                        error);
                    if (result == TransitionResult::Applied)
                    {
                        PrepareBodiesForPlay();
                        // After the bodies sit at their authored poses, so the
                        // spawn probe sees exactly the arrangement Play starts from.
                        SpawnPlayerForPlay();
                        CancelEditorGizmoDrag();
                    }
                    break;
                case SimulationCommand::Pause:
                    result = simulation_.Pause(error);
                    if (result == TransitionResult::Applied)
                    {
                        // Paused is for inspection: the mouse goes back to the
                        // editor, and the editor camera takes over from where the
                        // player was looking (its later navigation never moves the
                        // player).
                        ReleasePlayerInput("simulation paused");
                        MoveEditorCameraToPlayerView();
                    }
                    break;
                case SimulationCommand::Resume:
                    result = simulation_.Resume(error);
                    if (result == TransitionResult::Applied && HasPlayer())
                    {
                        CancelEditorGizmoDrag();
                        RAG_LOG_INFO("Player: back to the player view; click the view to take control again.");
                    }
                    break;
                case SimulationCommand::Stop:
                    // The restore destroys the player with the rest of the runtime
                    // scene; hand the cursor back first.
                    ReleasePlayerInput("simulation stopped");
                    result = simulation_.Stop(
                        [this](const PlaySnapshot& snapshot, std::string& restore_error)
                        {
                            return RestorePlaySnapshot(snapshot, restore_error);
                        },
                        error);
                    break;
                case SimulationCommand::Step:
                    result = simulation_.RequestStep(error);
                    break;
            }

            const char* const name = SimulationCommandName(command);
            if (result == TransitionResult::Applied)
            {
                simulation_error_.clear();
                if (command == SimulationCommand::Step)
                {
                    RAG_LOG_INFO(
                        "Simulation: Step (one ", SimulationTickSeconds * 1000.0f, " ms tick).");
                }
                else
                {
                    RAG_LOG_INFO(
                        "Simulation: ", name, " (", previous_state, " -> ", simulation_.StateName(), ").");
                }
            }
            else if (result == TransitionResult::Failed)
            {
                simulation_error_ = std::string(name) + " failed: " + error;
                if (command == SimulationCommand::Stop)
                {
                    simulation_error_ +=
                        " The pre-play snapshot is kept and the run is paused; press Stop to retry.";
                }
                else
                {
                    simulation_error_ += " Staying in Edit; the scene is unchanged.";
                }
                RAG_LOG_ERROR("Simulation: ", simulation_error_);
            }
            else
            {
                RAG_LOG_WARNING("Simulation: ", error);
            }
            return result;
        }

        // F5 starts a test run or stops it (restoring the authored scene), F6
        // pauses or resumes, F7 advances one tick while paused. Only legal
        // transitions are requested, so a stray key press is simply ignored.
        void HandleSimulationShortcuts(const rag::core::InputState& input)
        {
            if (input.WasKeyPressed(rag::core::KeyCode::F5))
            {
                RequestSimulationCommand(
                    simulation_.CanPlay() ? SimulationCommand::Play : SimulationCommand::Stop);
            }
            if (input.WasKeyPressed(rag::core::KeyCode::F6))
            {
                if (simulation_.CanPause())
                {
                    RequestSimulationCommand(SimulationCommand::Pause);
                }
                else if (simulation_.CanResume())
                {
                    RequestSimulationCommand(SimulationCommand::Resume);
                }
            }
            if (input.WasKeyPressed(rag::core::KeyCode::F7) && simulation_.CanStep())
            {
                RequestSimulationCommand(SimulationCommand::Step);
            }
        }

        // Captures the authored scene for Stop. It must not modify the scene:
        // Play only proceeds when this returns true, and a refusal leaves Edit
        // exactly as it was. Capture is refused when it would not be lossless.
        bool CapturePlaySnapshot(PlaySnapshot& out_snapshot, std::string& out_error)
        {
#if defined(RAG_ENABLE_PHYSICS)
            // Bookkeeping only (records of already-destroyed entities); the scene
            // itself is untouched.
            if (physics_ready_)
            {
                RemoveDestroyedPhysicsBodies();
            }
#endif
            out_snapshot.scene = CaptureScene(&out_snapshot.entities);
            if (out_snapshot.scene.entities.size() != scene_.EntityCount())
            {
                out_error = "captured " + std::to_string(out_snapshot.scene.entities.size()) +
                    " of " + std::to_string(scene_.EntityCount()) + " entities.";
                return false;
            }

            // Mesh handles are the one lossy mapping: a handle without a stable
            // identifier would come back as the cube, so refuse instead.
            for (std::size_t index = 0; index < out_snapshot.entities.size(); ++index)
            {
                const rag::scene::RenderableComponent* renderable =
                    scene_.GetRenderable(out_snapshot.entities[index]);
                if (renderable == nullptr)
                {
                    continue;
                }
                const rag::renderer::MeshHandle round_trip =
                    MeshHandleFromIdentifier(MeshIdentifier(renderable->mesh));
                if (round_trip.index != renderable->mesh.index ||
                    round_trip.generation != renderable->mesh.generation)
                {
                    out_error = "entity '" + out_snapshot.scene.entities[index].name +
                        "' uses mesh handle " + std::to_string(renderable->mesh.index) +
                        ", which has no stable identifier to restore it from.";
                    return false;
                }
            }

            for (const rag::scene::SerializedEntity& entity : out_snapshot.scene.entities)
            {
                if (entity.physics.has_value())
                {
                    ++out_snapshot.physics_record_count;
                }
            }
#if defined(RAG_ENABLE_PHYSICS)
            if (out_snapshot.physics_record_count != physics_bodies_.size())
            {
                out_error = "physics records are out of sync with the scene (" +
                    std::to_string(physics_bodies_.size()) + " records, " +
                    std::to_string(out_snapshot.physics_record_count) + " captured).";
                return false;
            }
            out_snapshot.gravity_y = physics_gravity_y_;
#endif
            out_snapshot.light_euler = light_euler_;
            out_snapshot.light_color = light_color_;
            out_snapshot.light_intensity = light_intensity_;
#if defined(RAG_EDITOR)
            out_snapshot.spawn_counter = spawn_counter_;
#endif
            return ValidatePlaySnapshot(out_snapshot, out_error);
        }

        // Non-destructive integrity check, run when the snapshot is taken and
        // again right before Stop replaces the live scene, so a bad snapshot is
        // refused while the current scene still exists.
        [[nodiscard]] bool ValidatePlaySnapshot(
            const PlaySnapshot& snapshot,
            std::string& out_error) const
        {
            if (snapshot.entities.size() != snapshot.scene.entities.size())
            {
                out_error = "snapshot entity table does not match its scene description.";
                return false;
            }

            std::size_t physics_records = 0;
            for (const rag::scene::SerializedEntity& entity : snapshot.scene.entities)
            {
                if (entity.renderable.has_value() && !IsKnownMeshIdentifier(entity.renderable->mesh))
                {
                    out_error = "entity '" + entity.name + "' has unknown mesh '" +
                        entity.renderable->mesh + "'.";
                    return false;
                }
                if (entity.physics.has_value())
                {
                    ++physics_records;
#if defined(RAG_ENABLE_PHYSICS)
                    if (!IsKnownBodyMode(entity.physics->mode) ||
                        !IsKnownColliderShape(entity.physics->shape))
                    {
                        out_error = "entity '" + entity.name + "' has invalid physics settings.";
                        return false;
                    }
#endif
                }
            }
            if (physics_records != snapshot.physics_record_count)
            {
                out_error = "snapshot physics record count does not match its scene description.";
                return false;
            }
            return true;
        }

        // Stop's restore. Validates first (nothing is destroyed if that fails),
        // rebuilds the authored scene through the same ApplyScene path Load uses
        // -- mesh handles re-resolve to the already-loaded GPU meshes, so nothing
        // is re-uploaded -- then verifies the rebuild before reporting success.
        // On false the controller keeps the snapshot so Stop can be retried.
        bool RestorePlaySnapshot(const PlaySnapshot& snapshot, std::string& out_error)
        {
            if (!ValidatePlaySnapshot(snapshot, out_error))
            {
                out_error = "the snapshot failed validation, so the runtime scene was left untouched (" +
                    out_error + ")";
                return false;
            }

            // Entity ids do not survive the rebuild, so remember the selection by
            // its position in the snapshot; a selected runtime spawn has none and
            // the selection simply clears.
            std::optional<std::size_t> reselect_index;
#if defined(RAG_EDITOR)
            const rag::scene::EntityId selected = editor_.Selected();
            if (selected.IsValid() && scene_.IsAlive(selected))
            {
                for (std::size_t index = 0; index < snapshot.entities.size(); ++index)
                {
                    if (snapshot.entities[index] == selected)
                    {
                        reselect_index = index;
                        break;
                    }
                }
            }
#endif

            std::vector<rag::scene::EntityId> rebuilt;
            ApplyScene(snapshot.scene, &rebuilt);
            light_euler_ = snapshot.light_euler;
            light_color_ = snapshot.light_color;
            light_intensity_ = snapshot.light_intensity;
#if defined(RAG_ENABLE_PHYSICS)
            physics_gravity_y_ = snapshot.gravity_y;
            ApplyGravity();
#endif
#if defined(RAG_EDITOR)
            spawn_counter_ = snapshot.spawn_counter;
#endif

            if (!VerifyRestoredScene(snapshot, rebuilt, out_error))
            {
                return false;
            }

#if defined(RAG_EDITOR)
            if (reselect_index.has_value())
            {
                editor_.Select(rebuilt[*reselect_index]);
            }
#else
            (void)reselect_index;
#endif
            return true;
        }

        // Confirms a rebuild produced the whole snapshot: every entity, every
        // physics record, a live body for each Static/Dynamic one, and no extra
        // (ghost) body left inside Jolt.
        [[nodiscard]] bool VerifyRestoredScene(
            const PlaySnapshot& snapshot,
            const std::vector<rag::scene::EntityId>& rebuilt,
            std::string& out_error) const
        {
            const std::size_t expected = snapshot.scene.entities.size();
            if (rebuilt.size() != expected || scene_.EntityCount() != expected)
            {
                out_error = "rebuilt " + std::to_string(scene_.EntityCount()) + " of " +
                    std::to_string(expected) + " entities.";
                return false;
            }

#if defined(RAG_ENABLE_PHYSICS)
            if (physics_ready_)
            {
                if (physics_bodies_.size() != snapshot.physics_record_count)
                {
                    out_error = "rebuilt " + std::to_string(physics_bodies_.size()) + " of " +
                        std::to_string(snapshot.physics_record_count) + " physics records.";
                    return false;
                }

                rag::u32 live_bodies = 0;
                for (const PhysicsBody& physics_body : physics_bodies_)
                {
                    if (physics_body.desc.mode == rag::physics::BodyMode::None)
                    {
                        continue;
                    }
                    if (!physics_body.body.IsValid())
                    {
                        out_error = "a physics body could not be recreated.";
                        return false;
                    }
                    ++live_bodies;
                }
                if (physics_.BodyCount() != live_bodies)
                {
                    out_error = "the physics world holds " + std::to_string(physics_.BodyCount()) +
                        " bodies, expected " + std::to_string(live_bodies) + ".";
                    return false;
                }
                // The player is runtime-only: Edit never has one.
                if (player_.has_value() || physics_.CharacterCount() != 0)
                {
                    out_error = "a player character survived the restore (" +
                        std::to_string(physics_.CharacterCount()) + " in the physics world).";
                    return false;
                }
            }
#endif
            return true;
        }

        // Scene documents are only touched in Edit: during a test run the live
        // scene holds runtime state that must never be written over a file, and
        // replacing it would orphan the pre-play snapshot. Returns true (and sets
        // out_error) when the operation must be refused.
        [[nodiscard]] bool RefuseDocumentOperation(
            const char* operation,
            std::string& out_error) const
        {
            if (simulation_.CanModifyDocument())
            {
                return false;
            }
            out_error = std::string(operation) + " is unavailable while the simulation is " +
                simulation_.StateName() + ". Stop simulation first.";
            RAG_LOG_WARNING("Scene: ", out_error);
            return true;
        }

        // The smoke test renders one frame per lifecycle state (Edit -> Playing ->
        // Edit) and fails if a transition does not happen, so the canonical
        // -SmokeTest run exercises Play/Stop as well as startup and rendering.
        // Requests made here apply at the next frame's safe point.
        void AdvanceSmokeTest()
        {
#if defined(RAG_EDITOR)
            using rag::runtime::SimulationState;
            ++smoke_test_frame_;
            if (smoke_test_frame_ == 1)
            {
                smoke_test_passed_ = simulation_.State() == SimulationState::Edit &&
                    simulation_.SimulatedSeconds() == 0.0;
                RequestSimulationCommand(SimulationCommand::Play);
                return;
            }
            if (smoke_test_frame_ == 2)
            {
                smoke_test_passed_ = smoke_test_passed_ &&
                    simulation_.State() == SimulationState::Playing &&
                    simulation_.SimulatedSeconds() > 0.0;
                RequestSimulationCommand(SimulationCommand::Stop);
                return;
            }
            smoke_test_passed_ = smoke_test_passed_ && simulation_.State() == SimulationState::Edit;
            if (smoke_test_passed_)
            {
                RAG_LOG_INFO("Smoke test: Edit -> Playing -> Edit lifecycle rendered and restored.");
            }
            else
            {
                RAG_LOG_ERROR("Smoke test: the Edit -> Playing -> Edit lifecycle did not complete.");
            }
#else
            smoke_test_passed_ = true;
#endif
            requested_close_ = true;
        }

        // Pushes the editor-controlled light state onto the scene each frame. It
        // is independent of physics and the renderer, so it runs unconditionally.
        void SyncLightFromUi()
        {
            if (rag::scene::LightComponent* light = scene_.GetLight(light_entity_))
            {
                const rag::math::Mat4 light_rotation =
                    rag::math::RotationEulerXYZ(light_euler_);
                light->local_direction = rag::math::TransformDirection(
                    light_rotation,
                    BaseDirectionalLightDirection);
                light->color = light_color_;
                light->intensity = light_intensity_;
            }
        }

        // --- Default scene building blocks (shared by BuildScene / New Scene) ---

        // Name of the scene file the sandbox loads on startup (see
        // FindDefaultSceneFile for where it looks). Nothing ever writes it
        // implicitly: Save only targets the open document's own file.
        static constexpr const char* DefaultSceneFilename = "default.ragscene";

        // Creates the free-fly editor camera at its default pose and makes it the
        // active camera, keeping the yaw/pitch members in sync. Shared by the
        // built-in scene and "New Scene" so both agree on the camera setup.
        void CreateDefaultCameraEntity()
        {
            camera_entity_ = scene_.CreateEntity();
            NameEntity(camera_entity_, "Camera");
            rag::scene::TransformComponent& transform = scene_.AddTransform(camera_entity_);
            transform.local_position = rag::math::Vec3{0.0f, 3.0f, 9.0f};
            camera_yaw_radians_ = 0.0f;
            camera_pitch_radians_ = -0.3f;
            transform.local_rotation_radians =
                rag::math::Vec3{camera_pitch_radians_, camera_yaw_radians_, 0.0f};

            rag::scene::CameraComponent& camera = scene_.AddCamera(camera_entity_);
            camera.vertical_fov_radians = rag::math::Pi / 3.0f;
            camera.near_plane = 0.1f;
            camera.far_plane = 100.0f;
            if (window_ != nullptr && window_->Width() > 0 && window_->Height() > 0)
            {
                camera.aspect_ratio =
                    static_cast<rag::f32>(window_->Width()) /
                    static_cast<rag::f32>(window_->Height());
            }
            scene_.SetActiveCamera(camera_entity_);
        }

        // Creates the editable directional "sun" from the current light members and
        // records it as light_entity_ (the one SyncLightFromUi drives each frame).
        void CreateDefaultLightEntity()
        {
            light_entity_ = scene_.CreateEntity();
            NameEntity(light_entity_, "Sun");
            scene_.AddTransform(light_entity_);

            rag::scene::LightComponent& light = scene_.AddLight(light_entity_);
            light.type = rag::renderer::RenderLightType::Directional;
            light.local_direction = rag::math::Normalize(BaseDirectionalLightDirection);
            light.color = light_color_;
            light.intensity = light_intensity_;
        }

        // --- Scene serialization: save / load / new ----------------------------

        // The canonical tooling starts in the repository root, making its scenes/
        // directory the editable source of truth. A direct launch from elsewhere
        // still gets a writable scenes/ directory relative to that working folder.
        // Executable-relative scenes remain a legacy load fallback below.
        [[nodiscard]] std::filesystem::path ScenesDirectory() const
        {
            std::error_code error;
            const std::filesystem::path working_directory =
                std::filesystem::current_path(error);
            return error
                ? rag::platform::ExecutableDirectory() / "scenes"
                : working_directory / "scenes";
        }

        [[nodiscard]] std::filesystem::path DefaultScenePath() const
        {
            return ScenesDirectory() / DefaultSceneFilename;
        }

        // Where an existing scene file is looked for, in order: the editable
        // working-directory scenes/ first, then the executable's scenes/ as a
        // compatibility fallback for legacy build folders.
        [[nodiscard]] static std::array<std::filesystem::path, 2> SceneFileCandidates(
            const std::string& filename)
        {
            std::error_code error;
            return {
                std::filesystem::current_path(error) / "scenes" / filename,
                rag::platform::ExecutableDirectory() / "scenes" / filename,
            };
        }

        [[nodiscard]] std::optional<std::filesystem::path> ResolveExistingScenePath(
            const std::string& filename) const
        {
            std::error_code error;
            for (const std::filesystem::path& candidate : SceneFileCandidates(filename))
            {
                if (std::filesystem::is_regular_file(candidate, error))
                {
                    return candidate;
                }
                error.clear();
            }
            return std::nullopt;
        }

        // The startup lookup for the default scene, logged in full: every
        // candidate path in order and why it was skipped, so a launch that falls
        // back to the built-in scene says exactly what it looked for. A direct
        // launch from the executable folder makes both candidates the same path.
        [[nodiscard]] std::optional<std::filesystem::path> FindDefaultSceneFile(
            std::string& out_reason) const
        {
            const std::array<std::filesystem::path, 2> candidates = SceneFileCandidates(DefaultSceneFilename);
            constexpr std::array<const char*, 2> Origins = {"working directory", "executable directory"};
            std::string checked;
            for (std::size_t index = 0; index < candidates.size(); ++index)
            {
                const std::filesystem::path candidate = candidates[index].lexically_normal();
                if (index > 0 && candidate == candidates[0].lexically_normal())
                {
                    RAG_LOG_INFO(
                        "Default scene: the ", Origins[index], " candidate is the same path as the ",
                        Origins[0], " candidate; not checked twice.");
                    continue;
                }

                std::error_code error;
                const std::filesystem::file_status status = std::filesystem::status(candidate, error);
                if (std::filesystem::is_regular_file(status))
                {
                    RAG_LOG_INFO("Default scene: found '", candidate.string(), "' (", Origins[index], ").");
                    return candidate;
                }

                std::string why = "not found";
                if (status.type() == std::filesystem::file_type::none)
                {
                    why = "could not be checked: " + error.message();
                }
                else if (std::filesystem::exists(status))
                {
                    why = "exists but is not a regular file";
                }
                RAG_LOG_INFO("Default scene: '", candidate.string(), "' (", Origins[index], "): ", why, ".");
                checked += (checked.empty() ? "" : "; ") + candidate.string() + ": " + why;
            }
            out_reason = "checked " + checked;
            return std::nullopt;
        }

        // Maps a renderer mesh handle to a stable, human-readable identifier that
        // survives a save/load, and back. The index convention MUST match the
        // renderer's mesh registry and BuildScene (0 = cube, 1 = sphere.obj,
        // 2 = ground, 3 = Duck.glb); an unknown identifier resolves to the cube.
        [[nodiscard]] static std::string MeshIdentifier(rag::renderer::MeshHandle mesh)
        {
            switch (mesh.index)
            {
                case 0: return "builtin:cube";
                case 1: return "models/sphere.obj";
                case 2: return "builtin:ground";
                case 3: return "models/Duck.glb";
                default: return "builtin:cube";
            }
        }

        [[nodiscard]] static bool IsKnownMeshIdentifier(const std::string& id)
        {
            return id == "builtin:cube" || id == "models/sphere.obj" ||
                   id == "builtin:ground" || id == "models/Duck.glb";
        }

        [[nodiscard]] static rag::renderer::MeshHandle MeshHandleFromIdentifier(
            const std::string& id)
        {
            if (id == "models/sphere.obj") return rag::renderer::MeshHandle{1, 0};
            if (id == "builtin:ground") return rag::renderer::MeshHandle{2, 0};
            if (id == "models/Duck.glb") return rag::renderer::MeshHandle{3, 0};
            return rag::renderer::MeshHandle{0, 0}; // builtin:cube + unknown fallback
        }

        // Human-readable texture id for the mesh's material. In this engine the bound
        // texture follows the mesh slot (the duck carries its own embedded base-color
        // image; everything else shares assets/texture.png), so this is descriptive
        // metadata; the mesh id re-resolves both geometry and texture on load.
        [[nodiscard]] static std::string TextureIdentifier(rag::renderer::MeshHandle mesh)
        {
            return mesh.index == 3
                ? "models/Duck.glb (embedded baseColor)"
                : "assets/texture.png";
        }

        // The name persisted for an entity: the editor display name if present, else
        // a stable index-based fallback (so a non-editor build still round-trips).
        [[nodiscard]] std::string NameForSerialization(rag::scene::EntityId entity) const
        {
#if defined(RAG_EDITOR)
            if (const std::string* name = editor_.Name(entity))
            {
                return *name;
            }
#endif
            return "Entity " + std::to_string(entity.index);
        }

        // Walks the live scene (+ editor names + physics records) into the neutral
        // serialized model. Captures authoring state only -- transforms, component
        // parameters, mesh/texture identifiers and each body's collider descriptor
        // -- never transient runtime state such as live velocities or selection.
        // out_entities (optional) receives the live id of each captured entity,
        // in the same order as the returned description.
        [[nodiscard]] rag::scene::SerializedScene CaptureScene(
            std::vector<rag::scene::EntityId>* out_entities = nullptr)
        {
            rag::scene::SerializedScene out;
            out.environment = scene_.Environment();
            if (out_entities != nullptr)
            {
                out_entities->clear();
            }
            scene_.ForEachEntity([this, &out, out_entities](rag::scene::EntityId entity)
            {
                if (out_entities != nullptr)
                {
                    out_entities->push_back(entity);
                }

                rag::scene::SerializedEntity serialized;
                serialized.name = NameForSerialization(entity);

                if (const rag::scene::TransformComponent* transform = scene_.GetTransform(entity))
                {
                    rag::scene::SerializedTransform t;
                    t.position = transform->local_position;
                    t.rotation_euler_radians = transform->local_rotation_radians;
                    t.rotation_quat = transform->local_rotation_quat;
                    t.use_quaternion_rotation = transform->use_quaternion_rotation;
                    t.scale = transform->local_scale;
                    serialized.transform = t;
                }

                if (const rag::scene::CameraComponent* camera = scene_.GetCamera(entity))
                {
                    rag::scene::SerializedCamera c;
                    c.projection_mode = camera->projection_mode;
                    c.vertical_fov_radians = camera->vertical_fov_radians;
                    c.aspect_ratio = camera->aspect_ratio;
                    c.near_plane = camera->near_plane;
                    c.far_plane = camera->far_plane;
                    c.orthographic_height = camera->orthographic_height;
                    c.active = entity == scene_.ActiveCamera();
                    serialized.camera = c;
                }

                if (const rag::scene::RenderableComponent* renderable = scene_.GetRenderable(entity))
                {
                    rag::scene::SerializedRenderable r;
                    r.mesh = MeshIdentifier(renderable->mesh);
                    r.texture = TextureIdentifier(renderable->mesh);
                    r.material_index = renderable->material.index;
                    r.material_generation = renderable->material.generation;
                    r.material_settings = renderable->material_settings;
                    r.local_bounds = renderable->local_bounds;
                    r.visibility_layer = renderable->visibility_layer;
                    r.visible = renderable->visible;
                    serialized.renderable = r;
                }

                if (const rag::scene::LightComponent* light = scene_.GetLight(entity))
                {
                    rag::scene::SerializedLight l;
                    l.type = light->type;
                    l.color = light->color;
                    l.intensity = light->intensity;
                    l.local_direction = light->local_direction;
                    l.range = light->range;
                    l.inner_cone_radians = light->inner_cone_radians;
                    l.outer_cone_radians = light->outer_cone_radians;
                    l.visibility_layer = light->visibility_layer;
                    l.visible = light->visible;
                    // The editable sun records the sandbox's authoring euler so its
                    // exact orientation restores; other lights have no such widget.
                    if (entity == light_entity_)
                    {
                        l.editor_euler_radians = light_euler_;
                    }
                    serialized.light = l;
                }

#if defined(RAG_ENABLE_PHYSICS)
                if (const PhysicsBody* physics_body = FindPhysicsBody(entity))
                {
                    rag::scene::SerializedPhysics p;
                    p.mode = BodyModeToString(physics_body->desc.mode);
                    p.shape = ColliderShapeToString(physics_body->desc.shape);
                    p.box_half_extents = physics_body->desc.box_half_extents;
                    p.sphere_radius = physics_body->desc.sphere_radius;
                    p.offset = physics_body->desc.offset;
                    p.gravity_enabled = physics_body->desc.gravity_enabled;
                    p.continuous_collision = physics_body->desc.continuous_collision;
                    serialized.physics = p;
                }
#endif

                // Authoring configuration only: the runtime player (velocity,
                // view, mouse capture) is never part of the scene.
                if (const rag::scene::PlayerStartComponent* player_start = scene_.GetPlayerStart(entity))
                {
                    serialized.player_start = rag::scene::SerializedPlayerStart{*player_start};
                }

                out.entities.push_back(std::move(serialized));
            });
            return out;
        }

        // Tears down the live scene ahead of a load / new / Stop restore: destroys
        // every physics body cleanly (no leaks), destroys every entity, and clears
        // editor names + selection + gizmo drag + the extracted render world so
        // nothing from the old scene lingers.
        void ClearSceneState()
        {
            scene_.Environment() = {};
#if defined(RAG_ENABLE_PHYSICS)
            // The player belongs to the runtime scene being replaced (Stop's
            // restore; Load and New only happen in Edit, where there is none).
            DestroyPlayer();
            for (PhysicsBody& physics_body : physics_bodies_)
            {
                if (physics_body.body.IsValid())
                {
                    physics_.DestroyBody(physics_body.body);
                }
            }
            physics_bodies_.clear();
#endif
            // Resets slot reuse too, so the scene rebuilt next iterates (and lists
            // in the Hierarchy) in description order instead of a reversed or
            // shuffled one -- which would otherwise change on every load or Stop.
            scene_.DestroyAllEntities();

#if defined(RAG_EDITOR)
            editor_.Clear();
            pending_delete_entity_ = rag::scene::EntityId{};
            pending_add_player_start_ = false;
            // The dragged matrix belongs to an entity that no longer exists.
            CancelGizmoDrag();
#endif
            camera_entity_ = rag::scene::EntityId{};
            light_entity_ = rag::scene::EntityId{};
            mouse_look_active_ = false;
            render_world_.Clear();
        }

        // Rebuilds the live scene from the neutral model. Clears first, then recreates
        // each entity in file order, re-resolving mesh identifiers back to the
        // renderer's registry and rebuilding physics through the existing per-object
        // registration path so colliders/bodies match the saved authoring exactly.
        // out_created (optional) receives the new id of each entity, in data order.
        void ApplyScene(
            const rag::scene::SerializedScene& data,
            std::vector<rag::scene::EntityId>* out_created = nullptr)
        {
            ClearSceneState();
            scene_.Environment() = data.environment;
            if (out_created != nullptr)
            {
                out_created->clear();
                out_created->reserve(data.entities.size());
            }

            rag::scene::EntityId active_camera{};
            rag::scene::EntityId first_camera{};
            rag::scene::EntityId first_light{};

            for (const rag::scene::SerializedEntity& serialized : data.entities)
            {
                const rag::scene::EntityId entity = scene_.CreateEntity();
                NameEntity(entity, serialized.name);
                if (out_created != nullptr)
                {
                    out_created->push_back(entity);
                }

                if (serialized.transform.has_value())
                {
                    rag::scene::TransformComponent& transform = scene_.AddTransform(entity);
                    transform.local_position = serialized.transform->position;
                    transform.local_rotation_radians = serialized.transform->rotation_euler_radians;
                    transform.local_rotation_quat = serialized.transform->rotation_quat;
                    transform.use_quaternion_rotation = serialized.transform->use_quaternion_rotation;
                    transform.local_scale = serialized.transform->scale;
                }

                if (serialized.renderable.has_value())
                {
                    const rag::scene::SerializedRenderable& source = *serialized.renderable;
                    if (!IsKnownMeshIdentifier(source.mesh))
                    {
                        RAG_LOG_WARNING(
                            "Scene load: unknown mesh identifier '",
                            source.mesh,
                            "' on entity '",
                            serialized.name,
                            "'; falling back to the built-in cube.");
                    }
                    rag::scene::RenderableComponent& renderable = scene_.AddRenderable(entity);
                    renderable.mesh = MeshHandleFromIdentifier(source.mesh);
                    renderable.material = rag::renderer::MaterialHandle{
                        source.material_index, source.material_generation};
                    renderable.material_settings = source.material_settings;
                    renderable.local_bounds = source.local_bounds;
                    renderable.visibility_layer = source.visibility_layer;
                    renderable.visible = source.visible;
                }

                if (serialized.camera.has_value())
                {
                    rag::scene::CameraComponent& camera = scene_.AddCamera(entity);
                    camera.projection_mode = serialized.camera->projection_mode;
                    camera.vertical_fov_radians = serialized.camera->vertical_fov_radians;
                    camera.aspect_ratio = serialized.camera->aspect_ratio;
                    camera.near_plane = serialized.camera->near_plane;
                    camera.far_plane = serialized.camera->far_plane;
                    camera.orthographic_height = serialized.camera->orthographic_height;
                    if (!first_camera.IsValid())
                    {
                        first_camera = entity;
                    }
                    if (serialized.camera->active && !active_camera.IsValid())
                    {
                        active_camera = entity;
                    }
                }

                if (serialized.light.has_value())
                {
                    rag::scene::LightComponent& light = scene_.AddLight(entity);
                    light.type = serialized.light->type;
                    light.color = serialized.light->color;
                    light.intensity = serialized.light->intensity;
                    light.local_direction = serialized.light->local_direction;
                    light.range = serialized.light->range;
                    light.inner_cone_radians = serialized.light->inner_cone_radians;
                    light.outer_cone_radians = serialized.light->outer_cone_radians;
                    light.visibility_layer = serialized.light->visibility_layer;
                    light.visible = serialized.light->visible;
                    if (!first_light.IsValid())
                    {
                        // Adopt the first light as the sandbox's editable sun and
                        // restore the authoring state SyncLightFromUi drives from.
                        first_light = entity;
                        light_euler_ = serialized.light->editor_euler_radians;
                        light_color_ = serialized.light->color;
                        light_intensity_ = serialized.light->intensity;
                    }
                }

                if (serialized.player_start.has_value())
                {
                    scene_.AddPlayerStart(entity) = serialized.player_start->config;
                }

                scene_.MarkTransformDirty(entity);

#if defined(RAG_ENABLE_PHYSICS)
                if (serialized.physics.has_value())
                {
                    rag::physics::BodyDesc desc;
                    desc.mode = BodyModeFromString(serialized.physics->mode);
                    desc.shape = ColliderShapeFromString(serialized.physics->shape);
                    desc.box_half_extents = serialized.physics->box_half_extents;
                    desc.sphere_radius = serialized.physics->sphere_radius;
                    desc.offset = serialized.physics->offset;
                    desc.gravity_enabled = serialized.physics->gravity_enabled;
                    desc.continuous_collision = serialized.physics->continuous_collision;
                    (void)RegisterPhysicsBody(entity, desc);
                }
#endif
            }

            camera_entity_ = active_camera.IsValid() ? active_camera : first_camera;
            light_entity_ = first_light;
            FinalizeLoadedCamera();
        }

        // Restores the sandbox camera members from the loaded active camera: refreshes
        // the aspect ratio from the current window and seeds yaw/pitch from the
        // camera's stored Euler so the free-fly controls continue seamlessly.
        void FinalizeLoadedCamera()
        {
            if (!scene_.IsAlive(camera_entity_))
            {
                return;
            }
            scene_.SetActiveCamera(camera_entity_);

            if (rag::scene::CameraComponent* camera = scene_.GetCamera(camera_entity_))
            {
                if (window_ != nullptr && window_->Width() > 0 && window_->Height() > 0)
                {
                    camera->aspect_ratio =
                        static_cast<rag::f32>(window_->Width()) /
                        static_cast<rag::f32>(window_->Height());
                }
            }
            if (const rag::scene::TransformComponent* transform = scene_.GetTransform(camera_entity_))
            {
                camera_pitch_radians_ = transform->local_rotation_radians.x;
                camera_yaw_radians_ = transform->local_rotation_radians.y;
            }
        }

        // Captures the current scene and writes it to path. Returns false (logging
        // the reason) if the write fails. Which file that is, and whether it
        // becomes the document's file, is decided by the callers via document_.
        bool WriteSceneFile(const std::filesystem::path& path, std::string& out_error)
        {
            const rag::scene::SerializedScene data = CaptureScene();
            if (!rag::scene::SaveSceneToFile(data, path, out_error))
            {
                RAG_LOG_ERROR("Failed to save scene to '", path.string(), "': ", out_error);
                return false;
            }
            RAG_LOG_INFO(
                "Saved scene (",
                data.entities.size(),
                " entities) to '",
                path.string(),
                "'.");
            return true;
        }

        // "Save Scene": rewrites the document's own file. An Untitled document is
        // never written; NeedsPath tells the editor to ask for a file name. Refused
        // outside Edit, so runtime state can never be saved as the document.
        rag::runtime::DocumentSaveResult SaveScene(std::string& out_error)
        {
            if (RefuseDocumentOperation("Saving", out_error))
            {
                return rag::runtime::DocumentSaveResult::Failed;
            }
            const rag::runtime::DocumentSaveResult result = document_.Save(
                [this](const std::filesystem::path& path, std::string& error)
                {
                    return WriteSceneFile(path, error);
                },
                out_error);
            if (result == rag::runtime::DocumentSaveResult::NeedsPath)
            {
                RAG_LOG_INFO("Scene: Save on an Untitled document wrote nothing; asking for a file name.");
            }
            return result;
        }

        // "Save Scene As": writes the scene to path and, only if that worked, makes
        // path the document's file. A failure keeps the previous file (or Untitled)
        // and the scene in memory. Refused outside Edit.
        bool SaveSceneAs(const std::filesystem::path& path, std::string& out_error)
        {
            if (RefuseDocumentOperation("Saving", out_error))
            {
                return false;
            }
            return document_.SaveAs(
                path,
                [this](const std::filesystem::path& target, std::string& error)
                {
                    return WriteSceneFile(target, error);
                },
                out_error);
        }

        // Reads a scene file and rebuilds the live scene from it, making it the
        // document's file. Returns false (logging the reason, leaving the current
        // scene and document untouched) on any error, including outside Edit.
        bool LoadSceneFromPath(const std::filesystem::path& path, std::string& out_error)
        {
            if (RefuseDocumentOperation("Loading a scene", out_error))
            {
                return false;
            }

            return document_.Open(
                path,
                [this](const std::filesystem::path& source, std::string& error)
                {
                    // Parse fully before touching the live scene.
                    rag::scene::SerializedScene data;
                    if (!rag::scene::LoadSceneFromFile(source, data, error))
                    {
                        RAG_LOG_WARNING("Could not load scene from '", source.string(), "': ", error);
                        return false;
                    }
                    ApplyScene(data);
                    RAG_LOG_INFO(
                        "Loaded scene (",
                        data.entities.size(),
                        " entities) from '",
                        source.string(),
                        "'.");
                    return true;
                },
                out_error);
        }

        // Replaces the scene with a minimal default: just the free-fly camera and the
        // directional sun, so the viewport stays navigable and lit but otherwise empty.
        // The document becomes Untitled, so a later Save cannot reach the previous
        // file. Refused outside Edit (returns false with out_error set).
        bool NewScene(std::string& out_error)
        {
            if (RefuseDocumentOperation("New Scene", out_error))
            {
                return false;
            }

            ClearSceneState();
            light_euler_ = rag::math::Vec3{};
            light_color_ = rag::math::Vec3{1.0f, 0.92f, 0.78f};
            light_intensity_ = 1.25f;
            CreateDefaultCameraEntity();
            CreateDefaultLightEntity();
            MarkDocumentUntitled();
            RAG_LOG_INFO("New scene created (camera + directional light); the document is Untitled.");
            return true;
        }

        // The scene no longer belongs to any file (New Scene, or the built-in
        // startup scene). The editor's file-name field is cleared too, so the
        // Save As prompt a later Save opens does not suggest the previous file.
        void MarkDocumentUntitled()
        {
            document_.MakeUntitled();
#if defined(RAG_EDITOR)
            scene_name_input_[0] = '\0';
#endif
        }

#if defined(RAG_ENABLE_PHYSICS)
        [[nodiscard]] static const char* BodyModeToString(rag::physics::BodyMode mode)
        {
            switch (mode)
            {
                case rag::physics::BodyMode::None: return "None";
                case rag::physics::BodyMode::Static: return "Static";
                case rag::physics::BodyMode::Dynamic: return "Dynamic";
            }
            return "None";
        }

        [[nodiscard]] static rag::physics::BodyMode BodyModeFromString(const std::string& text)
        {
            if (text == "Static") return rag::physics::BodyMode::Static;
            if (text == "Dynamic") return rag::physics::BodyMode::Dynamic;
            return rag::physics::BodyMode::None;
        }

        [[nodiscard]] static const char* ColliderShapeToString(rag::physics::ColliderShape shape)
        {
            return shape == rag::physics::ColliderShape::Sphere ? "Sphere" : "Box";
        }

        [[nodiscard]] static rag::physics::ColliderShape ColliderShapeFromString(const std::string& text)
        {
            return text == "Sphere"
                ? rag::physics::ColliderShape::Sphere
                : rag::physics::ColliderShape::Box;
        }

        // The *FromString helpers above fall back silently; these let snapshot
        // validation reject a value that would not restore as written.
        [[nodiscard]] static bool IsKnownBodyMode(const std::string& text)
        {
            return text == "None" || text == "Static" || text == "Dynamic";
        }

        [[nodiscard]] static bool IsKnownColliderShape(const std::string& text)
        {
            return text == "Box" || text == "Sphere";
        }
#endif

#if defined(RAG_EDITOR)
        // Builds the whole editor for the frame: a full-window dockspace host (with
        // the main menu bar and a pass-through central node so the 3D scene shows
        // through) plus the dockable panels. Invoked by the renderer between ImGui's
        // NewFrame and Render via RenderFrameContext::on_build_ui, only while editor
        // mode is on.
        void BuildEditorUi()
        {
            // ImGuizmo shares ImGui's frame; start it now that the renderer has
            // already called ImGui::NewFrame (BeginFrame -> on_build_ui).
            ImGuizmo::BeginFrame();

            // Drop the selection first if it points at an entity that no longer
            // exists, so no panel this frame acts on a stale id.
            editor_.RefreshValidity(scene_);

            BuildMainMenuBar();
            BuildDockspace();
            DrawPlayerStartMarkers();

            // Draw the transform gizmo before the Inspector so a drag this frame is
            // written into the TransformComponent and the Inspector fields below
            // read the updated values in the same frame.
            DrawTransformGizmo();

            if (show_hierarchy_panel_)
            {
                BuildHierarchyPanel();
            }

            if (show_inspector_panel_)
            {
                BuildInspectorPanel();
            }

            if (show_debug_panel_)
            {
                BuildDebugPanel();
            }

            if (show_assets_panel_)
            {
                BuildAssetsPanel();
            }

            // Top-level (outside the menu bar) so the modal is not tied to a menu
            // that closes when Save is clicked.
            BuildSaveAsDialog();
        }

        // DockSpaceOverViewport creates the full-client borderless host window.
        // Its pass-through central node remains empty, so the scene rendered
        // underneath stays visible while editor panels dock around the edges.
        void BuildDockspace()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImGuiID dockspace_id = ImGui::GetID("RagEditorDockSpace");
            constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
            ImGui::DockSpaceOverViewport(dockspace_id, viewport, dockspace_flags);

            // Build the default arrangement once (or after "Layout > Reset Layout").
            // We persist no imgui.ini, so this is what gives a docked panel on first
            // run instead of a floating one.
            if (!dock_layout_initialized_)
            {
                dock_layout_initialized_ = true;
                BuildDefaultDockLayout(dockspace_id, viewport->WorkSize);
            }
        }

        // Lays out the default docking arrangement: Hierarchy on the left, the
        // Inspector above the Debug panel on the right, the Assets browser as a
        // full-width strip along the bottom, and the central node left empty for
        // the 3D viewport. The user may re-dock freely afterwards.
        void BuildDefaultDockLayout(ImGuiID dockspace_id, const ImVec2& size)
        {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            // Recreate the root with the same pass-through flag used by
            // DockSpaceOverViewport above; otherwise the one-time layout reset
            // replaces it with an opaque central node and hides the 3D scene.
            ImGui::DockBuilderAddNode(
                dockspace_id,
                static_cast<ImGuiDockNodeFlags>(
                    static_cast<int>(ImGuiDockNodeFlags_DockSpace) |
                    static_cast<int>(ImGuiDockNodeFlags_PassthruCentralNode)));
            ImGui::DockBuilderSetNodeSize(dockspace_id, size);

            // SplitNode carves a node off the central area and reassigns the
            // "remaining" id through the out-param, so keep the original root id
            // intact for DockBuilderFinish. The central node stays empty for the
            // 3D viewport. Carve the bottom strip off first so the Assets browser
            // spans the full window width; the left/right columns then divide the
            // region above it (the classic editor layout).
            ImGuiID central = dockspace_id;
            const ImGuiID bottom_node =
                ImGui::DockBuilderSplitNode(central, ImGuiDir_Down, 0.26f, nullptr, &central);
            const ImGuiID right_node =
                ImGui::DockBuilderSplitNode(central, ImGuiDir_Right, 0.26f, nullptr, &central);
            const ImGuiID left_node =
                ImGui::DockBuilderSplitNode(central, ImGuiDir_Left, 0.22f, nullptr, &central);

            // Stack the Inspector (top) above the Debug panel (bottom) in the right
            // column so both are visible at once.
            ImGuiID right_lower = 0;
            const ImGuiID right_upper =
                ImGui::DockBuilderSplitNode(right_node, ImGuiDir_Up, 0.6f, nullptr, &right_lower);

            ImGui::DockBuilderDockWindow("Hierarchy", left_node);
            ImGui::DockBuilderDockWindow("Inspector", right_upper);
            ImGui::DockBuilderDockWindow("RAG Engine Debug", right_lower);
            ImGui::DockBuilderDockWindow("Assets", bottom_node);

            ImGui::DockBuilderFinish(dockspace_id);
        }

        // The File menu: New / Save / Save As / Load. Scene files live in the
        // working directory's "scenes" folder (path shown in the menu); the text
        // field names the file (".ragscene" is appended if you omit an extension).
        // "Save Scene" rewrites the open document's file, or asks for a name when
        // the document is Untitled. "Load Scene" resolves in the working
        // directory first, then next to the executable.
        void BuildFileMenu()
        {
            if (!ImGui::BeginMenu("File"))
            {
                return;
            }

            ImGui::TextDisabled("Document:");
            ImGui::TextWrapped(
                "%s",
                document_.IsUntitled()
                    ? "Untitled (no file yet; Save asks for a name)"
                    : document_.Path().string().c_str());
            ImGui::Separator();

            // New/Load/Save/Save As only operate on the authored scene. During a
            // test run they are disabled here (and refused by the operations
            // themselves) so runtime state can never replace or overwrite a file.
            const bool document_editable = simulation_.CanModifyDocument();
            if (!document_editable)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f), "Stop simulation first");
                ImGui::TextDisabled(
                    "New, Load and Save are disabled while %s.", simulation_.StateName());
                ImGui::Separator();
            }

            if (ImGui::MenuItem("New Scene", nullptr, false, document_editable))
            {
                std::string error;
                scene_status_ = NewScene(error) ? "New scene (camera + light), Untitled." : error;
            }

            // "..." marks that an Untitled Save opens the Save As prompt.
            if (ImGui::MenuItem(
                    document_.IsUntitled() ? "Save Scene...###save_scene" : "Save Scene###save_scene",
                    nullptr, false, document_editable))
            {
                HandleSaveSceneCommand();
            }

            ImGui::Separator();
            ImGui::TextDisabled("Scenes folder:");
            ImGui::TextWrapped("%s", ScenesDirectory().string().c_str());
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("File name", scene_name_input_, sizeof(scene_name_input_));

            // Save As never falls back to a default name: an empty field
            // disables it. Load keeps its default (it never writes).
            const bool has_name = !rag::runtime::SceneFileNameFromField(scene_name_input_).empty();
            if (ImGui::MenuItem("Save Scene As", nullptr, false, document_editable && has_name))
            {
                SaveAsFromNameField();
            }

            const std::string filename = SceneFilenameFromInput();
            if (ImGui::MenuItem("Load Scene", nullptr, false, document_editable))
            {
                std::string error;
                if (const std::optional<std::filesystem::path> path =
                        ResolveExistingScenePath(filename))
                {
                    scene_status_ = LoadSceneFromPath(*path, error)
                        ? "Loaded: " + path->string()
                        : "Load failed: " + error;
                }
                else
                {
                    scene_status_ = "Not found: scenes/" + filename;
                }
            }
            if (!has_name)
            {
                ImGui::TextDisabled("Type a file name to Save As. Load uses %s.", DefaultSceneFilename);
            }

            if (!scene_status_.empty())
            {
                ImGui::Separator();
                ImGui::TextWrapped("%s", scene_status_.c_str());
            }

            ImGui::EndMenu();
        }

        // The scene file name Load looks for: the text field, defaulting to the
        // default scene when empty.
        [[nodiscard]] std::string SceneFilenameFromInput() const
        {
            const std::string name = rag::runtime::SceneFileNameFromField(scene_name_input_);
            return name.empty() ? std::string(DefaultSceneFilename) : name;
        }

        // File > Save Scene. A document with a file is rewritten in place; an
        // Untitled one opens the Save As prompt instead and writes nothing.
        void HandleSaveSceneCommand()
        {
            std::string error;
            switch (SaveScene(error))
            {
                case rag::runtime::DocumentSaveResult::Saved:
                    scene_status_ = "Saved: " + document_.Path().string();
                    break;
                case rag::runtime::DocumentSaveResult::NeedsPath:
                    scene_status_ = "Untitled scene: choose a file name to save it.";
                    save_as_dialog_open_ = true;
                    break;
                case rag::runtime::DocumentSaveResult::Failed:
                    scene_status_ = "Save failed: " + error;
                    break;
            }
        }

        // Cancel (button, or Escape): closes the prompt without writing anything.
        void CancelSaveAsDialog()
        {
            if (!save_as_dialog_open_)
            {
                return;
            }
            save_as_dialog_open_ = false;
            scene_status_ = "Save As cancelled; nothing was written.";
            RAG_LOG_INFO("Scene: Save As cancelled; nothing was written.");
        }

        // Save As to path, from the prompt or the File menu. The prompt closes
        // only on success, so a failed write can be corrected and retried.
        bool CompleteSaveAs(const std::filesystem::path& path)
        {
            std::string error;
            if (!SaveSceneAs(path, error))
            {
                scene_status_ = "Save failed: " + error;
                return false;
            }
            scene_status_ = "Saved: " + path.string();
            save_as_dialog_open_ = false;
            return true;
        }

        // Save As from the shared file-name field. An empty field is refused
        // rather than turned into a default file name.
        void SaveAsFromNameField()
        {
            const std::string name = rag::runtime::SceneFileNameFromField(scene_name_input_);
            if (name.empty())
            {
                scene_status_ = "Save As needs a file name; nothing was written.";
                return;
            }
            (void)CompleteSaveAs(ScenesDirectory() / name);
        }

        static constexpr const char* SaveAsDialogId = "Save Scene As###save_scene_as";

        // The Save As prompt that "Save Scene" opens for an Untitled document.
        // It edits the File menu's own file-name field and saves through the
        // same Save As path; nothing is written until Save succeeds, and Cancel
        // (or Escape) leaves the scene and every file untouched.
        // save_as_dialog_open_ is the source of truth; ImGui's popup follows it.
        void BuildSaveAsDialog()
        {
            if (save_as_dialog_open_ && !ImGui::IsPopupOpen(SaveAsDialogId))
            {
                ImGui::OpenPopup(SaveAsDialogId);
            }
            ImGui::SetNextWindowPos(
                ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (!ImGui::BeginPopupModal(SaveAsDialogId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                return;
            }

            const bool document_editable = simulation_.CanModifyDocument();
            ImGui::Text("Save \"%s\" to a file in:", document_.DisplayName().c_str());
            ImGui::TextDisabled("%s", ScenesDirectory().string().c_str());
            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(320.0f);
            const bool submitted = ImGui::InputText(
                "File name", scene_name_input_, sizeof(scene_name_input_),
                ImGuiInputTextFlags_EnterReturnsTrue);

            const std::string name = rag::runtime::SceneFileNameFromField(scene_name_input_);
            if (name.empty())
            {
                ImGui::TextDisabled("Type a file name (\".ragscene\" is added if you omit an extension).");
            }
            else
            {
                const std::filesystem::path target = ScenesDirectory() / name;
                ImGui::TextWrapped("Writes: %s", target.string().c_str());
                std::error_code ignored;
                if (std::filesystem::exists(target, ignored))
                {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.78f, 0.30f, 1.0f), "A file with this name exists and will be replaced.");
                }
            }
            if (!document_editable)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f), "Stop simulation first.");
            }
            if (!scene_status_.empty())
            {
                ImGui::TextWrapped("%s", scene_status_.c_str());
            }

            const bool can_save = document_editable && !name.empty();
            ImGui::BeginDisabled(!can_save);
            const bool save_clicked = ImGui::Button("Save");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                CancelSaveAsDialog();
            }
            if (can_save && (save_clicked || submitted))
            {
                SaveAsFromNameField();
            }

            if (!save_as_dialog_open_)
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // The open document in the menu bar: its file name (full path on hover),
        // or "Untitled" after New Scene / for the built-in startup scene.
        void BuildDocumentLabel()
        {
            ImGui::TextDisabled("|");
            if (document_.IsUntitled())
            {
                ImGui::Text("Scene: Untitled");
                ImGui::SetItemTooltip("This scene has no file yet. Save Scene asks for a file name.");
            }
            else
            {
                ImGui::Text("Scene: %s", document_.DisplayName().c_str());
                ImGui::SetItemTooltip("%s", document_.Path().string().c_str());
            }
        }

        // The top menu bar. Actions are minimal but real: File saves/loads scenes;
        // View toggles the panels and editor mode; Layout rebuilds the default docks.
        void BuildMainMenuBar()
        {
            // Tint the whole bar while a test run is active so Playing/Paused can
            // never be mistaken for Edit (where changes are authored and saved).
            const rag::runtime::SimulationState state = simulation_.State();
            const bool tinted = state != rag::runtime::SimulationState::Edit;
            if (tinted)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_MenuBarBg,
                    state == rag::runtime::SimulationState::Playing
                        ? ImVec4(0.10f, 0.30f, 0.14f, 1.0f)
                        : ImVec4(0.36f, 0.27f, 0.06f, 1.0f));
            }
            const bool menu_bar_open = ImGui::BeginMainMenuBar();
            if (tinted)
            {
                ImGui::PopStyleColor();
            }
            if (!menu_bar_open)
            {
                return;
            }

            BuildFileMenu();

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Hierarchy", nullptr, &show_hierarchy_panel_);
                ImGui::MenuItem("Inspector", nullptr, &show_inspector_panel_);
                ImGui::MenuItem("Debug Panel", nullptr, &show_debug_panel_);
                ImGui::MenuItem("Assets", nullptr, &show_assets_panel_);
                ImGui::Separator();
                if (ImGui::MenuItem("Show Editor UI", "F1", &editor_mode_))
                {
                    OnEditorUiVisibilityChanged();
                }
                ImGui::TextDisabled("Hidden UI = game view; press F1 to return.");
                ImGui::TextDisabled("Does not change Play/Pause/Stop.");
                ImGui::EndMenu();
            }

            BuildSimulationMenu();

            if (ImGui::BeginMenu("Layout"))
            {
                if (ImGui::MenuItem("Reset Layout"))
                {
                    dock_layout_initialized_ = false; // rebuilt next frame
                    show_hierarchy_panel_ = true;
                    show_inspector_panel_ = true;
                    show_debug_panel_ = true;
                    show_assets_panel_ = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Gizmo"))
            {
                if (ImGui::MenuItem("Translate", "W", gizmo_operation_ == ImGuizmo::TRANSLATE))
                {
                    gizmo_operation_ = ImGuizmo::TRANSLATE;
                }
                if (ImGui::MenuItem("Rotate", "E", gizmo_operation_ == ImGuizmo::ROTATE))
                {
                    gizmo_operation_ = ImGuizmo::ROTATE;
                }
                if (ImGui::MenuItem("Scale", "R", gizmo_operation_ == ImGuizmo::SCALE))
                {
                    gizmo_operation_ = ImGuizmo::SCALE;
                }
                ImGui::Separator();
                // Scale is only meaningful in local space; ImGuizmo forces it there.
                const bool local = gizmo_mode_ == ImGuizmo::LOCAL;
                if (ImGui::MenuItem("Local space", nullptr, local))
                {
                    gizmo_mode_ = ImGuizmo::LOCAL;
                }
                if (ImGui::MenuItem("World space", nullptr, !local))
                {
                    gizmo_mode_ = ImGuizmo::WORLD;
                }
                ImGui::EndMenu();
            }

            // Inline toolbars: the gizmo mode switch, then the simulation controls.
            BuildGizmoToolbar();
            BuildSimulationToolbar();
            BuildDocumentLabel();

            ImGui::EndMainMenuBar();
        }

        // The Simulation menu: the same transitions as the toolbar, with their
        // shortcuts. Illegal transitions are greyed out.
        void BuildSimulationMenu()
        {
            if (!ImGui::BeginMenu("Simulation"))
            {
                return;
            }

            if (ImGui::MenuItem("Play", "F5", false, simulation_.CanPlay()))
            {
                RequestSimulationCommand(SimulationCommand::Play);
            }
            if (ImGui::MenuItem("Pause", "F6", false, simulation_.CanPause()))
            {
                RequestSimulationCommand(SimulationCommand::Pause);
            }
            if (ImGui::MenuItem("Resume", "F6", false, simulation_.CanResume()))
            {
                RequestSimulationCommand(SimulationCommand::Resume);
            }
            if (ImGui::MenuItem("Step One Tick", "F7", false, simulation_.CanStep()))
            {
                RequestSimulationCommand(SimulationCommand::Step);
            }
            if (ImGui::MenuItem("Stop", "F5", false, simulation_.CanStop()))
            {
                RequestSimulationCommand(SimulationCommand::Stop);
            }
            ImGui::Separator();
            ImGui::TextDisabled(
                "Tick: %.2f ms. Stop restores the scene captured at Play.",
                static_cast<double>(SimulationTickSeconds * 1000.0f));
            ImGui::TextDisabled("Shortcuts are ignored while typing in a field or dragging a widget.");
            ImGui::EndMenu();
        }

        // Play / Pause-Resume / Step / Stop buttons plus a colored state label.
        // Buttons only queue a request; it is applied at the start of the next
        // frame (ProcessPendingSimulationCommand). Illegal ones are disabled.
        void BuildSimulationToolbar()
        {
            ImGui::TextDisabled("|");

            const auto CommandButton = [this](
                const char* label, bool enabled, SimulationCommand command, const char* tooltip)
            {
                ImGui::BeginDisabled(!enabled);
                if (ImGui::SmallButton(label))
                {
                    RequestSimulationCommand(command);
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("%s", tooltip);
                }
            };

            const bool paused = simulation_.CanResume();
            CommandButton("Play", simulation_.CanPlay(), SimulationCommand::Play,
                "Play (F5): snapshot the scene in memory and start simulating.");
            // Stable id (###) so the button keeps its identity as its label flips.
            CommandButton(paused ? "Resume###sim_pause" : "Pause###sim_pause",
                simulation_.CanPause() || paused,
                paused ? SimulationCommand::Resume : SimulationCommand::Pause,
                "Pause / Resume (F6): freeze or continue the simulation. Rendering, "
                "the camera and the Inspector keep working while paused.");
            CommandButton("Step", simulation_.CanStep(), SimulationCommand::Step,
                "Step (F7, Paused only): advance exactly one 1/60 s simulation tick.");
            CommandButton("Stop", simulation_.CanStop(), SimulationCommand::Stop,
                "Stop (F5): discard every runtime change and restore the scene "
                "captured when Play was pressed.");

            switch (simulation_.State())
            {
                case rag::runtime::SimulationState::Edit:
                    ImGui::TextDisabled("EDIT");
                    break;
                case rag::runtime::SimulationState::Playing:
                    ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.50f, 1.0f), "PLAYING %.2fs",
                        simulation_.SimulatedSeconds());
                    break;
                case rag::runtime::SimulationState::Paused:
                    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f), "PAUSED %.2fs",
                        simulation_.SimulatedSeconds());
                    break;
            }

            if (!simulation_error_.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "(!)");
                ImGui::SetItemTooltip("%s", simulation_error_.c_str());
            }

            // Who has the view and the mouse during a run with a player.
            if (player_input_captured_)
            {
                ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.50f, 1.0f), "PLAYER (Esc frees mouse)");
            }
            else if (PlayerViewActive())
            {
                ImGui::TextDisabled("PLAYER (click view)");
                ImGui::SetItemTooltip("Click the game view to control the player. Esc releases the mouse again.");
            }
            else if (HasPlayer())
            {
                ImGui::TextDisabled("EDITOR VIEW");
                ImGui::SetItemTooltip(
                    "Paused: the editor camera inspects the scene (it started at the player's eyes). "
                    "Resume returns to the player exactly where it was.");
            }
            if (!player_status_.empty() && !simulation_.CanModifyDocument())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f), "(no player)");
                ImGui::SetItemTooltip("%s", player_status_.c_str());
            }
        }

        // Small radio-style buttons in the main menu bar for the active gizmo mode,
        // so the mode is switchable without opening a menu or knowing the hotkeys.
        void BuildGizmoToolbar()
        {
            ImGui::TextDisabled("|");
            const auto ModeButton = [this](const char* label, ImGuizmo::OPERATION op)
            {
                const bool selected = gizmo_operation_ == op;
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.65f));
                }
                if (ImGui::SmallButton(label))
                {
                    gizmo_operation_ = op;
                }
                if (selected)
                {
                    ImGui::PopStyleColor();
                }
            };
            ModeButton("Move", ImGuizmo::TRANSLATE);
            ModeButton("Rotate", ImGuizmo::ROTATE);
            ModeButton("Scale", ImGuizmo::SCALE);
        }

        // The dockable debug panel: the stats plus the live light, gravity and
        // reset controls that previously floated as a single overlay window.
        void BuildDebugPanel()
        {
            if (ImGui::Begin("RAG Engine Debug", &show_debug_panel_))
            {
                // Selection readout: confirms a Hierarchy click actually updates the
                // shared editor selection, even before an inspector panel exists.
                ImGui::SeparatorText("Selection");
                const rag::scene::EntityId selected = editor_.Selected();
                if (selected.IsValid() && scene_.IsAlive(selected))
                {
                    ImGui::Text("%s", NameOrFallback(selected).c_str());
                    ImGui::Text("id: index %u, gen %u", selected.index, selected.generation);
                    ImGui::TextDisabled("type: %s", CategoryFor(selected));
                }
                else
                {
                    ImGui::TextDisabled("(nothing selected)");
                }

                const ImGuiIO& io = ImGui::GetIO();
                rag::renderer::RendererStats stats{};
#if defined(RAG_ENABLE_VULKAN)
                if (renderer_ != nullptr)
                {
                    stats = renderer_->GetStats();
                }
#endif

                ImGui::SeparatorText("Stats");
                ImGui::Text("FPS: %.1f", static_cast<double>(io.Framerate));
                ImGui::Text("Frame: %.2f ms", stats.last_frame_ms);
                ImGui::Text("Objects: %d", static_cast<int>(render_world_.objects.size()));
                ImGui::Text(
                    "GPU: %s",
                    stats.active_gpu_name.empty() ? "(unknown)" : stats.active_gpu_name.c_str());

                ImGui::SeparatorText("Simulation");
                ImGui::Text("State: %s", simulation_.StateName());
                ImGui::Text(
                    "Simulated: %.3f s in %llu updates",
                    simulation_.SimulatedSeconds(),
                    static_cast<unsigned long long>(simulation_.SimulationUpdates()));
                ImGui::Text(
                    "Last advance: %.2f ms (tick %.2f ms, cap %.0f ms)",
                    static_cast<double>(simulation_.LastAdvanceSeconds() * 1000.0f),
                    static_cast<double>(SimulationTickSeconds * 1000.0f),
                    static_cast<double>(MaxSimulationFrameDeltaSeconds * 1000.0f));
                if (const PlaySnapshot* snapshot = simulation_.PrePlaySnapshot())
                {
                    ImGui::TextDisabled(
                        "Pre-play snapshot: %d entities (in memory)",
                        static_cast<int>(snapshot->scene.entities.size()));
                }
                if (!simulation_error_.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.30f, 1.0f));
                    ImGui::TextWrapped("%s", simulation_error_.c_str());
                    ImGui::PopStyleColor();
                }

                BuildDebugPlayerSection();

                ImGui::SeparatorText("Directional Light");
                ImGui::SliderFloat("Pitch (X, rad)", &light_euler_.x, -rag::math::Pi, rag::math::Pi);
                ImGui::SliderFloat("Yaw (Y, rad)", &light_euler_.y, -rag::math::Pi, rag::math::Pi);
                ImGui::SliderFloat("Roll (Z, rad)", &light_euler_.z, -rag::math::Pi, rag::math::Pi);
                ImGui::ColorEdit3("Color", &light_color_.x);
                ImGui::SliderFloat("Intensity", &light_intensity_, 0.0f, 5.0f);

#if defined(RAG_ENABLE_PHYSICS)
                ImGui::SeparatorText("Physics");
                if (ImGui::SliderFloat("Gravity Y", &physics_gravity_y_, -30.0f, 10.0f))
                {
                    ApplyGravity();
                }
                // Edit does not simulate, so there is nothing to reset there; Stop
                // is what returns everything to the authored scene.
                ImGui::BeginDisabled(!simulation_.CanStop());
                if (ImGui::Button("Reset bodies"))
                {
                    ResetPhysicsBodies();
                }
                ImGui::EndDisabled();
                ImGui::SetItemTooltip(
                    "During a run: drop dynamic bodies back to where they were when Play "
                    "started. In Edit nothing simulates; Stop restores the authored scene.");
                ImGui::SameLine();
                ImGui::Text("(%d dynamic)", static_cast<int>(DynamicBodyCount()));
#endif
            }
            ImGui::End();
        }

        // The runtime player at a glance: where it is, how it moves and whether
        // it has the mouse. Diagnostics only; nothing here is editable.
        void BuildDebugPlayerSection()
        {
            ImGui::SeparatorText("Player");
#if defined(RAG_ENABLE_PHYSICS)
            if (player_.has_value())
            {
                const rag::runtime::PlayerController& controller = player_->controller;
                const rag::math::Vec3 feet = controller.FeetPosition();
                const rag::math::Vec3 velocity = controller.Velocity();
                ImGui::Text("From: %s", player_->start_name.c_str());
                ImGui::Text("Feet: (%.2f, %.2f, %.2f)",
                    static_cast<double>(feet.x), static_cast<double>(feet.y), static_cast<double>(feet.z));
                ImGui::Text("Speed: %.2f m/s horizontal, %.2f m/s vertical",
                    static_cast<double>(std::sqrt((velocity.x * velocity.x) + (velocity.z * velocity.z))),
                    static_cast<double>(velocity.y));
                ImGui::Text("Ground: %s", PlayerGroundName(controller.Ground()));
                ImGui::Text("View: yaw %.1f, pitch %.1f deg",
                    static_cast<double>(rag::math::ToDegrees(controller.Yaw())),
                    static_cast<double>(rag::math::ToDegrees(controller.Pitch())));
                ImGui::Text("Mouse: %s", player_input_captured_ ? "captured (Esc releases)" : "free");
                ImGui::Text("Jumps: %llu, fall resets: %u%s",
                    static_cast<unsigned long long>(controller.JumpCount()),
                    player_->fall_resets,
                    controller.FallResetDisabled() ? " (fall reset off)" : "");
            }
            else
#endif
            {
                ImGui::TextDisabled(
                    "%s",
                    scene_.PlayerStartCount() > 0
                        ? "Spawns at the Player Start on Play."
                        : "No Player Start: Play keeps the free-fly camera.");
            }
            if (!player_status_.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.30f, 1.0f));
                ImGui::TextWrapped("%s", player_status_.c_str());
                ImGui::PopStyleColor();
            }
        }

        // The dockable Asset Browser panel, docked along the bottom by default. It
        // lists the files found in the project's asset folders (models/, assets/),
        // grouped by folder and labelled by type, sorted alphabetically so the view
        // is stable between runs. The list is rescanned whenever the panel (re)appears
        // -- so files added since last time show up on reopen -- plus a manual Refresh
        // button. Clicking a row selects it and fills the info pane; double-clicking a
        // known model (or the Spawn button) drops a new entity into the scene reusing
        // that model's already-registered mesh.
        //
        // This is primarily a VIEWER. Activation is funnelled through OnAssetActivated
        // so future actions (import, drag-into-viewport, material assignment) can hang
        // off the same selection without reworking the list.
        void BuildAssetsPanel()
        {
            if (ImGui::Begin("Assets", &show_assets_panel_))
            {
                // Rescan on (re)open so files added since last time appear. Appearing
                // is true the first frame the panel is shown -- initial layout, a View
                // menu re-enable, or its dock tab being re-selected.
                if (ImGui::IsWindowAppearing())
                {
                    asset_browser_.Refresh();
                }

                if (ImGui::Button("Refresh"))
                {
                    asset_browser_.Refresh();
                }
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "Click to inspect - double-click a model (or Spawn) to add it to the scene");
                ImGui::Separator();

                // File list on the left (stretches), details on the right (fixed).
                const float info_pane_width = 300.0f;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float available_width = ImGui::GetContentRegionAvail().x;
                const float list_width =
                    std::max(available_width - info_pane_width - spacing, 160.0f);

                ImGui::BeginChild("asset_list", ImVec2(list_width, 0.0f), ImGuiChildFlags_Borders);
                BuildAssetList();
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("asset_info", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
                BuildAssetInfo();
                ImGui::EndChild();
            }
            ImGui::End();
        }

        // The scrollable file list: one table per folder (so files stay grouped and
        // sorted within their folder) with a Name column and a Type column. A missing
        // folder shows a muted note instead of erroring; an empty one says so.
        void BuildAssetList()
        {
            for (const rag::editor::AssetFolder& folder : asset_browser_.Folders())
            {
                if (!folder.found)
                {
                    ImGui::SeparatorText(folder.label.c_str());
                    ImGui::TextDisabled("  folder '%s' not found", folder.subdirectory.c_str());
                    continue;
                }

                const std::string heading =
                    folder.label + " (" + std::to_string(folder.entries.size()) + ")";
                ImGui::SeparatorText(heading.c_str());

                if (folder.entries.empty())
                {
                    ImGui::TextDisabled("  (empty)");
                    continue;
                }

                // Scope ids by folder so the two tables (and equal file names across
                // folders) never collide.
                ImGui::PushID(folder.subdirectory.c_str());
                constexpr ImGuiTableFlags table_flags =
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
                if (ImGui::BeginTable("files", 2, table_flags))
                {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);

                    for (const rag::editor::AssetEntry& entry : folder.entries)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);

                        const bool selected = asset_browser_.IsSelected(folder, entry);
                        ImGui::PushID(entry.name.c_str());
                        const bool activated = ImGui::Selectable(
                            entry.name.c_str(),
                            selected,
                            ImGuiSelectableFlags_SpanAllColumns |
                                ImGuiSelectableFlags_AllowDoubleClick);
                        if (activated)
                        {
                            asset_browser_.Select(folder, entry);
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            {
                                OnAssetActivated(entry);
                            }
                        }
                        ImGui::PopID();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextDisabled("%s", rag::editor::AssetTypeLabel(entry.type));
                    }
                    ImGui::EndTable();
                }
                ImGui::PopID();
            }
        }

        // The right-hand details pane: name, folder, type, extension and full path of
        // the selected asset, plus the spawn action for a known model.
        void BuildAssetInfo()
        {
            ImGui::SeparatorText("Details");

            const rag::editor::AssetEntry* entry = asset_browser_.SelectedEntry();
            if (entry == nullptr)
            {
                ImGui::TextDisabled("Select an asset to see its details.");
                return;
            }

            ImGui::TextWrapped("%s", entry->name.c_str());
            ImGui::TextDisabled("Folder: %s", asset_browser_.SelectedFolderLabel().c_str());
            ImGui::Text("Type: %s", rag::editor::AssetTypeLabel(entry->type));
            ImGui::Text(
                "Extension: %s", entry->extension.empty() ? "(none)" : entry->extension.c_str());
            ImGui::TextWrapped("Path: %s", entry->path.c_str());

            if (entry->type == rag::editor::AssetType::Model)
            {
                ImGui::Separator();
                if (entry->spawn_mesh.has_value())
                {
                    if (ImGui::Button("Spawn into scene"))
                    {
                        OnAssetActivated(*entry);
                    }
                    ImGui::TextDisabled("Reuses the preloaded mesh; appears in front of the camera.");
                }
                else
                {
                    ImGui::BeginDisabled();
                    ImGui::Button("Spawn into scene");
                    ImGui::EndDisabled();
                    // TODO: spawning a model the renderer did not preload needs a
                    // runtime mesh-import API (upload a mesh mid-run, return a
                    // MeshHandle). Until then only the models the sandbox registers at
                    // startup (sphere.obj, Duck.glb) are spawnable.
                    ImGui::TextDisabled("No preloaded mesh for this file yet.");
                }
            }
            else if (entry->type == rag::editor::AssetType::Texture)
            {
                ImGui::Separator();
                // TODO (thumbnail): preview the texture by loading it through the
                // renderer and drawing it with ImGui::Image. That needs the renderer
                // to expose an ImGui texture handle (ImGui_ImplVulkan_AddTexture) and
                // own its lifetime; deferred to keep validation clean.
                ImGui::TextDisabled("Texture preview: not yet implemented.");
            }
        }

        // Single funnel for "the user activated an asset" (double-click or the Spawn
        // button). Today it spawns known models; future asset actions branch here.
        void OnAssetActivated(const rag::editor::AssetEntry& entry)
        {
            if (entry.spawn_mesh.has_value())
            {
                SpawnModel(entry);
            }
        }

        // Creates a complete runtime entity through the same render-bounds and
        // physics registration path as startup objects. Every spawned model gets
        // an independent Dynamic + gravity + CCD body and immediately appears in
        // the Hierarchy/Inspector as a normal editor-managed object.
        void SpawnModel(const rag::editor::AssetEntry& entry)
        {
            if (!entry.spawn_mesh.has_value())
            {
                return;
            }

            const rag::u32 spawn_index = ++spawn_counter_;
            const rag::scene::EntityId entity = scene_.CreateEntity();
            NameEntity(entity, entry.name + " (spawn " + std::to_string(spawn_index) + ")");

            rag::scene::TransformComponent& transform = scene_.AddTransform(entity);
            transform.local_position = ComputeSpawnPosition(spawn_index);

            rag::scene::RenderableComponent& renderable = scene_.AddRenderable(entity);
            renderable.mesh = *entry.spawn_mesh;
            renderable.local_bounds = BoundsForMesh(*entry.spawn_mesh);

#if defined(RAG_ENABLE_PHYSICS)
            if (!SpawnDynamicPhysicsForMesh(entity, *entry.spawn_mesh))
            {
                scene_.DestroyEntity(entity);
                RAG_LOG_ERROR(
                    "Failed to spawn asset '",
                    entry.name,
                    "': physics body creation failed.");
                return;
            }
#endif

            scene_.MarkTransformDirty(entity);
            editor_.Select(entity);
            RAG_LOG_INFO(
                "Spawned asset '",
                entry.name,
                "' as Dynamic physics entity ",
                entity.ToU64(),
                " (gravity=on, CCD=on).");
        }

        // A point a few metres in front of the camera, so a spawned asset lands in
        // view rather than at the busy origin. Spawn height is clamped above the
        // floor and consecutive objects are spread laterally to avoid exact overlap.
        [[nodiscard]] rag::math::Vec3 ComputeSpawnPosition(rag::u32 spawn_index) const
        {
            const rag::scene::TransformComponent* camera_transform =
                scene_.GetTransform(camera_entity_);
            if (camera_transform == nullptr)
            {
                return rag::math::Vec3{0.0f, 4.0f, 0.0f};
            }

            // In front of whatever the viewport shows: the player's eyes during a
            // run with a player, the editor camera otherwise.
            rag::math::Vec3 origin = camera_transform->local_position;
            rag::f32 yaw = camera_yaw_radians_;
            rag::f32 pitch = camera_pitch_radians_;
#if defined(RAG_ENABLE_PHYSICS)
            if (PlayerViewActive())
            {
                origin = player_->controller.EyePosition();
                yaw = player_->controller.Yaw();
                pitch = player_->controller.Pitch();
            }
#endif

            const rag::f32 cos_pitch = std::cos(pitch);
            const rag::math::Vec3 forward = rag::math::Normalize(rag::math::Vec3{
                -std::sin(yaw) * cos_pitch,
                std::sin(pitch),
                -std::cos(yaw) * cos_pitch});
            const rag::math::Vec3 right = rag::math::Normalize(rag::math::Vec3{
                std::cos(yaw),
                0.0f,
                -std::sin(yaw)});

            constexpr rag::f32 spawn_distance = 6.0f;
            constexpr rag::f32 lane_spacing = 2.5f;
            constexpr rag::f32 depth_spacing = 2.0f;
            constexpr rag::i32 lane_offsets[] = {0, 1, -1, 2, -2};

            const rag::u32 zero_based_index = spawn_index - 1;
            const rag::u32 lane_index = zero_based_index % 5;
            const rag::u32 depth_index = zero_based_index / 5;
            rag::math::Vec3 position =
                origin +
                (forward * (spawn_distance + depth_spacing * static_cast<rag::f32>(depth_index))) +
                (right * (lane_spacing * static_cast<rag::f32>(lane_offsets[lane_index])));
            position.y = std::max(position.y, 4.0f);
            return position;
        }

        // The dockable Scene Hierarchy panel. Lists every live entity in the scene,
        // one row each, with a readable name (or an "Entity <index>" fallback) plus a
        // type hint. Clicking a row updates the shared editor selection and the
        // selected row is highlighted. Rows are drawn as a collapsible tree that
        // honours parent/child transforms; nothing is parented yet, so every entity
        // is a root and the tree reads as a flat list until hierarchies are used.
        void BuildHierarchyPanel()
        {
            if (ImGui::Begin("Hierarchy", &show_hierarchy_panel_))
            {
                ImGui::TextDisabled("%d entities", static_cast<int>(scene_.EntityCount()));
                ImGui::SameLine();
                const char* const add_blocked = AddPlayerStartBlockedReason();
                ImGui::BeginDisabled(add_blocked != nullptr);
                if (ImGui::SmallButton("Add Player Start"))
                {
                    pending_add_player_start_ = true; // applied at the next safe point
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip(
                        "%s",
                        add_blocked != nullptr
                            ? add_blocked
                            : "Add the spawn point of the first-person player. Play then spawns a player "
                              "there instead of keeping the free-fly camera.");
                }
                ImGui::Separator();

                // Draw roots only; DrawEntityNode recurses into children through the
                // transform hierarchy, so a parented entity nests under its parent and
                // is never listed twice.
                scene_.ForEachEntity([this](rag::scene::EntityId entity)
                {
                    if (IsHierarchyRoot(entity))
                    {
                        DrawEntityNode(entity);
                    }
                });

                // Clicking empty space inside the panel clears the selection.
                if (ImGui::IsWindowHovered() &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                    !ImGui::IsAnyItemHovered())
                {
                    editor_.ClearSelection();
                }
            }
            ImGui::End();
        }

        // Renders one entity as a tree node and recurses into its transform children.
        void DrawEntityNode(rag::scene::EntityId entity)
        {
            const rag::scene::TransformComponent* transform = scene_.GetTransform(entity);
            const bool has_children = transform != nullptr && transform->first_child.IsValid();

            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (editor_.IsSelected(entity))
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (has_children)
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            else
            {
                // A leaf pushes no tree state, so it must not be paired with TreePop.
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                         ImGuiTreeNodeFlags_Bullet;
            }

            const std::string label = HierarchyLabel(entity);
            // The full id (index + generation) is a stable, unique ImGui id per
            // entity, so rows never collide even if two share a display name.
            void* node_id = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entity.ToU64()));
            const bool node_open = ImGui::TreeNodeEx(node_id, flags, "%s", label.c_str());

            // Select on a real click, but ignore the click that only toggled the arrow.
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                editor_.Select(entity);
            }

            if (node_open && has_children)
            {
                for (rag::scene::EntityId child = transform->first_child; child.IsValid();)
                {
                    const rag::scene::TransformComponent* child_transform =
                        scene_.GetTransform(child);
                    const rag::scene::EntityId next_sibling = child_transform != nullptr
                        ? child_transform->next_sibling
                        : rag::scene::EntityId{};
                    DrawEntityNode(child);
                    child = next_sibling;
                }
                ImGui::TreePop();
            }
        }

        // An entity is a hierarchy root when it has no transform parent (or that
        // parent is gone), matching how Scene::Update walks top-level transforms.
        [[nodiscard]] bool IsHierarchyRoot(rag::scene::EntityId entity) const
        {
            const rag::scene::TransformComponent* transform = scene_.GetTransform(entity);
            if (transform == nullptr)
            {
                return true;
            }
            return !transform->parent.IsValid() || !scene_.IsAlive(transform->parent);
        }

        // Row label: the display name (or fallback) plus the entity's category, with
        // the category dropped when it would merely repeat the name (e.g. "Camera").
        [[nodiscard]] std::string HierarchyLabel(rag::scene::EntityId entity) const
        {
            std::string label = NameOrFallback(entity);
            const char* category = CategoryFor(entity);
            if (label != category)
            {
                label += "  (";
                label += category;
                label += ")";
            }
            // Play only ever uses the first Player Start in this list.
            if (scene_.GetPlayerStart(entity) != nullptr && entity != scene_.FirstPlayerStart())
            {
                label += "  - not used";
            }
            return label;
        }

        // The editor display name if one was assigned, else a stable index fallback.
        [[nodiscard]] std::string NameOrFallback(rag::scene::EntityId entity) const
        {
            if (const std::string* name = editor_.Name(entity))
            {
                return *name;
            }
            return "Entity " + std::to_string(entity.index);
        }

        // A short, human-readable category derived from the entity's components, used
        // as the type hint in the Hierarchy and the selection readout.
        [[nodiscard]] const char* CategoryFor(rag::scene::EntityId entity) const
        {
            if (scene_.GetPlayerStart(entity) != nullptr)
            {
                return "Player Start";
            }
            if (scene_.GetCamera(entity) != nullptr)
            {
                return "Camera";
            }
            if (scene_.GetLight(entity) != nullptr)
            {
                return "Light";
            }
            if (scene_.GetRenderable(entity) != nullptr)
            {
                return "Mesh";
            }
            return "Node";
        }

        // The dockable Inspector panel: shows the selected entity's name, id and
        // components, and edits its Transform (position, rotation in degrees, scale)
        // live. For a physics-driven body the edits are pushed into the simulation so
        // they are not overwritten on the next Step(); otherwise the component is
        // edited directly. With nothing selected it shows a placeholder.
        void BuildInspectorPanel()
        {
            if (ImGui::Begin("Inspector", &show_inspector_panel_))
            {
                // Scene settings remain available even when no entity is selected.
                if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& environment = scene_.Environment();
                    ImGui::Checkbox("Skybox Enabled", &environment.skybox_enabled);
                    constexpr std::array face_names{"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
                    for (std::size_t i = 0; i < face_names.size(); ++i)
                        ImGui::TextWrapped("%s: %s", face_names[i], environment.skybox_faces[i].empty()
                            ? "(not configured)" : environment.skybox_faces[i].c_str());
                    ImGui::Separator();
                }
                const rag::scene::EntityId entity = editor_.Selected();
                if (!entity.IsValid() || !scene_.IsAlive(entity))
                {
                    ImGui::TextDisabled("No selection");
                    ImGui::TextDisabled("Select an entity in the Hierarchy panel.");
                }
                else
                {
                    ImGui::Text("%s", NameOrFallback(entity).c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", CategoryFor(entity));
                    ImGui::Text("id: index %u, gen %u", entity.index, entity.generation);

                    if (IsPhysicsDriven(entity))
                    {
                        // Read-only heads-up: a dynamic body owns this entity's pose,
                        // so edits are teleported into the body and velocities cleared.
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.35f, 1.0f));
                        ImGui::TextWrapped(
                            "Physics-controlled: transform edits are applied to the "
                            "body and velocities are reset.");
                        ImGui::PopStyleColor();
                    }

                    // Static and dynamic bodies both want transform edits pushed into
                    // the collider; a None-mode entity is edited as a plain visual.
                    BuildInspectorTransform(entity, HasActivePhysicsBody(entity));
                    BuildInspectorPhysics(entity);
                    BuildInspectorPlayerStart(entity);
                    BuildInspectorLight(entity);
                    BuildInspectorCameraRenderable(entity);
                    BuildInspectorMaterial(entity);
                    BuildInspectorDelete(entity);
                }
            }
            ImGui::End();
        }

        // Transform editor. DragFloat3 binds directly to the live component fields for
        // position/scale; rotation is shown in degrees and converted to/from the
        // stored radians. Any change marks the transform dirty (so Scene::Update
        // rebuilds the world matrix next frame) and, for a physics body, is pushed
        // into the simulation.
        void BuildInspectorTransform(rag::scene::EntityId entity, bool push_into_body)
        {
            rag::scene::TransformComponent* transform = scene_.GetTransform(entity);
            if (transform == nullptr)
            {
                return;
            }

            ImGui::SeparatorText("Transform");

            // Position and scale are plain Vec3s (three contiguous floats), so the
            // address of .x doubles as the float[3] DragFloat3 expects.
            const bool position_changed =
                ImGui::DragFloat3("Position", &transform->local_position.x, 0.05f);

            // A Player Start only uses its position (the feet); its facing is the
            // Yaw field below, so rotation and scale are not offered.
            if (scene_.GetPlayerStart(entity) != nullptr)
            {
                ImGui::TextDisabled("Position = the player's feet. Rotation and scale are not used.");
                if (position_changed)
                {
                    scene_.MarkTransformDirty(entity);
                }
                return;
            }

            // Rotation is stored in radians but edited in degrees.
            rag::math::Vec3 euler_degrees = rag::math::ToDegrees(transform->local_rotation_radians);
            const bool rotation_changed =
                ImGui::DragFloat3("Rotation (deg)", &euler_degrees.x, 0.5f);
            if (rotation_changed)
            {
                transform->local_rotation_radians = rag::math::ToRadians(euler_degrees);
            }

            const bool scale_changed =
                ImGui::DragFloat3("Scale", &transform->local_scale.x, 0.02f);

            if (!(position_changed || rotation_changed || scale_changed))
            {
                return;
            }

            if (push_into_body)
            {
                // A dynamic body overwrites position + rotation every Step(), so
                // editing the component alone would be undone next frame; a static
                // body must have its collider moved to match. Push the new pose into
                // the body (teleport + clear velocity for dynamic). Scale changes are
                // detected and applied to the collider before the next physics step.
                if (rotation_changed)
                {
                    transform->local_rotation_quat =
                        rag::math::QuatFromEulerXYZ(transform->local_rotation_radians);
                }
                if (position_changed || rotation_changed)
                {
                    PushTransformToPhysicsBody(
                        entity, transform->local_position, transform->local_rotation_quat);
                }
            }
            else if (rotation_changed)
            {
                // Non-physics entity: drive rotation from the edited Euler angles
                // instead of any stale quaternion left from a previous owner.
                transform->use_quaternion_rotation = false;
            }

            scene_.MarkTransformDirty(entity);
        }

        // Per-object physics controls: body type (None/Static/Dynamic) and, for a
        // dynamic body, a gravity toggle. Changes reconfigure the Jolt body live
        // (create/destroy on a mode change, gravity factor on the toggle). Only
        // entities the physics system manages show these controls.
        void BuildInspectorPhysics(rag::scene::EntityId entity)
        {
#if defined(RAG_ENABLE_PHYSICS)
            PhysicsBody* physics_body = FindPhysicsBody(entity);
            if (physics_body == nullptr)
            {
                return;
            }

            ImGui::SeparatorText("Physics");

            int mode = static_cast<int>(physics_body->desc.mode);
            const char* const mode_labels[] = {"None", "Static", "Dynamic"};
            if (ImGui::Combo("Body Type", &mode, mode_labels, 3))
            {
                SetEntityBodyMode(*physics_body, static_cast<rag::physics::BodyMode>(mode));
            }

            if (physics_body->desc.mode == rag::physics::BodyMode::Dynamic)
            {
                bool gravity = physics_body->desc.gravity_enabled;
                if (ImGui::Checkbox("Gravity", &gravity))
                {
                    SetEntityGravityEnabled(*physics_body, gravity);
                }
            }
            else if (physics_body->desc.mode == rag::physics::BodyMode::None)
            {
                ImGui::TextDisabled("Not simulated (visual only).");
            }
#else
            (void)entity;
#endif
        }

        // The Player Start's configuration. Every edit is clamped right away by
        // SanitizePlayerStart, the same rule load and Play apply, so the dependent
        // limits (height vs radius, eye within the capsule) always hold.
        void BuildInspectorPlayerStart(rag::scene::EntityId entity)
        {
            rag::scene::PlayerStartComponent* start = scene_.GetPlayerStart(entity);
            if (start == nullptr)
            {
                return;
            }

            using Limits = rag::scene::PlayerStartLimits;
            ImGui::SeparatorText("Player Start");
            const rag::scene::EntityId primary = scene_.FirstPlayerStart();
            if (entity == primary)
            {
                ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.50f, 1.0f), "Used by Play.");
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.30f, 1.0f));
                ImGui::TextWrapped(
                    "Not used: '%s' comes first in the Hierarchy and Play uses only one start.",
                    NameOrFallback(primary).c_str());
                ImGui::PopStyleColor();
            }
            if (!simulation_.CanModifyDocument())
            {
                ImGui::TextWrapped(
                    "Run in progress: edits are runtime changes that Stop reverts. Speed, sensitivity, jump, "
                    "eye height, fall reset and the start position apply live; the capsule size at the next Play.");
            }

            bool changed = false;
            rag::f32 yaw_degrees = rag::math::ToDegrees(start->yaw_radians);
            if (ImGui::DragFloat("Yaw (deg)", &yaw_degrees, 0.5f, -180.0f, 180.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
            {
                start->yaw_radians = rag::math::ToRadians(yaw_degrees);
                changed = true;
            }
            ImGui::SetItemTooltip("Initial facing. 0 looks down -Z; positive turns left (like the editor camera).");
            changed |= ImGui::DragFloat("Move Speed (m/s)", &start->move_speed, 0.05f,
                Limits::MinMoveSpeed, Limits::MaxMoveSpeed, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Mouse Sensitivity", &start->mouse_sensitivity_degrees, 0.001f,
                Limits::MinMouseSensitivityDegrees, Limits::MaxMouseSensitivityDegrees, "%.3f deg/count",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("Degrees of view rotation per raw mouse count.");
            changed |= ImGui::DragFloat("Jump Height (m)", &start->jump_height, 0.01f,
                Limits::MinJumpHeight, Limits::MaxJumpHeight, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip(
                "Apex of a standing jump under the current world gravity (Debug panel). 0 disables jumping.");
            changed |= ImGui::DragFloat("Capsule Radius (m)", &start->capsule_radius, 0.005f,
                Limits::MinCapsuleRadius, Limits::MaxCapsuleRadius, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Capsule Height (m)", &start->capsule_height, 0.01f,
                rag::scene::MinPlayerCapsuleHeight(start->capsule_radius), Limits::MaxCapsuleHeight, "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("Total height including both rounded ends; at least 2 x radius + 0.1 m.");
            changed |= ImGui::DragFloat("Eye Height (m)", &start->eye_height, 0.01f,
                Limits::MinEyeHeight, start->capsule_height, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("Camera height above the feet, inside the capsule.");
            changed |= ImGui::DragFloat("Fall Reset Y (m)", &start->fall_reset_height, 0.1f,
                -Limits::MaxAbsFallResetHeight, Limits::MaxAbsFallResetHeight, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip(
                "World height. Falling below it returns the player to this start with its motion cleared.");
            if (changed)
            {
                (void)rag::scene::SanitizePlayerStart(*start);
            }

            if (PlayerStartFeet(entity).y <= start->fall_reset_height)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.30f, 1.0f));
                ImGui::TextWrapped("The start is not above Fall Reset Y, so fall reset will be off.");
                ImGui::PopStyleColor();
            }

            const rag::scene::PlayerStartComponent defaults{};
            if (ImGui::Button("Reset to Defaults##player_start"))
            {
                const rag::f32 yaw = start->yaw_radians;
                *start = defaults;
                start->yaw_radians = yaw; // keep the facing; it is placement, not tuning
            }
        }

        // Editable light color/intensity (the light fields worth tweaking here).
        void BuildInspectorLight(rag::scene::EntityId entity)
        {
            rag::scene::LightComponent* light = scene_.GetLight(entity);
            if (light == nullptr)
            {
                return;
            }

            ImGui::SeparatorText("Light");
            ImGui::Text("Type: %s", LightTypeName(light->type));

            bool changed = false;
            changed |= ImGui::ColorEdit3("Color##inspector", &light->color.x);
            changed |= ImGui::SliderFloat("Intensity##inspector", &light->intensity, 0.0f, 5.0f);
            if (changed && entity == light_entity_)
            {
                // The sandbox re-pushes light_color_/light_intensity_ onto this light
                // every frame via SyncLightFromUi(); mirror the edit or it reverts.
                light_color_ = light->color;
                light_intensity_ = light->intensity;
            }
        }

        // Read-only presence/details for the remaining components.
        void BuildInspectorCameraRenderable(rag::scene::EntityId entity)
        {
            if (const rag::scene::CameraComponent* camera = scene_.GetCamera(entity))
            {
                ImGui::SeparatorText("Camera");
                ImGui::Text(
                    "Vertical FOV: %.1f deg",
                    static_cast<double>(rag::math::ToDegrees(camera->vertical_fov_radians)));
                ImGui::Text(
                    "Clip: %.2f - %.1f",
                    static_cast<double>(camera->near_plane),
                    static_cast<double>(camera->far_plane));
                if (entity == scene_.ActiveCamera())
                {
                    ImGui::TextDisabled("(active camera)");
                }
            }

            if (const rag::scene::RenderableComponent* renderable = scene_.GetRenderable(entity))
            {
                ImGui::SeparatorText("Renderable");
                ImGui::Text("Mesh handle: %u", renderable->mesh.index);
                ImGui::Text("Visible: %s", renderable->visible ? "yes" : "no");
            }
        }

        // Per-object base color, shown only for renderables. Widgets edit this
        // entity's own MaterialSettings in place; the next ExtractRenderWorld picks
        // the change up, so edits appear immediately and never touch other entities
        // that share the mesh or texture. The picker works in sRGB display values
        // (what the swatch/hex show); extraction converts them to linear.
        void BuildInspectorMaterial(rag::scene::EntityId entity)
        {
            rag::scene::RenderableComponent* renderable = scene_.GetRenderable(entity);
            if (renderable == nullptr)
            {
                return;
            }

            rag::renderer::MaterialSettings& material = renderable->material_settings;
            ImGui::SeparatorText("Material");
            ImGui::ColorEdit3("Base Color##material", &material.base_color.x);
            ImGui::SetItemTooltip(
                "sRGB color. Multiplies the texture when it is used; "
                "otherwise it is the surface color.");
            ImGui::Checkbox("Use Base-Color Texture##material", &material.use_base_color_texture);
            ImGui::TextDisabled(
                "Texture: %s%s",
                TextureIdentifier(renderable->mesh).c_str(),
                material.use_base_color_texture ? "" : " (not used)");

            ImGui::BeginDisabled(material == rag::renderer::MaterialSettings{});
            if (ImGui::Button("Reset to Defaults##material"))
            {
                material = rag::renderer::MaterialSettings{};
            }
            ImGui::EndDisabled();
        }

        // Delete button for the selected entity (same as the Delete key). The
        // deletion itself is deferred to the next frame's safe point.
        void BuildInspectorDelete(rag::scene::EntityId entity)
        {
            ImGui::Separator();
            const bool deletable = CanDeleteEntity(entity);
            ImGui::BeginDisabled(!deletable);
            if (ImGui::Button("Delete Entity"))
            {
                RequestDeleteEntity(entity);
            }
            ImGui::EndDisabled();
            if (!deletable)
            {
                ImGui::SetItemTooltip("The active camera is the editor view and cannot be deleted.");
            }
            else if (simulation_.CanModifyDocument())
            {
                ImGui::SetItemTooltip("Delete (Del). In Edit this is permanent: there is no undo.");
            }
            else
            {
                ImGui::SetItemTooltip("Delete (Del). A runtime change: Stop brings the entity back.");
            }
        }

        // --- Entity deletion -----------------------------------------------------

        // The active camera drives the editor view, so it is the one entity that
        // cannot be deleted.
        [[nodiscard]] bool CanDeleteEntity(rag::scene::EntityId entity) const
        {
            return scene_.IsAlive(entity) && entity != scene_.ActiveCamera() && entity != camera_entity_;
        }

        void RequestDeleteEntity(rag::scene::EntityId entity)
        {
            if (CanDeleteEntity(entity))
            {
                pending_delete_entity_ = entity;
            }
        }

        void ProcessPendingEntityDeletion()
        {
            const rag::scene::EntityId entity = pending_delete_entity_;
            pending_delete_entity_ = rag::scene::EntityId{};
            if (entity.IsValid())
            {
                (void)DeleteEntity(entity);
            }
        }

        // --- Player Start authoring ---------------------------------------------

        // Why "Add Player Start" is unavailable right now, or nullptr if it is
        // available. One per scene from the editor (a hand-edited file may hold
        // more; Play then uses the first in the Hierarchy), and only in Edit so
        // the start is part of the authored scene rather than a runtime change.
        [[nodiscard]] const char* AddPlayerStartBlockedReason() const
        {
            if (!simulation_.CanModifyDocument())
            {
                return "Stop simulation first: a Player Start is authored in Edit.";
            }
            if (scene_.PlayerStartCount() > 0)
            {
                return "This scene already has a Player Start; select it in the Hierarchy to edit it.";
            }
            return nullptr;
        }

        void ProcessPendingPlayerStartCreation()
        {
            if (pending_add_player_start_)
            {
                pending_add_player_start_ = false;
                (void)AddPlayerStart();
            }
        }

        // Creates a Player Start 4 m ahead of the editor view, at roughly the
        // view's eye level and facing the same way, then selects it so it can be
        // placed with the gizmo or the Inspector. Nothing is saved: the scene is
        // only written by an explicit Save.
        bool AddPlayerStart()
        {
            if (const char* reason = AddPlayerStartBlockedReason())
            {
                RAG_LOG_WARNING("Add Player Start: ", reason);
                return false;
            }

            const rag::scene::PlayerStartComponent defaults{};
            rag::math::Vec3 feet{0.0f, 0.0f, 0.0f};
            if (const rag::scene::TransformComponent* camera = scene_.GetTransform(camera_entity_))
            {
                constexpr rag::f32 Distance = 4.0f;
                feet = camera->local_position + (rag::runtime::PlayerHorizontalForward(camera_yaw_radians_) * Distance);
                feet.y = camera->local_position.y - defaults.eye_height;
            }

            const rag::scene::EntityId entity = scene_.CreateEntity();
            NameEntity(entity, "Player Start");
            scene_.AddTransform(entity).local_position = feet;
            rag::scene::PlayerStartComponent& start = scene_.AddPlayerStart(entity);
            start.yaw_radians = camera_yaw_radians_;
            (void)rag::scene::SanitizePlayerStart(start);
            scene_.MarkTransformDirty(entity);
            editor_.Select(entity);
            RAG_LOG_INFO(
                "Added a Player Start at (", feet.x, ", ", feet.y, ", ", feet.z,
                "). Its transform position is the player's feet; set the facing and movement in the Inspector. "
                "Save the scene to keep it.");
            return true;
        }

        // Destroys an entity (with its transform subtree) and releases its physics
        // body right away, so no record outlives the entity even for a frame. In
        // Edit the deletion is authored; during a run Stop brings it back.
        bool DeleteEntity(rag::scene::EntityId entity)
        {
            if (!CanDeleteEntity(entity))
            {
                return false;
            }

            const std::string name = NameOrFallback(entity);
            scene_.DestroyEntity(entity);
#if defined(RAG_ENABLE_PHYSICS)
            if (physics_ready_)
            {
                RemoveDestroyedPhysicsBodies();
            }
#endif
            editor_.RefreshValidity(scene_);
            RAG_LOG_INFO(
                "Deleted entity '",
                name,
                simulation_.CanModifyDocument() ? "'." : "' (runtime change; Stop restores it).");
            return true;
        }

        // Ends any in-progress gizmo drag. ImGuizmo keeps its "using" state until
        // it sees the mouse released, so without this a drag interrupted by a
        // scene replacement or by hiding the UI could resume on the wrong entity.
        void CancelGizmoDrag()
        {
            ImGuizmo::Enable(false);
            ImGuizmo::Enable(true);
            gizmo_using_ = false;
            gizmo_over_ = false;
        }

        // Called on every F1 / View toggle. Logs the way back so it is visible
        // even with no UI on screen, and drops any half-finished gizmo drag.
        void OnEditorUiVisibilityChanged()
        {
            CancelGizmoDrag();
            if (editor_mode_)
            {
                RAG_LOG_INFO("Editor UI shown (simulation: ", simulation_.StateName(), ").");
            }
            else
            {
                RAG_LOG_INFO(
                    "Editor UI hidden (game view, simulation: ", simulation_.StateName(),
                    "). Press F1 to show it again; F5/F6/F7 still control the simulation.");
            }
        }

        // True when a dynamic physics body drives this entity's transform (its pose
        // is owned by Jolt and rewritten every Step()). Static/None bodies are not
        // "driven" even though they are managed by the physics system.
        [[nodiscard]] bool IsPhysicsDriven(rag::scene::EntityId entity) const
        {
#if defined(RAG_ENABLE_PHYSICS)
            for (const PhysicsBody& physics_body : physics_bodies_)
            {
                if (physics_body.entity == entity)
                {
                    return physics_body.body.IsValid() &&
                        physics_body.desc.mode == rag::physics::BodyMode::Dynamic;
                }
            }
#endif
            (void)entity;
            return false;
        }

        // True when the entity has a live collider (Static or Dynamic) that transform
        // edits should be pushed into so the collider tracks gizmo/Inspector moves.
        [[nodiscard]] bool HasActivePhysicsBody(rag::scene::EntityId entity) const
        {
#if defined(RAG_ENABLE_PHYSICS)
            for (const PhysicsBody& physics_body : physics_bodies_)
            {
                if (physics_body.entity == entity)
                {
                    return physics_body.body.IsValid();
                }
            }
#endif
            (void)entity;
            return false;
        }

        // Teleports a body to a pose so an editor edit sticks: a dynamic body also
        // has its velocity cleared; a static body just has its collider moved. A
        // no-op for entities with no active body (None / non-physics / module off).
        void PushTransformToPhysicsBody(
            rag::scene::EntityId entity,
            const rag::math::Vec3& position,
            const rag::math::Quat& rotation)
        {
#if defined(RAG_ENABLE_PHYSICS)
            if (PhysicsBody* physics_body = FindPhysicsBody(entity))
            {
                if (physics_body->body.IsValid())
                {
                    physics_.SetBodyPose(physics_body->body, position, rotation);
                }
            }
#else
            (void)entity;
            (void)position;
            (void)rotation;
#endif
        }

        static const char* LightTypeName(rag::renderer::RenderLightType type)
        {
            switch (type)
            {
                case rag::renderer::RenderLightType::Directional: return "Directional";
                case rag::renderer::RenderLightType::Point: return "Point";
                case rag::renderer::RenderLightType::Spot: return "Spot";
            }
            return "Unknown";
        }

        // --- Viewport selection (mouse picking + highlight) --------------------

        // A world-space, axis-aligned bounding box as its min/max corners.
        struct WorldAabb
        {
            rag::math::Vec3 min{};
            rag::math::Vec3 max{};
        };

        // Transforms the 8 corners of a local bounds box by a world matrix and
        // returns the enclosing world-space AABB. Because every corner is
        // transformed, this correctly accounts for the rotation and scale baked
        // into the world matrix (a rotated object simply yields a larger AABB).
        [[nodiscard]] static WorldAabb ComputeWorldAabb(
            const rag::math::Mat4& world_matrix,
            const rag::renderer::Bounds& local_bounds)
        {
            const rag::math::Vec3 center = local_bounds.center;
            const rag::math::Vec3 extents = local_bounds.extents;

            WorldAabb aabb{};
            for (int corner = 0; corner < 8; ++corner)
            {
                const rag::math::Vec3 local{
                    center.x + ((corner & 1) != 0 ? extents.x : -extents.x),
                    center.y + ((corner & 2) != 0 ? extents.y : -extents.y),
                    center.z + ((corner & 4) != 0 ? extents.z : -extents.z)};
                const rag::math::Vec3 world = rag::math::TransformPoint(world_matrix, local);

                if (corner == 0)
                {
                    aabb.min = world;
                    aabb.max = world;
                }
                else
                {
                    aabb.min = rag::math::Vec3{
                        std::min(aabb.min.x, world.x),
                        std::min(aabb.min.y, world.y),
                        std::min(aabb.min.z, world.z)};
                    aabb.max = rag::math::Vec3{
                        std::max(aabb.max.x, world.x),
                        std::max(aabb.max.y, world.y),
                        std::max(aabb.max.z, world.z)};
                }
            }
            return aabb;
        }

        // The world box around the capsule a Player Start spawns, or nothing for
        // an entity that is not a Player Start. Used for picking and the
        // selection outline, since a Player Start has no mesh.
        [[nodiscard]] std::optional<WorldAabb> PlayerStartAabb(rag::scene::EntityId entity) const
        {
            const rag::scene::PlayerStartComponent* start = scene_.GetPlayerStart(entity);
            if (start == nullptr)
            {
                return std::nullopt;
            }
            const rag::math::Vec3 feet = PlayerStartFeet(entity);
            return WorldAabb{
                rag::math::Vec3{feet.x - start->capsule_radius, feet.y, feet.z - start->capsule_radius},
                rag::math::Vec3{
                    feet.x + start->capsule_radius,
                    feet.y + start->capsule_height,
                    feet.z + start->capsule_radius}};
        }

        // Projects a world point through the frame's camera to main-viewport
        // pixels, or nothing if it is behind the camera. Inverse of BuildPickRay.
        [[nodiscard]] std::optional<ImVec2> ProjectToViewport(const rag::math::Vec3& point) const
        {
            const rag::renderer::RenderCamera& camera = render_world_.camera;
            const rag::math::Vec3 view = rag::math::TransformPoint(camera.view, point);
            if (view.z > -camera.near_plane)
            {
                return std::nullopt;
            }
            const rag::math::Mat4& projection = camera.projection;
            const auto clip = [&](rag::u32 row)
            {
                return (projection(row, 0) * view.x) + (projection(row, 1) * view.y) +
                       (projection(row, 2) * view.z) + projection(row, 3);
            };
            const rag::f32 w = clip(3);
            if (std::abs(w) < 1e-6f)
            {
                return std::nullopt;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const rag::f32 ndc_x = clip(0) / w;
            const rag::f32 ndc_y = clip(1) / w;
            return ImVec2{
                viewport->Pos.x + ((ndc_x * 0.5f) + 0.5f) * viewport->Size.x,
                viewport->Pos.y + (0.5f - (ndc_y * 0.5f)) * viewport->Size.y};
        }

        // Marks every Player Start in the 3D view (they have no mesh): the
        // capsule's axis from the feet to the top, a ring at the feet, the eye
        // with an arrow along the start's yaw, and a label. Green = the start
        // Play uses, grey = ignored extras. Hidden in the player view.
        void DrawPlayerStartMarkers()
        {
            if (PlayerViewActive() || !render_world_.camera.valid || scene_.PlayerStartCount() == 0)
            {
                return;
            }

            ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
            const rag::scene::EntityId primary = scene_.FirstPlayerStart();
            scene_.ForEachEntity([&](rag::scene::EntityId entity)
            {
                const rag::scene::PlayerStartComponent* start = scene_.GetPlayerStart(entity);
                if (start == nullptr)
                {
                    return;
                }
                const rag::math::Vec3 feet = PlayerStartFeet(entity);
                const rag::math::Vec3 eye = feet + rag::math::Vec3{0.0f, start->eye_height, 0.0f};
                const std::optional<ImVec2> feet_px = ProjectToViewport(feet);
                const std::optional<ImVec2> top_px =
                    ProjectToViewport(feet + rag::math::Vec3{0.0f, start->capsule_height, 0.0f});
                if (!feet_px.has_value() || !top_px.has_value())
                {
                    return;
                }

                const ImU32 color = entity == primary ? IM_COL32(90, 255, 120, 230) : IM_COL32(170, 170, 170, 200);
                draw_list->AddLine(*feet_px, *top_px, color, 2.0f);
                draw_list->AddCircle(*feet_px, 7.0f, color, 0, 2.0f);
                const std::optional<ImVec2> eye_px = ProjectToViewport(eye);
                const std::optional<ImVec2> facing_px = ProjectToViewport(
                    eye + (rag::runtime::PlayerHorizontalForward(start->yaw_radians) * 0.75f));
                if (eye_px.has_value() && facing_px.has_value())
                {
                    draw_list->AddLine(*eye_px, *facing_px, color, 2.0f);
                    draw_list->AddCircleFilled(*facing_px, 3.5f, color);
                }
                draw_list->AddText(
                    ImVec2{top_px->x + 6.0f, top_px->y - 8.0f},
                    color,
                    entity == primary ? "Player Start" : "Player Start (not used)");
            });
        }

        // Slab test of a ray against an AABB. On a hit returns true and writes the
        // entry distance (>= 0) to out_distance; a ray whose origin is inside the
        // box reports distance 0. Rays parallel to and outside a slab miss.
        [[nodiscard]] static bool RayIntersectsAabb(
            const rag::math::Vec3& origin,
            const rag::math::Vec3& direction,
            const WorldAabb& aabb,
            rag::f32& out_distance)
        {
            const rag::f32 origin_axes[3] = {origin.x, origin.y, origin.z};
            const rag::f32 direction_axes[3] = {direction.x, direction.y, direction.z};
            const rag::f32 min_axes[3] = {aabb.min.x, aabb.min.y, aabb.min.z};
            const rag::f32 max_axes[3] = {aabb.max.x, aabb.max.y, aabb.max.z};

            rag::f32 t_near = 0.0f;
            rag::f32 t_far = std::numeric_limits<rag::f32>::max();

            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(direction_axes[axis]) < 1e-8f)
                {
                    // Parallel to this slab: a miss unless the origin lies within it.
                    if (origin_axes[axis] < min_axes[axis] || origin_axes[axis] > max_axes[axis])
                    {
                        return false;
                    }
                    continue;
                }

                const rag::f32 inverse = 1.0f / direction_axes[axis];
                rag::f32 t1 = (min_axes[axis] - origin_axes[axis]) * inverse;
                rag::f32 t2 = (max_axes[axis] - origin_axes[axis]) * inverse;
                if (t1 > t2)
                {
                    std::swap(t1, t2);
                }
                t_near = std::max(t_near, t1);
                t_far = std::min(t_far, t2);
                if (t_near > t_far)
                {
                    return false;
                }
            }

            out_distance = t_near;
            return true;
        }

        // Reconstructs an EntityId from the packed id carried on a RenderObject
        // (EntityId::ToU64 stores generation in the high 32 bits, index in the low).
        [[nodiscard]] static rag::scene::EntityId EntityFromObjectId(rag::u64 object_id)
        {
            return rag::scene::EntityId{
                static_cast<rag::u32>(object_id & 0xffffffffu),
                static_cast<rag::u32>(object_id >> 32u)};
        }

        // Builds a world-space picking ray from the cursor, the viewport size and
        // the active camera. The cursor is unprojected through the camera's GL-style
        // projection (the Vulkan Y flip is applied later, inside the renderer) into
        // a view-space direction, which is then rotated into the world by the
        // inverse of the view matrix (the camera's world transform).
        static void BuildPickRay(
            const rag::renderer::RenderCamera& camera,
            rag::i32 mouse_x,
            rag::i32 mouse_y,
            rag::u32 viewport_width,
            rag::u32 viewport_height,
            rag::math::Vec3& out_origin,
            rag::math::Vec3& out_direction)
        {
            // Pixel -> normalized device coordinates. Top-of-screen maps to
            // ndc_y = +1 in the GL convention these matrices use.
            const rag::f32 ndc_x =
                (2.0f * static_cast<rag::f32>(mouse_x) / static_cast<rag::f32>(viewport_width)) - 1.0f;
            const rag::f32 ndc_y =
                1.0f - (2.0f * static_cast<rag::f32>(mouse_y) / static_cast<rag::f32>(viewport_height));

            // Invert the perspective projection analytically: for PerspectiveRH_ZO,
            // ndc.x = P00 * (view_x / -view_z) and ndc.y = P11 * (view_y / -view_z).
            // Fixing view_z = -1 (forward) gives the view-space ray direction.
            const rag::f32 p00 = camera.projection(0, 0);
            const rag::f32 p11 = camera.projection(1, 1);
            const rag::math::Vec3 view_direction{ndc_x / p00, ndc_y / p11, -1.0f};

            // The view matrix is rigid (the camera carries no scale), so its inverse
            // is the camera's world transform; use it to rotate the ray into world
            // space. TransformDirection normalizes the result.
            const rag::math::Mat4 camera_world = rag::math::InverseRigidTransform(camera.view);
            out_origin = camera.position;
            out_direction = rag::math::TransformDirection(camera_world, view_direction);
        }

        // Left-clicking in the 3D viewport (when ImGui does not want the mouse)
        // casts a ray through the cursor and selects the closest renderable whose
        // world-space bounding box it hits; a click that hits nothing clears the
        // selection. Right-mouse still drives camera look, so left-click is free for
        // picking. Uses the same selection state the Hierarchy and Inspector use.
        void UpdateViewportSelection(const rag::core::InputState& input, bool ui_wants_mouse)
        {
            if (ui_wants_mouse || !input.WasMouseButtonPressed(rag::core::MouseButton::Left))
            {
                return;
            }

            // A click that lands on (or grabs) a gizmo handle manipulates the object
            // instead of picking. gizmo_over_/gizmo_using_ reflect the previous
            // frame's ImGuizmo state (the gizmo is drawn later in the frame); since
            // the handle is hovered for a frame before the press, this reliably
            // suppresses the stray re-pick without a full-frame reorder.
            if (gizmo_over_ || gizmo_using_)
            {
                return;
            }

            if (!render_world_.camera.valid || window_ == nullptr)
            {
                return;
            }

            const rag::u32 width = window_->Width();
            const rag::u32 height = window_->Height();
            if (width == 0 || height == 0)
            {
                return;
            }

            rag::math::Vec3 ray_origin{};
            rag::math::Vec3 ray_direction{};
            BuildPickRay(
                render_world_.camera,
                input.MouseX(),
                input.MouseY(),
                width,
                height,
                ray_origin,
                ray_direction);

            // render_world_.objects are exactly the visible renderables, with their
            // world transforms and bounds already extracted for this frame.
            rag::scene::EntityId best_entity{};
            rag::f32 best_distance = std::numeric_limits<rag::f32>::max();
            for (const rag::renderer::RenderObject& object : render_world_.objects)
            {
                const WorldAabb aabb = ComputeWorldAabb(object.world_transform, object.local_bounds);
                rag::f32 distance = 0.0f;
                if (RayIntersectsAabb(ray_origin, ray_direction, aabb, distance) &&
                    distance < best_distance)
                {
                    best_distance = distance;
                    best_entity = EntityFromObjectId(object.object_id);
                }
            }

            // Player Starts draw no mesh; pick them by the capsule they spawn.
            scene_.ForEachEntity([&](rag::scene::EntityId entity)
            {
                const std::optional<WorldAabb> aabb = PlayerStartAabb(entity);
                rag::f32 distance = 0.0f;
                if (aabb.has_value() &&
                    RayIntersectsAabb(ray_origin, ray_direction, *aabb, distance) &&
                    distance < best_distance)
                {
                    best_distance = distance;
                    best_entity = entity;
                }
            });

            if (best_entity.IsValid() && scene_.IsAlive(best_entity))
            {
                editor_.Select(best_entity);
            }
            else
            {
                editor_.ClearSelection();
            }
        }

        // Fills the frame's selection highlight from the current selection: the
        // world-space AABB of the selected renderable, slightly inflated so the box
        // reads as enclosing the object rather than sitting on its silhouette.
        // Non-renderable selections (camera, light) and an empty selection draw no
        // box. Recomputed every frame, so the box tracks the object as it moves.
        [[nodiscard]] rag::renderer::SelectionHighlight ComputeSelectionHighlight() const
        {
            rag::renderer::SelectionHighlight highlight{};

            const rag::scene::EntityId entity = editor_.Selected();
            if (!entity.IsValid() || !scene_.IsAlive(entity))
            {
                return highlight;
            }

            const rag::scene::TransformComponent* transform = scene_.GetTransform(entity);
            const rag::scene::RenderableComponent* renderable = scene_.GetRenderable(entity);
            std::optional<WorldAabb> player_start_box = PlayerStartAabb(entity);
            if (transform == nullptr || (renderable == nullptr && !player_start_box.has_value()))
            {
                return highlight;
            }

            const WorldAabb aabb = player_start_box.has_value()
                ? *player_start_box
                : ComputeWorldAabb(transform->world_matrix, renderable->local_bounds);
            const rag::math::Vec3 center{
                (aabb.min.x + aabb.max.x) * 0.5f,
                (aabb.min.y + aabb.max.y) * 0.5f,
                (aabb.min.z + aabb.max.z) * 0.5f};
            const rag::math::Vec3 half_size{
                (aabb.max.x - aabb.min.x) * 0.5f,
                (aabb.max.y - aabb.min.y) * 0.5f,
                (aabb.max.z - aabb.min.z) * 0.5f};

            // Inflate a touch (relative + absolute) so the lines clear the surface.
            constexpr rag::f32 relative_padding = 1.04f;
            constexpr rag::f32 absolute_padding = 0.03f;
            constexpr rag::math::Vec3 highlight_color{1.0f, 0.55f, 0.1f};

            highlight.active = true;
            highlight.center = center;
            highlight.extents = rag::math::Vec3{
                (half_size.x * relative_padding) + absolute_padding,
                (half_size.y * relative_padding) + absolute_padding,
                (half_size.z * relative_padding) + absolute_padding};
            highlight.color = highlight_color;
            return highlight;
        }

        // --- Transform gizmo (ImGuizmo) ----------------------------------------

        // Draws the ImGuizmo translate/rotate/scale handles on the selected entity
        // and, while a handle is dragged, writes the manipulated pose back into the
        // TransformComponent (and into the physics body for a simulated entity). The
        // gizmo uses the scene camera's view/projection so it lines up with the
        // object at any angle and stays a usable on-screen size. No handles are
        // drawn for the active camera (manipulating the view you look through in
        // that same view is degenerate) or for an entity without a transform.
        //
        // gizmo_using_/gizmo_over_ are refreshed here every frame; UpdateViewportSelection
        // (which runs earlier in the frame) reads the previous frame's values to keep
        // a click on a handle from also re-picking.
        void DrawTransformGizmo()
        {
            gizmo_using_ = false;
            gizmo_over_ = false;

            // In the player view a click on the view takes control; handles
            // drawn there (from the player's eyes) would compete with it.
            if (PlayerViewActive())
            {
                return;
            }

            const rag::scene::EntityId entity = editor_.Selected();
            if (!entity.IsValid() || !scene_.IsAlive(entity) || entity == scene_.ActiveCamera())
            {
                return;
            }

            rag::scene::TransformComponent* transform = scene_.GetTransform(entity);
            if (transform == nullptr || !render_world_.camera.valid || window_ == nullptr)
            {
                return;
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
            ImGuizmo::SetRect(viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);

            // RenderCamera view/projection are GL-convention (the Vulkan Y-flip is
            // applied only to the renderer's UBO copy, not here), which is exactly
            // what ImGuizmo expects, so they are fed as-is. Mat4 storage is
            // column-major, matching ImGuizmo's float[16] layout.
            const float* view = render_world_.camera.view.elements.data();
            const float* projection = render_world_.camera.projection.elements.data();

            // Not dragging: keep the gizmo's matrix synced to the live object so it
            // tracks physics motion. Dragging: ImGuizmo owns and accumulates into
            // gizmo_matrix_, so re-decomposing it each frame does not add drift.
            if (!ImGuizmo::IsUsing())
            {
                gizmo_matrix_ = transform->world_matrix;
            }

            // A Player Start is placed by position only (its facing is the Yaw
            // field), so it always gets the move handles.
            const ImGuizmo::OPERATION operation =
                scene_.GetPlayerStart(entity) != nullptr ? ImGuizmo::TRANSLATE : gizmo_operation_;
            ImGuizmo::Manipulate(
                view,
                projection,
                operation,
                gizmo_mode_,
                gizmo_matrix_.elements.data());

            gizmo_using_ = ImGuizmo::IsUsing();
            gizmo_over_ = ImGuizmo::IsOver();

            if (gizmo_using_)
            {
                ApplyGizmoMatrix(entity, *transform, gizmo_matrix_);
            }
        }

        // Writes a manipulated world matrix into the entity's transform. No sandbox
        // entity is parented, so world == local and the decomposed world TRS is
        // assigned straight to the local fields. Position and scale always update;
        // rotation only updates on a ROTATE drag so a translate/scale never disturbs
        // the stored orientation. Rotation is kept as Euler radians (the Inspector's
        // degree fields read it) and, for a physics body, mirrored into the
        // quaternion and teleported into Jolt so gravity does not undo the edit.
        void ApplyGizmoMatrix(
            rag::scene::EntityId entity,
            rag::scene::TransformComponent& transform,
            const rag::math::Mat4& world_matrix)
        {
            const rag::math::TransformDecomposition decomposed =
                rag::math::DecomposeTransform(world_matrix);

            transform.local_position = decomposed.position;
            if (scene_.GetPlayerStart(entity) != nullptr)
            {
                // Translate-only (see DrawTransformGizmo): position is all it uses.
                scene_.MarkTransformDirty(entity);
                return;
            }
            transform.local_scale = decomposed.scale;

            const bool rotation_changed = gizmo_operation_ == ImGuizmo::ROTATE;
            if (rotation_changed)
            {
                transform.local_rotation_radians = decomposed.rotation_radians;
            }

            if (HasActivePhysicsBody(entity))
            {
                // A dynamic body overwrites position + rotation every Step() and a
                // static body must have its collider moved, so push the pose in
                // (teleport + clear velocity for dynamic). On a rotate drag recompute
                // the quat from the edited Euler; otherwise reuse the live physics
                // quat that StepPhysics keeps current, matching the Inspector.
                // Collider scale is synchronized before the next simulation step.
                if (rotation_changed)
                {
                    transform.local_rotation_quat =
                        rag::math::QuatFromEulerXYZ(transform.local_rotation_radians);
                }
                transform.use_quaternion_rotation = true;
                PushTransformToPhysicsBody(
                    entity, transform.local_position, transform.local_rotation_quat);
            }
            else if (rotation_changed)
            {
                // Non-physics entity: drive rotation from the edited Euler angles,
                // not a stale quaternion left by a previous owner.
                transform.use_quaternion_rotation = false;
            }

            scene_.MarkTransformDirty(entity);
        }

        // --- Play-mode self-test (--play-mode-self-test) -----------------------
        //
        // Drives the real lifecycle frame by frame through the same request ->
        // safe-point path the toolbar and shortcuts use, against in-memory scenes
        // only: it never loads, saves or edits files under scenes/. Each step runs
        // at the top of one frame (before the safe point), so a request it makes
        // is applied in that same frame and the next step observes the result
        // after a full frame has been simulated and rendered. Scene state is
        // compared through CaptureScene -> JSON, which covers every serialized
        // authoring field; physics is checked against Jolt directly. The first
        // failing step ends the run with a non-zero exit code.
        struct SelfTestStep
        {
            std::string name;
            std::function<void()> run;
            rag::u32 extra_frames = 0; // frames to let run before the next step

            // Optional: the step runs on the first frame this returns true, so a
            // check never depends on how fast frames happen to be. Waiting longer
            // than MaxSelfTestWaitFrames fails the test.
            std::function<bool()> ready;
        };

        static constexpr rag::u32 MaxSelfTestWaitFrames = 3000;

        struct PlayModeSelfTestState
        {
            std::vector<SelfTestStep> steps;
            std::size_t next_step = 0;
            rag::u32 wait_frames = 0;
            rag::u32 waited_frames = 0;
            std::size_t checks = 0;
            bool failed = false;
            bool completed = false;
            rag::f64 frame_delta_seconds = 0.0;

            // The authored reference for the scene under test, compared after Stop.
            std::string authored_json;
            std::size_t authored_entities = 0;
            rag::u32 authored_bodies = 0;
            std::size_t authored_records = 0;
            rag::f32 authored_gravity_y = 0.0f;
            rag::f32 authored_light_intensity = 0.0f;
            std::filesystem::path document_path; // empty = Untitled

            // Disposable scene files for the document checks, all inside a fresh
            // folder under the system temp directory (never scenes/), plus a
            // read-only record of the real default scene to prove it is untouched.
            std::filesystem::path document_root;
            std::filesystem::path document_a;
            std::filesystem::path document_b;
            std::filesystem::path document_blocked; // a directory: writing it fails
            std::optional<std::string> document_a_bytes;
            std::optional<std::string> document_b_bytes;
            std::vector<std::string> document_listing;
            std::optional<std::string> default_scene_bytes;

            // Scratch values handed from one step to the next.
            std::string checkpoint_json;
            rag::math::Vec3 checkpoint_position{};
            rag::f64 checkpoint_seconds = 0.0;
            rag::u64 checkpoint_count = 0;
            rag::f32 checkpoint_yaw = 0.0f;
        };

        void RunPlayModeSelfTestFrame(const rag::core::FrameTime& frame_time)
        {
            PlayModeSelfTestState& test = *self_test_;
            test.frame_delta_seconds = frame_time.delta_seconds;
            if (requested_close_)
            {
                return;
            }
            if (test.wait_frames > 0)
            {
                --test.wait_frames;
                return;
            }

            if (test.failed || test.next_step >= test.steps.size())
            {
                RemoveSelfTestDocumentFiles();
                test.completed = !test.failed;
                if (test.completed)
                {
                    RAG_LOG_INFO(
                        "[self-test] PASSED: ", test.checks, " checks in ", test.steps.size(), " steps.");
                }
                else
                {
                    RAG_LOG_ERROR(
                        "[self-test] FAILED in step '", test.steps[test.next_step - 1].name,
                        "' (", test.checks, " checks run).");
                }
                requested_close_ = true;
                return;
            }

            const SelfTestStep& step = test.steps[test.next_step];
            if (step.ready && !step.ready())
            {
                if (++test.waited_frames > MaxSelfTestWaitFrames)
                {
                    ++test.next_step;
                    (void)SelfTestExpect(false, "timed out waiting to run '" + step.name + "'");
                }
                return;
            }

            test.waited_frames = 0;
            ++test.next_step;
            RAG_LOG_INFO("[self-test] step ", test.next_step, "/", test.steps.size(), ": ", step.name);
            step.run();
            test.wait_frames = test.failed ? 0 : step.extra_frames;
        }

        bool SelfTestExpect(bool condition, const std::string& message)
        {
            ++self_test_->checks;
            if (!condition)
            {
                RAG_LOG_ERROR("[self-test] check failed: ", message);
                self_test_->failed = true;
            }
            return condition;
        }

        // The document checks' temp folder is the self-test's own; remove it
        // whether the run passed or failed.
        void RemoveSelfTestDocumentFiles()
        {
            if (self_test_.has_value() && !self_test_->document_root.empty())
            {
                std::error_code ignored;
                std::filesystem::remove_all(self_test_->document_root, ignored);
                self_test_->document_root.clear();
            }
        }

        [[nodiscard]] std::string CaptureSceneJson()
        {
            return rag::scene::WriteSceneToJson(CaptureScene());
        }

        [[nodiscard]] static std::vector<std::string> SplitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            std::size_t start = 0;
            while (start <= text.size())
            {
                const std::size_t end = text.find('\n', start);
                if (end == std::string::npos)
                {
                    lines.push_back(text.substr(start));
                    break;
                }
                lines.push_back(text.substr(start, end - start));
                start = end + 1;
            }
            return lines;
        }

        // The first line on which two scene descriptions differ, for failure logs.
        [[nodiscard]] static std::string FirstDifference(
            const std::string& expected,
            const std::string& actual)
        {
            const std::vector<std::string> expected_lines = SplitLines(expected);
            const std::vector<std::string> actual_lines = SplitLines(actual);
            const std::size_t count = std::max(expected_lines.size(), actual_lines.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                const std::string expected_line =
                    index < expected_lines.size() ? expected_lines[index] : "<end>";
                const std::string actual_line =
                    index < actual_lines.size() ? actual_lines[index] : "<end>";
                if (expected_line != actual_line)
                {
                    return "line " + std::to_string(index + 1) + ": expected `" + expected_line +
                        "`, got `" + actual_line + "`";
                }
            }
            return "no difference";
        }

        bool SelfTestExpectScene(const std::string& expected_json, const std::string& message)
        {
            const std::string actual = CaptureSceneJson();
            if (actual == expected_json)
            {
                return SelfTestExpect(true, message);
            }
            return SelfTestExpect(false, message + " -- " + FirstDifference(expected_json, actual));
        }

        [[nodiscard]] static bool Vec3Equal(const rag::math::Vec3& left, const rag::math::Vec3& right)
        {
            return left.x == right.x && left.y == right.y && left.z == right.z;
        }

        [[nodiscard]] static bool Vec3Near(const rag::math::Vec3& left, const rag::math::Vec3& right)
        {
            constexpr rag::f32 Tolerance = 0.0001f;
            return std::abs(left.x - right.x) <= Tolerance &&
                   std::abs(left.y - right.y) <= Tolerance &&
                   std::abs(left.z - right.z) <= Tolerance;
        }

        [[nodiscard]] rag::scene::EntityId FindEntityByName(std::string_view name) const
        {
            rag::scene::EntityId found{};
            scene_.ForEachEntity([this, name, &found](rag::scene::EntityId entity)
            {
                const std::string* entity_name = editor_.Name(entity);
                if (!found.IsValid() && entity_name != nullptr && *entity_name == name)
                {
                    found = entity;
                }
            });
            return found;
        }

        [[nodiscard]] rag::scene::EntityId FindEntityByNameFragment(std::string_view fragment) const
        {
            rag::scene::EntityId found{};
            scene_.ForEachEntity([this, fragment, &found](rag::scene::EntityId entity)
            {
                const std::string* entity_name = editor_.Name(entity);
                if (!found.IsValid() && entity_name != nullptr &&
                    entity_name->find(fragment) != std::string::npos)
                {
                    found = entity;
                }
            });
            return found;
        }

        // The same edit the Inspector's Position field makes (component write +
        // push into the body, which teleports it and clears its velocity).
        void MoveEntityLikeInspector(rag::scene::EntityId entity, const rag::math::Vec3& position)
        {
            rag::scene::TransformComponent* transform = scene_.GetTransform(entity);
            if (transform == nullptr)
            {
                return;
            }
            transform->local_position = position;
            if (HasActivePhysicsBody(entity))
            {
                PushTransformToPhysicsBody(entity, transform->local_position, transform->local_rotation_quat);
            }
            scene_.MarkTransformDirty(entity);
        }

        [[nodiscard]] static rag::editor::AssetEntry SelfTestModel(
            const char* name,
            rag::renderer::MeshHandle mesh)
        {
            rag::editor::AssetEntry entry;
            entry.name = name;
            entry.type = rag::editor::AssetType::Model;
            entry.spawn_mesh = mesh;
            return entry;
        }

        // A scene made mostly of supported entities that draw nothing: a camera,
        // a light, a transform-only node, a component-less entity and an invisible
        // static collider, plus one dynamic cube that falls onto that collider.
        [[nodiscard]] static rag::scene::SerializedScene BuildNonRenderableTestScene()
        {
            rag::scene::SerializedScene data;
            data.entities.resize(6);

            rag::scene::SerializedEntity& camera = data.entities[0];
            camera.name = "Camera";
            camera.transform = rag::scene::SerializedTransform{};
            camera.transform->position = rag::math::Vec3{0.0f, 3.0f, 9.0f};
            camera.transform->rotation_euler_radians = rag::math::Vec3{-0.3f, 0.0f, 0.0f};
            camera.camera = rag::scene::SerializedCamera{};
            camera.camera->active = true;

            rag::scene::SerializedEntity& sun = data.entities[1];
            sun.name = "Sun";
            sun.transform = rag::scene::SerializedTransform{};
            sun.light = rag::scene::SerializedLight{};

            rag::scene::SerializedEntity& node = data.entities[2];
            node.name = "Empty Node";
            node.transform = rag::scene::SerializedTransform{};
            node.transform->position = rag::math::Vec3{1.0f, 2.0f, 3.0f};

            data.entities[3].name = "Bare Entity";

            rag::scene::SerializedEntity& floor = data.entities[4];
            floor.name = "Invisible Floor";
            floor.transform = rag::scene::SerializedTransform{};
            floor.transform->scale = rag::math::Vec3{20.0f, 0.2f, 20.0f};
            floor.physics = rag::scene::SerializedPhysics{};
            floor.physics->mode = "Static";

            rag::scene::SerializedEntity& crate = data.entities[5];
            crate.name = "Falling Crate";
            crate.transform = rag::scene::SerializedTransform{};
            crate.transform->position = rag::math::Vec3{0.0f, 4.0f, 0.0f};
            crate.renderable = rag::scene::SerializedRenderable{};
            crate.physics = rag::scene::SerializedPhysics{};
            crate.physics->mode = "Dynamic";
            return data;
        }

        // The first-person checks' level, in memory only: a 40 m floor (top at
        // y = -0.5), a wall whose face is at z = -5.75, a light dynamic crate, a
        // 3 m pillar (a start inside it is buried), a 0.5 m step block (a start
        // inside it is lifted onto it) and a Player Start at the origin.
        [[nodiscard]] static rag::scene::SerializedScene BuildPlayerTestScene()
        {
            using namespace rag::scene;
            SerializedScene data;
            const auto add_box = [&data](
                const char* name, rag::math::Vec3 position, rag::math::Vec3 scale, const char* mode, const char* mesh)
            {
                SerializedEntity entity;
                entity.name = name;
                entity.transform = SerializedTransform{};
                entity.transform->position = position;
                entity.transform->scale = scale;
                entity.renderable = SerializedRenderable{};
                entity.renderable->mesh = mesh;
                entity.physics = SerializedPhysics{};
                entity.physics->mode = mode;
                data.entities.push_back(std::move(entity));
            };

            SerializedEntity camera;
            camera.name = "Camera";
            camera.transform = SerializedTransform{};
            camera.transform->position = rag::math::Vec3{0.0f, 3.0f, 9.0f};
            camera.transform->rotation_euler_radians = rag::math::Vec3{-0.3f, 0.0f, 0.0f};
            camera.camera = SerializedCamera{};
            camera.camera->active = true;
            data.entities.push_back(std::move(camera));

            SerializedEntity sun;
            sun.name = "Sun";
            sun.transform = SerializedTransform{};
            sun.light = SerializedLight{};
            data.entities.push_back(std::move(sun));

            add_box("Floor", {0.0f, -0.6f, 0.0f}, {40.0f, 0.2f, 40.0f}, "Static", "builtin:ground");
            add_box("Wall", {0.0f, 1.0f, -6.0f}, {8.0f, 3.0f, 0.5f}, "Static", "builtin:cube");
            add_box("Crate", {3.0f, -0.25f, -2.0f}, {0.5f, 0.5f, 0.5f}, "Dynamic", "builtin:cube");
            add_box("Pillar", {-6.0f, 1.0f, 0.0f}, {1.0f, 3.0f, 1.0f}, "Static", "builtin:cube");
            add_box("Step Block", {6.0f, -0.25f, 0.0f}, {1.0f, 0.5f, 1.0f}, "Static", "builtin:cube");

            SerializedEntity start;
            start.name = "Player Start";
            start.transform = SerializedTransform{};
            start.transform->position = rag::math::Vec3{0.0f, -0.5f, 0.0f};
            start.player_start = SerializedPlayerStart{};
            start.player_start->config.fall_reset_height = -10.0f;
            data.entities.push_back(std::move(start));
            return data;
        }

#if defined(RAG_ENABLE_PHYSICS)
        // Where Jolt holds the entity's body (or its transform, without a body).
        [[nodiscard]] rag::math::Vec3 SimulatedPosition(rag::scene::EntityId entity)
        {
            if (PhysicsBody* physics_body = FindPhysicsBody(entity);
                physics_body != nullptr && physics_body->body.IsValid())
            {
                return physics_.GetBodyState(physics_body->body).position;
            }
            const rag::scene::TransformComponent* transform = scene_.GetTransform(entity);
            return transform != nullptr ? transform->local_position : rag::math::Vec3{};
        }

        // Captured one frame after the scene was put in place, so per-frame editor
        // sync (the sun direction follows its sliders) has already settled.
        void RecordAuthoredBaseline()
        {
            PlayModeSelfTestState& test = *self_test_;
            test.authored_json = CaptureSceneJson();
            test.authored_entities = scene_.EntityCount();
            test.authored_bodies = physics_.BodyCount();
            test.authored_records = physics_bodies_.size();
            test.authored_gravity_y = physics_gravity_y_;
            test.authored_light_intensity = light_intensity_;
        }

        void ExpectBodiesMatchTransforms(const std::string& when)
        {
            bool all_match = true;
            for (const PhysicsBody& physics_body : physics_bodies_)
            {
                const rag::scene::TransformComponent* transform = scene_.GetTransform(physics_body.entity);
                if (physics_body.body.IsValid() &&
                    (transform == nullptr ||
                     !Vec3Near(physics_.GetBodyState(physics_body.body).position, transform->local_position)))
                {
                    all_match = false;
                }
            }
            SelfTestExpect(all_match, "every body must sit at its entity's transform " + when);
        }

        void ExpectAuthoredSceneRestored(const std::string& when)
        {
            using rag::runtime::SimulationState;
            const PlayModeSelfTestState& test = *self_test_;
            SelfTestExpect(
                simulation_.State() == SimulationState::Edit && simulation_.PrePlaySnapshot() == nullptr,
                "Stop must return to Edit and drop the snapshot " + when);
            SelfTestExpect(simulation_error_.empty(), "Stop must not report an error " + when);
            SelfTestExpectScene(
                test.authored_json,
                "Stop must restore every authored entity, name, transform, material, camera, light, "
                "physics setting and the environment " + when);
            SelfTestExpect(scene_.EntityCount() == test.authored_entities, "entity count " + when);
            SelfTestExpect(physics_bodies_.size() == test.authored_records, "physics record count " + when);
            SelfTestExpect(
                physics_.BodyCount() == test.authored_bodies,
                "Jolt body count (no leaked or ghost bodies) " + when);
            SelfTestExpect(physics_gravity_y_ == test.authored_gravity_y, "world gravity " + when);
            SelfTestExpect(light_intensity_ == test.authored_light_intensity, "light settings " + when);
            SelfTestExpect(document_.Path() == test.document_path, "the document path " + when);
            SelfTestExpect(!FindEntityByNameFragment("(spawn").IsValid(), "runtime spawns must be gone " + when);
            ExpectBodiesMatchTransforms(when);
        }

        void ExpectDocumentOperationsBlocked(const std::string& when)
        {
            const std::size_t entities = scene_.EntityCount();
            std::string error;
            SelfTestExpect(!simulation_.CanModifyDocument(), "document operations must be disabled " + when);

            // The probe path is outside scenes/, so even a broken guard could not
            // overwrite a user's scene.
            std::error_code ignored;
            const std::filesystem::path temp = std::filesystem::temp_directory_path(ignored);
            if (!ignored)
            {
                const std::filesystem::path probe = temp / "rag-play-mode-self-test-guard.ragscene";
                std::filesystem::remove(probe, ignored);
                SelfTestExpect(
                    !SaveSceneAs(probe, error) && !std::filesystem::exists(probe, ignored),
                    "Save As must be refused " + when);
            }
            // Plain Save is not probed here: the document may be the real
            // scenes/default.ragscene. The document checks below probe it while
            // the document is a disposable temp file or Untitled.
            SelfTestExpect(!NewScene(error) && scene_.EntityCount() == entities, "New Scene must be refused " + when);
            SelfTestExpect(
                !LoadSceneFromPath(DefaultScenePath(), error) && scene_.EntityCount() == entities,
                "Load must be refused " + when);
            SelfTestExpect(document_.Path() == self_test_->document_path, "the document path must not change " + when);
        }

        [[nodiscard]] static std::optional<std::string> ReadFileBytes(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream.is_open())
            {
                return std::nullopt;
            }
            return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        }

        // Sorted entry names in the document checks' temp folder.
        [[nodiscard]] std::vector<std::string> ListDocumentFolder() const
        {
            std::vector<std::string> names;
            std::error_code error;
            for (std::filesystem::directory_iterator it(self_test_->document_root, error), end;
                 !error && it != end;
                 it.increment(error))
            {
                names.push_back(it->path().filename().string());
            }
            std::sort(names.begin(), names.end());
            return names;
        }

        // Byte-for-byte: A and B hold what they held, the temp folder gained or
        // lost nothing, and the real default scene was not written.
        void ExpectDocumentFilesUnchanged(const std::string& when)
        {
            const PlayModeSelfTestState& test = *self_test_;
            SelfTestExpect(ReadFileBytes(test.document_a) == test.document_a_bytes, "A's bytes must be unchanged " + when);
            SelfTestExpect(ReadFileBytes(test.document_b) == test.document_b_bytes, "B's bytes must be unchanged " + when);
            SelfTestExpect(ListDocumentFolder() == test.document_listing, "no file may be created or removed " + when);
            SelfTestExpect(
                ReadFileBytes(DefaultScenePath()) == test.default_scene_bytes,
                "the real default scene must be untouched " + when);
        }
#endif

        void BuildPlayModeSelfTest()
        {
            self_test_.emplace();
            std::vector<SelfTestStep>& steps = self_test_->steps;
            const auto add = [&steps](
                std::string name,
                rag::u32 extra_frames,
                std::function<void()> run,
                std::function<bool()> ready = {})
            {
                steps.push_back(SelfTestStep{std::move(name), std::move(run), extra_frames, std::move(ready)});
            };

#if !defined(RAG_ENABLE_PHYSICS)
            add("physics module", 0, [this]
            {
                (void)SelfTestExpect(false, "the play-mode self-test needs RAG_ENABLE_PHYSICS");
            });
#else
            using rag::runtime::SimulationState;
            using rag::runtime::TransitionResult;
            constexpr rag::renderer::MeshHandle SphereMesh{1, 0};
            constexpr rag::renderer::MeshHandle DuckMesh{3, 0};

            add("author the reference scene in memory", 0, [this]
            {
                if (!SelfTestExpect(physics_ready_, "physics must be initialized") ||
                    !SelfTestExpect(simulation_.State() == SimulationState::Edit, "the editor must start in Edit"))
                {
                    return;
                }
                self_test_->document_path = document_.Path();

                // The built-in scene plus non-default authoring values, so Stop is
                // checked against more than defaults -- and against a scene that
                // exists only in memory, never the copy on disk.
                ClearSceneState();
                BuildScene();
                if (rag::scene::RenderableComponent* renderable = scene_.GetRenderable(FindEntityByName("Cube 3")))
                {
                    renderable->material_settings =
                        rag::renderer::MaterialSettings{rag::math::Vec3{0.2f, 0.4f, 0.6f}, false};
                }
                if (PhysicsBody* physics_body = FindPhysicsBody(FindEntityByName("Cube 4")))
                {
                    SetEntityBodyMode(*physics_body, rag::physics::BodyMode::None);
                }
                (void)SelfTestExpect(
                    FindEntityByName("Center Cube").IsValid() && FindEntityByName("Cube 7").IsValid(),
                    "the built-in scene must contain its named entities");
            });

            add("record the authored baseline", 5, [this]
            {
                RecordAuthoredBaseline();
                PlayModeSelfTestState& test = *self_test_;
                test.checkpoint_position = SimulatedPosition(FindEntityByName("Center Cube"));
                (void)SelfTestExpect(
                    test.authored_bodies > 0 && test.authored_records == test.authored_bodies + 1,
                    "every record except the None-mode Cube 4 must own a Jolt body");
            });

            add("Edit does not simulate and rejects illegal transitions", 0, [this]
            {
                const PlayModeSelfTestState& test = *self_test_;
                SelfTestExpectScene(test.authored_json, "five Edit frames must leave the authored scene unchanged");
                SelfTestExpect(
                    Vec3Equal(SimulatedPosition(FindEntityByName("Center Cube")), test.checkpoint_position),
                    "Edit must not move physics bodies");
                SelfTestExpect(
                    simulation_.SimulatedSeconds() == 0.0 && simulation_.LastAdvanceSeconds() == 0.0f,
                    "Edit must not advance simulated time");
                SelfTestExpect(simulation_.CanModifyDocument(), "document operations must be allowed in Edit");
                for (const SimulationCommand command :
                     {SimulationCommand::Pause, SimulationCommand::Resume, SimulationCommand::Stop, SimulationCommand::Step})
                {
                    SelfTestExpect(
                        ApplySimulationCommand(command) == TransitionResult::Rejected,
                        std::string(SimulationCommandName(command)) + " must be rejected in Edit");
                }
                SelfTestExpect(
                    simulation_.State() == SimulationState::Edit && simulation_.PrePlaySnapshot() == nullptr,
                    "rejected transitions must leave Edit untouched");
            });

            add("a failed Play entry keeps Edit and the scene", 0, [this]
            {
                const PlayModeSelfTestState& test = *self_test_;
                rag::scene::RenderableComponent* renderable = scene_.GetRenderable(FindEntityByName("Cube 0"));
                if (!SelfTestExpect(renderable != nullptr, "Cube 0 must be renderable"))
                {
                    return;
                }
                // A mesh handle with no stable identifier cannot be captured
                // losslessly, so Play must refuse it rather than start a run that
                // Stop could not restore faithfully.
                const rag::renderer::MeshHandle original = renderable->mesh;
                renderable->mesh = rag::renderer::MeshHandle{99, 0};
                RAG_LOG_INFO("[self-test] expecting the next Play to be refused (one [Error] line follows).");
                const TransitionResult result = ApplySimulationCommand(SimulationCommand::Play);
                renderable->mesh = original;

                SelfTestExpect(result == TransitionResult::Failed, "Play must fail when the scene cannot be captured");
                SelfTestExpect(
                    simulation_.State() == SimulationState::Edit && simulation_.PrePlaySnapshot() == nullptr,
                    "a failed Play entry must stay in Edit without a snapshot");
                SelfTestExpect(!simulation_error_.empty(), "a failed Play entry must be reported");
                SelfTestExpectScene(test.authored_json, "a failed Play entry must preserve the scene");
                SelfTestExpect(physics_.BodyCount() == test.authored_bodies, "a failed Play entry must not touch bodies");

                // Start the real run through the deferred path the toolbar uses.
                RequestSimulationCommand(SimulationCommand::Play);
            });

            add("Playing holds a snapshot and blocks scene documents", 20, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Playing, "Play must enter Playing at the safe point");
                const PlaySnapshot* snapshot = simulation_.PrePlaySnapshot();
                SelfTestExpect(
                    snapshot != nullptr && snapshot->scene.entities.size() == self_test_->authored_entities,
                    "Playing must hold an in-memory snapshot of the authored scene");
                ExpectDocumentOperationsBlocked("while Playing");
            });

            add("Playing simulates; make runtime edits", 10, [this, SphereMesh]
            {
                SelfTestExpect(
                    SimulatedPosition(FindEntityByName("Center Cube")).y < self_test_->checkpoint_position.y - 0.05f,
                    "a dynamic body must fall while Playing");
                SelfTestExpect(simulation_.SimulatedSeconds() > 0.0, "Playing must advance simulated time");

                // Every kind of runtime edit Stop must discard.
                SpawnModel(SelfTestModel("sphere.obj", SphereMesh));
                const rag::scene::EntityId probe = editor_.Selected(); // SpawnModel selects it
                if (!SelfTestExpect(scene_.IsAlive(probe), "spawning during Play must create an entity"))
                {
                    return;
                }
                // Park the spawned body far outside the level: free fall, no contacts.
                MoveEntityLikeInspector(probe, rag::math::Vec3{30.0f, 60.0f, 30.0f});

                RequestDeleteEntity(FindEntityByName("Cube 1"));
                if (rag::scene::RenderableComponent* renderable = scene_.GetRenderable(FindEntityByName("Cube 2")))
                {
                    renderable->material_settings =
                        rag::renderer::MaterialSettings{rag::math::Vec3{1.0f, 0.0f, 0.0f}, false};
                }
                if (PhysicsBody* physics_body = FindPhysicsBody(FindEntityByName("Cube 5")))
                {
                    SetEntityBodyMode(*physics_body, rag::physics::BodyMode::Static);
                }
                if (PhysicsBody* physics_body = FindPhysicsBody(FindEntityByName("Cube 6")))
                {
                    SetEntityGravityEnabled(*physics_body, false);
                }
                const rag::scene::EntityId ground = FindEntityByName("Ground");
                if (const rag::scene::TransformComponent* transform = scene_.GetTransform(ground))
                {
                    MoveEntityLikeInspector(ground, transform->local_position + rag::math::Vec3{0.0f, 0.5f, 0.0f});
                }
                const rag::scene::EntityId scaled = FindEntityByName("Cube 7");
                if (rag::scene::TransformComponent* transform = scene_.GetTransform(scaled))
                {
                    transform->local_scale = rag::math::Vec3{2.0f, 2.0f, 2.0f};
                    scene_.MarkTransformDirty(scaled);
                }
                if (rag::scene::TransformComponent* transform = scene_.GetTransform(camera_entity_))
                {
                    transform->local_position.x += 1.0f;
                    scene_.MarkTransformDirty(camera_entity_);
                }
                scene_.Environment().skybox_enabled = false;
                physics_gravity_y_ = -3.0f;
                ApplyGravity();
                light_intensity_ = 4.0f;
            },
            // Enough simulated time for the 12 m drop to be unmistakable,
            // whatever the frame rate.
            [this] { return simulation_.SimulatedSeconds() >= 0.3; });

            add("runtime edits are live; request Pause", 0, [this]
            {
                SelfTestExpect(!FindEntityByName("Cube 1").IsValid(), "a deleted entity must be gone during the run");
                SelfTestExpect(FindEntityByNameFragment("(spawn").IsValid(), "the spawned entity must exist during the run");
                SelfTestExpect(
                    scene_.EntityCount() == self_test_->authored_entities,
                    "one spawn plus one deletion must leave the entity count unchanged");
                RequestSimulationCommand(SimulationCommand::Pause);
            });

            add("Paused: record a checkpoint", 5, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Paused, "Pause must enter Paused");
                ExpectDocumentOperationsBlocked("while Paused");
                PlayModeSelfTestState& test = *self_test_;
                test.checkpoint_json = CaptureSceneJson();
                test.checkpoint_position = SimulatedPosition(FindEntityByNameFragment("(spawn"));
                test.checkpoint_seconds = simulation_.SimulatedSeconds();
            });

            add("Paused does not simulate; request one Step", 0, [this]
            {
                const PlayModeSelfTestState& test = *self_test_;
                SelfTestExpectScene(test.checkpoint_json, "five Paused frames must leave the scene unchanged");
                SelfTestExpect(
                    Vec3Equal(SimulatedPosition(FindEntityByNameFragment("(spawn")), test.checkpoint_position),
                    "Paused must not move physics bodies");
                SelfTestExpect(simulation_.SimulatedSeconds() == test.checkpoint_seconds, "Paused must not advance simulated time");
                SelfTestExpect(
                    ApplySimulationCommand(SimulationCommand::Play) == TransitionResult::Rejected &&
                        ApplySimulationCommand(SimulationCommand::Pause) == TransitionResult::Rejected,
                    "Play and Pause must be rejected while Paused");
                RequestSimulationCommand(SimulationCommand::Step);
            });

            add("Step advanced exactly one tick; edit while Paused", 0, [this]
            {
                PlayModeSelfTestState& test = *self_test_;
                const rag::scene::EntityId probe = FindEntityByNameFragment("(spawn");
                SelfTestExpect(simulation_.State() == SimulationState::Paused, "Step must stay Paused");
                SelfTestExpect(simulation_.LastAdvanceSeconds() == SimulationTickSeconds, "Step must advance exactly one tick");
                SelfTestExpect(
                    std::abs(simulation_.SimulatedSeconds() - test.checkpoint_seconds - SimulationTickSeconds) < 1e-9,
                    "Step must add exactly one tick of simulated time");
                const rag::f32 step_dy = SimulatedPosition(probe).y - test.checkpoint_position.y;
                SelfTestExpect(
                    step_dy < 0.0f && step_dy > -0.05f,
                    "one tick must move a falling body down slightly (dy=" + std::to_string(step_dy) + ")");

                // A Paused edit is pushed into the body: teleported, velocity cleared.
                MoveEntityLikeInspector(probe, rag::math::Vec3{30.0f, 50.0f, 30.0f});
                SelfTestExpect(
                    Vec3Near(SimulatedPosition(probe), rag::math::Vec3{30.0f, 50.0f, 30.0f}),
                    "a Paused transform edit must move the physics body");
                test.checkpoint_seconds = simulation_.SimulatedSeconds();

                // Hold this paused frame so the next frame delta is long.
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            });

            add("Resume after a long paused frame", 0, [this]
            {
                SelfTestExpect(self_test_->frame_delta_seconds >= 0.2, "the held paused frame must give a long frame delta");
                SelfTestExpect(
                    simulation_.SimulatedSeconds() == self_test_->checkpoint_seconds,
                    "a long paused frame must not advance the simulation");
                RequestSimulationCommand(SimulationCommand::Resume);
            });

            add("Resume does not catch up paused time", 5, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Playing, "Resume must re-enter Playing");
                const rag::f64 advanced = simulation_.SimulatedSeconds() - self_test_->checkpoint_seconds;
                SelfTestExpect(
                    advanced > 0.0 && advanced <= SimulationTickSeconds + 1e-9,
                    "the Resume frame must advance at most one tick, not the paused wall time (advanced " +
                        std::to_string(advanced) + " s)");
                // From rest under gravity -3, one tick moves the body ~0.0008 m; a
                // 33 ms (or longer) catch-up step would move it at least 3x that.
                const rag::f32 resume_dy = SimulatedPosition(FindEntityByNameFragment("(spawn")).y - 50.0f;
                SelfTestExpect(
                    resume_dy < 0.0f && resume_dy > -0.0015f,
                    "the body must move by one tick after Resume (dy=" + std::to_string(resume_dy) + ")");
            });

            add("select an original entity and Stop", 0, [this]
            {
                editor_.Select(FindEntityByName("Center Cube"));
                RequestSimulationCommand(SimulationCommand::Stop);
            });

            add("Stop restored the authored scene", 3, [this]
            {
                ExpectAuthoredSceneRestored("after the first run");
                SelfTestExpect(FindEntityByName("Cube 1").IsValid(), "an entity deleted during the run must return");
                const rag::scene::EntityId selected = editor_.Selected();
                const std::string* selected_name = editor_.Name(selected);
                SelfTestExpect(
                    scene_.IsAlive(selected) && selected_name != nullptr && *selected_name == "Center Cube",
                    "Stop must re-select the restored counterpart of a selected original entity");
            });

            add("Edit after Stop does not simulate", 0, [this]
            {
                SelfTestExpectScene(self_test_->authored_json, "three Edit frames after Stop must not change the scene");
                ExpectBodiesMatchTransforms("after three Edit frames");
            });

            for (int cycle = 1; cycle <= 3; ++cycle)
            {
                const std::string label = "cycle " + std::to_string(cycle);
                add(label + ": Play", 8, [this]
                {
                    RequestSimulationCommand(SimulationCommand::Play);
                });
                add(label + ": spawn, delete and recolor during the run", 4, [this, DuckMesh]
                {
                    SelfTestExpect(simulation_.State() == SimulationState::Playing, "each cycle must be Playing");
                    SpawnModel(SelfTestModel("Duck.glb", DuckMesh));
                    RequestDeleteEntity(FindEntityByName("Sphere"));
                    if (rag::scene::RenderableComponent* renderable = scene_.GetRenderable(FindEntityByName("Duck")))
                    {
                        renderable->material_settings.base_color = rag::math::Vec3{0.0f, 1.0f, 0.0f};
                    }
                });
                add(label + ": Stop", 0, [this]
                {
                    RequestSimulationCommand(SimulationCommand::Stop);
                });
                add(label + ": verify", 0, [this, label]
                {
                    ExpectAuthoredSceneRestored("after " + label);
                });
            }

            add("empty scene: replace in memory", 0, [this]
            {
                ApplyScene(rag::scene::SerializedScene{});
            });
            add("empty scene: record and Play", 0, [this]
            {
                RecordAuthoredBaseline();
                SelfTestExpect(
                    scene_.EntityCount() == 0 && physics_.BodyCount() == 0 && physics_bodies_.empty(),
                    "the empty scene must hold no entities or bodies");
                RequestSimulationCommand(SimulationCommand::Play);
            });
            add("empty scene: spawn during the run", 5, [this, SphereMesh]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Playing, "an empty scene must be playable");
                SpawnModel(SelfTestModel("sphere.obj", SphereMesh));
                SelfTestExpect(
                    scene_.EntityCount() == 1 && physics_.BodyCount() == 1,
                    "a spawn into the empty scene must create one entity and one body");
            });
            add("empty scene: Stop", 0, [this]
            {
                RequestSimulationCommand(SimulationCommand::Stop);
            });
            add("empty scene: verify", 0, [this]
            {
                ExpectAuthoredSceneRestored("for the empty scene");
            });

            add("non-renderable entities: replace in memory", 0, [this]
            {
                ApplyScene(BuildNonRenderableTestScene());
            });
            add("non-renderable entities: record and Play", 0, [this]
            {
                RecordAuthoredBaseline();
                SelfTestExpect(
                    scene_.EntityCount() == 6 && physics_.BodyCount() == 2,
                    "the non-renderable scene must hold six entities and two bodies");
                RequestSimulationCommand(SimulationCommand::Play);
            });
            add("non-renderable entities: delete some during the run", 10, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Playing, "the non-renderable scene must be playable");
                SelfTestExpect(DeleteEntity(FindEntityByName("Bare Entity")), "a component-less entity must be deletable");
                RequestDeleteEntity(FindEntityByName("Invisible Floor"));
            });
            add("non-renderable entities: Stop", 0, [this]
            {
                SelfTestExpect(scene_.EntityCount() == 4, "both deletions must have applied");
                RequestSimulationCommand(SimulationCommand::Stop);
            });
            add("non-renderable entities: verify", 0, [this]
            {
                ExpectAuthoredSceneRestored("for a scene of non-renderable entities");
                SelfTestExpect(
                    FindEntityByName("Bare Entity").IsValid() && FindEntityByName("Invisible Floor").IsValid(),
                    "deleted non-renderable entities must return");
            });

            // --- Scene document identity -----------------------------------------
            // New leaves no save path, so Open A -> New -> Save cannot overwrite A;
            // Save As and Load adopt a path only on success; Play/Stop keeps the
            // document, saved or Untitled. Every file written lives in a fresh
            // temp folder. The real default scene is only read, to prove that
            // nothing wrote it. Save itself is only exercised while the document
            // is a temp file or Untitled.
            add("documents: create disposable scene files in a temp folder", 0, [this]
            {
                PlayModeSelfTestState& test = *self_test_;
                std::error_code error;
                const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
                if (!SelfTestExpect(!error, "the system temp directory must be available"))
                {
                    return;
                }
                test.document_root = temp / ("rag-scene-document-self-test-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                test.document_a = test.document_root / "A.ragscene";
                test.document_b = test.document_root / "B.ragscene";
                test.document_blocked = test.document_root / "blocked.ragscene";
                std::filesystem::create_directories(test.document_blocked, error);
                if (!SelfTestExpect(!error, "the temp folder must be created"))
                {
                    return;
                }
                std::string write_error;
                const bool written =
                    rag::scene::SaveSceneToFile(BuildNonRenderableTestScene(), test.document_a, write_error);
                if (!SelfTestExpect(written, "fixture A must be written: " + write_error))
                {
                    return;
                }
                test.document_a_bytes = ReadFileBytes(test.document_a);
                test.document_b_bytes.reset(); // B does not exist yet
                test.document_listing = ListDocumentFolder();
                test.default_scene_bytes = ReadFileBytes(DefaultScenePath());
                SelfTestExpect(simulation_.State() == SimulationState::Edit, "the document checks must start in Edit");
            });

            add("documents: Open A makes A the document", 0, [this]
            {
                const PlayModeSelfTestState& test = *self_test_;
                std::string error;
                const bool opened = LoadSceneFromPath(test.document_a, error);
                SelfTestExpect(opened, "Open A must succeed: " + error);
                SelfTestExpect(document_.Path() == test.document_a, "a successful Open must make A the document");
                SelfTestExpect(scene_.EntityCount() == 6, "Open must load A's six entities");
                ExpectDocumentFilesUnchanged("after Open A");
            });

            // One frame passes before the next step so per-frame editor sync (the
            // sun follows its sliders) has settled before the scene is recorded.
            add("documents: New makes the document Untitled", 1, [this]
            {
                std::string error;
                SelfTestExpect(NewScene(error), "New Scene must succeed in Edit");
                SelfTestExpect(document_.IsUntitled() && document_.Path().empty(), "New must leave no active save path");
                SelfTestExpect(
                    scene_name_input_[0] == '\0',
                    "New must clear the file-name field so Save As does not suggest A");
            });

            add("documents: Save on Untitled opens Save As and cannot overwrite A", 2, [this]
            {
                PlayModeSelfTestState& test = *self_test_;
                test.checkpoint_json = CaptureSceneJson();
                HandleSaveSceneCommand(); // exactly what File > Save Scene runs
                SelfTestExpect(save_as_dialog_open_, "Save on an Untitled document must open the Save As prompt");
                SelfTestExpect(document_.IsUntitled(), "Save on an Untitled document must keep it Untitled");
                SelfTestExpectScene(test.checkpoint_json, "Save on an Untitled document must not change the scene");
                ExpectDocumentFilesUnchanged("after Open A -> New -> Save");
            });

            add("documents: cancelling Save As writes nothing", 1, [this]
            {
                const PlayModeSelfTestState& test = *self_test_;
                CancelSaveAsDialog(); // the prompt's Cancel button (and Escape)
                SelfTestExpect(!save_as_dialog_open_, "Cancel must close the Save As prompt");
                SelfTestExpect(document_.IsUntitled(), "Cancel must keep the document Untitled");
                SelfTestExpectScene(test.checkpoint_json, "Cancel must keep the scene");
                ExpectDocumentFilesUnchanged("after cancelling Save As");
            });

            add("documents: a failed Save As keeps the document Untitled", 1, [this]
            {
                const PlayModeSelfTestState& test = *self_test_;
                HandleSaveSceneCommand();
                SelfTestExpect(save_as_dialog_open_, "Save must reopen the Save As prompt");
                RAG_LOG_INFO("[self-test] expecting a failed Save As onto a directory (one [Error] line follows).");
                SelfTestExpect(!CompleteSaveAs(test.document_blocked), "Save As onto a directory must fail");
                SelfTestExpect(save_as_dialog_open_, "a failed Save As must keep the prompt open for a retry");
                SelfTestExpect(document_.IsUntitled(), "a failed Save As must keep the document Untitled");
                SelfTestExpectScene(test.checkpoint_json, "a failed Save As must keep the scene");
                SelfTestExpect(
                    std::filesystem::is_directory(test.document_blocked),
                    "a failed Save As must not replace what is at the target");
                ExpectDocumentFilesUnchanged("after a failed Save As while Untitled");
            });

            add("documents: Save As B makes B the document and leaves A untouched", 0, [this]
            {
                PlayModeSelfTestState& test = *self_test_;
                const bool saved = CompleteSaveAs(test.document_b);
                SelfTestExpect(saved, "Save As B must succeed: " + scene_status_);
                SelfTestExpect(!save_as_dialog_open_, "a successful Save As must close the prompt");
                SelfTestExpect(document_.Path() == test.document_b, "a successful Save As must make B the document");
                test.document_b_bytes = ReadFileBytes(test.document_b);
                SelfTestExpect(
                    test.document_b_bytes == test.checkpoint_json + "\n",
                    "B must hold exactly the Untitled scene");
                SelfTestExpect(
                    ListDocumentFolder() == std::vector<std::string>{"A.ragscene", "B.ragscene", "blocked.ragscene"},
                    "Save As B must add only B");
                test.document_listing = ListDocumentFolder();
                ExpectDocumentFilesUnchanged("after Save As B");
            });

            add("documents: Save after Save As rewrites B, not A", 0, [this]
            {
                PlayModeSelfTestState& test = *self_test_;
                if (rag::scene::TransformComponent* transform = scene_.GetTransform(camera_entity_))
                {
                    transform->local_position.x += 2.0f;
                    scene_.MarkTransformDirty(camera_entity_);
                }
                const std::string edited_json = CaptureSceneJson();
                SelfTestExpect(edited_json != test.checkpoint_json, "the camera edit must change the scene");
                HandleSaveSceneCommand();
                SelfTestExpect(!save_as_dialog_open_, "Save on a saved document must not open the prompt");
                SelfTestExpect(document_.Path() == test.document_b, "Save must keep B as the document");
                test.document_b_bytes = ReadFileBytes(test.document_b);
                SelfTestExpect(test.document_b_bytes == edited_json + "\n", "Save must rewrite B with the edited scene");
                test.checkpoint_json = edited_json;
                ExpectDocumentFilesUnchanged("after Save to B");
            });

            add("documents: a failed Save As keeps B and the scene", 0, [this]
            {
                const PlayModeSelfTestState& test = *self_test_;
                RAG_LOG_INFO("[self-test] expecting a failed Save As onto a directory (one [Error] line follows).");
                SelfTestExpect(!CompleteSaveAs(test.document_blocked), "Save As onto a directory must fail");
                SelfTestExpect(document_.Path() == test.document_b, "a failed Save As must keep B as the document");
                SelfTestExpectScene(test.checkpoint_json, "a failed Save As must keep the scene in memory");
                ExpectDocumentFilesUnchanged("after a failed Save As from B");
            });

            add("documents: a failed Load keeps the scene and B", 0, [this]
            {
                PlayModeSelfTestState& test = *self_test_;
                const std::filesystem::path corrupt = test.document_root / "corrupt.ragscene";
                {
                    std::ofstream stream(corrupt, std::ios::binary | std::ios::trunc);
                    stream << "{ this is not a scene";
                }
                test.document_listing = ListDocumentFolder();
                const std::size_t entities = scene_.EntityCount();
                const rag::u32 bodies = physics_.BodyCount();
                RAG_LOG_INFO("[self-test] expecting two failed Loads (two [Warning] lines follow).");
                for (const std::filesystem::path& bad : {corrupt, test.document_root / "missing.ragscene"})
                {
                    std::string error;
                    const std::string label = "'" + bad.filename().string() + "'";
                    SelfTestExpect(!LoadSceneFromPath(bad, error), "Load of " + label + " must fail");
                    SelfTestExpect(!error.empty(), "a failed Load of " + label + " must report why");
                    SelfTestExpect(document_.Path() == test.document_b, "a failed Load of " + label + " must keep B");
                    SelfTestExpectScene(test.checkpoint_json, "a failed Load of " + label + " must keep the scene");
                    SelfTestExpect(
                        scene_.EntityCount() == entities && physics_.BodyCount() == bodies,
                        "a failed Load of " + label + " must keep entities and bodies");
                }
                ExpectDocumentFilesUnchanged("after failed Loads");
            });

            add("documents: Play with B as the document", 0, [this]
            {
                RecordAuthoredBaseline();
                self_test_->document_path = document_.Path();
                RequestSimulationCommand(SimulationCommand::Play);
            });
            add("documents: Save is refused while Playing B; Stop", 0, [this]
            {
                const PlayModeSelfTestState& test = *self_test_;
                SelfTestExpect(simulation_.State() == SimulationState::Playing, "Play must enter Playing");
                HandleSaveSceneCommand();
                SelfTestExpect(!save_as_dialog_open_, "a refused Save must not open the prompt");
                SelfTestExpect(document_.Path() == test.document_b, "Playing must keep B as the document");
                ExpectDocumentFilesUnchanged("after Save while Playing B");
                RequestSimulationCommand(SimulationCommand::Stop);
            });
            add("documents: Stop keeps B as the document", 0, [this]
            {
                ExpectAuthoredSceneRestored("for the saved document B");
                ExpectDocumentFilesUnchanged("after Play/Stop with B");
            });

            add("documents: New for an Untitled run", 1, [this]
            {
                std::string error;
                SelfTestExpect(NewScene(error) && document_.IsUntitled(), "New must make the document Untitled");
            });
            add("documents: Play while Untitled", 0, [this]
            {
                RecordAuthoredBaseline();
                self_test_->document_path = document_.Path();
                SelfTestExpect(self_test_->document_path.empty(), "the recorded document must be Untitled");
                RequestSimulationCommand(SimulationCommand::Play);
            });
            add("documents: Save is refused while Playing Untitled; Stop", 0, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Playing, "Play must enter Playing");
                HandleSaveSceneCommand();
                SelfTestExpect(!save_as_dialog_open_, "a refused Save must not open the prompt while Playing");
                SelfTestExpect(document_.IsUntitled(), "Playing must keep the document Untitled");
                ExpectDocumentFilesUnchanged("after Save while Playing Untitled");
                RequestSimulationCommand(SimulationCommand::Stop);
            });
            add("documents: Stop keeps the document Untitled", 0, [this]
            {
                ExpectAuthoredSceneRestored("for an Untitled document");
                SelfTestExpect(document_.IsUntitled(), "Stop must keep the document Untitled");
                HandleSaveSceneCommand();
                SelfTestExpect(save_as_dialog_open_, "Save after Stop must still ask for a file name");
                CancelSaveAsDialog();
                ExpectDocumentFilesUnchanged("after Play/Stop while Untitled");
            });

            AddPlayerSelfTestSteps(add);
#endif
        }

#if defined(RAG_ENABLE_PHYSICS)
        void SetSelfTestPlayerIntent(rag::f32 forward, rag::f32 right, bool jump)
        {
            rag::runtime::PlayerIntent intent;
            intent.forward = forward;
            intent.right = right;
            intent.jump_pressed = jump;
            self_test_player_intent_ = intent;
        }

        [[nodiscard]] bool UiInputBlocked() const
        {
            return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) != 0 &&
                   (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoKeyboard) != 0;
        }

        [[nodiscard]] rag::math::Vec3 EditorCameraPosition() const
        {
            const rag::scene::TransformComponent* transform = scene_.GetTransform(camera_entity_);
            return transform != nullptr ? transform->local_position : rag::math::Vec3{};
        }

        // First-person player checks, on BuildPlayerTestScene (in memory; the
        // document stays Untitled and nothing is saved). Movement is driven by
        // injected intent through the same capture / sample / substep path live
        // input uses; capture here is logical only (OsCursorCaptureAllowed), so
        // the real cursor of whoever runs the test is never hidden or confined.
        template <typename Add>
        void AddPlayerSelfTestSteps(const Add& add)
        {
            using rag::runtime::SimulationState;
            const rag::math::Vec3 start_feet{0.0f, -0.5f, 0.0f};
            const rag::f32 wall_face_z = -5.75f;

            add("player: author a scene with a Player Start", 1, [this]
            {
                ApplyScene(BuildPlayerTestScene());
                const rag::scene::EntityId start = FindEntityByName("Player Start");
                SelfTestExpect(scene_.PlayerStartCount() == 1 && scene_.FirstPlayerStart() == start,
                    "the test scene must hold exactly one Player Start");
                SelfTestExpect(std::string_view(CategoryFor(start)) == "Player Start",
                    "the Hierarchy must list it as a Player Start");
                SelfTestExpect(AddPlayerStartBlockedReason() != nullptr && !AddPlayerStart() &&
                        scene_.PlayerStartCount() == 1,
                    "Add Player Start must be refused while one exists");
            });

            add("player: Play", 0, [this]
            {
                RecordAuthoredBaseline();
                self_test_->document_path = document_.Path();
                self_test_->checkpoint_position = EditorCameraPosition();
                SelfTestExpect(!HasPlayer() && physics_.CharacterCount() == 0, "Edit must have no player");
                RequestSimulationCommand(SimulationCommand::Play);
            });

            add("player: Play spawns one player at the start without taking the mouse", 1, [this, start_feet]
            {
                const PlayModeSelfTestState& test = *self_test_;
                if (!SelfTestExpect(simulation_.State() == SimulationState::Playing && HasPlayer(),
                        "Play must spawn a player from the Player Start"))
                {
                    return;
                }
                SelfTestExpect(physics_.CharacterCount() == 1, "exactly one character must exist");
                SelfTestExpect(physics_.BodyCount() == test.authored_bodies, "the player must not add rigid bodies");
                SelfTestExpect(rag::math::Length(player_->controller.FeetPosition() - start_feet) < 0.05f,
                    "the player must stand at the start");
                SelfTestExpect(player_->controller.IsGrounded(), "the player must start on the floor");
                SelfTestExpect(!player_input_captured_ && !CrosshairVisible() && PlayerViewActive(),
                    "Play must show the player view but wait for a click before taking the mouse");
                SelfTestExpect(Vec3Equal(EditorCameraPosition(), test.checkpoint_position),
                    "Play must leave the editor camera where it was");
            });

            add("player: the view is the player's eyes; capture and walk", 0, [this]
            {
                SelfTestExpect(Vec3Near(render_world_.camera.position, player_->controller.EyePosition()),
                    "the rendered camera must be the player's eyes while Playing");
                CapturePlayerInput();
                SelfTestExpect(player_input_captured_ && CrosshairVisible() && UiInputBlocked(),
                    "capture must show the crosshair and block editor panel input");
                SetSelfTestPlayerIntent(1.0f, 0.0f, false); // hold W
            });

            // Keep pushing into the wall for a while after reaching it.
            add("player: reached the wall", 30, [] {}, [this, wall_face_z]
            {
                return player_.has_value() &&
                    player_->controller.FeetPosition().z - player_->capsule_radius < wall_face_z + 0.1f;
            });

            add("player: walking forward stops at the wall; press jump", 0, [this, wall_face_z]
            {
                const rag::scene::PlayerStartComponent& config = player_->config;
                const rag::math::Vec3 feet = player_->controller.FeetPosition();
                SelfTestExpect(feet.z < -4.0f && std::abs(feet.x) < 0.01f, "W must walk straight toward -Z (yaw 0)");
                SelfTestExpect(feet.z - config.capsule_radius >= wall_face_z - 0.03f,
                    "the player must stop at the wall, not pass into it");
                SelfTestExpect(physics_.IsCharacterSpaceFree(config.capsule_radius, config.capsule_height, feet),
                    "the capsule must never end a frame inside the wall or floor");
                SelfTestExpect(std::abs(feet.y + 0.5f) < 0.05f && player_->controller.IsGrounded(),
                    "walking must keep the player on the floor");
                SelfTestExpect(Vec3Equal(EditorCameraPosition(), self_test_->checkpoint_position),
                    "player input must never move the editor camera");
                SetSelfTestPlayerIntent(0.0f, 0.0f, true); // one press of Space
            });

            add("player: one press is one jump", 5, [this]
            {
                SelfTestExpect(player_->controller.JumpCount() == 1 && !player_->controller.IsGrounded(),
                    "a press on the ground must jump once");
                SetSelfTestPlayerIntent(0.0f, 0.0f, false); // released
            });

            add("player: lands; Escape releases the mouse without quitting", 0, [this]
            {
                SelfTestExpect(player_->controller.JumpCount() == 1, "no further jump without a new press");
                HandleEscape();
                SelfTestExpect(!player_input_captured_ && !CrosshairVisible() && !UiInputBlocked(),
                    "Escape must release the mouse and the editor panels");
                SelfTestExpect(!requested_close_, "Escape that releases the mouse must not also quit");
                SelfTestExpect(PlayerViewActive(), "releasing the mouse must keep Playing in the player view");
            }, [this] { return player_.has_value() && player_->controller.IsGrounded(); });

            add("player: focus loss releases the mouse", 0, [this]
            {
                CapturePlayerInput();
                SelfTestExpect(player_input_captured_, "a second capture must work");
                OnEvent(rag::core::Event::WindowFocus(false));
                SelfTestExpect(!player_input_captured_ && !UiInputBlocked(), "losing focus must release the mouse");
            });

            add("player: tune live, then fall off the level", 0, [this]
            {
                CapturePlayerInput();
                SetSelfTestPlayerIntent(0.0f, 0.0f, false);
                scene_.GetPlayerStart(FindEntityByName("Player Start"))->move_speed = 7.0f; // runtime edit
                self_test_->checkpoint_json = CaptureSceneJson();
                self_test_->checkpoint_position = SimulatedPosition(FindEntityByName("Crate"));
                const rag::math::Vec3 off_level{25.0f, -0.5f, 0.0f};
                physics_.SetCharacterPosition(player_->character, off_level);
                player_->controller.Reset(off_level, 0.0f, rag::runtime::PlayerGround::Unsupported);
            });

            add("player: the fall reset returns only the player to the start", 0, [this, start_feet]
            {
                const PlayModeSelfTestState& test = *self_test_;
                SelfTestExpect(player_->fall_resets == 1, "falling below Fall Reset Y must reset once");
                SelfTestExpect(rag::math::Length(player_->controller.FeetPosition() - start_feet) < 0.05f,
                    "the reset must return the player to the start");
                SelfTestExpect(rag::math::Length(player_->controller.Velocity()) < 0.5f,
                    "the reset must clear the fall velocity");
                SelfTestExpect(simulation_.State() == SimulationState::Playing &&
                        scene_.EntityCount() == test.authored_entities,
                    "the reset must not reset or stop the scene");
                SelfTestExpect(rag::math::Length(SimulatedPosition(FindEntityByName("Crate")) - test.checkpoint_position) < 0.01f,
                    "the reset must leave other bodies where they are");
                SelfTestExpect(player_->controller.Settings().move_speed == 7.0f,
                    "a runtime edit of the Player Start must tune the running player");
                RequestSimulationCommand(SimulationCommand::Pause);
            }, [this] { return player_.has_value() && player_->fall_resets > 0; });

            add("player: Pause hands the view to the editor camera at the player's eyes", 10, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Paused && !PlayerViewActive(),
                    "Pause must end the player view");
                SelfTestExpect(!player_input_captured_ && !UiInputBlocked(), "Pause must release the mouse");
                SelfTestExpect(Vec3Near(EditorCameraPosition(), player_->controller.EyePosition()),
                    "the editor camera must start inspection at the player's eyes");
                self_test_->checkpoint_position = player_->controller.FeetPosition();
                self_test_->checkpoint_yaw = player_->controller.Yaw();
                // Editor navigation while paused, and stale intent: neither may
                // move the player.
                SetSelfTestPlayerIntent(1.0f, 1.0f, true);
                if (rag::scene::TransformComponent* camera = scene_.GetTransform(camera_entity_))
                {
                    camera->local_position.x += 3.0f;
                    scene_.MarkTransformDirty(camera_entity_);
                }
            });

            add("player: Paused frames move nothing; Step", 0, [this]
            {
                SelfTestExpect(Vec3Equal(player_->controller.FeetPosition(), self_test_->checkpoint_position),
                    "the player must not move while Paused, whatever the editor camera does");
                SelfTestExpect(Vec3Near(render_world_.camera.position, EditorCameraPosition()),
                    "Paused must render the editor camera");
                self_test_->checkpoint_count = player_->controller.StepCount();
                RequestSimulationCommand(SimulationCommand::Step);
            });

            add("player: Step advances the player one tick without live input", 0, [this]
            {
                const rag::math::Vec3 feet = player_->controller.FeetPosition();
                SelfTestExpect(player_->controller.StepCount() == self_test_->checkpoint_count + 1,
                    "Step must advance the player exactly one substep");
                SelfTestExpect(std::abs(feet.x - self_test_->checkpoint_position.x) < 0.0001f &&
                        std::abs(feet.z - self_test_->checkpoint_position.z) < 0.0001f &&
                        player_->controller.JumpCount() == 1,
                    "Step must not consume movement or jump input");
                SelfTestExpect(player_->controller.Yaw() == self_test_->checkpoint_yaw,
                    "Step must not consume mouse look");
                RequestSimulationCommand(SimulationCommand::Resume);
            });

            add("player: Resume returns to the player view at the same pose", 0, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Playing && PlayerViewActive(),
                    "Resume must return to the player view");
                SelfTestExpect(!player_input_captured_, "Resume must wait for a click before taking the mouse");
                SelfTestExpect(rag::math::Length(player_->controller.FeetPosition() - self_test_->checkpoint_position) < 0.01f,
                    "editor navigation while paused must not have moved the player");
                SelfTestExpect(Vec3Near(render_world_.camera.position, player_->controller.EyePosition()),
                    "the rendered camera must be the player's eyes again");
                CapturePlayerInput();
                RequestSimulationCommand(SimulationCommand::Stop);
            });

            add("player: Stop removes the player and restores the authored scene", 0, [this]
            {
                ExpectAuthoredSceneRestored("after a player run");
                SelfTestExpect(!HasPlayer() && physics_.CharacterCount() == 0, "Stop must destroy the player");
                SelfTestExpect(!player_input_captured_ && !UiInputBlocked(), "Stop must release the mouse");
                SelfTestExpect(scene_.GetPlayerStart(FindEntityByName("Player Start"))->move_speed == 4.5f,
                    "Stop must revert the runtime Player Start edit");
                self_test_player_intent_.reset();
            });

            for (int cycle = 1; cycle <= 3; ++cycle)
            {
                const std::string label = " (cycle " + std::to_string(cycle) + ")";
                add("player: Play again" + label, 2, [this]
                {
                    RequestSimulationCommand(SimulationCommand::Play);
                });
                add("player: one player per run; Stop" + label, 0, [this, label]
                {
                    SelfTestExpect(HasPlayer() && physics_.CharacterCount() == 1, "each run must have one player" + label);
                    RequestSimulationCommand(SimulationCommand::Stop);
                });
                add("player: nothing accumulates" + label, 0, [this, label]
                {
                    ExpectAuthoredSceneRestored("after player cycle" + label);
                    SelfTestExpect(physics_.CharacterCount() == 0, "no character may survive Stop" + label);
                });
            }

            add("player: a start inside a low block is lifted onto it", 1, [this]
            {
                MoveEntityLikeInspector(FindEntityByName("Player Start"), rag::math::Vec3{6.0f, -0.5f, 0.0f});
                RequestSimulationCommand(SimulationCommand::Play);
            });
            add("player: the lifted spawn is clear; Stop", 0, [this]
            {
                if (SelfTestExpect(HasPlayer(), "a start inside a 0.5 m block must still spawn a player"))
                {
                    const rag::math::Vec3 feet = player_->controller.FeetPosition();
                    SelfTestExpect(feet.y > -0.05f && feet.y < 0.1f, "the spawn must be lifted onto the block");
                    SelfTestExpect(physics_.IsCharacterSpaceFree(
                            player_->capsule_radius, player_->capsule_height, feet),
                        "the lifted spawn must not start in penetration");
                }
                RequestSimulationCommand(SimulationCommand::Stop);
            });

            add("player: a buried start", 1, [this]
            {
                MoveEntityLikeInspector(FindEntityByName("Player Start"), rag::math::Vec3{-6.0f, -0.5f, 0.0f});
                RAG_LOG_INFO("[self-test] expecting a buried Player Start (one [Warning] line follows).");
                RequestSimulationCommand(SimulationCommand::Play);
            });
            add("player: a buried start plays with the free camera; Stop", 0, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Playing,
                    "a buried start must not refuse Play");
                SelfTestExpect(!HasPlayer() && physics_.CharacterCount() == 0 && !PlayerViewActive(),
                    "a buried start must not spawn a player inside geometry");
                SelfTestExpect(!player_status_.empty(), "a buried start must be reported");
                RequestSimulationCommand(SimulationCommand::Stop);
            });

            add("player: with two starts the first in the Hierarchy is used", 1, [this, start_feet]
            {
                MoveEntityLikeInspector(FindEntityByName("Player Start"), start_feet);
                const rag::scene::EntityId second = scene_.CreateEntity();
                NameEntity(second, "Second Start");
                scene_.AddTransform(second).local_position = rag::math::Vec3{10.0f, -0.5f, 10.0f};
                scene_.AddPlayerStart(second);
                SelfTestExpect(scene_.FirstPlayerStart() == FindEntityByName("Player Start") &&
                        HierarchyLabel(second).find("not used") != std::string::npos,
                    "the later start must be reported as not used");
                RAG_LOG_INFO("[self-test] expecting a two-Player-Start warning (one [Warning] line follows).");
                RequestSimulationCommand(SimulationCommand::Play);
            });
            add("player: the first start spawned the player; Stop", 0, [this, start_feet]
            {
                if (SelfTestExpect(HasPlayer(), "two starts must still spawn one player"))
                {
                    SelfTestExpect(player_->start_entity == FindEntityByName("Player Start") &&
                            rag::math::Length(player_->controller.FeetPosition() - start_feet) < 0.05f,
                        "the first start in the Hierarchy must be used");
                }
                RequestSimulationCommand(SimulationCommand::Stop);
            });

            add("player: without a Player Start, Play keeps the free camera", 1, [this]
            {
                SelfTestExpect(DeleteEntity(FindEntityByName("Second Start")) &&
                        DeleteEntity(FindEntityByName("Player Start")) && scene_.PlayerStartCount() == 0,
                    "deleting the starts must leave none");
                RequestSimulationCommand(SimulationCommand::Play);
            });
            add("player: the free-camera run; Stop", 1, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Playing && !HasPlayer() &&
                        player_status_.empty() && physics_.CharacterCount() == 0 && !PlayerViewActive(),
                    "a scene without a Player Start must play without a player and without a complaint");
                SelfTestExpect(Vec3Near(render_world_.camera.position, EditorCameraPosition()),
                    "it must render the editor camera");
                RequestSimulationCommand(SimulationCommand::Stop);
            });

            add("player: Add Player Start places one ahead of the editor view; it round-trips", 0, [this]
            {
                SelfTestExpect(simulation_.State() == SimulationState::Edit && AddPlayerStartBlockedReason() == nullptr,
                    "Add Player Start must be available in Edit without a start");
                SelfTestExpect(AddPlayerStart() && scene_.PlayerStartCount() == 1, "Add Player Start must create one");
                const rag::scene::EntityId added = scene_.FirstPlayerStart();
                SelfTestExpect(editor_.Selected() == added && NameOrFallback(added) == "Player Start",
                    "the new Player Start must be named and selected");
                const rag::math::Vec3 offset = PlayerStartFeet(added) - EditorCameraPosition();
                SelfTestExpect(std::abs(std::sqrt((offset.x * offset.x) + (offset.z * offset.z)) - 4.0f) < 0.01f,
                    "it must be placed 4 m ahead of the editor view");
                SelfTestExpect(!AddPlayerStart(), "a second Add Player Start must be refused");

                const std::string json = CaptureSceneJson();
                SelfTestExpect(json.find("\"playerStart\"") != std::string::npos, "the start must be saved");
                rag::scene::SerializedScene reparsed;
                std::string error;
                if (SelfTestExpect(rag::scene::ParseSceneFromJson(json, reparsed, error), "the saved scene must parse"))
                {
                    ApplyScene(reparsed);
                    SelfTestExpectScene(json, "the Player Start must survive a save/load round-trip");
                }
            });
        }
#endif
#endif

        void UpdateCamera(
            const rag::core::FrameTime& frame_time,
            const rag::core::InputState& input,
            bool ui_wants_mouse,
            bool ui_wants_keyboard)
        {
            rag::scene::TransformComponent* transform = scene_.GetTransform(camera_entity_);
            if (transform == nullptr)
            {
                return;
            }

            bool transform_changed = false;
            const rag::i32 mouse_x = input.MouseX();
            const rag::i32 mouse_y = input.MouseY();
            // When ImGui owns the mouse, don't drag the view; previous_mouse_* are
            // still refreshed below so the camera doesn't jump on the next look.
            const bool mouse_look =
                !ui_wants_mouse && input.IsMouseButtonDown(rag::core::MouseButton::Right);

            if (mouse_look)
            {
                if (mouse_look_active_)
                {
                    const rag::i32 delta_x = mouse_x - previous_mouse_x_;
                    const rag::i32 delta_y = mouse_y - previous_mouse_y_;

                    camera_yaw_radians_ -= static_cast<rag::f32>(delta_x) * MouseSensitivity;
                    camera_pitch_radians_ -= static_cast<rag::f32>(delta_y) * MouseSensitivity;
                    camera_pitch_radians_ = std::clamp(
                        camera_pitch_radians_,
                        -MaximumPitchRadians,
                        MaximumPitchRadians);
                    transform_changed = delta_x != 0 || delta_y != 0;
                }

                mouse_look_active_ = true;
            }
            else
            {
                mouse_look_active_ = false;
            }

            previous_mouse_x_ = mouse_x;
            previous_mouse_y_ = mouse_y;

            const rag::f32 cos_pitch = std::cos(camera_pitch_radians_);
            const rag::math::Vec3 forward = rag::math::Normalize(rag::math::Vec3{
                -std::sin(camera_yaw_radians_) * cos_pitch,
                std::sin(camera_pitch_radians_),
                -std::cos(camera_yaw_radians_) * cos_pitch});
            const rag::math::Vec3 right = rag::math::Normalize(rag::math::Vec3{
                std::cos(camera_yaw_radians_),
                0.0f,
                -std::sin(camera_yaw_radians_)});
            constexpr rag::math::Vec3 WorldUp{0.0f, 1.0f, 0.0f};

            rag::math::Vec3 movement{};
            // Skip WASD/up/down while ImGui is using the keyboard (e.g. typing in
            // a field) so editing the UI never drives the camera.
            if (!ui_wants_keyboard)
            {
                if (input.IsKeyDown(rag::core::KeyCode::W))
                {
                    movement = movement + forward;
                }
                if (input.IsKeyDown(rag::core::KeyCode::S))
                {
                    movement = movement - forward;
                }
                if (input.IsKeyDown(rag::core::KeyCode::D))
                {
                    movement = movement + right;
                }
                if (input.IsKeyDown(rag::core::KeyCode::A))
                {
                    movement = movement - right;
                }
                if (input.IsKeyDown(rag::core::KeyCode::Space) ||
                    input.IsKeyDown(rag::core::KeyCode::E))
                {
                    movement = movement + WorldUp;
                }
                if (input.IsKeyDown(rag::core::KeyCode::LeftControl) ||
                    input.IsKeyDown(rag::core::KeyCode::Q))
                {
                    movement = movement - WorldUp;
                }
            }

            if (rag::math::Length(movement) > 0.000001f)
            {
                const rag::f32 speed =
                    input.IsKeyDown(rag::core::KeyCode::LeftShift)
                        ? MoveSpeed * SprintMultiplier
                        : MoveSpeed;
                const rag::f32 distance =
                    speed * static_cast<rag::f32>(frame_time.delta_seconds);
                transform->local_position =
                    transform->local_position + (rag::math::Normalize(movement) * distance);
                transform_changed = true;
            }

            if (transform_changed)
            {
                transform->local_rotation_radians =
                    rag::math::Vec3{camera_pitch_radians_, camera_yaw_radians_, 0.0f};
                scene_.MarkTransformDirty(camera_entity_);
            }
        }

        rag::platform::IWindow* window_ = nullptr;
        rag::scene::Scene scene_;
        rag::renderer::RenderWorld render_world_;
        rag::scene::EntityId camera_entity_{};

        // The file the scene was opened from / last saved to ("Save Scene"
        // rewrites it), or Untitled after New Scene or a built-in startup scene.
        // Only Load / Save As / New change it, and only in Edit.
        rag::runtime::SceneDocument document_;

#if defined(RAG_EDITOR)
        // Editor (developer-only) state. editor_mode_ defaults on for the sandbox
        // and is flipped at runtime with F1; with it off the engine renders only
        // the game. The panel flags drive the dockspace/panel UI, and editor_ owns
        // the shared selection + display names future panels (inspector, gizmos) use.
        bool editor_mode_ = true;
        bool show_hierarchy_panel_ = true;
        bool show_inspector_panel_ = true;
        bool show_debug_panel_ = true;
        bool show_assets_panel_ = true;
        bool dock_layout_initialized_ = false;
        rag::editor::EditorContext editor_;

        // Asset Browser state (bottom panel): the on-disk file list + click
        // selection. spawn_counter_ makes each spawned entity's name unique.
        rag::editor::AssetBrowser asset_browser_;
        rag::u32 spawn_counter_ = 0;

        // File-menu state: the scene-name text field (shared with the Save As
        // prompt), the last save/load status line shown under the menu items, and
        // whether the Save As prompt is up.
        char scene_name_input_[128] = "default";
        std::string scene_status_;
        bool save_as_dialog_open_ = false;

        // Transform-gizmo state. gizmo_operation_ is the active mode (W/E/R or the
        // toolbar); gizmo_mode_ toggles local/world axes. gizmo_matrix_ is the pose
        // ImGuizmo manipulates (owned by ImGuizmo during a drag, synced to the object
        // otherwise). gizmo_using_/gizmo_over_ cache last frame's ImGuizmo state so
        // click-to-select can defer to a grabbed/hovered handle.
        ImGuizmo::OPERATION gizmo_operation_ = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE gizmo_mode_ = ImGuizmo::WORLD;
        rag::math::Mat4 gizmo_matrix_ = rag::math::Identity();
        bool gizmo_using_ = false;
        bool gizmo_over_ = false;

        // Entity the Delete key / Inspector button asked to delete, applied at the
        // next safe point.
        rag::scene::EntityId pending_delete_entity_{};

        // "Add Player Start" was clicked; applied at the next safe point.
        bool pending_add_player_start_ = false;

        // Present only for --play-mode-self-test.
        std::optional<PlayModeSelfTestState> self_test_;

        // Self-test only: the intent fed to a captured player instead of live
        // input (which the self-test ignores).
        std::optional<rag::runtime::PlayerIntent> self_test_player_intent_;
#endif

        // Directional-light state owned by the editor panel and pushed onto the
        // scene each frame by SyncLightFromUi().
        rag::scene::EntityId light_entity_{};
        rag::math::Vec3 light_euler_{};
        rag::math::Vec3 light_color_{1.0f, 0.92f, 0.78f};
        rag::f32 light_intensity_ = 1.25f;

#if defined(RAG_ENABLE_PHYSICS)
        rag::physics::PhysicsWorld physics_;
        std::vector<PhysicsBody> physics_bodies_;
        bool physics_ready_ = false;
        rag::f32 physics_gravity_y_ = -9.81f;
#endif

        static constexpr rag::f32 MoveSpeed = 4.5f;
        static constexpr rag::f32 SprintMultiplier = 4.0f;
        static constexpr rag::f32 MouseSensitivity = 0.0025f;
        static constexpr rag::f32 MaximumPitchRadians = (rag::math::Pi * 0.5f) - 0.01f;
        // A small component on every axis makes each fixed-axis Euler control
        // affect the light direction independently, including Z rotation.
        static constexpr rag::math::Vec3 BaseDirectionalLightDirection{
            0.25f,
            -0.85f,
            -0.46f};
        rag::f32 camera_yaw_radians_ = 0.0f;
        rag::f32 camera_pitch_radians_ = 0.0f;
        rag::f64 time_since_title_update_ = 0.0;
        rag::i32 previous_mouse_x_ = 0;
        rag::i32 previous_mouse_y_ = 0;
        bool mouse_look_active_ = false;
        bool smoke_test_ = false;
        bool play_mode_self_test_requested_ = false;
        bool smoke_test_passed_ = false;
        rag::u32 smoke_test_frame_ = 0;
        bool requested_close_ = false;

        // Edit / Playing / Paused lifecycle (owns the in-memory pre-play
        // snapshot), the request waiting for the next safe point, and the last
        // failed transition's message (shown in the toolbar and Debug panel).
        rag::runtime::SimulationController<PlaySnapshot> simulation_{
            SimulationTickSeconds, MaxSimulationFrameDeltaSeconds};
        std::optional<SimulationCommand> pending_simulation_command_;
        std::string simulation_error_;

        // The first-person player of the current run (none in Edit, or when the
        // scene has no usable Player Start). player_input_captured_ means the
        // mouse belongs to the player (cursor hidden and confined, editor panels
        // ignore input): set by a click on the game view while Playing, cleared
        // by Escape, Pause, Stop, focus loss and scene replacement.
        // player_intent_ is this frame's sampled input; player_status_ says why
        // a run with a Player Start has no player.
#if defined(RAG_ENABLE_PHYSICS)
        std::optional<PlayerRuntime> player_;
#endif
        bool player_input_captured_ = false;
        rag::runtime::PlayerIntent player_intent_{};
        std::string player_status_;

#if defined(RAG_ENABLE_VULKAN)
        rag::renderer::RendererPtr renderer_;
#endif
    };
}

int main(int argc, char** argv)
{
    try
    {
        LogBuildIdentity(argc, argv);
        bool smoke_test = false;
        bool play_mode_self_test = false;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index] != nullptr ? argv[index] : "");
            if (argument == "--smoke-test")
            {
                smoke_test = true;
            }
            else if (argument == "--play-mode-self-test")
            {
                play_mode_self_test = true;
            }
        }
        SandboxClient client(smoke_test, play_mode_self_test);

        rag::core::ApplicationDesc desc{};
        desc.name = "RAG Engine Sandbox";
        desc.window_width = 1280;
        desc.window_height = 720;

        rag::core::Application app(desc, client);
        client.InitializeRenderer(app.Window());
        const int run_result = app.Run();
        return run_result != 0 ? run_result : client.ExitCode();
    }
    catch (const std::exception& exception)
    {
        RAG_LOG_FATAL("Fatal error: ", exception.what());
        return 1;
    }
}
