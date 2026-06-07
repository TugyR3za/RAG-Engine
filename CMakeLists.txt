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
        src/Rag/RendererVK/Internal/VulkanInstance.cpp
        src/Rag/RendererVK/Internal/VulkanInstance.h
        src/Rag/RendererVK/Internal/VulkanSurface.cpp
        src/Rag/RendererVK/Internal/VulkanSurface.h
        src/Rag/RendererVK/Internal/VulkanSwapchain.cpp
        src/Rag/RendererVK/Internal/VulkanSwapchain.h
    )
    rag_configure_target(RagRendererVK)
    target_compile_definitions(RagRendererVK
        PUBLIC RAG_ENABLE_VULKAN=1
        PRIVATE
            VK_USE_PLATFORM_WIN32_KHR
            $<$<CONFIG:Debug>:RAG_ENABLE_VULKAN_VALIDATION=1>
    )
    target_link_libraries(RagRendererVK PUBLIC RagRenderer PRIVATE Vulkan::Vulkan)

    target_link_libraries(RagEngine INTERFACE RagRendererVK)
endif()

if(RAG_BUILD_SANDBOX)
    add_executable(RagSandbox sandbox/SandboxMain.cpp)
    rag_configure_target(RagSandbox)
    target_link_libraries(RagSandbox PRIVATE RagEngine)
endif()

if(RAG_BUILD_TESTS)
    add_executable(RagSceneTests tests/SceneRuntimeTests.cpp)
    rag_configure_target(RagSceneTests)
    target_link_libraries(RagSceneTests PRIVATE RagScene)
endif()
