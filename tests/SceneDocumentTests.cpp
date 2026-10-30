#include "SceneDocument.h"

#include "Rag/Scene/SceneSerialization.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    using rag::runtime::DocumentSaveResult;
    using rag::runtime::SceneDocument;
    using rag::runtime::SceneFileNameFromField;

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Scene document test failed: " << message << '\n';
        }
        return condition;
    }

    // A fresh folder under the system temp directory, removed when the test
    // ends. Every file these tests write lives inside it; nothing touches the
    // repository's scenes/ folder.
    class TempFolder
    {
    public:
        explicit TempFolder(const char* label)
        {
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = std::filesystem::temp_directory_path() /
                ("rag-scene-document-test-" + std::string(label) + "-" + std::to_string(ticks));
            std::filesystem::create_directories(path_);
        }

        ~TempFolder()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        TempFolder(const TempFolder&) = delete;
        TempFolder& operator=(const TempFolder&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
        [[nodiscard]] std::filesystem::path operator/(const char* name) const { return path_ / name; }

        // Sorted entry names, so a check can prove nothing was created or removed.
        [[nodiscard]] std::vector<std::string> Listing() const
        {
            std::vector<std::string> names;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path_))
            {
                names.push_back(entry.path().filename().string());
            }
            std::sort(names.begin(), names.end());
            return names;
        }

    private:
        std::filesystem::path path_;
    };

    [[nodiscard]] std::optional<std::string> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            return std::nullopt;
        }
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << text;
    }

    // A recognizable scene: one entity whose name says which document it is.
    [[nodiscard]] rag::scene::SerializedScene SceneNamed(const char* name)
    {
        rag::scene::SerializedScene scene;
        rag::scene::SerializedEntity entity;
        entity.name = name;
        entity.transform = rag::scene::SerializedTransform{};
        scene.entities.push_back(entity);
        return scene;
    }

    // Exactly what SaveSceneToFile writes for a scene.
    [[nodiscard]] std::string FileBytesFor(const rag::scene::SerializedScene& scene)
    {
        return rag::scene::WriteSceneToJson(scene) + "\n";
    }

    // Stand-in for the editor: the live scene, plus the write/read callbacks
    // the sandbox hands SceneDocument (real scene serialization, real files).
    struct FakeEditor
    {
        rag::scene::SerializedScene scene;
        int writes = 0;
        int reads = 0;

        [[nodiscard]] std::string SceneJson() const { return rag::scene::WriteSceneToJson(scene); }

        auto Writer()
        {
            return [this](const std::filesystem::path& path, std::string& error)
            {
                ++writes;
                return rag::scene::SaveSceneToFile(scene, path, error);
            };
        }

        // Like the sandbox: parse into a local first and replace the live scene
        // only after the file proved valid.
        auto Reader()
        {
            return [this](const std::filesystem::path& path, std::string& error)
            {
                ++reads;
                rag::scene::SerializedScene loaded;
                if (!rag::scene::LoadSceneFromFile(path, loaded, error))
                {
                    return false;
                }
                scene = std::move(loaded);
                return true;
            };
        }
    };

    // Open A -> New -> Save must not write A (or anything else).
    bool NewSceneCannotOverwritePreviousFileTests()
    {
        TempFolder folder("new");
        const std::filesystem::path a = folder / "A.ragscene";
        std::string error;
        bool passed = Expect(rag::scene::SaveSceneToFile(SceneNamed("A"), a, error), "Fixture A must be written.");
        const std::optional<std::string> a_bytes = ReadFile(a);

        SceneDocument document;
        FakeEditor editor;
        passed = Expect(document.Open(a, editor.Reader(), error), "Opening A must succeed.") && passed;
        passed = Expect(document.Path() == a && !document.IsUntitled(), "Open must make A the document.") && passed;
        passed = Expect(document.DisplayName() == "A.ragscene", "The document must be shown by its file name.") && passed;

        // New Scene: the sandbox replaces the scene and makes the document Untitled.
        editor.scene = SceneNamed("New");
        document.MakeUntitled();
        passed = Expect(document.IsUntitled() && document.Path().empty(), "New must leave no active save path.") && passed;
        passed = Expect(document.DisplayName() == "Untitled", "New must be shown as Untitled.") && passed;

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            error.clear();
            passed = Expect(document.Save(editor.Writer(), error) == DocumentSaveResult::NeedsPath,
                "Save on an Untitled document must ask for a path.") && passed;
            passed = Expect(!error.empty(), "Save on an Untitled document must explain why nothing was written.") && passed;
        }
        passed = Expect(editor.writes == 0, "Save on an Untitled document must never run the writer.") && passed;
        passed = Expect(ReadFile(a) == a_bytes, "A's bytes must be unchanged after New -> Save.") && passed;
        passed = Expect(folder.Listing() == std::vector<std::string>{"A.ragscene"},
            "New -> Save must create no file.") && passed;
        passed = Expect(document.IsUntitled(), "The document must stay Untitled after the refused Save.") && passed;
        return passed;
    }

    // The prompt Save opens can be cancelled: nothing is written and the
    // document stays Untitled. An empty name never reaches the writer.
    bool CancelledSaveAsTests()
    {
        TempFolder folder("cancel");
        SceneDocument document;
        FakeEditor editor{SceneNamed("Untitled work")};
        const std::string scene_json = editor.SceneJson();
        std::string error;

        bool passed = Expect(document.Save(editor.Writer(), error) == DocumentSaveResult::NeedsPath,
            "Save on a fresh document must ask for a path.");
        // Cancel = the editor closes the prompt without calling SaveAs.
        passed = Expect(folder.Listing().empty(), "Cancelling Save As must write nothing.") && passed;
        passed = Expect(document.IsUntitled(), "Cancelling Save As must keep the document Untitled.") && passed;
        passed = Expect(editor.SceneJson() == scene_json, "Cancelling Save As must keep the scene.") && passed;

        // An empty field resolves to no file name at all, and a path without a
        // file name is refused before anything is written.
        passed = Expect(SceneFileNameFromField("").empty() && SceneFileNameFromField("   \t").empty(),
            "An empty file-name field must not resolve to any file (not default.ragscene).") && passed;
        passed = Expect(!document.SaveAs(std::filesystem::path{}, editor.Writer(), error),
            "Save As with an empty path must be refused.") && passed;
        passed = Expect(!document.SaveAs(folder.Path() / "", editor.Writer(), error),
            "Save As to a folder with no file name must be refused.") && passed;
        passed = Expect(editor.writes == 0 && folder.Listing().empty(),
            "Refused Save As attempts must write nothing.") && passed;
        passed = Expect(document.IsUntitled(), "Refused Save As attempts must keep the document Untitled.") && passed;
        return passed;
    }

    // Untitled -> Save As B: B holds the scene, A is untouched, B becomes the
    // document, and the next Save rewrites B (not A).
    bool SaveUntitledAsNewFileTests()
    {
        TempFolder folder("saveas");
        const std::filesystem::path a = folder / "A.ragscene";
        const std::filesystem::path b = folder / "B.ragscene";
        std::string error;
        bool passed = Expect(rag::scene::SaveSceneToFile(SceneNamed("A"), a, error), "Fixture A must be written.");
        const std::optional<std::string> a_bytes = ReadFile(a);

        SceneDocument document;
        FakeEditor editor;
        passed = Expect(document.Open(a, editor.Reader(), error), "Opening A must succeed.") && passed;
        editor.scene = SceneNamed("New");
        document.MakeUntitled();

        passed = Expect(document.SaveAs(b, editor.Writer(), error), "Save As B must succeed.") && passed;
        passed = Expect(document.Path() == b && document.DisplayName() == "B.ragscene",
            "A successful Save As must make B the document.") && passed;
        passed = Expect(ReadFile(b) == FileBytesFor(SceneNamed("New")), "B must hold exactly the Untitled scene.") && passed;
        passed = Expect(ReadFile(a) == a_bytes, "Save As B must leave A's bytes unchanged.") && passed;

        editor.scene.entities[0].transform->position = rag::math::Vec3{1.0f, 2.0f, 3.0f};
        passed = Expect(document.Save(editor.Writer(), error) == DocumentSaveResult::Saved,
            "Save after Save As must write the document's file.") && passed;
        passed = Expect(ReadFile(b) == FileBytesFor(editor.scene), "Save must rewrite B with the edited scene.") && passed;
        passed = Expect(ReadFile(a) == a_bytes, "Save after Save As B must leave A unchanged.") && passed;
        passed = Expect(folder.Listing() == std::vector<std::string>{"A.ragscene", "B.ragscene"},
            "Only A and B may exist.") && passed;
        return passed;
    }

    // A failed write keeps the previous document identity, the file on disk and
    // the scene in memory.
    bool FailedSaveTests()
    {
        TempFolder folder("failsave");
        const std::filesystem::path b = folder / "B.ragscene";
        // A directory where the file should go makes the write fail.
        const std::filesystem::path blocked = folder / "blocked.ragscene";
        std::filesystem::create_directories(blocked);
        std::string error;

        // Untitled + failed Save As stays Untitled.
        SceneDocument document;
        FakeEditor editor{SceneNamed("Work")};
        bool passed = Expect(!document.SaveAs(blocked, editor.Writer(), error), "Save As onto a directory must fail.");
        passed = Expect(!error.empty(), "A failed Save As must report why.") && passed;
        passed = Expect(document.IsUntitled(), "A failed Save As must keep an Untitled document Untitled.") && passed;

        // Saved as B + failed Save As keeps B.
        passed = Expect(document.SaveAs(b, editor.Writer(), error), "Save As B must succeed.") && passed;
        const std::optional<std::string> b_bytes = ReadFile(b);
        const std::string scene_json = editor.SceneJson();
        error.clear();
        passed = Expect(!document.SaveAs(blocked, editor.Writer(), error), "Save As onto a directory must fail again.") && passed;
        passed = Expect(document.Path() == b, "A failed Save As must keep B as the document.") && passed;
        passed = Expect(ReadFile(b) == b_bytes, "A failed Save As must leave B's bytes unchanged.") && passed;
        passed = Expect(editor.SceneJson() == scene_json, "A failed Save As must keep the scene in memory.") && passed;
        passed = Expect(std::filesystem::is_directory(blocked), "A failed Save As must not replace the blocker.") && passed;

        // A writer that throws is a failure too.
        error.clear();
        const auto throwing_writer = [](const std::filesystem::path&, std::string&) -> bool
        {
            throw std::runtime_error("disk exploded");
        };
        passed = Expect(!document.SaveAs(folder / "C.ragscene", throwing_writer, error) && error == "disk exploded",
            "A throwing writer must fail Save As with its message.") && passed;
        passed = Expect(document.Path() == b, "A throwing writer must keep B as the document.") && passed;

        // A failed Save to the document's own (read-only) file keeps everything.
        // (Windows maps "no write permission at all" to the read-only attribute.)
        constexpr std::filesystem::perms AllWrite = std::filesystem::perms::owner_write |
            std::filesystem::perms::group_write | std::filesystem::perms::others_write;
        std::filesystem::permissions(b, AllWrite, std::filesystem::perm_options::remove);
        editor.scene = SceneNamed("Edited");
        error.clear();
        passed = Expect(document.Save(editor.Writer(), error) == DocumentSaveResult::Failed,
            "Save to a read-only document file must fail.") && passed;
        passed = Expect(document.Path() == b, "A failed Save must keep B as the document.") && passed;
        passed = Expect(ReadFile(b) == b_bytes, "A failed Save must leave B's bytes unchanged.") && passed;
        std::filesystem::permissions(b, AllWrite, std::filesystem::perm_options::add);

        passed = Expect(folder.Listing() == std::vector<std::string>{"B.ragscene", "blocked.ragscene"},
            "Failed saves must create no file.") && passed;
        return passed;
    }

    // A failed Open keeps the scene in memory and the document identity; a
    // successful one adopts the opened file.
    bool OpenTests()
    {
        TempFolder folder("open");
        const std::filesystem::path a = folder / "A.ragscene";
        const std::filesystem::path b = folder / "B.ragscene";
        const std::filesystem::path corrupt = folder / "corrupt.ragscene";
        std::string error;
        bool passed = Expect(rag::scene::SaveSceneToFile(SceneNamed("A"), a, error), "Fixture A must be written.");
        WriteFile(corrupt, "{ this is not a scene");
        const std::optional<std::string> corrupt_bytes = ReadFile(corrupt);

        SceneDocument document;
        FakeEditor editor{SceneNamed("B")};
        passed = Expect(document.SaveAs(b, editor.Writer(), error), "Save As B must succeed.") && passed;
        const std::optional<std::string> b_bytes = ReadFile(b);
        const std::string scene_json = editor.SceneJson();

        for (const std::filesystem::path& bad : {corrupt, folder / "missing.ragscene"})
        {
            error.clear();
            passed = Expect(!document.Open(bad, editor.Reader(), error), "Opening a bad file must fail.") && passed;
            passed = Expect(!error.empty(), "A failed Open must report why.") && passed;
            passed = Expect(document.Path() == b, "A failed Open must keep B as the document.") && passed;
            passed = Expect(editor.SceneJson() == scene_json, "A failed Open must keep the scene in memory.") && passed;
        }
        const int reads_before_empty = editor.reads;
        passed = Expect(!document.Open(std::filesystem::path{}, editor.Reader(), error), "Opening an empty path must fail.") && passed;
        passed = Expect(editor.reads == reads_before_empty, "An empty Open path must not reach the reader.") && passed;
        passed = Expect(ReadFile(b) == b_bytes && ReadFile(corrupt) == corrupt_bytes,
            "Failed Opens must not modify any file.") && passed;

        // A failed Open on an Untitled document stays Untitled.
        SceneDocument untitled;
        passed = Expect(!untitled.Open(corrupt, editor.Reader(), error) && untitled.IsUntitled(),
            "A failed Open must keep an Untitled document Untitled.") && passed;

        passed = Expect(document.Open(a, editor.Reader(), error), "Opening A must succeed.") && passed;
        passed = Expect(document.Path() == a, "A successful Open must make A the document.") && passed;
        passed = Expect(editor.SceneJson() == rag::scene::WriteSceneToJson(SceneNamed("A")),
            "A successful Open must load A's scene.") && passed;
        passed = Expect(ReadFile(b) == b_bytes, "Opening A must leave B unchanged.") && passed;
        return passed;
    }

    bool FileNameFieldTests()
    {
        bool passed = Expect(SceneFileNameFromField("level") == "level.ragscene", "A bare name gets .ragscene.");
        passed = Expect(SceneFileNameFromField("  level \t") == "level.ragscene", "Surrounding whitespace is ignored.") && passed;
        passed = Expect(SceneFileNameFromField("level.json") == "level.json", "An explicit extension is kept.") && passed;
        passed = Expect(SceneFileNameFromField("default") == "default.ragscene",
            "Typing 'default' explicitly still names the default scene.") && passed;
        return passed;
    }
}

int main()
{
    bool passed = true;
    try
    {
        passed = NewSceneCannotOverwritePreviousFileTests() && passed;
        passed = CancelledSaveAsTests() && passed;
        passed = SaveUntitledAsNewFileTests() && passed;
        passed = FailedSaveTests() && passed;
        passed = OpenTests() && passed;
        passed = FileNameFieldTests() && passed;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Scene document test threw: " << exception.what() << '\n';
        passed = false;
    }
    if (!passed)
    {
        return 1;
    }

    std::cout << "Scene document tests passed.\n";
    return 0;
}
