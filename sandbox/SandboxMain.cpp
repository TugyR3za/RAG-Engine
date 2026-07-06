#include "Rag/Core/Application.h"
#include "Rag/Core/Event.h"
#include "Rag/Core/Log.h"
#include "Rag/Core/Math.h"
#include "Rag/Scene/Scene.h"

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
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    class SandboxClient final : public rag::core::IApplicationClient
    {
    public:
        explicit SandboxClient(bool smoke_test)
            : smoke_test_(smoke_test)
        {
        }

        void OnStart() override
        {
            RAG_LOG_INFO("RAG Engine Vulkan scene sandbox started. Press Escape to quit, F11 to toggle fullscreen.");
            RAG_LOG_INFO(
                "Free-fly camera: WASD move, Space/E up, Left Ctrl/Q down, "
                "hold Right Mouse to look, Left Shift to sprint.");
            InitializePhysics();
            BuildScene();

            if (smoke_test_)
            {
#if defined(RAG_ENABLE_VULKAN)
                RAG_LOG_INFO("Smoke test requested; rendering one Vulkan frame before shutdown.");
#else
                RAG_LOG_INFO("Smoke test requested; closing after startup.");
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
                requested_close_ = true;
            }

            if (input.WasKeyPressed(rag::core::KeyCode::F11) && window_ != nullptr)
            {
                window_->SetFullscreen(!window_->IsFullscreen());
            }

#if defined(RAG_EDITOR)
            // F1 flips between "editing" (dockspace + panels visible) and "playing"
            // (bare game, no UI) at runtime without restarting.
            if (input.WasKeyPressed(rag::core::KeyCode::F1))
            {
                editor_mode_ = !editor_mode_;
                RAG_LOG_INFO("Editor mode ", editor_mode_ ? "ON" : "OFF");
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

#if defined(RAG_EDITOR)
            // Gizmo mode hotkeys W/E/R (translate/rotate/scale). These letters are
            // also free-fly movement keys, so only switch modes when the camera is
            // NOT being flown (right mouse up) and ImGui is not typing, leaving the
            // fly controls untouched. The gizmo toolbar buttons work regardless.
            if (editor_mode_ && !ui_wants_keyboard &&
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

            UpdateCamera(frame_time, input, ui_wants_mouse, ui_wants_keyboard);
            StepPhysics(frame_time);
            SyncLightFromUi();
            scene_.Update();
            scene_.ExtractRenderWorld(render_world_);

#if defined(RAG_EDITOR)
            // Resolve a viewport left-click into a selection now that the camera and
            // world transforms for this frame are up to date. Editor mode only.
            if (editor_mode_)
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
                // Only hand the renderer a UI builder while editor mode is on; when
                // it is off the overlay frame stays empty and the renderer draws the
                // game alone. (RAG_EDITOR implies RAG_ENABLE_IMGUI.)
                if (editor_mode_)
                {
                    render_context.on_build_ui = [this]() { BuildEditorUi(); };
                    // Highlight the selected object in the 3D view with a world-space
                    // bounding-box outline that tracks it as physics moves it.
                    render_context.selection_highlight = ComputeSelectionHighlight();
                }
#endif
                renderer_->RenderFrame(render_context);
            }
#endif

            if (smoke_test_)
            {
                requested_close_ = true;
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
        }

    private:
#if defined(RAG_ENABLE_PHYSICS)
        // Links a dynamic Jolt body to the scene entity it drives, remembering
        // its spawn position so the editor's reset button can drop it again.
        struct PhysicsBody
        {
            rag::scene::EntityId entity{};
            rag::physics::BodyHandle body{};
            rag::math::Vec3 start_position{};
            rag::math::Vec3 synchronized_scale{1.0f, 1.0f, 1.0f};
        };

        void InitializePhysics()
        {
            rag::physics::PhysicsWorldDesc desc{};
            desc.gravity = rag::math::Vec3{0.0f, -9.81f, 0.0f};
            desc.fixed_timestep_seconds = 1.0f / 60.0f;
            desc.max_frame_delta_seconds = 0.033f;

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

        void BuildScene()
        {
            constexpr rag::u32 RingCubeCount = 8;
            constexpr rag::f32 RingRadius = 4.0f;
            constexpr rag::f32 CenterCubeScale = 1.5f;

            // A unit cube spans +/-0.5. Keep this collider base unscaled; the
            // transform scale is applied through PhysicsWorld before stepping.
            constexpr rag::math::Vec3 CubeBaseHalfExtents{0.5f, 0.5f, 0.5f};
            constexpr rag::f32 SphereRadius = 1.0f; // models/sphere.obj is a unit sphere.

            // Dynamic bodies start elevated so they visibly fall and settle on
            // the floor at startup. Tweak these to change the drop heights.
            constexpr rag::f32 RingCubeStartHeight = 7.0f;
            constexpr rag::f32 RingCubeStartStagger = 0.6f; // raises each successive cube
            constexpr rag::f32 CenterCubeStartHeight = 12.0f;
            constexpr rag::f32 SphereStartHeight = 9.0f;

            // The static ground collider matches the visual ground plane: a unit
            // cube mesh scaled to (40, 0.2, 40) centred at y=-0.6, giving a top
            // surface at y=-0.5.
            constexpr rag::math::Vec3 GroundCenter{0.0f, -0.6f, 0.0f};
            constexpr rag::math::Vec3 GroundScale{40.0f, 0.2f, 40.0f};
            constexpr rag::math::Vec3 GroundHalfExtents{20.0f, 0.1f, 20.0f};

            // Duck.glb's root node applies a 0.01 scale. Its geometry starts
            // roughly 0.1 units above local Y=0, so this rests it on the floor.
            // The duck is left static (not simulated) to keep things simple.
            constexpr rag::f32 DuckGroundOffset = -0.6f;

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
                SpawnDynamicBox(
                    entity,
                    start_position,
                    CubeBaseHalfExtents);
            }

            const rag::math::Vec3 center_start{0.0f, CenterCubeStartHeight, 0.0f};
            const rag::scene::EntityId center_cube = scene_.CreateEntity();
            NameEntity(center_cube, "Center Cube");
            rag::scene::TransformComponent& center_transform = scene_.AddTransform(center_cube);
            center_transform.local_position = center_start;
            center_transform.local_scale =
                rag::math::Vec3{CenterCubeScale, CenterCubeScale, CenterCubeScale};
            scene_.AddRenderable(center_cube).mesh = CubeMeshHandle;
            SpawnDynamicBox(
                center_cube,
                center_start,
                CubeBaseHalfExtents);

            const rag::scene::EntityId ground = scene_.CreateEntity();
            NameEntity(ground, "Ground");
            rag::scene::TransformComponent& ground_transform = scene_.AddTransform(ground);
            ground_transform.local_position = GroundCenter;
            ground_transform.local_scale = GroundScale;
            scene_.AddRenderable(ground).mesh = GroundMeshHandle;
            SpawnStaticBox(GroundCenter, GroundHalfExtents);

            // The OBJ sphere drops onto open floor off to the side of the ring so
            // its shadow stays clearly visible as it falls and lands.
            const rag::math::Vec3 sphere_start{6.5f, SphereStartHeight, 0.0f};
            const rag::scene::EntityId sphere = scene_.CreateEntity();
            NameEntity(sphere, "Sphere");
            scene_.AddTransform(sphere).local_position = sphere_start;
            rag::scene::RenderableComponent& sphere_renderable = scene_.AddRenderable(sphere);
            sphere_renderable.mesh = SphereMeshHandle;
            sphere_renderable.local_bounds.extents =
                rag::math::Vec3{SphereRadius, SphereRadius, SphereRadius};
            SpawnDynamicSphere(sphere, sphere_start, SphereRadius);

            const rag::scene::EntityId duck = scene_.CreateEntity();
            NameEntity(duck, "Duck");
            rag::scene::TransformComponent& duck_transform = scene_.AddTransform(duck);
            duck_transform.local_position =
                rag::math::Vec3{-6.5f, DuckGroundOffset, 0.0f};
            rag::scene::RenderableComponent& duck_renderable = scene_.AddRenderable(duck);
            duck_renderable.mesh = DuckMeshHandle;
            duck_renderable.local_bounds.extents = rag::math::Vec3{1.0f, 1.0f, 1.0f};

            camera_entity_ = scene_.CreateEntity();
            NameEntity(camera_entity_, "Camera");
            rag::scene::TransformComponent& camera_transform = scene_.AddTransform(camera_entity_);
            camera_transform.local_position = rag::math::Vec3{0.0f, 3.0f, 9.0f};
            camera_yaw_radians_ = 0.0f;
            camera_pitch_radians_ = -0.3f;
            camera_transform.local_rotation_radians =
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

            // The directional light is kept editable from the debug panel: its
            // rotation, color and intensity live in members the UI mutates and
            // SyncLightFromUi() pushes onto the scene each frame.
            light_entity_ = scene_.CreateEntity();
            NameEntity(light_entity_, "Sun");
            scene_.AddTransform(light_entity_);

            rag::scene::LightComponent& light = scene_.AddLight(light_entity_);
            light.type = rag::renderer::RenderLightType::Directional;
            light.local_direction = rag::math::Normalize(BaseDirectionalLightDirection);
            light.color = light_color_;
            light.intensity = light_intensity_;

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
        void SpawnDynamicBox(
            rag::scene::EntityId entity,
            const rag::math::Vec3& position,
            const rag::math::Vec3& half_extents)
        {
            if (!physics_ready_)
            {
                return;
            }

            const rag::physics::BodyHandle body = physics_.CreateDynamicBox(position, half_extents);
            if (!body.IsValid())
            {
                return;
            }

            physics_bodies_.push_back(PhysicsBody{entity, body, position});
            if (rag::scene::TransformComponent* transform = scene_.GetTransform(entity))
            {
                transform->use_quaternion_rotation = true;
            }
        }

        void SpawnDynamicSphere(
            rag::scene::EntityId entity,
            const rag::math::Vec3& position,
            rag::f32 radius)
        {
            if (!physics_ready_)
            {
                return;
            }

            const rag::physics::BodyHandle body = physics_.CreateDynamicSphere(position, radius);
            if (!body.IsValid())
            {
                return;
            }

            physics_bodies_.push_back(PhysicsBody{entity, body, position});
            if (rag::scene::TransformComponent* transform = scene_.GetTransform(entity))
            {
                transform->use_quaternion_rotation = true;
            }
        }

        void SpawnStaticBox(const rag::math::Vec3& position, const rag::math::Vec3& half_extents)
        {
            if (physics_ready_)
            {
                (void)physics_.CreateStaticBox(position, half_extents);
            }
        }

        [[nodiscard]] std::size_t DynamicBodyCount() const
        {
            return physics_bodies_.size();
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

        // Detect transform scale edits from any source before simulation. Jolt
        // rebuilds from each body's immutable base shape, and only when this
        // comparison detects a real change.
        void SynchronizeDynamicColliderScales()
        {
            for (PhysicsBody& physics_body : physics_bodies_)
            {
                const rag::scene::TransformComponent* transform =
                    scene_.GetTransform(physics_body.entity);
                if (transform == nullptr ||
                    !PhysicsScalesDiffer(
                        transform->local_scale,
                        physics_body.synchronized_scale))
                {
                    continue;
                }

                if (physics_.SetDynamicBodyScale(
                        physics_body.body,
                        transform->local_scale))
                {
                    physics_body.synchronized_scale = transform->local_scale;
                }
            }
        }

        void StepPhysics(const rag::core::FrameTime& frame_time)
        {
            if (!physics_ready_)
            {
                return;
            }

            SynchronizeDynamicColliderScales();
            physics_.Step(static_cast<rag::f32>(frame_time.delta_seconds));

            // Write each simulated body's pose back onto its scene transform so
            // the renderer draws every object where physics put it.
            for (const PhysicsBody& physics_body : physics_bodies_)
            {
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

        // Drops every dynamic body back to its spawn position so they fall again.
        void ResetPhysicsBodies()
        {
            if (!physics_ready_)
            {
                return;
            }

            for (const PhysicsBody& physics_body : physics_bodies_)
            {
                physics_.ResetDynamicBody(physics_body.body, physics_body.start_position, rag::math::Quat{});
            }
            RAG_LOG_INFO("Physics bodies reset to their start positions.");
        }

        // Returns the dynamic body tracked for an entity, or nullptr if that entity
        // is not physics-driven (the static ground, the duck, the camera, the light).
        // Lets the inspector push transform edits into the simulation.
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
#else
        void SpawnDynamicBox(rag::scene::EntityId, const rag::math::Vec3&, const rag::math::Vec3&) {}
        void SpawnDynamicSphere(rag::scene::EntityId, const rag::math::Vec3&, rag::f32) {}
        void SpawnStaticBox(const rag::math::Vec3&, const rag::math::Vec3&) {}
        [[nodiscard]] std::size_t DynamicBodyCount() const { return 0; }
        void StepPhysics(const rag::core::FrameTime&) {}
#endif

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
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
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

        // The top menu bar. Actions are minimal but real: View toggles the panel and
        // editor mode; Layout rebuilds the default dock arrangement.
        void BuildMainMenuBar()
        {
            if (!ImGui::BeginMainMenuBar())
            {
                return;
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Hierarchy", nullptr, &show_hierarchy_panel_);
                ImGui::MenuItem("Inspector", nullptr, &show_inspector_panel_);
                ImGui::MenuItem("Debug Panel", nullptr, &show_debug_panel_);
                ImGui::MenuItem("Assets", nullptr, &show_assets_panel_);
                ImGui::Separator();
                ImGui::MenuItem("Editor Mode", "F1", &editor_mode_);
                ImGui::EndMenu();
            }

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

            // Inline toolbar: the same mode switch one click away in the menu bar.
            BuildGizmoToolbar();

            ImGui::EndMainMenuBar();
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
                if (ImGui::Button("Reset bodies"))
                {
                    ResetPhysicsBodies();
                }
                ImGui::SameLine();
                ImGui::Text("(%d dynamic)", static_cast<int>(DynamicBodyCount()));
#endif
            }
            ImGui::End();
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

        // Creates a new scene entity that reuses a mesh the renderer already
        // registered at startup (no runtime GPU upload), places it in front of the
        // camera so it is visible, names it, and selects it so the Inspector and gizmo
        // target it immediately. The spawned entity has no physics body, so it stays
        // where placed and the gizmo can move it -- a deliberately simple, safe "drop
        // it into the world" action that touches no Vulkan state.
        void SpawnModel(const rag::editor::AssetEntry& entry)
        {
            if (!entry.spawn_mesh.has_value())
            {
                return;
            }

            const rag::scene::EntityId entity = scene_.CreateEntity();
            NameEntity(entity, entry.name + " (spawn " + std::to_string(++spawn_counter_) + ")");

            rag::scene::TransformComponent& transform = scene_.AddTransform(entity);
            transform.local_position = ComputeSpawnPosition();

            rag::scene::RenderableComponent& renderable = scene_.AddRenderable(entity);
            renderable.mesh = *entry.spawn_mesh;
            renderable.local_bounds.extents = rag::math::Vec3{1.0f, 1.0f, 1.0f};

            scene_.MarkTransformDirty(entity);
            editor_.Select(entity);
            RAG_LOG_INFO("Spawned asset '", entry.name, "' as a new scene entity.");
        }

        // A point a few metres in front of the camera, so a spawned asset lands in
        // view rather than at the busy origin. Falls back to a fixed spot when the
        // camera transform is unavailable.
        [[nodiscard]] rag::math::Vec3 ComputeSpawnPosition() const
        {
            const rag::scene::TransformComponent* camera_transform =
                scene_.GetTransform(camera_entity_);
            if (camera_transform == nullptr)
            {
                return rag::math::Vec3{0.0f, 2.0f, 0.0f};
            }

            const rag::f32 cos_pitch = std::cos(camera_pitch_radians_);
            const rag::math::Vec3 forward = rag::math::Normalize(rag::math::Vec3{
                -std::sin(camera_yaw_radians_) * cos_pitch,
                std::sin(camera_pitch_radians_),
                -std::cos(camera_yaw_radians_) * cos_pitch});
            constexpr rag::f32 spawn_distance = 6.0f;
            return camera_transform->local_position + (forward * spawn_distance);
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

                    const bool physics_driven = IsPhysicsDriven(entity);
                    if (physics_driven)
                    {
                        // Read-only heads-up: this entity's pose is owned by Jolt, so
                        // edits are teleported into the body and its velocities cleared.
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.35f, 1.0f));
                        ImGui::TextWrapped(
                            "Physics-controlled: transform edits are applied to the "
                            "body and velocities are reset.");
                        ImGui::PopStyleColor();
                    }

                    BuildInspectorTransform(entity, physics_driven);
                    BuildInspectorLight(entity);
                    BuildInspectorCameraRenderable(entity);
                }
            }
            ImGui::End();
        }

        // Transform editor. DragFloat3 binds directly to the live component fields for
        // position/scale; rotation is shown in degrees and converted to/from the
        // stored radians. Any change marks the transform dirty (so Scene::Update
        // rebuilds the world matrix next frame) and, for a physics body, is pushed
        // into the simulation.
        void BuildInspectorTransform(rag::scene::EntityId entity, bool physics_driven)
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

            if (physics_driven)
            {
                // The body overwrites position + rotation every Step(), so editing the
                // component alone would be undone next frame. Push the new pose into
                // the body instead (teleport + clear velocity). Scale changes are
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

        // True when a dynamic physics body drives this entity's transform.
        [[nodiscard]] bool IsPhysicsDriven(rag::scene::EntityId entity) const
        {
#if defined(RAG_ENABLE_PHYSICS)
            for (const PhysicsBody& physics_body : physics_bodies_)
            {
                if (physics_body.entity == entity)
                {
                    return true;
                }
            }
#endif
            (void)entity;
            return false;
        }

        // Teleports a physics-driven entity's body to a pose and clears its velocity
        // so an inspector edit sticks. A no-op for non-physics entities (and when the
        // physics module is compiled out).
        void PushTransformToPhysicsBody(
            rag::scene::EntityId entity,
            const rag::math::Vec3& position,
            const rag::math::Quat& rotation)
        {
#if defined(RAG_ENABLE_PHYSICS)
            if (PhysicsBody* physics_body = FindPhysicsBody(entity))
            {
                physics_.ResetDynamicBody(physics_body->body, position, rotation);
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
            if (transform == nullptr || renderable == nullptr)
            {
                return highlight;
            }

            const WorldAabb aabb = ComputeWorldAabb(transform->world_matrix, renderable->local_bounds);
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

            ImGuizmo::Manipulate(
                view,
                projection,
                gizmo_operation_,
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
            transform.local_scale = decomposed.scale;

            const bool rotation_changed = gizmo_operation_ == ImGuizmo::ROTATE;
            if (rotation_changed)
            {
                transform.local_rotation_radians = decomposed.rotation_radians;
            }

            if (IsPhysicsDriven(entity))
            {
                // Jolt overwrites position + rotation every Step(), so push the pose
                // in (teleport + clear velocity). On a rotate drag recompute the quat
                // from the edited Euler; otherwise reuse the live physics quat that
                // StepPhysics keeps current, matching the Inspector's behavior.
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
        bool requested_close_ = false;

#if defined(RAG_ENABLE_VULKAN)
        rag::renderer::RendererPtr renderer_;
#endif
    };
}

int main(int argc, char** argv)
{
    try
    {
        const bool smoke_test = argc > 1 && std::string_view(argv[1]) == "--smoke-test";
        SandboxClient client(smoke_test);

        rag::core::ApplicationDesc desc{};
        desc.name = "RAG Engine Sandbox";
        desc.window_width = 1280;
        desc.window_height = 720;

        rag::core::Application app(desc, client);
        client.InitializeRenderer(app.Window());
        return app.Run();
    }
    catch (const std::exception& exception)
    {
        RAG_LOG_FATAL("Fatal error: ", exception.what());
        return 1;
    }
}
