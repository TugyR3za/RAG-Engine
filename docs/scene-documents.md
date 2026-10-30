# Scene documents: New, Save, Save As, Load

The editor edits one scene document at a time. A document is either **backed
by a file**, because it was opened from that file or last saved to it, or
**Untitled**, meaning no file belongs to it yet. The current document is shown:

- in the menu bar: `Scene: default.ragscene` (hover for the full path) or
  `Scene: Untitled`;
- at the top of the File menu: the full path, or
  `Untitled (no file yet; Save asks for a name)`.

The rules live in `sandbox/SceneDocument.h`, which is engine-free and
unit-tested. The sandbox supplies the actual file reads and writes.

## Operations

| Operation | Result | On failure |
|---|---|---|
| **New Scene** | Replaces the scene with a camera and a sun. The document becomes **Untitled**, and the File-menu name field is cleared. | n/a |
| **Save Scene**, file-backed | Rewrites the document's own file. | The document and scene are unchanged. A file that cannot be opened for writing (read-only, for example) is left as it was. See "Saves are not atomic" below. |
| **Save Scene...**, Untitled | **Writes nothing.** It opens the Save As prompt. It never falls back to the previously open file or to `default.ragscene`. | n/a |
| **Save As** (prompt, or File menu field + item) | Writes `scenes/<name>`. Only after that write succeeds does the file become the document. | The previous file (or Untitled) and the in-memory scene are kept. The prompt stays open so the name can be fixed. |
| **Save As prompt, Cancel or Escape** | Closes the prompt. Nothing is written, and the document stays as it was. | n/a |
| **Load Scene** | Parses the file fully, then replaces the scene. The loaded file becomes the document. | For a missing, unreadable, malformed or incompatible file, the current scene and document are kept. |
| **Play / Stop** | The document is untouched: a saved document is still that file, and Untitled stays Untitled. | n/a |

Further details:

- **Empty names.** Save As needs a non-empty name. The File-menu Save As item
  is disabled while the field is empty, and the prompt's Save button is
  disabled too. An empty field never turns into `default.ragscene`. Load still
  uses `default.ragscene` for an empty field, because Load never writes.
- **Extensions.** `.ragscene` is appended to a name that has no extension.
- **Overwriting.** The prompt shows the target path. If a file with that name
  already exists, it says so ("will be replaced"). There is no second
  confirmation.
- **During a run.** All of these operations are refused while Playing or Paused
  (`RefuseDocumentOperation`). A refused Save does not open the prompt.

## Startup: the default scene

On launch the sandbox looks for `default.ragscene` in two places, in order:

1. `<working directory>/scenes/`
2. `<executable directory>/scenes/`, a legacy fallback

The log names each candidate and why it was skipped, for example:

```text
Scene files: Save As writes to '<working directory>\scenes'.
Default scene: '<path>' (working directory): not found.
Default scene: the executable directory candidate is the same path as the working directory candidate; not checked twice.
No usable default scene file (checked <path>: not found); building the built-in scene in memory as an Untitled document. ...
```

If a file is found but fails to load, a `Could not load scene from ...` warning
gives the parse or version error before the fallback line.

**The built-in fallback scene is Untitled.** Pressing Save asks for a name; it
never creates or overwrites a `default.ragscene`. Nothing is ever regenerated
automatically.

### Direct launch vs `tools\build-and-run.ps1`

Both runs were observed on 2026-10-29 with `--smoke-test`:

| | `.\tools\build-and-run.ps1` | `build\skybox\Debug\RagSandbox.exe` started from its own folder |
|---|---|---|
| Working directory | repository root | `build\skybox\Debug` |
| Default scene | `scenes\default.ragscene` **found and loaded** (14 entities) | `build\skybox\Debug\scenes\default.ragscene` **not found**. That folder exists but is empty. The executable candidate is the same path. |
| Document at startup | `default.ragscene` | Untitled (built-in scene) |
| Save As writes to | `<repo>\scenes\` | `build\skybox\Debug\scenes\` |

This is by design, not a loading bug. The build copies `assets/` and `models/`
next to the executable but deliberately does **not** copy `scenes/`. The
source `scenes/` folder is the only copy, and it is found only when the
working directory is the repository root. That is the case for the script,
for Visual Studio (`VS_DEBUGGER_WORKING_DIRECTORY`) and for `ctest`. To use
the source scenes from a direct launch, start the executable with the
repository root as its working directory.

## Limitations

- **No unsaved-change tracking.** There is no dirty flag, so this is not full
  unsaved-change protection. New Scene, Load Scene and quitting all discard
  in-memory edits without a prompt, Untitled or not.
- **Saves are not atomic.** A write that fails after the file was opened, such
  as a full disk, can leave that file truncated. A file that cannot be opened
  is never modified (read-only file, directory in the way).
- **The name field accepts paths.** The Save As/Load name is joined to
  `scenes/`, so an absolute path or `..` in the field writes outside `scenes/`.
  This is unchanged from before. The prompt always shows the resolved path.
- **Escape** cancels the Save As prompt while it is open. Everywhere else it
  still quits immediately.

## Tests

- `SceneDocument` (`tests/SceneDocumentTests.cpp`, unit). Uses real scene
  serialization and real files in a fresh temp folder. Covers:
  - Open A -> New -> Save writes nothing, and A stays byte-identical;
  - cancelled and empty-name Save As write nothing;
  - Save As B makes B the document and leaves A unchanged, and a later Save
    rewrites B;
  - failed Save As, failed Save (read-only file) and failed Open keep the
    previous identity, the file bytes and the scene.
- `SandboxPlayMode` (label `gpu`). The same sequence in the real editor,
  through the File-menu handlers. Also covers the prompt's Cancel and Play/Stop
  with both a saved and an Untitled document. See [play-mode.md](play-mode.md).
