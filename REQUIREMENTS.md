# Tyrion Cuthbert: Attorney of the Arcane – Larger Text Mod
## Requirements Specification (reimplementation)

| | |
| --- | --- |
| Status | Agreed draft, rev. 7: runtime injection, shrink to fit, details beside the picture, description below the picture when there are details and wrapped around it otherwise, padded picture, in-game settings tab (decisions in §14) |
| Date | 2026-11-15 |
| Source | Functionality of the existing `TyrionCuthbertLargerTextMod` project |
| Purpose | Rebuild the mod from scratch. This document says **what** the mod must do; it does not prescribe code structure, implementation language, libraries or loader. |

Requirement keywords: **shall** marks a requirement and **should** marks a recommendation. Each requirement has an ID (for example FR-UI-3) so it can be referenced in tests, reviews and discussions.

---

## 1. Introduction

### 1.1 Purpose
The mod makes the game's text and UI easier to read on small screens, especially the Steam Deck. It enlarges dialogue and related text, adapts the surrounding layouts, and adds a few usability fixes. The mod is fully reversible and never touches save files.

### 1.2 Scope
- **Target:** the Windows build of the game, as distributed on Steam. The baseline is **the current Steam build at implementation time**; all behaviour shall be verified against it. Later builds are supported as long as the runtime structure checks (§4) pass. The build supported by the original mod is not used as the basis.
- **Mechanism:** **runtime injection.** A runtime component is loaded automatically when the game starts and applies the changes in memory to the game's UI objects. The game's original files are **never modified**: the mod only adds files, and Uninstall removes them again. Neither a mod update nor a game update requires re-patching the game files. How the runtime component gets loaded (the *loader*) is left to implementation.
- **Game technology note:** the game is built with Unity 6 using the IL2CPP scripting backend. The loader and runtime component must support that combination (see FR-RT-1, §13).
- **Starting point:** the mod is installed on a fresh, unmodded game. Migrating installations of the original mod is out of scope (see FR-INST-6).
- **Distribution:** the existing GitHub repository of the original mod (`donthub/tyrion-cuthbert-larger-text-mod`). This project's working solution replaces that repository's contents.
- **Host platforms:** Windows, and Linux / Steam Deck (where the Windows game runs through Proton).
- **In scope:** the UI changes, the runtime component and its configuration, the installer, uninstaller, status check, platform launchers, the self-updater, release packaging and validation.
- **Out of scope:** save files, modifying the game's original files, migrating the original mod, and guaranteed compatibility with other mods.

### 1.3 Definitions
| Term | Meaning |
| --- | --- |
| Game folder | The game's installation directory (Steam: *Manage → Browse local files*). |
| Mod folder | The folder named `TyrionCuthbertLargerTextMod`, placed directly inside the game folder. It contains the scripts, config, manifest and log. |
| Runtime component | The mod's code, which runs inside the game process and applies the UI changes. |
| Loader | Whatever makes the runtime component load into the game at startup, e.g. a proxy library plus a loader framework, or a launcher. Its choice is left to implementation. |
| Config | A human-editable settings file in the mod folder. The runtime component reads it at game start. |
| Manifest | A machine-readable record in the mod folder listing every file the mod added (inside or outside the mod folder) with its hash, plus files that already existed before install. |
| Log | A text file in the mod folder where the runtime component records what it applied, skipped and why. |
| Scale | A multiplier for text size, from 1.1 to 2.0. The default is 1.5, i.e. 50 % larger. |
| Opacity | The alpha of the dialogue and narrator panel backgrounds: 0 is transparent and 1 is opaque. The default is 0.99. |
| Feature | One independently applied group of changes, e.g. dialogue box, cross-examination box, story images, text log, evidence menu, evidence notification, pause when unfocused. |

---

## 2. Users and usage scenarios

| # | Actor | Scenario |
| --- | --- | --- |
| UC-1 | Player | Fresh install with the default settings. |
| UC-2 | Player | Install with a custom scale and/or opacity. |
| UC-3 | Player | Change the settings in the game's Settings menu (*Mod* tab), by editing the config or by re-running Install; the change takes effect at the next game start. |
| UC-4 | Player | Uninstall to remove everything the mod added. |
| UC-5 | Player | Check the installation status. |
| UC-6 | Player | The game was updated by Steam: nothing to do. Re-run Install only if Status reports that Steam removed some of the mod's files. |
| UC-7 | Player | Update the mod to the latest release; the new version is active at the next game start. |
| UC-8 | Player | Temporarily disable the mod, or a single feature, in the game's Settings menu or through the config, without uninstalling. |
| UC-9 | Maintainer | Publish a release by pushing a version tag. |
| UC-10 | Maintainer | Validate the mod in-game and with automated tests. |

---

## 3. Functional requirements – in-game UI changes

