#include "Rag/Core/Log.h"
#include "Rag/Platform/Window.h"
#include "Rag/RendererVK/Internal/VulkanCubemapData.h"
#include "Rag/RendererVK/VulkanRenderer.h"
#include "Rag/Scene/SceneSerialization.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace
{
    const rag::renderer::SkyboxFaces StarterFaces{
        "assets/skyboxes/starter/px.png", "assets/skyboxes/starter/nx.png",
        "assets/skyboxes/starter/py.png", "assets/skyboxes/starter/ny.png",
        "assets/skyboxes/starter/pz.png", "assets/skyboxes/starter/nz.png"};

    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // Fixtures stay under the build directory, never touching scene/asset files.
    struct Fixtures
    {
        std::filesystem::path directory = std::filesystem::path("build") /
            ("skybox-test-" + std::to_string(GetCurrentProcessId()));
        Fixtures()
        {
            std::filesystem::create_directories(directory);
            WritePpm("square.ppm", 2, 2);
            WritePpm("nonsquare.ppm", 2, 1);
            std::ofstream(directory / "corrupt.png") << "not an image";
            std::ofstream(directory / "truncated.ppm", std::ios::binary) << "P6\n2 2\n255\n";
        }
        ~Fixtures()
        {
            std::error_code error;
            for (const char* file : {"square.ppm", "nonsquare.ppm", "corrupt.png", "truncated.ppm"})
                std::filesystem::remove(directory / file, error);
            std::filesystem::remove(directory, error);
        }
        std::string Path(const char* file) const { return (directory / file).generic_string(); }
        void WritePpm(const char* file, int width, int height)
        {
            std::ofstream stream(directory / file, std::ios::binary);
            stream << "P6\n" << width << " " << height << "\n255\n";
            for (int i = 0; i < width * height; ++i) stream.write("\x20\x60\xA0", 3);
        }
    };

    void AssetTests(const Fixtures& fixtures)
    {
        const auto data = rag::renderer::vk::LoadCubemapData(StarterFaces, 4096);
        Require(data.size == 256 && data.pixels.size() == 6 * 256 * 256 * 4, "Starter cubemap decode failed.");
        for (const char* file : {"missing.png", "corrupt.png", "nonsquare.ppm", "square.ppm"})
        {
            auto faces = StarterFaces;
            faces[4] = fixtures.Path(file);
            bool rejected = false;
            try { (void)rag::renderer::vk::LoadCubemapData(faces, 4096); }
            catch (const std::exception& error)
            {
                rejected = std::string(error.what()).find(file) != std::string::npos;
            }
            Require(rejected, "Missing, corrupt, nonsquare or mismatched face must report its path.");
        }
        bool rejected = false;
        try { (void)rag::renderer::vk::LoadCubemapData(StarterFaces, 128); }
        catch (const std::exception&) { rejected = true; }
        Require(rejected, "Device cube dimension limit must be checked before upload.");
        auto truncated_faces = StarterFaces;
        truncated_faces.fill(fixtures.Path("truncated.ppm"));
        rejected = false;
        try { (void)rag::renderer::vk::LoadCubemapData(truncated_faces, 4096); }
        catch (const std::exception& error)
        {
            rejected = std::string(error.what()).find("truncated.ppm") != std::string::npos;
        }
        Require(rejected, "A valid header with truncated pixels must fail decoding with its path.");
        RAG_LOG_INFO("Skybox asset tests passed (expected invalid-asset errors above).");
    }

    void GpuSmoke(const Fixtures& fixtures)
    {
        auto window = rag::platform::CreatePlatformWindow({"RAG skybox GPU smoke test", 800, 600});
        rag::renderer::RendererDesc desc;
        desc.window = window.get();
        desc.enable_validation = true;
        auto renderer = rag::renderer::vk::CreateVulkanRenderer(desc);
        rag::renderer::RenderWorld world;
        world.camera.valid = true;
        world.camera.view = rag::math::InverseRigidTransform(rag::math::Translation({0, 2, 6}));
        world.objects.push_back({}); // Built-in cube and texture fallback.
        world.objects[0].world_transform = rag::math::Identity();
        rag::renderer::RenderFrameContext frame;
        frame.render_world = &world;
        frame.selection_highlight.active = true;
        window->SetEventCallback([&](const rag::core::Event& event) {
            if (event.type == rag::core::EventType::WindowResize)
            {
                const auto& size = std::get<rag::core::WindowResizeEvent>(event.payload);
                renderer->OnWindowResized(size.width, size.height);
            }
        });
        auto draw = [&] {
            for (int i = 0; i < 4; ++i)
            {
                window->PollEvents();
                world.camera.projection = rag::math::PerspectiveRH_ZO(rag::math::Pi / 3,
                    static_cast<float>(window->Width()) / static_cast<float>(window->Height()), 0.1f, 100.0f);
                renderer->RenderFrame(frame);
            }
        };

        RAG_LOG_INFO("GPU smoke baseline: existing geometry and clear color, skybox disabled.");
        draw();

        // Repeated changes with multiple frames in flight exercise resource retirement,
        // failed loads, successful replacement, disabled state and legacy defaults.
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            world.environment = {true, StarterFaces};
            draw();
            world.camera.view = rag::math::InverseRigidTransform(rag::math::Multiply(
                rag::math::Translation({100, 30, -70}), rag::math::RotationY(0.8f)));
            draw();
            world.environment.skybox_enabled = false;
            draw();
            world.environment.skybox_enabled = true;
            draw();
            for (const char* file : {"missing.png", "corrupt.png", "nonsquare.ppm", "square.ppm"})
            {
                world.environment.skybox_faces[5] = fixtures.Path(file);
                draw(); // Four frames per bad description: only one load attempt.
            }
            rag::scene::SerializedScene legacy;
            std::string error;
            Require(rag::scene::ParseSceneFromJson(R"({"sceneVersion":1,"entities":[]})", legacy, error), "Legacy parse failed.");
            world.environment = legacy.environment;
            draw();
        }
        world.environment = {true, StarterFaces};
        draw();
        SetWindowPos(static_cast<HWND>(window->GetNativeHandle().window), nullptr,
            0, 0, 1024, 768, SWP_NOMOVE | SWP_NOZORDER);
        draw();
        window->SetFullscreen(true);
        draw();
        window->SetFullscreen(false);
        draw();
        frame.render_world = nullptr; // Retires environment even without a scene.
        draw();
        renderer->WaitIdle();
        window->SetEventCallback({});
        renderer.reset();
        RAG_LOG_INFO("Skybox GPU smoke sequence completed; inspect validation output. No pixel/visual assertions were made.");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Fixtures fixtures;
        AssetTests(fixtures);
        if (argc > 1 && std::string_view(argv[1]) == "--gpu-smoke") GpuSmoke(fixtures);
        return 0;
    }
    catch (const std::exception& error)
    {
        RAG_LOG_ERROR("Skybox test failed: ", error.what());
        return 1;
    }
}
