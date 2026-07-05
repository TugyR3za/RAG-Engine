#pragma once

#include "Rag/Core/Base.h"
#include "Rag/Platform/SystemPaths.h"
#include "Rag/Renderer/Renderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Developer-only Asset Browser model that deliberately lives OUTSIDE the engine
// (like EditorContext): it scans the project's on-disk asset folders and exposes
// a stable, sorted list the sandbox's "Assets" panel renders. It performs no
// Vulkan work and holds no GPU resources -- it is purely a filesystem view plus
// the click/selection state -- so it stays a plain header the editor includes.
namespace rag::editor
{
    // How the browser groups a file for display. Derived from the extension only;
    // the browser never opens a file to sniff its contents.
    enum class AssetType
    {
        Model,
        Texture,
        Other,
    };

    // One scanned file. Everything here is display-ready UTF-8; nothing points
    // back into the filesystem, so an entry stays valid after a rescan replaces
    // the folder it came from.
    struct AssetEntry
    {
        std::string name;      // file name, e.g. "sphere.obj"
        std::string path;      // absolute path, for the info pane
        std::string extension; // lower-cased, includes the dot, e.g. ".obj"
        AssetType type = AssetType::Other;

        // When set, this model can be dropped into the scene by reusing a mesh the
        // renderer already registered at startup (no runtime GPU upload). Empty for
        // textures, non-mesh files, and model files the renderer did not preload
        // -- importing arbitrary new meshes at runtime is a separate TODO (it needs
        // a renderer API to upload a mesh mid-run and hand back a MeshHandle).
        std::optional<renderer::MeshHandle> spawn_mesh;
    };

    // One source folder (e.g. models/, assets/) and the files found inside it.
    struct AssetFolder
    {
        std::string label;         // panel heading, e.g. "Models"
        std::string subdirectory;  // folder name resolved on disk, e.g. "models"
        std::string resolved_path; // absolute directory actually scanned, else ""
        bool found = false;        // false when the folder exists nowhere
        std::vector<AssetEntry> entries;
    };

    namespace detail
    {
        // std::filesystem::path::string() can throw when a path is not representable
        // in the active narrow code page; u8string() never does. Reinterpret its
        // bytes as UTF-8, which is exactly what ImGui renders.
        [[nodiscard]] inline std::string PathToUtf8(const std::filesystem::path& path)
        {
            const std::u8string utf8 = path.u8string();
            return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        }

        [[nodiscard]] inline std::string ToLowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        [[nodiscard]] inline bool CaseInsensitiveLess(const std::string& a, const std::string& b)
        {
            return std::lexicographical_compare(
                a.begin(), a.end(),
                b.begin(), b.end(),
                [](unsigned char x, unsigned char y)
                {
                    return std::tolower(x) < std::tolower(y);
                });
        }

        [[nodiscard]] inline AssetType ClassifyExtension(std::string_view lower_extension)
        {
            constexpr std::array model_extensions = {
                std::string_view{".obj"}, std::string_view{".glb"},
                std::string_view{".gltf"}, std::string_view{".fbx"},
                std::string_view{".dae"}, std::string_view{".ply"},
                std::string_view{".stl"}};
            constexpr std::array texture_extensions = {
                std::string_view{".png"}, std::string_view{".jpg"},
                std::string_view{".jpeg"}, std::string_view{".bmp"},
                std::string_view{".tga"}, std::string_view{".dds"},
                std::string_view{".ktx"}, std::string_view{".ktx2"},
                std::string_view{".hdr"}, std::string_view{".psd"}};

            for (std::string_view candidate : model_extensions)
            {
                if (lower_extension == candidate)
                {
                    return AssetType::Model;
                }
            }
            for (std::string_view candidate : texture_extensions)
            {
                if (lower_extension == candidate)
                {
                    return AssetType::Texture;
                }
            }
            return AssetType::Other;
        }

        // Maps a known model file to the renderer mesh slot the sandbox registers
        // for it at startup, so spawning reuses that mesh with zero GPU work. These
        // slots MIRROR the mesh registry convention in VulkanRenderer/BuildScene
        // (0 = cube, 1 = sphere.obj, 2 = ground, 3 = Duck.glb); keep them in sync
        // if that convention changes. An unrecognised model returns no handle.
        [[nodiscard]] inline std::optional<renderer::MeshHandle> SpawnMeshFor(
            const std::string& lower_name,
            AssetType type)
        {
            if (type != AssetType::Model)
            {
                return std::nullopt;
            }
            if (lower_name == "sphere.obj")
            {
                return renderer::MeshHandle{1, 0};
            }
            if (lower_name == "duck.glb")
            {
                return renderer::MeshHandle{3, 0};
            }
            return std::nullopt;
        }