All UI changes **shall** be scaled by the chosen *scale*. At runtime they **shall** be calculated from the game's original values, never from values the mod has already changed.

### 3.0 General runtime application
- **FR-UI-0a** Each change shall be applied to every relevant UI instance, including instances created after startup (scene loads, instantiated prefabs, pooled objects being reused). It shall be applied before or at the moment the instance first becomes visible, so the player never sees the unmodified layout flash briefly.
- **FR-UI-0b** Each change shall be applied exactly once per instance. Repeated activation, scene reloads or re-entering a menu shall never compound the changes.
- **FR-UI-0c** Each feature shall be independently switchable in the config (FR-RT-4).
- **FR-UI-0d** **Shrink to fit.** Text that would extend outside its container at the enlarged size shall be scaled down until it fits. The container is the area the text has to stay in: its own text rectangle, or the fixed-size panel or screen area that holds it. Containers that grow with their contents and scroll, such as the text log, already fit. The text shall shrink only as far as needed and only while it doesn't fit; shorter text keeps the full enlarged size. It shall never become smaller than the size the game itself uses for it (its original size, or the lower bound of the game's own automatic sizing). Text that doesn't fit even at that size behaves as in the unmodded game. Text split into several parts (FR-UI-22, FR-UI-33) shrinks as a whole, so all parts keep the same size (FR-UI-23). Shrinking shall not change how the text is revealed, its timing or its events.

### 3.1 Dialogue and narrator boxes
- **FR-UI-1** The dialogue text in the normal dialogue box and in the narrator box shall be enlarged by the scale factor. A line that doesn't fit the enlarged text area shrinks to fit (FR-UI-0d).
- **FR-UI-2** The height of the text area and the panel shall grow so that the larger, more frequently wrapping text fits. The height growth shall be at least proportional to the extra wrapping (roughly the square of the scale). The panel shall also stay tall enough for the character-info rows above the dialogue text.
- **FR-UI-3** The normal dialogue panel shall be widened to span the screen width minus a small, equal margin on each side.
- **FR-UI-4** When the panel is widened, its child elements (nameplate, character info, dialogue text, toolbar buttons) shall keep their relative horizontal position and proportion on the background artwork. They shall be stretched by resizing rectangles, never by scaling transforms, so glyphs keep their shape.
- **FR-UI-5** The speaker nameplate text shall be enlarged, and shrink to fit the nameplate for long names (FR-UI-0d). The nameplate's height shall follow the panel's vertical stretch, because the nameplate background is part of the panel artwork.
- **FR-UI-6** The character-info labels (Occupation, Age, Arcane Art) shall be enlarged, together with their row heights, and repositioned to match the taller panel. Long labels shrink to fit their rows (FR-UI-0d).
- **FR-UI-7** The toolbar button row shall move as one unit, so labels, icons and click targets stay aligned.
- **FR-UI-8** The opacity of the panel background shall be configurable. Only the background image shall be affected; text, nameplate and controls shall keep their own opacity.

### 3.2 Cross-examination (testimony) box
- **FR-UI-9** The cross-examination dialogue box shall get the same enlarged layout as the normal dialogue box (FR-UI-1 … FR-UI-8).
- **FR-UI-10** The *Press* and *Present* buttons shall move to the top-left and top-right corners of the screen respectively. They shall keep their layout fitters, input hints, localization and event bindings.
- **FR-UI-11** The previous/next navigation arrows shall sit just above the panel on their respective sides, at their original size, with the left arrow still mirrored.

### 3.3 Popup story images
- **FR-UI-12** Story illustrations shown during gameplay, including building plans, shall shrink so that their lower edge stays above the taller of the enlarged dialogue and narrator panels. The original gap between image and panel shall be kept.
- **FR-UI-13** These images shall keep their original top margin and aspect ratio.
- **FR-UI-14** The separate image viewer inside the evidence menu shall not change.

### 3.4 Text log
- **FR-UI-15** The text log's speaker names and line text shall be enlarged by the scale factor. The spacing between entries shall scale accordingly.
- **FR-UI-16** The extra letter spacing the original text log applies shall be removed, so normal font spacing is used.
- **FR-UI-17** The text log viewport shall be widened.
- **FR-UI-18** Entries shall keep their height fitting to the preferred text height, so long lines are never clipped.
- **FR-UI-19** When the player opens and closes the text log, the current dialogue or narration line shall reappear as it was, both for a completed line and for one still being revealed. Playback of later lines, its timing and its events shall not change.

