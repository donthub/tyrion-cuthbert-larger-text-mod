# Tyrion Cuthbert: Attorney of the Arcane – Larger Text Mod

Makes the game's text easier to read on small screens such as the Steam Deck:

- **Dialogue, narrator and cross-examination boxes:** larger text and taller panels. The normal dialogue panel is as wide as the screen. The panel background opacity is configurable.
- **Cross-examination:** *Press* and *Present* move to the top corners; the arrows sit above the panel.
- **Story images and building plans:** shrink so they stay above the taller dialogue panel.
- **Text log:** larger, wider, normal letter spacing. The current line no longer disappears when you close the log.
- **Evidence menu (Clues, Spells, People):** larger description. The picture is on the right, with the same padding on its left and below it as between it and the panel edge. The details and View button stay beside it under the item name. When there are details, the description starts below the picture at full width; otherwise it starts below the name and wraps around the picture: lines beside it are narrower, and the text runs on at full width below it, without a gap.
- **"Obtained / updated evidence" popup:** wider and larger, with the same picture-and-description layout: details beside the picture, and the description below the picture or, without details, wrapping around it.
- **Pause when unfocused:** the game no longer reacts to input while another window has focus.

Text that would not fit its box at the larger size (a very long line, name or description) is scaled down just enough to fit, but never below the game's own size.

The mod works at runtime: a small loader (`version.dll`) applies the changes in memory when the game starts. **No game file is modified**, save files are never touched, and Uninstall removes everything the mod added.

## Prerequisites

