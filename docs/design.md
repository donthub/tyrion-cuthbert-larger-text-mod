# Design and interface contract

This file is for maintainers. It describes how the parts of the mod fit together
and the file formats they share. The requirements live in `REQUIREMENTS.md`.

## Components

| Part | Language | Location in repo | Shipped as |
| --- | --- | --- | --- |
| Loader + runtime component | C++17 / C (Zig toolchain) | `runtime/` | `TyrionCuthbertLargerTextMod/runtime/version.dll`, copied to `<game>/version.dll` by Install |
| Installer, uninstaller, status | Python 3.8+ standard library only | `tool/larger_text_mod.py` | `TyrionCuthbertLargerTextMod/larger_text_mod.py` |
| Self-updater | Python 3.8+ standard library only | `tool/update_mod.py` | `TyrionCuthbertLargerTextMod/update_mod.py` |
| Launchers | batch / POSIX sh | `tool/*.bat`, `tool/*.sh` | in the mod folder |
| Build, packaging | Python | `scripts/` | not shipped |

### Why a native proxy loader

The game is Unity 6000.5.8f1 with IL2CPP metadata v107. BepInEx 6 and MelonLoader
both generate interop assemblies with Cpp2IL, which (as of November 2026) rejects
metadata versions above 106, so neither loads into this build. The runtime component
here uses the IL2CPP **C API exported by GameAssembly.dll** (`il2cpp_class_from_name`,
`il2cpp_runtime_invoke`, ...). That API doesn't depend on the metadata file format,
so game and engine updates don't break loading as long as Unity keeps the exports.

Loading sequence:

1. `UnityPlayer.dll` imports `VERSION.dll`; Windows (and Wine with
   `WINEDLLOVERRIDES="version=n,b"`) loads `<game>/version.dll` first.
2. `version.dll` forwards all 17 version APIs to the system DLL, then patches
   UnityPlayer's import of `GetProcAddress`.
3. When UnityPlayer resolves `il2cpp_init`, it receives a wrapper. After the real
   `il2cpp_init` returns (main thread, no scene loaded yet) the runtime starts:
   reads the config, opens the log, resolves classes and installs method hooks
   with MinHook.
4. Hooks on the game's UI controllers (`Awake`, `SetImage`) apply each feature to
   the new instance before it is first rendered. A hook on
   `UnityEngine.Canvas.SendWillRenderCanvases` gives a per-frame tick right before
   the canvas layout pass, used for the evidence-text overflow and focus handling.

### Runtime notes (learned in-game, Unity 6000.5.8f1)

- **GC handles are pointer-sized.** Since Unity 2023, `il2cpp_gchandle_new` returns
  a pointer-sized handle. Declaring it `uint32_t` truncates it and crashes the game.
- **Stripped members.** `Application.set_runInBackground` is stripped from the
  managed code. The runtime calls the engine's internal call instead
  (`il2cpp_resolve_icall`).
- **Hook points.** The runtime hooks these game methods:
  - `VisualNovel.DialogueBox.Awake`: dialogue, narrator and the "Testimony GUI".
  - `VisualNovel.TextLogGUI.Awake`, `Courtroom.ItemMenu.Awake` and
    `UIBase.AnimatedGUIObject.Awake` (the latter for the "Add Item Popup").
  - `Courtroom.AddItemDisplay.YarnAddItem` / `YarnUpdateItem`.
  - `VisualNovel.ImageDisplay.SetImage` and `UIBase.GUIObject.Show`.