### 3.5 Evidence menu (Clues, Spells, People)
- **FR-UI-20** The description text in the evidence menu shall be enlarged. The game's own automatic font shrinking shall be disabled for it. Instead, when the description, laid out as in FR-UI-22, doesn't fit in the panel, it shrinks until it does (FR-UI-0d).
- **FR-UI-21** The picture shall be on the **right**, and the item name and description on its left.
- **FR-UI-22** When detail rows are shown, the space beside the picture belongs to the item name, the detail fields and the *View* button, and the description shall start **below the picture** and its bottom padding (FR-UI-27a), at the **full panel width**, or below the *View* button if that reaches lower. When no detail rows are shown, the description shall start in the area to the left of the picture, below the item name and *View* button, and **wrap around the picture** like a floating picture in a document: lines that start beside the picture or its bottom padding end at the picture's left padding, and the following lines run across the **full panel width** below it. The text shall read as one block, with the same line spacing throughout and no gap or extra space where it continues below the picture. As with a floating picture, the last line beside the picture may extend a little below its lower edge, but no text shall overlap the picture or start a full-width line inside its padding. No text shall be repeated or lost at the transition, and the continuation shall be cleared when a shorter entry is shown.
- **FR-UI-23** Both description regions shall use the same font and font size.
- **FR-UI-24** The game shall keep supplying the localized description to its original text component. The part below the picture (the continuation) shall only display the text that doesn't fit beside it and never be filled from any other source.
- **FR-UI-25** The detail fields (name/value rows) and the *View* button shall stay in their original place beside the picture, directly below the item name and in that order, and take the space beside the picture. The description shall start below them, and below the picture when detail rows are shown (FR-UI-22).
- **FR-UI-26** Detail rows shall take their text's preferred height and not a fixed height, and take no height when empty. A row counts as empty, and not as a shown detail row (FR-UI-22, FR-UI-34), when it or the whole details area is hidden, or when neither its name nor its value shows any text (empty, only spaces or only formatting tags). An entry whose rows are all empty has no details, so its description wraps around the picture. The original name/value column split shall be kept.
- **FR-UI-27** The picture shall keep its original square size. The evidence menu's outer panel bounds shall not change.
- **FR-UI-27a** The picture shall have padding on its **left** and **bottom** equal to the gap between the picture and the edge of the panel that contains it. Text beside the picture ends at the left padding; the description below the picture starts below the bottom padding. When that gap can't be determined, the game's own gap between the text and the picture is used.
- **FR-UI-28** All three tabs (Clues, Spells, People) shall get this layout, because they share the same display.

### 3.6 "Obtained evidence!" / "Evidence updated!" notification
- **FR-UI-29** Both notification variants shall share the changes in this section.
- **FR-UI-30** The notification panel shall be wider than the original.
- **FR-UI-31** The heading shall be enlarged. It may shrink automatically so that long translations fit, but never below its original size, and it shall never extend above the screen.
- **FR-UI-32** The item name, description and detail rows shall be enlarged.
- **FR-UI-33** The picture shall be on the right, at a fixed square size, with padding as in FR-UI-27a. As in FR-UI-22 to FR-UI-24, the description shall start below the picture at full width when detail rows are shown, and otherwise start to its left, below the item name, and wrap around the picture, with no gap where it continues full-width below the picture.
- **FR-UI-34** Detail fields shall stay in their original place beside the picture, directly below the item name, and take the space beside the picture. The description shall start below them, and below the picture when they are shown. Rows shall take their preferred height, keep the original name/value split, and take **no** height when empty. When the item name and detail fields are taller than the picture, the picture row shall grow to fit them.
- **FR-UI-35** The panel height shall fit its contents: the heading and the picture row, which is as tall as the picture or as the item name, detail fields and the description (below the picture and its padding, or wrapped around it), whichever is taller. The panel shall stay on the screen: when its contents would take it past the screen edge, the description shrinks until they fit (FR-UI-0d).

### 3.7 Pause when unfocused
- **FR-UI-36** The game shall not keep running while another application has focus, so inputs sent while the game is in the background can't advance dialogue or activate controls. Switching back to the game shall resume it normally.
- **FR-UI-37** This shall be done at runtime by turning off the engine's "run in background" behaviour. No game file shall be changed for this.

### 3.8 In-game settings
- **FR-UI-41** The game's Settings menu shall have an additional tab button labelled **Mod**, placed directly after *Workshop*. Selecting it shall show the mod's settings like the game's own tabs, with the game's look and its mouse, keyboard and controller handling.
- **FR-UI-42** The tab shall offer every config setting of FR-RT-4: the master switch, the scale (at least 1.1 to 2.0 in steps of 0.1), the opacity (a range of steps from 0 to 1 including the default), and one on/off switch per feature. When the tab opens, it shall show the values currently saved in the config. A value that is not one of the offered steps shall be offered too, so opening the tab never changes a setting.
- **FR-UI-43** A change shall be saved to the config immediately and atomically. Only the changed setting's line shall change; comments, unknown keys and other settings shall be kept (as FR-INST-13). If the config can't be written, the error is logged and the game is unaffected.
- **FR-UI-44** Changes shall take effect at the next game start (FR-RT-5). The tab shall say so.
- **FR-UI-45** The tab shall not read or change the game's own settings, and shall not change the game's other tabs.
- **FR-UI-46** The tab shall be available when the master switch is off, so the mod can be switched back on in the game (FR-RT-10). It shall follow FR-RT-6 and FR-RT-7: if the menu is not as expected, no button or tab is added and the reason is logged.

