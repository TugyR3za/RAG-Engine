cmake_minimum_required(VERSION 3.22)

project(RAGEngine
    VERSION 0.2.0
    DESCRIPTION "RAG Engine - Vulkan-first C++ game engine"
    LANGUAGES CXX
)

option(RAG_BUILD_SANDBOX "Build the RAG Engine sandbox application" ON)
option(RAG_BUILD_TESTS "Build RAG Engine test executables" ON)
option(RAG_ENABLE_VULKAN "Build the Vulkan renderer backend" ON)
option(RAG_ENABLE_PHYSICS "Build the Jolt-backed physics module" ON)
option(RAG_ENABLE_IMGUI "Build the Dear ImGui editor overlay (requires the Vulkan backend)" ON)
option(RAG_EDITOR "Compile the in-engine editor UI (dockspace, panels, menu bar) into the sandbox; OFF ships the bare game" ON)
option(RAG_SHADOW_DEBUG "Draw the directional shadow depth map as a debug overlay quad" OFF)

include(cmake/RagCompilerOptions.cmake)
include(cmake/RagVulkan.cmake)

add_library(RagCore STATIC
    src/Rag/Core/Base.h
    src/Rag/Core/Clock.cpp
    src/Rag/Core/Clock.h
    src/Rag/Core/Event.h
    src/Rag/Core/Input.cpp
    src/Rag/Core/Input.h
    src/Rag/Core/Log.cpp
    src/Rag/Core/Log.h
    src/Rag/Core/Math.h
    src/Rag/Core/Assert.h
)
rag_configure_target(RagCore)

add_library(RagPlatform STATIC
    src/Rag/Platform/SystemPaths.h
    src/Rag/Platform/Window.h
)
rag_configure_target(RagPlatform)
target_link_libraries(RagPlatform PUBLIC RagCore)

if(WIN32)
    target_sources(RagPlatform
        PRIVATE
            src/Rag/Platform/Win32/Win32Window.cpp
            src/Rag/Platform/Win32/Win32Window.h
    )

    target_link_libraries(RagPlatform PRIVATE user32)
else()
    message(FATAL_ERROR "RAG Engine currently implements the Win32 platform backend only.")
endif()

add_library(RagRuntime STATIC
    src/Rag/Core/Application.cpp
    src/Rag/Core/Application.h
)
rag_configure_target(RagRuntime)
target_link_libraries(RagRuntime PUBLIC RagCore RagPlatform)

add_library(RagRenderer STATIC
    src/Rag/Renderer/Environment.cpp
    src/Rag/Renderer/Environment.h
    src/Rag/Renderer/Material.cpp
    src/Rag/Renderer/Material.h
    src/Rag/Renderer/Renderer.cpp
    src/Rag/Renderer/Renderer.h
)
rag_configure_target(RagRenderer)
target_link_libraries(RagRenderer PUBLIC RagCore RagPlatform)

add_library(RagScene STATIC
    src/Rag/Scene/ComponentStorage.h
    src/Rag/Scene/Components.h
    src/Rag/Scene/Entity.h
    src/Rag/Scene/PlayerStart.cpp
    src/Rag/Scene/PlayerStart.h
    src/Rag/Scene/Scene.cpp
    src/Rag/Scene/Scene.h
    src/Rag/Scene/SceneSerialization.cpp
    src/Rag/Scene/SceneSerialization.h
)
rag_configure_target(RagScene)
target_link_libraries(RagScene PUBLIC RagCore RagRenderer)
# Vendored single-header nlohmann/json, used only by SceneSerialization.cpp; kept
# PRIVATE so <json.hpp> stays out of anyone who merely links RagScene.
target_include_directories(RagScene PRIVATE ${PROJECT_SOURCE_DIR}/third_party)

