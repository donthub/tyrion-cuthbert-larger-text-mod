"""Uninstall and Status."""

import json
import os
from unittest import mock

from support import FAKE_RUNTIME, MOD, ToolTestCase, ltm, make_fake_game, read, tree, write

LOG_SAMPLE = (
    "2026-11-14 12:00:00 INFO Larger Text Mod 2.0.0 starting\n"
    "2026-11-14 12:00:01 INFO FEATURE dialogue_box applied\n"
    "2026-11-14 12:00:01 WARN FEATURE evidence_menu skipped: DetailPanel/Image not found\n"
    "2026-11-14 12:00:02 ERROR FEATURE text_log failed: exception in Awake hook\n"
    "2026-11-14 12:00:02 INFO FEATURE pause_when_unfocused disabled\n"
)


class UninstallTests(ToolTestCase):
    def setUp(self):
        super().setUp()
        self.game = make_fake_game(self.root)
        self.mod = os.path.join(self.game, MOD)
        self.before = tree(self.game)

    def test_clean_removal(self):
        self.install(self.game, "--scale", "1.3")
        write(os.path.join(self.mod, "runtime.log"), LOG_SAMPLE)
        write(os.path.join(self.mod, "runtime.log.1"), LOG_SAMPLE)
        with open(os.path.join(self.mod, "config.ini"), "a", encoding="utf-8") as f:
            f.write("\n; edited by the player\n")  # config is removed even when edited
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 0, err)
        self.assertIn("Uninstall complete", out)
        self.assertEqual(tree(self.game), self.before)

    def test_no_installation_is_success(self):
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 0)
        self.assertIn("No installation found", out)
        self.assertEqual(tree(self.game), self.before)

    def test_refuses_while_game_runs(self):
        self.install(self.game)
        installed = tree(self.game)
        self.game_running = True
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "is running")
        self.assertEqual(tree(self.game), installed)

    def test_preexisting_file_is_kept(self):
        write(os.path.join(self.game, "version.dll"), FAKE_RUNTIME)
        before = tree(self.game)
        self.install(self.game)
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 0, err)
        self.assertIn("Kept (existed before install", out)
        self.assertEqual(tree(self.game), before)

    def test_externally_changed_file_is_left_and_reported(self):
        self.install(self.game)
        write(os.path.join(self.game, "version.dll"), b"MZ other loader")
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 0, err)
        self.assertIn("WARNING: Left in place (changed by something else since install): version.dll", out)
        self.assertEqual(read(os.path.join(self.game, "version.dll")), b"MZ other loader")
        self.assertFalse(os.path.exists(os.path.join(self.mod, "config.ini")))
        self.assertFalse(os.path.exists(os.path.join(self.mod, "manifest.json")))

    def test_missing_files_are_fine(self):
        self.install(self.game)
        os.remove(os.path.join(self.game, "version.dll"))
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 0, err)
        self.assertEqual(tree(self.game), self.before)

    def test_failed_removal_changes_nothing(self):
        self.install(self.game)
        installed = tree(self.game)
        real_replace = os.replace
        calls = []

        def flaky(src, dst):
            calls.append(src)
            if len(calls) == 2:
                raise PermissionError(13, "Access is denied")
            return real_replace(src, dst)

        with mock.patch.object(ltm.os, "replace", side_effect=flaky):
            rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "Nothing was removed")
        self.assertEqual(tree(self.game), installed)

    def test_unsafe_manifest_paths_are_refused(self):
        self.install(self.game)
        path = os.path.join(self.mod, "manifest.json")
        m = json.loads(read(path))
        m["files"].append({"path": "Tyrion1.exe", "sha256": "0" * 64, "kind": "other"})
        write(path, json.dumps(m))
        installed = tree(self.game)
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "invalid")
        self.assertEqual(tree(self.game), installed)
        for bad in ("../x", "/abs", "C:/x", MOD + "/../version.dll", "a\\b", MOD + "/"):
            self.assertFalse(ltm.check_rel_path(bad), bad)


class StatusTests(ToolTestCase):
    def setUp(self):
        super().setUp()
        self.game = make_fake_game(self.root)
        self.mod = os.path.join(self.game, MOD)

    def status(self):
        before = tree(self.root)
        rc, out, err = self.run_tool("status", "--game-dir", self.game)
        self.assertEqual(tree(self.root), before, "status must never write")
        self.assertEqual(rc, 0, err)
        states = [s for s in ("NOT INSTALLED", "INSTALLED (", "INCOMPLETE") if "Status: " + s in out]
        self.assertEqual(len(states), 1, out)
        return out

    def test_not_installed(self):
        out = self.status()
        self.assertIn("Status: NOT INSTALLED", out)
        self.assertIn(os.path.join(self.mod, "runtime.log"), out)

    def test_installed_shows_settings_and_switches(self):
        self.install(self.game, "--scale", "1.3", "--opacity", "0.8")
        path = os.path.join(self.mod, "config.ini")
        write(path, read(path).decode().replace("story_images = true", "story_images = false"))
        out = self.status()
        self.assertIn("Status: INSTALLED (mod version 2.0.0)", out)
        self.assertIn("Scale:   1.3", out)
        self.assertIn("Opacity: 0.8", out)
        self.assertIn("Master switch (enabled): on", out)
        self.assertRegex(out, r"Feature story_images:\s+off")
        self.assertRegex(out, r"Feature text_log:\s+on")
        self.assertIn("no log yet", out)

    def test_log_skipped_and_failed_features(self):
        self.install(self.game)
        write(os.path.join(self.mod, "runtime.log"), LOG_SAMPLE)
        out = self.status()
        self.assertIn("evidence_menu skipped: DetailPanel/Image not found", out)
        self.assertIn("text_log failed: exception in Awake hook", out)
        self.assertIn("applied: dialogue_box", out)

    def test_missing_file_is_incomplete(self):
        self.install(self.game)
        os.remove(os.path.join(self.game, "version.dll"))
        out = self.status()
        self.assertIn("Status: INCOMPLETE", out)
        self.assertIn("missing: version.dll", out)
        self.assertIn("Run Install again", out)
        # Re-running Install restores it.
        self.install(self.game)
        self.assertIn("Status: INSTALLED", self.status())

    def test_changed_file_is_incomplete(self):
        self.install(self.game)
        write(os.path.join(self.game, "version.dll"), b"MZ replaced by Steam")
        out = self.status()
        self.assertIn("changed: version.dll", out)

    def test_missing_config_is_incomplete(self):
        self.install(self.game)
        os.remove(os.path.join(self.mod, "config.ini"))
        self.assertIn("missing: " + MOD + "/config.ini", self.status())

    def test_edited_config_is_still_installed(self):
        self.install(self.game)
        with open(os.path.join(self.mod, "config.ini"), "a", encoding="utf-8") as f:
            f.write("[general]\nscale = 9\n")
        out = self.status()
        self.assertIn("Status: INSTALLED", out)
        self.assertIn("Config warning: scale: 9 is outside", out)

    def test_linux_status_mentions_launch_option(self):
        self.platform = "linux"
        self.install(self.game)
        self.assertIn("WINEDLLOVERRIDES", self.status())