### 3.9 Preserved behaviour
- **FR-UI-38** Dialogue choices and unrelated menus shall keep their existing text size and layout.
- **FR-UI-39** All changed elements shall keep their localization, controllers, layout fitters, images, and button and event references.
- **FR-UI-40** Save files shall never be read or written.

---

## 4. Functional requirements – runtime component

- **FR-RT-1** The runtime component shall load automatically each time the game starts, with no manual action needed per launch. It shall work with the game's engine and scripting backend (Unity 6, IL2CPP) on Windows and under Proton.
- **FR-RT-2** If any one-time platform step is unavoidable (e.g. a Steam launch option needed under Proton), Install shall print the exact instructions and the README shall document them. The step shall not be needed again after mod updates.
- **FR-RT-3** At startup the runtime component shall read the config. Values that are missing, malformed or out of range shall fall back to their defaults, and a warning shall be logged. A broken config shall never stop the game from starting.
- **FR-RT-4** The config shall contain at least: *scale*, *opacity*, a master *enabled* switch, and one switch per feature. All of these shall be documented in the config file itself (comments) and in the README.
- **FR-RT-5** Config changes, including those made in the in-game settings tab (§3.8), shall take effect at the next game start.
- **FR-RT-6** Before changing anything, each feature shall check that the UI structure it relies on is present and as expected. On a mismatch, only that feature shall be skipped, its partial changes shall not be left behind, and the reason shall be logged. All other features shall still be applied.
- **FR-RT-7** The runtime component shall never let an error propagate into the game. It shall catch errors per feature, log them, and disable only the affected feature for the rest of the session.
- **FR-RT-8** The log shall be written to the mod folder and overwritten (or rotated) at each start, so it can't grow without limit. It shall record the mod version, the effective settings, the detected game and engine versions, which features were applied, and which were skipped and why.
- **FR-RT-9** The runtime component shall have no noticeable effect on frame rate, loading times or input latency.
- **FR-RT-10** When the master switch is off, the runtime component shall change nothing and leave the game exactly as it would be without the mod, except for the in-game settings tab (FR-UI-46).
- **FR-RT-11** A game update shall not need a new build of the mod as long as the UI structure checks still pass.
- **FR-RT-12** The runtime component shall make no network connections and shall not write anywhere except its log and, when the player changes a setting in the game (FR-UI-43), its config.

---

## 5. Functional requirements – installer

### 5.1 Interface
- **FR-INST-1** The installer shall take two optional parameters, *scale* (default 1.5) and *opacity* (default 0.99), and write them to the config.
- **FR-INST-2** The scale shall be a finite number from 1.1 to 2.0, inclusive. The opacity shall be a finite number from 0.0 to 1.0, inclusive. Invalid values shall be rejected **before** anything is downloaded or written.
- **FR-INST-3** Every platform's user-facing launcher shall accept these as positional arguments, e.g. `Install 1.3 0.8`. The core tool shall also accept them as named options, plus an option to set the game folder explicitly. By default, the game folder is the parent of the mod folder.

### 5.2 Preconditions
- **FR-INST-4** The installer shall refuse to run while the game is running. It shall detect the game process both on Windows and on Linux, where it runs through Proton/Wine.
- **FR-INST-5** The installer shall check that the mod folder is directly inside a game folder and refuse otherwise.
- **FR-INST-6** The installer shall detect an existing installation of the original (asset-patching) version of this mod, e.g. by its state file or backup folder, and refuse without writing anything. It shall tell the player to run the original mod's uninstaller, or use Steam's *Verify integrity of game files*, first.
- **FR-INST-7** Before installing, the installer shall check that the game's engine version and scripting backend are the ones the runtime component and loader support, and refuse with a clear message otherwise.

### 5.3 Writing
- **FR-INST-8** The installer shall never modify or overwrite any of the game's original files. If a file it needs to add already exists and doesn't belong to this mod, e.g. because another mod installed the same loader, it shall either reuse the existing file when it is compatible (and record it as pre-existing in the manifest), or refuse with a clear message. It shall never silently replace it.
- **FR-INST-9** Every file shall be written atomically: write to a temporary file, then rename it over the target.
- **FR-INST-10** The installer shall record every file it adds, with its hash, in the manifest. The manifest shall be written so that an interrupted install can still be uninstalled cleanly.
- **FR-INST-11** If installing fails, every file added during that run shall be removed, the previous config and manifest shall be restored, and the error shall be reported.
- **FR-INST-12** Re-running Install on an existing installation shall only update the config and add or replace mod-owned files that are missing or outdated. The result shall be the same as a fresh install with the same settings.
- **FR-INST-13** Install shall keep any unknown keys and the player's per-feature switches in an existing config. It shall change only scale and opacity, and only when they are given.
- **FR-INST-14** After a successful install, the installer shall print a summary: the settings, the features enabled, where the config and log are, and any one-time platform step (FR-RT-2).