if(RAG_ENABLE_PHYSICS)
    include(FetchContent)

    # Jolt defaults to the static MSVC runtime and also builds its samples,
    # viewer, tests and tools. Force the dynamic runtime so it matches the rest
    # of the engine (which uses the default /MD[d]) and disable the extra
    # targets so the first build only pays for the core Jolt library.
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)

    # Pinned to a specific release tag (never HEAD) so future pulls are stable.
    FetchContent_Declare(
        JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG v5.5.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR Build
    )
    FetchContent_MakeAvailable(JoltPhysics)

    add_library(RagPhysics STATIC
        src/Rag/Physics/PhysicsTypes.h
        src/Rag/Physics/PhysicsWorld.cpp
        src/Rag/Physics/PhysicsWorld.h
    )
    rag_configure_target(RagPhysics)
    # RagPhysics depends downward on RagCore only. Jolt is linked PRIVATE so its
    # headers never leak past PhysicsWorld.cpp.
    target_link_libraries(RagPhysics PUBLIC RagCore PRIVATE Jolt)
    target_compile_definitions(RagPhysics PUBLIC RAG_ENABLE_PHYSICS=1)
endif()

add_library(RagEngine INTERFACE)
target_link_libraries(RagEngine INTERFACE RagRuntime RagRenderer RagScene)

if(RAG_ENABLE_PHYSICS)
    target_link_libraries(RagEngine INTERFACE RagPhysics)
endif()

