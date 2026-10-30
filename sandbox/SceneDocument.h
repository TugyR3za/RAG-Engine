#pragma once

#include "Rag/Core/Base.h"

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace rag::runtime
{
    enum class DocumentSaveResult : u8
    {
        Saved,     // written to the document's own file
        NeedsPath, // Untitled: nothing was written; the caller must ask for a path
        Failed     // refused or the write failed; the document is unchanged
    };

    // Which file the scene being edited belongs to, with no engine dependencies
    // so it can be unit-tested against real files. A document is either backed
    // by a file (opened from it or last saved to it) or Untitled (New Scene, or a
    // startup scene built in memory because no default scene file loaded). The
    // owner does the reading and writing through callbacks,
    //   bool(const std::filesystem::path&, std::string& error),
    // and this class guarantees the identity rules around them:
    //
    //   * Save on an Untitled document never writes anything, anywhere. It
    //     returns NeedsPath so the editor can ask for a file name; it never falls
    //     back to the previously open file or to the default scene.
    //   * Save As and Open adopt the new path only after the write/read
    //     succeeded. A failure keeps the previous path (or Untitled).
    //   * A Save As / Open path without a file name is refused before the
    //     callback runs.
    //
    // There is no dirty flag: New and Open replace in-memory edits without
    // asking, and nothing here knows whether the scene differs from its file.
    class SceneDocument
    {
    public:
        [[nodiscard]] bool IsUntitled() const { return path_.empty(); }

        // The backing file, or an empty path while Untitled.
        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

        // The backing file's name for the editor, or "Untitled".
        [[nodiscard]] std::string DisplayName() const
        {
            return IsUntitled() ? std::string("Untitled") : path_.filename().string();
        }

        // The scene no longer belongs to any file, so a later Save cannot reach
        // the file it came from.
        void MakeUntitled() { path_.clear(); }

        template <typename Write>
        DocumentSaveResult Save(Write&& write, std::string& out_error) const
        {
            if (IsUntitled())
            {
                out_error = "The scene is Untitled; choose a file name with Save As.";
                return DocumentSaveResult::NeedsPath;
            }
            return Run(std::forward<Write>(write), path_, "The scene could not be saved.", out_error)
                ? DocumentSaveResult::Saved
                : DocumentSaveResult::Failed;
        }

        template <typename Write>
        bool SaveAs(const std::filesystem::path& path, Write&& write, std::string& out_error)
        {
            if (!path.has_filename())
            {
                out_error = "Save As needs a file name.";
                return false;
            }
            if (!Run(std::forward<Write>(write), path, "The scene could not be saved.", out_error))
            {
                return false;
            }
            path_ = path;
            return true;
        }

        // read must leave the live scene untouched when it returns false (parse
        // first, replace the scene only once the file proved valid).
        template <typename Read>
        bool Open(const std::filesystem::path& path, Read&& read, std::string& out_error)
        {
            if (!path.has_filename())
            {
                out_error = "Open needs a file name.";
                return false;
            }
            if (!Run(std::forward<Read>(read), path, "The scene could not be loaded.", out_error))
            {
                return false;
            }
            path_ = path;
            return true;
        }

    private:
        template <typename Callback>
        static bool Run(
            Callback&& callback,
            const std::filesystem::path& path,
            const char* fallback_error,
            std::string& out_error)
        {
            bool succeeded = false;
            try
            {
                succeeded = std::forward<Callback>(callback)(path, out_error);
            }
            catch (const std::exception& exception)
            {
                out_error = exception.what();
                succeeded = false;
            }
            if (!succeeded && out_error.empty())
            {
                out_error = fallback_error;
            }
            return succeeded;
        }

        std::filesystem::path path_;
    };

    // The editor's file-name field as a scene file name. Surrounding whitespace
    // is ignored and ".ragscene" is appended to a name with no extension. An
    // empty (or all-whitespace) field yields "", so the caller has to decide
    // what that means: Save As refuses it rather than picking a default file.
    [[nodiscard]] inline std::string SceneFileNameFromField(std::string_view text)
    {
        constexpr std::string_view Whitespace = " \t\r\n";
        const std::size_t first = text.find_first_not_of(Whitespace);
        if (first == std::string_view::npos)
        {
            return {};
        }
        const std::size_t last = text.find_last_not_of(Whitespace);
        std::string name(text.substr(first, last - first + 1));
        if (name.find('.') == std::string::npos)
        {
            name += ".ragscene";
        }
        return name;
    }
}