---

## 6. Functional requirements – uninstall and status

### 6.1 Uninstall
- **FR-UNI-1** Uninstall shall remove every file the manifest lists as added by the mod, plus any folders it created that are now empty. Afterwards, the game folder shall contain exactly what it contained before install, apart from the mod folder's own scripts.
- **FR-UNI-2** Files the manifest records as pre-existing, such as a loader shared with another mod, shall not be removed.
- **FR-UNI-3** If a mod-owned file has been changed by something else, e.g. a different version of a shared loader, uninstall shall leave it in place and report it, not delete it blindly. The config and log are exceptions: they are owned by the mod and are removed even if they were edited.
- **FR-UNI-4** Uninstall shall report "no installation" when there is no manifest, and treat this as success.
- **FR-UNI-5** Uninstall shall refuse to run while the game is running.
- **FR-UNI-6** Uninstall shall need no network access and no third-party components.

### 6.2 Status
- **FR-STAT-1** Status shall report exactly one of: *not installed*; *installed*, with its scale, opacity, master switch and feature switches; or *incomplete*, listing the missing or changed files (e.g. after Steam's file verification) with advice to re-run Install.
- **FR-STAT-2** Status shall show where the log is and, if the log exists, which features were skipped at the last game start.
- **FR-STAT-3** Status shall never write anything, shall need no third-party components and shall work offline.

---

## 7. Functional requirements – game updates

- **FR-GU-1** A Steam update of the game shall need no action from the player as long as the mod's added files are still in place.
- **FR-GU-2** If a game update or Steam's *Verify integrity of game files* removes or replaces any of the mod's added files, Status shall report the installation as incomplete. Re-running Install shall restore it without affecting anything else.
- **FR-GU-3** If a game update changes the UI structure, the affected features shall be skipped according to FR-RT-6, and the game shall stay fully playable.
- **FR-GU-4** The README shall list the game builds on which the mod has been verified in-game.

---

## 8. Functional requirements – platform launchers

### 8.1 Common
- **FR-LAUNCH-1** Each platform shall have Install, Uninstall and Update launchers. On Windows they shall be double-clickable and keep the window open at the end. On Linux they shall be runnable from a terminal or Steam Deck Desktop Mode.
- **FR-LAUNCH-2** Launchers shall print clear progress messages and clear success or failure messages.
- **FR-LAUNCH-3** Any third-party dependencies of the installer shall be installed in a local, private location inside the mod or game folder. Nothing shall be installed system-wide, and the read-only SteamOS root shall never be modified.
- **FR-LAUNCH-4** Dependencies shall be downloaded only when missing or when the installed version doesn't match the pinned one. Downloads shall use HTTPS only. Preferably, everything needed shall ship in the release archive so that no download is required at all.
- **FR-LAUNCH-5** Uninstall and Status shall not need the private dependencies or network access.
- **FR-LAUNCH-6** Any prerequisite the player has to install themselves shall be minimal and documented in the README. When it is missing, the launcher shall print clear instructions for getting it.
- **FR-LAUNCH-7** Dependencies prepared for one runtime or architecture shall never be reused with an incompatible one.
- **FR-LAUNCH-8** Before downloading anything, the installer shall run the preflight checks: game not running, valid placement, valid parameters, no original-mod installation, supported engine and backend (§5.2).
- **FR-LAUNCH-9** Launchers shall support `--help` and reject wrong argument counts with a usage message.
- **FR-LAUNCH-10** Each launcher shall refuse to run on the wrong operating system and point to the correct launcher.

### 8.2 Windows
- **FR-LAUNCH-11** The launchers shall work on a standard 64-bit Windows 10/11 PC.

### 8.3 Linux / Steam Deck
- **FR-LAUNCH-12** The launchers shall work in Steam Deck Desktop Mode on a stock SteamOS, and on common desktop Linux distributions where the game runs through Proton.
- **FR-LAUNCH-13** Shell launchers shall be executable and use Unix line endings.

### 8.4 Self-replacing Update launcher
- **FR-LAUNCH-14** The Update launcher on each platform shall keep working when the update replaces the launcher file itself, e.g. by handing control to the updater or by reading the whole launcher before the update starts.

---

## 9. Functional requirements – self-updater

- **FR-UPD-1** The updater shall find the **latest stable** release in the project's GitHub repository (`donthub/tyrion-cuthbert-larger-text-mod`, see §1.2). It shall reject drafts, prereleases and tags that are not of the form `vMAJOR.MINOR.PATCH`.
- **FR-UPD-2** The updater shall select exactly one archive for the current platform (Windows or Linux / Steam Deck) by its expected name.
- **FR-UPD-3** The asset's download URL shall point to this project's repository and the expected tag and name. Anything else shall be rejected.
- **FR-UPD-4** Downloads shall have a timeout and a size limit. The download size shall match the release metadata. When the metadata includes a SHA-256 digest, the updater shall check it.
- **FR-UPD-5** The updater shall read the archive in memory and never extract it to disk as-is. It shall accept only regular files at expected paths whose names are on an explicit allowlist. Links, unexpected directories, duplicate entries, unknown files and archives larger than the size limit shall be rejected.
- **FR-UPD-6** If any of the platform's required files is missing, the archive shall be rejected as incomplete.
- **FR-UPD-7** A *check* mode shall download and validate the release without changing any files.
- **FR-UPD-8** The updater shall refuse to run while the game is running, because the runtime component's files may be locked or in use.
- **FR-UPD-9** The updater shall replace both the mod folder's files and the mod-owned runtime and loader files listed in the manifest, and update the manifest. Before replacing anything, it shall snapshot the files it will replace. It shall refuse to overwrite links, non-files and files the manifest records as pre-existing, and write each file atomically. Shell scripts shall be marked executable; other files shall keep their existing permissions.
- **FR-UPD-10** If replacement fails, the updater shall restore the previous files and remove newly added ones. If the rollback also fails, it shall name the affected files and tell the player how to recover.
- **FR-UPD-11** The config (including the player's settings and switches), the log, local dependency environments and all other files not on the allowlist shall be left untouched.
- **FR-UPD-12** After a successful update, the new version shall be active at the next game start, with no re-install needed. The updater shall say so, and shall also repeat any new one-time platform step a release introduces.
- **FR-UPD-13** Releases without a digest in their metadata shall still be accepted. The size check and archive validation still apply.
- **FR-UPD-14** Network errors, a missing release (HTTP 404) and rate limiting (403/429) shall each produce a specific, readable error message.
- **FR-UPD-15** The updater shall need no third-party components beyond the documented prerequisites (FR-LAUNCH-6).

---

## 10. Functional requirements – packaging and release

- **FR-REL-1** A release shall be triggered by pushing a tag of the form `vMAJOR.MINOR.PATCH`, optionally with a prerelease suffix (e.g. `-rc.1`). Tags with a suffix shall produce GitHub prereleases.
- **FR-REL-2** CI shall build the runtime component and run the automated tests before packaging, and stop the release if either fails.
- **FR-REL-3** Two archives shall be produced:
  - **Windows** `.zip`: the built runtime component, the loader files (if the chosen approach ships them), the installer and updater, the README, the Windows Install/Uninstall/Update launchers, and the licence files of any bundled third-party components.
  - **Linux / Steam Deck** `.tar.gz`: the same, with the Linux Install/Uninstall/Update launchers instead.
- **FR-REL-4** Archive contents shall come from explicit file lists. Game files, configs, manifests, logs, dependency environments, test files, screenshots and `AGENTS.md` shall be excluded.
- **FR-REL-5** Shell scripts shall be packaged with Unix line endings and executable permissions. Batch files shall be packaged with Windows line endings, whatever the line endings of the checkout.
- **FR-REL-6** Building and packaging shall run locally without publishing and without game files. Output shall go to a `dist/` folder. If compiling the runtime component needs reference assemblies or stubs generated from the game, the build shall document how to get them, and they shall never be committed or redistributed.
- **FR-REL-7** The release shall be published with generated release notes and both archives. An existing release shall never be replaced, and no secrets beyond the CI's built-in token shall be required.
- **FR-REL-8** This project replaces the contents of the existing repository. Its first release tag shall be higher than every existing release tag in the repository, so that "latest stable release" resolves to the new implementation.

---

## 11. Non-functional requirements

| ID | Requirement |
| --- | --- |
| NFR-1 Safety | The game's original files shall never be modified. Any install, update or uninstall failure shall leave the game folder and the mod's files as they were before the operation. |
| NFR-2 Reversibility | After uninstall, the game folder shall contain exactly what it contained before install, apart from files the manifest records as pre-existing. |
| NFR-3 Idempotence | Re-running Install with the same settings shall produce the same result. Runtime changes shall never compound (FR-UI-0b). |
| NFR-4 Resilience | A change in the game's UI structure or an internal error shall at most disable the affected feature. It shall never crash, block or corrupt the game (FR-RT-6, FR-RT-7). |
| NFR-5 Performance | The runtime component shall have no noticeable effect on frame rate, load times or input latency (FR-RT-9). |
| NFR-6 Minimal dependencies | Third-party components shall be kept to a minimum and pinned to exact versions. |
| NFR-7 Technology | The implementation language, runtime, loader and tooling are not prescribed. Choose them to suit the project: few prerequisites for players, reliable support for Unity 6 IL2CPP on Windows and Proton, and testability without game files. |
| NFR-8 Platforms | Windows (64-bit) and Linux / Steam Deck (via Proton). Other systems shall be rejected with a clear message. |
| NFR-9 Error handling | Every launcher or tool failure shall print a single readable `ERROR:` line, plus a hint where useful, and exit with a non-zero code. Runtime problems shall go to the log (FR-RT-8). |
| NFR-10 Isolation | The mod shall write only to its own folder, its private environment folders, and the specific files it adds to the game folder. |
| NFR-11 Licensing | The licences of all bundled third-party components (e.g. a loader framework) shall be respected, and their licence files shall be shipped with the release. |
| NFR-12 Documentation | A README shall cover prerequisites, installation, any one-time platform step, configuration, updating, uninstalling, game updates, troubleshooting with the log, validation, and publishing releases. |
| NFR-13 AI provenance | The project shall carry an AI provenance and usage notice, both in the README and in an agent-readable `AGENTS.md`. The notice states that the materials were generated by AI and prohibits their use as machine-learning source data (training, fine-tuning, distillation, dataset creation). It allows AI agents to read and modify the materials to help users install, understand or maintain the mod. It covers only the mod's original materials, not the game or third-party dependencies. |

---

## 12. Verification

### 12.1 Automated tests (no game files needed)
These tests shall run in CI. They shall cover:
- config parsing: defaults, valid values, out-of-range, malformed and missing values, and unknown keys being kept;
- config writing by the runtime: only the changed line changes, comments, unknown keys, BOM and line endings are kept, missing keys and sections are added, and the result reads back as intended;
- the in-game settings tab, using a stand-in Settings menu: the button follows Workshop, the tab is registered once, it shows the saved values (including values off the offered steps), a change saves only that setting, it works with the master switch off, and a mismatch or error leaves nothing behind;
- layout calculations implemented as pure functions (panel heights, margins, image clearance, column splits, shrink-to-fit sizes and limits) at several scales;
- shrink to fit (FR-UI-0d): text that fits keeps the enlarged size, text that doesn't shrinks just enough, never below the game's own size, and split text keeps one size in all parts;
- the runtime feature framework, using stand-in UI structures: each feature applies once and never compounds; a structure mismatch skips only that feature without leaving partial changes; an exception in one feature leaves the others working; the master switch off means no changes;
- the installer: fresh install, repeat install, settings change, refusal while the game runs, original-mod detection, existing foreign files not overwritten, rollback on a write failure, manifest correctness;
- uninstall and status: clean removal, pre-existing files kept, externally changed files reported, incomplete installations detected;
- the updater: success on both platforms with the config, log and environments preserved; check mode makes no writes; refusal while the game runs; bad checksum or size; invalid release or missing asset; unsafe, duplicate or incomplete archives; links in archives; rollback after a failed replacement; network errors and oversized responses; acceptance of archives produced by the packaging script.

### 12.2 In-game verification (current Steam build)
- **Feasibility first:** confirm that the chosen loader loads the runtime component into the current build on Windows and under Proton / Steam Deck before any feature work.
- **Matrix:** scales 1.1, 1.3, 1.5 and 2.0 combined with opacities 0, 0.8, 0.99 and 1, all on Windows and on Proton / Steam Deck.
- **Log check:** every feature is reported as applied and none as skipped.
- **Master switch off:** the game looks and behaves exactly as unmodded.
- **Install, uninstall and verify:** after uninstall, Steam's *Verify integrity of game files* finds nothing to repair, and the game folder contents match those before install.

### 12.3 Manual in-game checklist
- Dialogue, narrator and cross-examination boxes: check the layout, and that Press, Present and the arrows respond to input. At scale 2.0, check that the longest lines, names and character-info labels shrink to stay inside the box, that short lines keep the full size, and that the reveal of a shrunk line plays normally.
- Text log: open and close it repeatedly on a completed line and during a line's reveal, for both dialogue and narration. Check that the next line plays normally.
- Evidence menu: switch between short and long clues repeatedly, open and close the evidence page, and check Spells and People at the largest scale. Check that the detail fields and View button sit beside the picture under the item name and work. For an entry with detail fields, check that the description starts below the picture at full width; for one without, that it starts below the name and View button and wraps around the picture without a gap or a change in line spacing where it continues below it. Check that the gap left of and below the picture equals the gap between the picture and the panel edge, that no text overlaps the picture, and that no text is repeated or missing at the transition.
- Notifications: trigger both "Obtained" and "Updated" variants with short and long text, with and without detail fields. Check that the details sit beside the picture under the item name, that the description starts below the picture when there are details and wraps around it when there are none, and that the picture's left and bottom padding match its gap to the panel edge, as in the evidence menu. At scale 2.0, check that a long entry stays on the screen.
- Story images and building plans: check that they don't overlap the dialogue or narrator box.
- Scene changes and reloading a save: check the changes are still applied and haven't compounded.
- Settings menu, from the title screen and from the pause menu: *Mod* follows *Workshop*; the tab opens with the mouse, keyboard and controller and its dropdowns work with each; every change appears in `config.ini` with the comments kept and is active after a restart; with the master switch off the tab is still there; the game's own tabs keep working and keep their values.
- Focus: switch away during dialogue, press the advance and menu inputs, then switch back. Check that dialogue hasn't advanced and that controls work again. Do this on Windows and on Proton / Steam Deck.

---

## 13. Known limitations, risks and out of scope
- **Feasibility risk:** the game uses Unity 6 with IL2CPP. Code-loader support for this engine version may be new or experimental, so it has to be confirmed before implementation starts (§12.2).
- **Proton:** loading the runtime component under Proton may need a one-time Steam launch option or similar step (FR-RT-2).
- **Future game and engine updates:** an update to the engine or loader-relevant parts of the game may stop the runtime component from loading until the mod is updated. UI structure changes only disable the affected features (FR-GU-3).
- **Steam file verification** may remove files the mod added; re-run Install (FR-GU-2).
- Not every language and screen resolution has been visually tested. Text shrinks to fit (FR-UI-0d), but never below the game's own size; text that doesn't fit even then overflows as in the unmodded game. The workaround is a smaller scale.
- The evidence menu's outer bounds don't change, so very long descriptions shrink, at most down to the game's own minimum size.
- Larger settings take up more screen space.
- Other mods that change the same UI elements, or use an incompatible loader, are not supported.
- The original (asset-patching) version of this mod is not migrated. Its users shall uninstall it with its own uninstaller, or use Steam's *Verify integrity of game files*, before installing the new version (FR-INST-6). The original mod's updater is not required to be able to update to the new implementation.

---

## 14. Decisions
These questions were raised while the spec was being written and have been agreed:

| # | Topic | Decision |
| --- | --- | --- |
| D-1 | Target game build | The current Steam build at implementation time is the baseline, and the in-game behaviour is checked against it (§1.2, §12.2). |
| D-2 | AI provenance notice | This project carries its own notice (NFR-13, `AGENTS.md`). |
| D-3 | Implementation language and tooling | Not part of the requirements; chosen to fit the project (NFR-7). |
| D-4 | Repository | The existing GitHub repository stays. This project's working solution replaces its contents (§1.2, FR-REL-8). |
| D-5 | Migration from the original mod | Not required. The new mod is first installed on a fresh, unmodded game. An existing original-mod installation is detected and refused (FR-INST-6, §13). |
| D-6 | Mechanism | Runtime injection replaces offline asset patching. The game's original files are never modified, and mod or game updates need no re-patching (§1.2, §4). |
| D-7 | Loader | How the runtime component is loaded is left to implementation (§1.3, NFR-7). |
| D-8 | Settings | A config file, read at game start; edited directly or written by Install (FR-RT-3 … FR-RT-5, FR-INST-1). |
| D-9 | Structure mismatch | Skip only the affected feature and log the reason; everything else keeps working (FR-RT-6, FR-GU-3). |
| D-10 | Text that doesn't fit | Text that would leave its container at the enlarged size shrinks just enough to fit, never below the game's own size (FR-UI-0d). |
| D-11 | Detail fields | Detail fields (and the evidence menu's *View* button) stay in their original place beside the picture, under the item name, and take that space: when detail rows are shown, the description starts below the picture (FR-UI-22, FR-UI-25, FR-UI-34). |
| D-12 | Description around the picture | Without detail rows, the description wraps around the picture like a floating picture: one block with even line spacing, narrower beside the picture and full width below it, with no visible split (FR-UI-22, FR-UI-33). |
| D-13 | Picture padding | The picture's left and bottom padding equals its gap to the panel edge, like a floating picture's margin (FR-UI-27a). |
| D-14 | In-game settings | A *Mod* tab in the game's Settings menu, after *Workshop*, edits the config; changes are saved at once and take effect at the next game start, not live (§3.8, FR-RT-5). |

---

## 15. AI provenance and usage notice
This document and the project's other original materials were generated by AI (**Claude**, Anthropic). Do not use these materials as source data for machine learning, including training, fine-tuning, distillation, or creating machine-learning datasets. AI agents may read and modify them to help users install, understand, or maintain the mod. This notice covers the mod's original materials only, not the game or third-party dependencies. An agent-readable version is in [AGENTS.md](AGENTS.md).