| Platform | You need |
| --- | --- |
| Windows 10/11 (64-bit) | **Python 3.8 or newer**, for the Install/Uninstall/Update scripts only: `winget install Python.Python.3.12`, or the installer from [python.org](https://www.python.org/downloads/) with *Add python.exe to PATH* ticked. |
| Steam Deck / Linux (Proton) | Nothing extra: SteamOS and common distributions ship `python3`. |

The game itself does not need Python. Nothing is installed system-wide and nothing is downloaded during installation.

## Installation

1. Download the archive for your platform from the [latest release](https://github.com/donthub/tyrion-cuthbert-larger-text-mod/releases/latest):
   - Windows: `TyrionCuthbertLargerTextMod-vX.Y.Z-windows.zip`
   - Steam Deck / Linux: `TyrionCuthbertLargerTextMod-vX.Y.Z-linux.tar.gz`
2. In Steam, right-click the game → *Manage* → *Browse local files*. Extract the archive there, so that the `TyrionCuthbertLargerTextMod` folder is directly inside the game folder (next to `Tyrion1.exe`).
3. Close the game, then run Install:
   - **Windows:** double-click `TyrionCuthbertLargerTextMod\Install.bat`.
   - **Steam Deck (Desktop Mode) / Linux:** open a terminal in the `TyrionCuthbertLargerTextMod` folder and run `./Install.sh`.

   For other settings, pass a scale (1.1–2.0, default 1.5) and an opacity (0.0–1.0, default 0.99): `Install.bat 1.3 0.8` or `./Install.sh 1.3 0.8`.
4. **Steam Deck / Linux only, once:** in Steam, open the game's *Properties* → *General* → *Launch Options* and enter

   ```
   WINEDLLOVERRIDES="version=n,b" %command%
   ```

   Proton otherwise ignores the mod's `version.dll`. The option stays valid for mod updates. On Windows, no extra step is needed.
5. Start the game normally.

If the original asset-patching version of this mod (v1.x) is installed, Install refuses. Run that version's `Uninstall` first, or use Steam's *Verify integrity of game files*, then install this version.

## Configuration

Install writes `TyrionCuthbertLargerTextMod/config.ini`. Edit it with any text editor; changes take effect at the next game start. Re-running Install changes only `scale` and `opacity` and keeps everything else.

```ini
[general]
enabled = true          ; master switch: false = the mod changes nothing
scale = 1.5             ; text size, 1.1 to 2.0
opacity = 0.99          ; dialogue panel background, 0.0 (transparent) to 1.0

[features]
dialogue_box = true           ; dialogue and narrator boxes
cross_examination = true      ; testimony box, Press/Present, arrows
story_images = true           ; story images and building plans
text_log = true               ; text log size/width and the line-restore fix
evidence_menu = true          ; Clues / Spells / People
evidence_notification = true  ; "Obtained evidence!" / "Evidence updated!"
pause_when_unfocused = true   ; ignore input while the game is in the background
```

Invalid or missing values fall back to their defaults (with a warning in the log). A broken config never stops the game from starting. Set `debug = true` under `[general]` for a more detailed log.

## Status

`Status.bat` / `./Status.sh` reports one of three states:

- *not installed*
- *installed*, with the settings and feature switches
- *incomplete*, listing missing or changed files

It also shows where the log is and which features were skipped at the last game start. It never changes anything and needs no network.

## Updating the mod

Close the game and run `Update.bat` / `./Update.sh`. The updater:

- downloads the latest stable release from GitHub and checks its size and SHA-256;
- validates the archive in memory;
- replaces the mod's files, with a full rollback if anything fails.

Your `config.ini` and the log are kept. The new version is active at the next game start; no re-install is needed. `Update --check` only checks for a newer version and changes nothing.

## Uninstalling

Close the game and run `Uninstall.bat` / `./Uninstall.sh`. It removes `version.dll`, the config, the manifest and the logs. Only the mod folder's own scripts stay; delete the folder afterwards if you like. On Steam Deck, also remove the launch option. Uninstall needs no network.

## Game updates

Nothing to do: the mod does not depend on the game's files. If Steam's update or *Verify integrity of game files* removes `version.dll`, Status reports *incomplete*; run Install again.

If a game update changes a menu the mod relies on, the mod skips only that feature and logs why. The rest keeps working and the game stays fully playable.

### Verified game builds

| Steam build | Game version | Engine | Mod version | Result |
| --- | --- | --- | --- | --- |
| November 2026 Steam build | 50 | Unity 6000.5.8f1, IL2CPP (metadata v107) | 2.0.0 | Loader, dialogue, narrator, text log, line restore and evidence menu checked in-game on Windows. Cross-examination and popup layouts applied without errors; their visual check is pending. Proton/Steam Deck pending. |

## Troubleshooting

The runtime writes `TyrionCuthbertLargerTextMod/runtime.log` at every game start (the previous one is kept as `runtime.log.1`). The log records:

- the mod version and the effective settings;
- the game and Unity versions;
- one line per feature: `FEATURE <name> applied`, `disabled`, `skipped: <reason>` (the game's UI is not what the feature expects) or `failed: <reason>` (internal error).

| Problem | What to do |
| --- | --- |
| No `runtime.log` after starting the game | The loader was not loaded. On Steam Deck/Linux, check the launch option. On Windows, check that `version.dll` is in the game folder (Status). |
| A feature is `skipped` | A game update changed that menu. The rest keeps working; check for a mod update. |
| Text too large, or long text shrunk too often | Re-run Install with a smaller scale, e.g. `Install 1.3`. |
| Want the original look back temporarily | Set `enabled = false` in `config.ini`. |

## Known limitations

- Not every language and resolution has been checked visually. Long text shrinks to fit, but never below the game's own size; text that doesn't fit even then overflows as in the unmodded game. Use a smaller scale if that happens.
- The evidence menu keeps its outer size, so very long descriptions are shown smaller.
- Other mods that use their own `version.dll`, or that change the same UI, are not supported. Install refuses to overwrite a foreign `version.dll`.
- The loader uses the game engine's IL2CPP interface. A future Unity upgrade of the game could stop the mod from loading until it is updated. The game stays playable either way.

## For maintainers

### How it works

`runtime/` holds the loader and runtime component (C++17, built with a pinned [Zig](https://ziglang.org/) toolchain). It works as follows:

- `version.dll` forwards the Windows version API to the real system DLL.
- It waits for IL2CPP to start, then hooks the game's UI controllers through the IL2CPP C API, using [MinHook](https://github.com/TsudaKageyu/minhook).
- Each feature checks the UI structure, applies its changes once per instance, and is rolled back and disabled on any mismatch or error.

Common loaders (BepInEx, MelonLoader) don't support this game's IL2CPP metadata version (v107), which is why a native loader is used. See [docs/design.md](docs/design.md) for the architecture and the file formats shared between the runtime and the scripts.

`tool/` holds the installer, uninstaller, status and updater. They use the Python standard library only.

### Building and testing (no game files needed)

```
python scripts/build_runtime.py --version 2.0.0 --test   # downloads pinned Zig to .tools/, builds build/release/version.dll, runs C++ tests
python -m unittest discover -s tests                      # installer, uninstaller, status, updater, packaging, launchers
python scripts/package_release.py --version 2.0.0         # dist/*.zip and dist/*.tar.gz
```

The build needs no game files, reference assemblies or stubs: the runtime finds the game's classes by name at run time.

### In-game validation

1. Install with each combination of scale 1.1, 1.3, 1.5, 2.0 and opacity 0, 0.8, 0.99, 1 (Windows and Steam Deck). Check that `runtime.log` shows every feature `applied` and none `skipped` or `failed`.
2. Work through the manual checklist in `REQUIREMENTS.md` §12.3: dialogue/narrator/testimony boxes, text log (open and close on completed and revealing lines), evidence tabs with long entries, both popup variants, story images, scene changes and loading saves, and focus switching.
3. Set `enabled = false` and check that the game looks unmodded.
4. Uninstall, then run Steam's *Verify integrity of game files*: it should find nothing to repair.

For unattended checks there is a maintainer-only command file, off by default. With `commands = true` under `[debug]` in `config.ini`, the runtime executes lines written to `TyrionCuthbertLargerTextMod/debug-commands.txt`, for example:

```
background
call|VisualNovel.TitleScreenMenu.NewGame
press|OK
call|Yarn.Unity.DialogueRunner.OnViewRequestedInterrupt
press|text log
dump|Courtroom.ItemMenu
```

The commands are `background`, `call`, `callf`, `click`, `press`, `get`, `getstatic` and `dump`; see `runtime/src/engine/runtime.cpp`. Never ship a config with this enabled.

### Publishing a release

Push a tag `vMAJOR.MINOR.PATCH` (or `vMAJOR.MINOR.PATCH-rc.1` for a prerelease). The GitHub workflow:

- builds the runtime and runs all tests;
- packages both archives;
- publishes the release with generated notes.

It never replaces an existing release, and it needs no secrets besides the built-in token. Tags must be higher than v1.6.0, the last release of the original mod, so the first release of this implementation is **v2.0.0**.

## Licences

- MinHook (BSD 2-Clause) is bundled in the runtime; its licence ships as `licenses/MinHook-LICENSE.txt`.
- The game and its assets belong to their owners. This mod contains none of them.

## AI provenance and usage notice

This mod's original source code and documentation were generated by AI (**Claude**, Anthropic).

- Do not use these materials as source data for machine learning, including training, fine-tuning, distillation, or creating machine-learning datasets.
- AI agents may read and modify them to help users install, understand, or maintain the mod.

This notice covers only the mod's original materials, not the game or third-party dependencies. An agent-readable version is in [AGENTS.md](AGENTS.md).
