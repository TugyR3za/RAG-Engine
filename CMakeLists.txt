cmake_minimum_required(VERSION 3.22)

project(RAGEngine
    VERSION 0.2.0
    DESCRIPTION "RAG Engine - Vulkan-first C++ game engine"
    LANGUAGES CXX
)

option(RAG_BUILD_SANDBOX "Build the RAG Engine sandbox application" ON)
option(RAG_BUILD_TESTS "Build RAG Engine test executables" ON)
option(RAG_ENABLE_VULKAN "Build the Vulkan renderer backend" ON)
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
    src/Rag/Renderer/Renderer.cpp
    src/Rag/Renderer/Renderer.h
)
rag_configure_target(RagRenderer)
target_link_libraries(RagRenderer PUBLIC RagCore RagPlatform)

add_library(RagScene STATIC
    src/Rag/Scene/ComponentStorage.h
    src/Rag/Scene/Components.h
    src/Rag/Scene/Entity.h
    src/Rag/Scene/Scene.cpp
    src/Rag/Scene/Scene.h
)
rag_configure_target(RagScene)
target_link_libraries(RagScene PUBLIC RagCore RagRenderer)

add_library(RagEngine INTERFACE)
target_link_libraries(RagEngine INTERFACE RagRuntime RagRenderer RagScene)

if(RAG_ENABLE_VULKAN)
    rag_find_vulkan()

    add_library(RagRendererVK STATIC
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
        src/Rag/RendererVK/Internal/VulkanSwapchain.cpp
        src/Rag/RendererVK/Internal/VulkanSwapchain.h
        src/Rag/RendererVK/Internal/VulkanUniformResources.cpp
        src/Rag/RendererVK/Internal/VulkanUniformResources.h
        src/Rag/RendererVK/Internal/VulkanVertexBuffer.cpp
        src/Rag/RendererVK/Internal/VulkanVertexBuffer.h
        src/Rag/RendererVK/Shaders/ShadowDebug.frag
        src/Rag/RendererVK/Shaders/ShadowDebug.vert
        src/Rag/RendererVK/Shaders/ShadowDepth.vert
        src/Rag/RendererVK/Shaders/Triangle.frag
        src/Rag/RendererVK/Shaders/Triangle.vert
    )
    rag_configure_target(RagRendererVK)
    # Vendored single-header libraries (tinyobjloader).
    target_include_directories(RagRendererVK PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
    target_compile_definitions(RagRendererVK
        PUBLIC RAG_ENABLE_VULKAN=1
        PRIVATE
            VK_USE_PLATFORM_WIN32_KHR
            $<$<CONFIG:Debug>:RAG_ENABLE_VULKAN_VALIDATION=1>
            $<$<BOOL:${RAG_SHADOW_DEBUG}>:RAG_SHADOW_DEBUG=1>
    )
    target_link_libraries(RagRendererVK PUBLIC RagRenderer PRIVATE Vulkan::Vulkan)

    set(RAG_VULKAN_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")

    # "<source file>|<output spv name>" pairs. The output names are what the
    # renderer looks up at runtime, so keep them in sync with the *.cpp filenames.
    set(RAG_VULKAN_SHADER_DEFS
        "Triangle.vert|triangle.vert.spv"
        "Triangle.frag|triangle.frag.spv"
        "ShadowDepth.vert|shadow_depth.vert.spv"
        "ShadowDebug.vert|shadow_debug.vert.spv"
        "ShadowDebug.frag|shadow_debug.frag.spv"
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
    add_executable(RagSandbox sandbox/SandboxMain.cpp)
    rag_configure_target(RagSandbox)
    target_link_libraries(RagSandbox PRIVATE RagEngine)

    if(RAG_ENABLE_VULKAN)
        add_dependencies(RagSandbox RagRendererVKShaders)
        add_custom_command(
            TARGET RagSandbox
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RagSandbox>/shaders"
            COMMENT "Preparing RAG Engine shader directory next to RagSandbox"
            VERBATIM
        )
        foreach(shader_spv IN LISTS RAG_VULKAN_SHADER_SPV_FILES)
            get_filename_component(shader_spv_name "${shader_spv}" NAME)
            add_custom_command(
                TARGET RagSandbox
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${shader_spv}"
                    "$<TARGET_FILE_DIR:RagSandbox>/shaders/${shader_spv_name}"
                COMMENT "Copying RAG Engine Vulkan shader ${shader_spv_name} next to RagSandbox"
                VERBATIM
            )
        endforeach()

        add_custom_command(
            TARGET RagSandbox
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/models"
                "$<TARGET_FILE_DIR:RagSandbox>/models"
            COMMENT "Copying RAG Engine model assets next to RagSandbox"
            VERBATIM
        )
    endif()
endif()

if(RAG_BUILD_TESTS)
    add_executable(RagSceneTests tests/SceneRuntimeTests.cpp)
    rag_configure_target(RagSceneTests)
    target_link_libraries(RagSceneTests PRIVATE RagScene)
endif()