        // Finds a folder by name, preferring next to the executable (where the
        // build copies models/ and assets/) and falling back to the working
        // directory, mirroring how the renderer resolves runtime assets. Returns
        // the first existing directory, or nothing when the folder is absent.
        [[nodiscard]] inline std::optional<std::filesystem::path> ResolveFolder(
            std::string_view subdirectory)
        {
            std::error_code error;
            const std::array<std::filesystem::path, 2> candidates = {
                platform::ExecutableDirectory() / subdirectory,
                std::filesystem::current_path(error) / subdirectory,
            };
            for (const std::filesystem::path& candidate : candidates)
            {
                if (std::filesystem::is_directory(candidate, error))
                {
                    return candidate;
                }
                error.clear();
            }
            return std::nullopt;
        }
    }

    // A short, human-readable name for an asset type, used as the list's type
    // column and in the info pane.
    [[nodiscard]] inline const char* AssetTypeLabel(AssetType type)
    {
        switch (type)
        {
            case AssetType::Model: return "Model";
            case AssetType::Texture: return "Texture";
            case AssetType::Other: return "File";
        }
        return "File";
    }

    // Scans a fixed set of project asset folders and holds the current click
    // selection. Rescanning is explicit (Refresh), so the panel decides when to
    // pick up newly added files -- typically when it is (re)opened, plus a manual
    // Refresh button. Selection is remembered by (folder, name) so it survives a
    // rescan that reorders or rebuilds the list.
    class AssetBrowser
    {
    public:
        AssetBrowser()
        {
            folders_.push_back(AssetFolder{"Models", "models", {}, false, {}});
            folders_.push_back(AssetFolder{"Textures", "assets", {}, false, {}});
        }

        [[nodiscard]] const std::vector<AssetFolder>& Folders() const { return folders_; }

        // Re-reads every configured folder from disk. Never throws: missing folders
        // and per-file errors are reported through AssetFolder::found rather than
        // exceptions, so it is safe to call from inside the UI build.
        void Refresh()
        {
            for (AssetFolder& folder : folders_)
            {
                folder.entries.clear();
                folder.resolved_path.clear();
                folder.found = false;

                const std::optional<std::filesystem::path> directory =
                    detail::ResolveFolder(folder.subdirectory);
                if (!directory.has_value())
                {
                    continue;
                }

                folder.found = true;
                folder.resolved_path = detail::PathToUtf8(*directory);

                std::error_code error;
                std::filesystem::directory_iterator iterator(*directory, error);
                const std::filesystem::directory_iterator end;
                for (; !error && iterator != end; iterator.increment(error))
                {
                    if (!iterator->is_regular_file(error))
                    {
                        error.clear();
                        continue;
                    }

                    const std::filesystem::path& file_path = iterator->path();
                    AssetEntry entry{};
                    entry.name = detail::PathToUtf8(file_path.filename());
                    entry.path = detail::PathToUtf8(file_path);
                    entry.extension = detail::ToLowerAscii(detail::PathToUtf8(file_path.extension()));
                    entry.type = detail::ClassifyExtension(entry.extension);
                    entry.spawn_mesh =
                        detail::SpawnMeshFor(detail::ToLowerAscii(entry.name), entry.type);
                    folder.entries.push_back(std::move(entry));
                }

                // Sort files alphabetically (case-insensitive) so the list reads
                // the same between runs regardless of directory-iteration order.
                std::sort(
                    folder.entries.begin(),
                    folder.entries.end(),
                    [](const AssetEntry& a, const AssetEntry& b)
                    {
                        return detail::CaseInsensitiveLess(a.name, b.name);
                    });
            }

            RevalidateSelection();
        }

        void Select(const AssetFolder& folder, const AssetEntry& entry)
        {
            selection_ = entry;
            selection_folder_subdirectory_ = folder.subdirectory;
            selection_folder_label_ = folder.label;
        }

        void ClearSelection()
        {
            selection_.reset();
            selection_folder_subdirectory_.clear();
            selection_folder_label_.clear();
        }

        [[nodiscard]] bool IsSelected(const AssetFolder& folder, const AssetEntry& entry) const
        {
            return selection_.has_value() &&
                   selection_folder_subdirectory_ == folder.subdirectory &&
                   selection_->name == entry.name;
        }

        [[nodiscard]] const AssetEntry* SelectedEntry() const
        {
            return selection_.has_value() ? &selection_.value() : nullptr;
        }

        [[nodiscard]] const std::string& SelectedFolderLabel() const
        {
            return selection_folder_label_;
        }

    private:
        // After a rescan, keep the selection only if the same file still exists in
        // the same folder (refreshing its copy in case its type changed); otherwise
        // drop it so the info pane never shows a file that is gone.
        void RevalidateSelection()
        {
            if (!selection_.has_value())
            {
                return;
            }
            for (const AssetFolder& folder : folders_)
            {
                if (folder.subdirectory != selection_folder_subdirectory_)
                {
                    continue;
                }
                for (const AssetEntry& entry : folder.entries)
                {
                    if (entry.name == selection_->name)
                    {
                        selection_ = entry;
                        return;
                    }
                }
            }
            ClearSelection();
        }

        std::vector<AssetFolder> folders_;
        std::optional<AssetEntry> selection_;
        std::string selection_folder_subdirectory_;
        std::string selection_folder_label_;
    };
}