- **Text log line restore (FR-UI-19).** Super Text Mesh's `rememberReadPosition`
  (the original mod's fix) has no effect in this build. While the log is open, the
  dialogue text is rebuilt and a fully revealed line is drawn unrevealed. The
  runtime records which line was fully revealed, and after the log closes it calls
  `SuperTextMesh.SetMesh(readTime)` for that same line. That call only redraws:
  it does not start, stop or re-time reading and fires no events.
- **Shrink to fit (FR-UI-0d).** Three mechanisms, each with the floor "never below
  the game's own size":
  - Nameplate and character-info labels: TextMesh Pro auto-sizing, from the enlarged
    size down to the original size (or the game's own auto-size minimum).
  - Dialogue, narrator and testimony text: Super Text Mesh's own best fit
    (`bestFit = BestFitMode.MultilineFull`, `uiLimit = true`), looked up by name at
    run time. STM computes it when it builds the line, so reveal and events are
    unchanged. Separate units (`dialogue_text_fit`, `cross_examination_text_fit`),
    so an STM change only loses the fitting. They are skipped when the game already
    uses a best-fit or vertical-limit mode.
  - Evidence description and popup: the per-frame tick finds the largest size at
    which the description, wrapped around the picture, ends above where the
    original description ended (evidence menu; the details and View button stay
    beside the picture, under the name) or the panel stays on screen (popup), by
    bisection with `ForceMeshUpdate`. It runs only when the text or width
    changes, plus every 30 frames.
- **Description around the picture (FR-UI-22, FR-UI-33).** TextMesh Pro can't
  wrap one text box around a region, so the game's description component (the
  head, beside the picture) uses linked overflow into a continuation component
  that is full row width. Both sit in the text column on one line grid: with
  line pitch `p` and one-line height `h`, the head holds the `n = ceil(room / p)`
  lines that start beside the picture (height `h + (n-1)p`), and the
  continuation starts at `n·p` below the head's top, i.e. on the next grid line
  and never beside the picture. `p` and `h` come from
  `TMP_Text.GetPreferredValues` with sample text ("A", and "A A" wrapped onto
  two lines) on an invisible clone of the description (`LTM Line Probe`), so the
  measuring never touches the displayed text and follows font changes from the
  localization font switcher.
  The room beside the picture includes its bottom padding, like a CSS float's
  margin, so the first full-width line starts below the padding. When detail
  rows are shown, the description starts at `max(below the details/View,
  picture + padding)`, so the head gets no lines and all text goes to the
  continuation. A row counts only when it is active in the hierarchy (the
  game may hide the whole details area and leave stale row texts) and its name
  or value has visible text after removing rich-text tags and whitespace,
  because TextMesh Pro reports a line height even for blank text.
- **Picture padding (FR-UI-27a).** The container is the nearest ancestor of the
  picture row with a `Graphic` (the panel background). The padding is its right
  edge minus the picture's right edge, both mapped into the row's space with
  `Transform.TransformPoint` / `InverseTransformPoint` (`Ui::bounds_in`). It
  sets the evidence row's `HorizontalLayoutGroup` spacing and the popup's text
  column inset, and is re-measured with each layout pass. Without such an
  ancestor, or if the gap is not between 0.5 and half the picture's width, the
  game's own gap is kept (row spacing, or 30 in the popup).
- **Unattended in-game checks.** `[debug] commands = true` enables a command file
  (see the README).

## Mod folder layout (installed game)

```
<game>/
  Tyrion1.exe, UnityPlayer.dll, GameAssembly.dll, Tyrion1_Data/ ...   (never modified)
  version.dll                         <- added by Install (copy of runtime/version.dll)
  TyrionCuthbertLargerTextMod/
    README.md
    VERSION                           <- e.g. "2.0.0"
    larger_text_mod.py
    update_mod.py
    Install.bat Uninstall.bat Update.bat Status.bat      (Windows archive)
    Install.sh  Uninstall.sh  Update.sh  Status.sh       (Linux archive)
    runtime/version.dll
    licenses/MinHook-LICENSE.txt
    config.ini                        <- written by Install, owned by the mod
    manifest.json                     <- written by Install
    runtime.log                       <- written by the runtime at each game start
```

## Release archives

- `TyrionCuthbertLargerTextMod-vX.Y.Z-windows.zip`
- `TyrionCuthbertLargerTextMod-vX.Y.Z-linux.tar.gz`

Both contain exactly one top-level folder `TyrionCuthbertLargerTextMod/` with the
shipped files listed above (each archive only has its own platform's launchers).

**The updater's allowlist is frozen into every released `update_mod.py`.** A
release that adds a file is rejected by older updaters. Add new files only together
with a note in the release that players must download the archive manually once.

## config.ini

INI syntax: `[section]`, `key = value`, comments start with `;` or `#`.
Keys are case-insensitive. Unknown sections and keys are kept by Install and ignored
by the runtime. Booleans accept `true/false/yes/no/on/off/1/0`.

```ini
[general]
enabled = true      ; master switch
scale = 1.5         ; 1.1 .. 2.0
opacity = 0.99      ; 0.0 .. 1.0

[features]
dialogue_box = true
cross_examination = true
story_images = true
text_log = true
evidence_menu = true
evidence_notification = true
pause_when_unfocused = true
```

Missing, malformed or out-of-range values fall back to the default and are logged
as warnings by the runtime.

## manifest.json

```json
{
  "format": 1,
  "mod_version": "2.0.0",
  "installed_at": "2026-11-14T12:00:00Z",
  "files": [
    {"path": "version.dll", "sha256": "<hex>", "kind": "loader"},
    {"path": "TyrionCuthbertLargerTextMod/config.ini", "sha256": "<hex>", "kind": "config"}
  ],
  "generated": ["TyrionCuthbertLargerTextMod/runtime.log", "TyrionCuthbertLargerTextMod/runtime.log.1"],
  "preexisting": [{"path": "version.dll", "sha256": "<hex>"}],
  "created_dirs": []
}
```

Paths are relative to the game folder with forward slashes. `files` lists every file
Install added; `generated` lists files the runtime may create (removed on uninstall if
present); `preexisting` lists files that existed before install and are never removed.
`kind` is `loader`, `config` or `other`. Config and generated files are removed on
uninstall even when edited; other files are only removed when their hash still
matches.

## Runtime log

`TyrionCuthbertLargerTextMod/runtime.log`, rotated to `runtime.log.1` at each start.
Every line is `YYYY-MM-DD HH:MM:SS LEVEL message`. Feature outcomes use fixed
wording that Status parses:

```
FEATURE <key> applied
FEATURE <key> skipped: <reason>
FEATURE <key> failed: <reason>
FEATURE <key> disabled
```

A feature is reported `applied` once, when it is applied to the first instance.
`skipped` means a UI structure mismatch, `failed` means an internal error. Both
disable the feature for the rest of the session.

## Platform notes

- Proton/Wine: Steam launch option `WINEDLLOVERRIDES="version=n,b" %command%`
  (one-time, survives mod updates).
- Game process name: `Tyrion1.exe`.
- Supported engine: Unity 6000.x (read from the `Tyrion1_Data/data.unity3d` header),
  IL2CPP backend (`GameAssembly.dll` and `Tyrion1_Data/il2cpp_data/Metadata/global-metadata.dat`
  present), and `UnityPlayer.dll` importing `VERSION.dll`.