if(RAG_ENABLE_VULKAN)
    rag_find_vulkan()

    # Dear ImGui ships no CMake of its own, so its core plus the official Win32
    # and Vulkan backends are compiled into a small static library here. Pinned
    # to a specific release tag (never HEAD) so future pulls stay reproducible.
    # The '-docking' tag is the docking-branch counterpart of the same release;
    # it provides DockSpace and ImGuiConfigFlags_DockingEnable, which the plain
    # release tag does not provide.
    if(RAG_ENABLE_IMGUI)
        include(FetchContent)
        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.92.7-docking
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(imgui)

        add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        )
        target_include_directories(imgui PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
        # Vulkan headers/loader are needed only to compile the Vulkan backend;
        # keep them PRIVATE so code that includes only imgui.h stays Vulkan-free.
        target_link_libraries(imgui PRIVATE Vulkan::Vulkan)
        if(WIN32)
            target_link_libraries(imgui PRIVATE user32 dwmapi imm32)
        endif()
        if(MSVC)
            # Third party: don't subject ImGui to the engine's /W4 /permissive-.
            target_compile_options(imgui PRIVATE /W0)
        endif()

        # ImGuizmo draws the translate/rotate/scale handles in the 3D viewport. It
        # pairs with Dear ImGui and only needs ImGui's headers, so just its one
        # gizmo translation unit is compiled into a small static library that links
        # the imgui target above (sharing its single GImGui context) -- the repo's
        # own CMake also pulls in GraphEditor/Sequencer/etc. and a second imgui,
        # which are not wanted here. SOURCE_SUBDIR points MakeAvailable at src/ (which
        # has no CMakeLists.txt) so it only downloads the sources and does not run
        # ImGuizmo's CMake. Pinned to a specific master commit (never HEAD): ImGuizmo
        # has no release tag new enough to build against the ImGui docking branch
        # used here. GIT_SHALLOW is off because a shallow clone cannot fetch an
        # arbitrary (unadvertised) SHA.
        FetchContent_Declare(
            imguizmo
            GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
            GIT_TAG 87fb88b13a4f6bb04cd6ecd4e9d1625929935ff1
            SOURCE_SUBDIR src
        )
        FetchContent_MakeAvailable(imguizmo)

        add_library(imguizmo STATIC
            ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
        )
        target_include_directories(imguizmo PUBLIC ${imguizmo_SOURCE_DIR}/src)
        target_link_libraries(imguizmo PUBLIC imgui)
        if(MSVC)
            # Third party: keep it out of the engine's /W4 /permissive-.
            target_compile_options(imguizmo PRIVATE /W0)
        endif()
    endif()

    add_library(RagRendererVK STATIC
        src/Rag/RendererVK/Internal/VulkanCubemapData.cpp
        src/Rag/RendererVK/Internal/VulkanCubemapData.h
        src/Rag/RendererVK/Internal/VulkanSkyboxPipeline.cpp
        src/Rag/RendererVK/Internal/VulkanSkyboxPipeline.h
        src/Rag/RendererVK/Shaders/Skybox.vert
        src/Rag/RendererVK/Shaders/Skybox.frag
        src/Rag/RendererVK/VulkanRenderer.cpp
        src/Rag/RendererVK/VulkanRenderer.h
        src/Rag/RendererVK/Internal/VulkanAssetPath.cpp
        src/Rag/RendererVK/Internal/VulkanAssetPath.h
        src/Rag/RendererVK/Internal/VulkanCommon.cpp
        src/Rag/RendererVK/Internal/VulkanCommon.h
        src/Rag/RendererVK/Internal/VulkanDevice.cpp
        src/Rag/RendererVK/Internal/VulkanDevice.h
        src/Rag/RendererVK/Internal/VulkanFrameResources.cpp
        src/Rag/RendererVK/Internal/VulkanFrameResources.h
        src/Rag/RendererVK/Internal/VulkanGltfLoader.cpp
        src/Rag/RendererVK/Internal/VulkanGltfLoader.h
        src/Rag/RendererVK/Internal/VulkanGraphicsPipeline.cpp
        src/Rag/RendererVK/Internal/VulkanGraphicsPipeline.h
        src/Rag/RendererVK/Internal/VulkanIndexBuffer.cpp
        src/Rag/RendererVK/Internal/VulkanIndexBuffer.h
        src/Rag/RendererVK/Internal/VulkanInstance.cpp
        src/Rag/RendererVK/Internal/VulkanInstance.h
        src/Rag/RendererVK/Internal/VulkanMesh.cpp
        src/Rag/RendererVK/Internal/VulkanMesh.h
        src/Rag/RendererVK/Internal/VulkanMeshLoader.cpp
        src/Rag/RendererVK/Internal/VulkanMeshLoader.h
        src/Rag/RendererVK/Internal/VulkanSelectionOutlinePipeline.cpp
        src/Rag/RendererVK/Internal/VulkanSelectionOutlinePipeline.h
        src/Rag/RendererVK/Internal/VulkanShaderModule.cpp
        src/Rag/RendererVK/Internal/VulkanShaderModule.h
        src/Rag/RendererVK/Internal/VulkanShadowDebugPipeline.cpp
        src/Rag/RendererVK/Internal/VulkanShadowDebugPipeline.h
        src/Rag/RendererVK/Internal/VulkanShadowMap.cpp
        src/Rag/RendererVK/Internal/VulkanShadowMap.h
        src/Rag/RendererVK/Internal/VulkanShadowPipeline.cpp
        src/Rag/RendererVK/Internal/VulkanShadowPipeline.h
        src/Rag/RendererVK/Internal/VulkanSurface.cpp
        src/Rag/RendererVK/Internal/VulkanSurface.h
        src/Rag/RendererVK/Internal/VulkanStbImage.cpp
        src/Rag/RendererVK/Internal/VulkanSwapchain.cpp
        src/Rag/RendererVK/Internal/VulkanSwapchain.h
        src/Rag/RendererVK/Internal/VulkanTexture.cpp
        src/Rag/RendererVK/Internal/VulkanTexture.h
        src/Rag/RendererVK/Internal/VulkanTextureDescriptors.cpp
        src/Rag/RendererVK/Internal/VulkanTextureDescriptors.h
        src/Rag/RendererVK/Internal/VulkanUniformResources.cpp
        src/Rag/RendererVK/Internal/VulkanUniformResources.h
        src/Rag/RendererVK/Internal/VulkanVertexBuffer.cpp
        src/Rag/RendererVK/Internal/VulkanVertexBuffer.h
        src/Rag/RendererVK/Shaders/SelectionOutline.frag
        src/Rag/RendererVK/Shaders/SelectionOutline.vert
        src/Rag/RendererVK/Shaders/ShadowDebug.frag
        src/Rag/RendererVK/Shaders/ShadowDebug.vert
        src/Rag/RendererVK/Shaders/ShadowDepth.vert
        src/Rag/RendererVK/Shaders/Triangle.frag
        src/Rag/RendererVK/Shaders/Triangle.vert
    )
    rag_configure_target(RagRendererVK)
    # Vendored single-header libraries (tinyobjloader, tinygltf, JSON, stb_image).
    target_include_directories(RagRendererVK PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
    target_compile_definitions(RagRendererVK
        PUBLIC RAG_ENABLE_VULKAN=1
        PRIVATE
            VK_USE_PLATFORM_WIN32_KHR
            $<$<CONFIG:Debug>:RAG_ENABLE_VULKAN_VALIDATION=1>
            $<$<BOOL:${RAG_SHADOW_DEBUG}>:RAG_SHADOW_DEBUG=1>
    )
    target_link_libraries(RagRendererVK PUBLIC RagRenderer PRIVATE Vulkan::Vulkan)

    if(RAG_ENABLE_IMGUI)
        target_sources(RagRendererVK PRIVATE
            src/Rag/RendererVK/Internal/VulkanImGuiLayer.cpp
            src/Rag/RendererVK/Internal/VulkanImGuiLayer.h
        )
        # imgui is linked PUBLIC so the editor/game layer can build panels with
        # imgui.h, while the Win32/Vulkan backends stay used only in RendererVK.
        target_link_libraries(RagRendererVK PUBLIC imgui)
        target_compile_definitions(RagRendererVK PUBLIC RAG_ENABLE_IMGUI=1)
    endif()

    set(RAG_VULKAN_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")

    # "<source file>|<output spv name>" pairs. The output names are what the
    # renderer looks up at runtime, so keep them in sync with the *.cpp filenames.
    set(RAG_VULKAN_SHADER_DEFS
        "Skybox.vert|skybox.vert.spv"
        "Skybox.frag|skybox.frag.spv"
        "Triangle.vert|triangle.vert.spv"
        "Triangle.frag|triangle.frag.spv"
        "ShadowDepth.vert|shadow_depth.vert.spv"
        "ShadowDebug.vert|shadow_debug.vert.spv"
        "ShadowDebug.frag|shadow_debug.frag.spv"
        "SelectionOutline.vert|selection_outline.vert.spv"
        "SelectionOutline.frag|selection_outline.frag.spv"
    )

    set(RAG_VULKAN_SHADER_SPV_FILES "")
    foreach(shader_def IN LISTS RAG_VULKAN_SHADER_DEFS)
        string(REPLACE "|" ";" shader_parts "${shader_def}")
        list(GET shader_parts 0 shader_source_name)
        list(GET shader_parts 1 shader_output_name)
        set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/src/Rag/RendererVK/Shaders/${shader_source_name}")
        set(shader_spv "${RAG_VULKAN_SHADER_OUTPUT_DIR}/${shader_output_name}")

        add_custom_command(
            OUTPUT "${shader_spv}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${RAG_VULKAN_SHADER_OUTPUT_DIR}"
            COMMAND "${RAG_GLSLANG_VALIDATOR_EXECUTABLE}" -V "${shader_source}" -o "${shader_spv}"
            DEPENDS "${shader_source}"
            COMMENT "Compiling RAG Engine Vulkan shader ${shader_source_name}"
            VERBATIM
        )
        list(APPEND RAG_VULKAN_SHADER_SPV_FILES "${shader_spv}")
    endforeach()

    add_custom_target(RagRendererVKShaders DEPENDS ${RAG_VULKAN_SHADER_SPV_FILES})
    add_dependencies(RagRendererVK RagRendererVKShaders)

    target_link_libraries(RagEngine INTERFACE RagRendererVK)
endif()

if(RAG_BUILD_SANDBOX)
    add_executable(RagSandbox
        sandbox/AssetBrowser.h
        sandbox/EditorContext.h
        sandbox/PlayerController.h
        sandbox/SandboxMain.cpp
        sandbox/SceneDocument.h
        sandbox/SimulationController.h
    )
    rag_configure_target(RagSandbox)
    target_link_libraries(RagSandbox PRIVATE RagEngine)
    target_compile_definitions(RagSandbox PRIVATE
        RAG_BUILD_CONFIGURATION="$<CONFIG>"
        RAG_CMAKE_BUILD_DIRECTORY="${CMAKE_BINARY_DIR}"
        RAG_PROJECT_VERSION="${PROJECT_VERSION}"
    )

    # Keep command-line and Visual Studio launches consistent. The canonical
    # development workflow runs from the repository root, so source scenes are
    # found in scenes/ while generated models/assets live beside the executable.
    set(RAG_SANDBOX_RUNTIME_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    set_target_properties(RagSandbox PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${RAG_SANDBOX_RUNTIME_DIRECTORY}"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT RagSandbox)

    # The editor UI (dockspace, panels, menu bar) is a developer-only layer built
    # on the ImGui overlay. Defining RAG_EDITOR only when ImGui is also enabled
    # keeps "RAG_EDITOR implies ImGui present" true in the code; with it off the
    # sandbox compiles to the bare game with no editor code at all.
    if(RAG_EDITOR AND RAG_ENABLE_IMGUI AND RAG_ENABLE_VULKAN)
        target_compile_definitions(RagSandbox PRIVATE RAG_EDITOR=1)
        # ImGuizmo (translate/rotate/scale gizmo) is editor-only; the sandbox calls
        # it directly from BuildEditorUi. imgui itself is already public via
        # RagRendererVK, so this only adds the gizmo translation unit + headers.
        target_link_libraries(RagSandbox PRIVATE imguizmo)
    endif()

    if(RAG_ENABLE_VULKAN)
        add_dependencies(RagSandbox RagRendererVKShaders)

        # This target is intentionally always out of date. Building RagSandbox
        # therefore refreshes runtime files even when only an asset or generated
        # shader changed and the executable itself does not need to relink.
        add_custom_target(RagSandboxRuntimeFiles
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RagSandbox>/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/models"
                "$<TARGET_FILE_DIR:RagSandbox>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:RagSandbox>/assets"
            COMMENT "Refreshing RAG Engine runtime files next to RagSandbox"
            VERBATIM
        )
        add_dependencies(RagSandboxRuntimeFiles RagRendererVKShaders)
        foreach(shader_spv IN LISTS RAG_VULKAN_SHADER_SPV_FILES)
            get_filename_component(shader_spv_name "${shader_spv}" NAME)
            add_custom_command(
                TARGET RagSandboxRuntimeFiles
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${shader_spv}"
                    "$<TARGET_FILE_DIR:RagSandbox>/shaders/${shader_spv_name}"
                COMMENT "Copying RAG Engine Vulkan shader ${shader_spv_name} next to RagSandbox"
                VERBATIM
            )
        endforeach()
        add_dependencies(RagSandbox RagSandboxRuntimeFiles)
    endif()
endif()

if(RAG_BUILD_TESTS)
    enable_testing()
    add_executable(RagSceneTests tests/SceneRuntimeTests.cpp)
    rag_configure_target(RagSceneTests)
    target_link_libraries(RagSceneTests PRIVATE RagScene)
    add_test(NAME SceneRuntime COMMAND RagSceneTests)

    add_executable(RagSceneSerializationTests tests/SceneSerializationTests.cpp)
    rag_configure_target(RagSceneSerializationTests)
    target_link_libraries(RagSceneSerializationTests PRIVATE RagScene)
    add_test(NAME SceneSerialization COMMAND RagSceneSerializationTests)

    # The Edit/Playing/Paused lifecycle is a header-only sandbox helper with no
    # engine dependencies beyond RagCore's base types, so it is tested directly.
    add_executable(RagSimulationControllerTests
        sandbox/SimulationController.h
        tests/SimulationControllerTests.cpp
    )
    rag_configure_target(RagSimulationControllerTests)
    target_include_directories(RagSimulationControllerTests PRIVATE ${PROJECT_SOURCE_DIR}/sandbox)
    target_link_libraries(RagSimulationControllerTests PRIVATE RagCore)
    add_test(NAME SimulationController COMMAND RagSimulationControllerTests)

    # Scene document identity (which file Save writes; Untitled after New) is a
    # header-only sandbox helper. It is tested with real scene serialization
    # against disposable files in the system temp directory, never scenes/.
    add_executable(RagSceneDocumentTests
        sandbox/SceneDocument.h
        tests/SceneDocumentTests.cpp
    )
    rag_configure_target(RagSceneDocumentTests)
    target_include_directories(RagSceneDocumentTests PRIVATE ${PROJECT_SOURCE_DIR}/sandbox)
    target_link_libraries(RagSceneDocumentTests PRIVATE RagScene)
    add_test(NAME SceneDocument COMMAND RagSceneDocumentTests)

    if(RAG_ENABLE_VULKAN AND RAG_BUILD_SANDBOX)
        add_executable(RagSkyboxTests tests/SkyboxTests.cpp)
        rag_configure_target(RagSkyboxTests)
        target_link_libraries(RagSkyboxTests PRIVATE RagScene RagRendererVK)
        # Sandbox's existing post-build copies provide runtime assets/shaders in
        # the shared executable directory; no additional asset staging system.
        add_dependencies(RagSkyboxTests RagSandbox)
        add_test(NAME SkyboxAssets COMMAND RagSkyboxTests)
        set_tests_properties(SkyboxAssets PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

        # Scripted Play/Pause/Step/Stop run inside the real editor (real Scene,
        # Jolt and Vulkan renderer). It opens a window for a few seconds, so it is
        # labelled "gpu" and can be skipped with `ctest -LE gpu`. It only uses
        # in-memory scenes; nothing under scenes/ is written.
        if(RAG_EDITOR AND RAG_ENABLE_IMGUI AND RAG_ENABLE_PHYSICS)
            add_test(NAME SandboxPlayMode COMMAND RagSandbox --play-mode-self-test)
            set_tests_properties(SandboxPlayMode PROPERTIES
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                LABELS "gpu"
                TIMEOUT 180
            )
        endif()
    endif()

    # First-person movement rules (walk speed, diagonals, jump gating, air
    # control, fall reset, lifecycle gating) are a header-only, engine-free
    # sandbox helper tested against an analytic stand-in for collision.
    add_executable(RagPlayerControllerTests
        sandbox/PlayerController.h
        tests/PlayerControllerTests.cpp
    )
    rag_configure_target(RagPlayerControllerTests)
    target_include_directories(RagPlayerControllerTests PRIVATE ${PROJECT_SOURCE_DIR}/sandbox)
    target_link_libraries(RagPlayerControllerTests PRIVATE RagCore)
    add_test(NAME PlayerController COMMAND RagPlayerControllerTests)

    if(RAG_ENABLE_PHYSICS)
        add_executable(RagPhysicsTests tests/PhysicsWorldTests.cpp)
        rag_configure_target(RagPhysicsTests)
        target_link_libraries(RagPhysicsTests PRIVATE RagPhysics)
        add_test(NAME PhysicsWorld COMMAND RagPhysicsTests)

        # The same movement rules driving a real Jolt character through
        # PhysicsWorld: floors, walls, ledges, ceilings, dynamic props, slopes,
        # frame rates and long frames. No window or GPU.
        add_executable(RagPlayerPhysicsTests
            sandbox/PlayerController.h
            tests/PlayerPhysicsTests.cpp
        )
        rag_configure_target(RagPlayerPhysicsTests)
        target_include_directories(RagPlayerPhysicsTests PRIVATE ${PROJECT_SOURCE_DIR}/sandbox)
        target_link_libraries(RagPlayerPhysicsTests PRIVATE RagPhysics RagScene)
        add_test(NAME PlayerPhysics COMMAND RagPlayerPhysicsTests)
    endif()
endif()
