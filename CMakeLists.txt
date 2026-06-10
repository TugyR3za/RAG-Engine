cmake_minimum_required(VERSION 3.22)

project(RAGEngine
    VERSION 0.2.0
    DESCRIPTION "RAG Engine - Vulkan-first C++ game engine"
    LANGUAGES CXX
)

option(RAG_BUILD_SANDBOX "Build the RAG Engine sandbox application" ON)
option(RAG_BUILD_TESTS "Build RAG Engine test executables" ON)
option(RAG_ENABLE_VULKAN "Build the Vulkan renderer backend" ON)

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
        src/Rag/RendererVK/Internal/VulkanSurface.cpp
        src/Rag/RendererVK/Internal/VulkanSurface.h
        src/Rag/RendererVK/Internal/VulkanSwapchain.cpp
        src/Rag/RendererVK/Internal/VulkanSwapchain.h
        src/Rag/RendererVK/Internal/VulkanVertexBuffer.cpp
        src/Rag/RendererVK/Internal/VulkanVertexBuffer.h
        src/Rag/RendererVK/Shaders/Triangle.frag
        src/Rag/RendererVK/Shaders/Triangle.vert
    )
    rag_configure_target(RagRendererVK)
    target_compile_definitions(RagRendererVK
        PUBLIC RAG_ENABLE_VULKAN=1
        PRIVATE
            VK_USE_PLATFORM_WIN32_KHR
            $<$<CONFIG:Debug>:RAG_ENABLE_VULKAN_VALIDATION=1>
    )
    target_link_libraries(RagRendererVK PUBLIC RagRenderer PRIVATE Vulkan::Vulkan)

    set(RAG_VULKAN_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
    set(RAG_VULKAN_VERTEX_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/src/Rag/RendererVK/Shaders/Triangle.vert")
    set(RAG_VULKAN_FRAGMENT_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/src/Rag/RendererVK/Shaders/Triangle.frag")
    set(RAG_VULKAN_VERTEX_SPV "${RAG_VULKAN_SHADER_OUTPUT_DIR}/triangle.vert.spv")
    set(RAG_VULKAN_FRAGMENT_SPV "${RAG_VULKAN_SHADER_OUTPUT_DIR}/triangle.frag.spv")

    add_custom_command(
        OUTPUT
            "${RAG_VULKAN_VERTEX_SPV}"
            "${RAG_VULKAN_FRAGMENT_SPV}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RAG_VULKAN_SHADER_OUTPUT_DIR}"
        COMMAND "${RAG_GLSLANG_VALIDATOR_EXECUTABLE}" -V "${RAG_VULKAN_VERTEX_SHADER}" -o "${RAG_VULKAN_VERTEX_SPV}"
        COMMAND "${RAG_GLSLANG_VALIDATOR_EXECUTABLE}" -V "${RAG_VULKAN_FRAGMENT_SHADER}" -o "${RAG_VULKAN_FRAGMENT_SPV}"
        DEPENDS
            "${RAG_VULKAN_VERTEX_SHADER}"
            "${RAG_VULKAN_FRAGMENT_SHADER}"
        COMMENT "Compiling RAG Engine Phase 2D Vulkan shaders"
        VERBATIM
    )

    add_custom_target(RagRendererVKShaders
        DEPENDS
            "${RAG_VULKAN_VERTEX_SPV}"
            "${RAG_VULKAN_FRAGMENT_SPV}"
    )
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
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RAG_VULKAN_VERTEX_SPV}"
                "$<TARGET_FILE_DIR:RagSandbox>/shaders/triangle.vert.spv"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RAG_VULKAN_FRAGMENT_SPV}"
                "$<TARGET_FILE_DIR:RagSandbox>/shaders/triangle.frag.spv"
            COMMENT "Copying RAG Engine Vulkan shaders next to RagSandbox"
            VERBATIM
        )
    endif()
endif()

if(RAG_BUILD_TESTS)
    add_executable(RagSceneTests tests/SceneRuntimeTests.cpp)
    rag_configure_target(RagSceneTests)
    target_link_libraries(RagSceneTests PRIVATE RagScene)
endif()
