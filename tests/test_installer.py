"""Installer: preflight checks, fresh/repeat install, settings, refusals, rollback, manifest."""

import os
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from support import (FAKE_RUNTIME, FAKE_RUNTIME_NEW, MOD, ToolTestCase, ltm, make_fake_game, make_pe, read,
                     sha, tree, write)


class InstallTests(ToolTestCase):
    def setUp(self):
        super().setUp()
        self.game = make_fake_game(self.root)
        self.mod = os.path.join(self.game, MOD)

    def config_settings(self):
        return ltm.parse_config(read(os.path.join(self.mod, "config.ini")).decode("utf-8"))

    def test_fresh_install_with_defaults(self):
        out = self.install(self.game)
        self.assertEqual(read(os.path.join(self.game, "version.dll")), FAKE_RUNTIME)
        settings, warnings = self.config_settings()
        self.assertEqual(settings, ltm.default_settings())
        self.assertEqual(warnings, [])
        self.assertIn("Scale:   1.5", out)
        self.assertIn("Opacity: 0.99", out)
        self.assertIn("Features enabled:  dialogue_box, cross_examination", out)
        self.assertIn(os.path.join(self.mod, "config.ini"), out)
        self.assertIn(os.path.join(self.mod, "runtime.log"), out)
        self.assertNotIn("WINEDLLOVERRIDES", out)  # Windows: no Proton step

    def test_manifest_correctness(self):
        self.install(self.game)
        m = self.manifest(self.game)
        config = read(os.path.join(self.mod, "config.ini"))
        self.assertEqual(m["format"], 1)
        self.assertEqual(m["mod_version"], "2.0.0")
        self.assertRegex(m["installed_at"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")
        self.assertEqual(m["files"], [
            {"path": "version.dll", "sha256": sha(FAKE_RUNTIME), "kind": "loader"},
            {"path": MOD + "/config.ini", "sha256": sha(config), "kind": "config"},
        ])
        self.assertEqual(m["generated"], [MOD + "/runtime.log", MOD + "/runtime.log.1"])
        self.assertEqual(m["preexisting"], [])
        self.assertEqual(m["created_dirs"], [])

    def test_only_expected_files_are_added(self):
        before = tree(self.game)
        self.install(self.game)
        added = set(tree(self.game)) - set(before)
        self.assertEqual(added, {"version.dll", MOD + "/config.ini", MOD + "/manifest.json"})
        for rel, data in before.items():
            self.assertEqual(tree(self.game)[rel], data, rel)

    def test_linux_install_prints_proton_step(self):
        self.platform = "linux"
        out = self.install(self.game)
        self.assertIn('WINEDLLOVERRIDES="version=n,b" %command%', out)
        self.assertIn("Launch Options", out)

    def test_custom_settings(self):
        out = self.install(self.game, "--scale", "1.3", "--opacity", "0.8")
        settings, _ = self.config_settings()
        self.assertEqual((settings["scale"], settings["opacity"]), (1.3, 0.8))
        self.assertIn("Scale:   1.3", out)

    def test_repeat_install_is_idempotent(self):
        self.install(self.game, "--scale", "1.3")
        first = tree(self.game)
        self.install(self.game, "--scale", "1.3")
        self.assertEqual(tree(self.game), first)
        self.install(self.game)
        self.assertEqual(tree(self.game), first)

    def test_repeat_install_equals_fresh_install(self):
        self.install(self.game, "--scale", "1.3", "--opacity", "0.5")
        self.install(self.game, "--scale", "1.7", "--opacity", "0.2")
        other = make_fake_game(os.path.join(self.root, "other"))
        self.install(other, "--scale", "1.7", "--opacity", "0.2")
        a, b = tree(self.game), tree(other)
        for t in (a, b):
            t.pop(MOD + "/manifest.json")
        self.assertEqual(a, b)

    def test_settings_change_keeps_unknown_keys_and_switches(self):
        self.install(self.game)
        path = os.path.join(self.mod, "config.ini")
        text = read(path).decode("utf-8")
        text = text.replace("text_log = true", "text_log = false").replace("enabled = true", "enabled = no")
        text += "\n[my_section]\nfoo = bar\n"
        text = text.replace("[features]", "my_unknown = 42\n\n[features]")
        write(path, text)
        out = self.install(self.game, "--opacity", "0.5")
        new_text = read(path).decode("utf-8")
        self.assertEqual(new_text, text.replace("opacity = 0.99", "opacity = 0.5"))
        settings, _ = self.config_settings()
        self.assertFalse(settings["features"]["text_log"])
        self.assertFalse(settings["enabled"])
        self.assertEqual(settings["scale"], 1.5)
        self.assertIn("Features disabled: text_log", out)
        self.assertEqual(self.manifest(self.game)["files"][1]["sha256"], sha(new_text.encode("utf-8")))

    def test_invalid_parameters_are_rejected_before_anything_else(self):
        before = tree(self.root)
        self.game_running = True  # parameter validation comes first
        for args in (("--scale", "2.5"), ("--scale", "1.0"), ("--scale", "abc"), ("--scale", "nan"),
                     ("--scale", "inf"), ("--opacity", "-0.1"), ("--opacity", "1.5")):
            with self.subTest(args=args):
                rc, out, err = self.run_tool("install", "--game-dir", self.game, *args)
                self.assertEqual(rc, 1)
                self.assertSingleError(err, "Invalid")
        self.assertEqual(tree(self.root), before)

    def test_usage_error(self):
        rc, out, err = self.run_tool("install", "--bogus")
        self.assertEqual(rc, 2)
        self.assertSingleError(err, "unrecognized arguments")
        rc, out, err = self.run_tool()
        self.assertEqual(rc, 2)

    def test_refuses_while_game_runs(self):
        self.game_running = True
        before = tree(self.root)
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "is running")
        self.assertEqual(tree(self.root), before)

    def test_unsupported_platform(self):
        self.platform = "darwin"
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "Unsupported operating system")
        self.assertFalse(os.path.exists(os.path.join(self.game, "version.dll")))

    def test_refuses_outside_game_folder(self):
        os.remove(os.path.join(self.game, "Tyrion1.exe"))
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "is not the game folder")

    def test_mod_folder_name_is_required(self):
        with self.assertRaises(ltm.ModError):
            ltm.resolve_dirs(None, os.path.join(self.root, "game", "SomeOtherName", "larger_text_mod.py"))
        game, mod = ltm.resolve_dirs(None, os.path.join(self.mod, "larger_text_mod.py"))
        self.assertEqual((game, mod), (self.game, self.mod))

    def test_original_mod_is_detected(self):
        markers = (
            (os.path.join(self.mod, "state.json"), "file"),
            (os.path.join(self.mod, "backup"), "dir"),
            (os.path.join(self.game, "DialogueTextMod", "state.json"), "file"),
        )
        for path, kind in markers:
            with self.subTest(path=path):
                if kind == "dir":
                    os.makedirs(path)
                else:
                    write(path, "{}")
                before = tree(self.root)
                rc, out, err = self.run_tool("install", "--game-dir", self.game)
                self.assertEqual(rc, 1)
                self.assertSingleError(err, "original (asset-patching)")
                self.assertIn("Verify integrity of game files", err)
                self.assertEqual(tree(self.root), before)
                if kind == "dir":
                    os.rmdir(path)
                else:
                    os.remove(path)

    def test_unsupported_engine_version(self):
        write(os.path.join(self.game, "Tyrion1_Data", "data.unity3d"),
              b"UnityFS\0\0\0\0\x08" + b"5.x.x\0" + b"2022.3.10f1\0")
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "Unsupported Unity version 2022.3.10f1")

    def test_unknown_engine_version(self):
        write(os.path.join(self.game, "Tyrion1_Data", "data.unity3d"), b"garbage")
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertSingleError(err, "Could not determine the game's Unity version")

    def test_not_il2cpp(self):
        os.remove(os.path.join(self.game, "GameAssembly.dll"))
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "IL2CPP")

    def test_unity_player_without_version_import(self):
        write(os.path.join(self.game, "UnityPlayer.dll"), make_pe(("KERNEL32.dll", "USER32.dll")))
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "does not import VERSION.dll")

    def test_broken_unity_player(self):
        write(os.path.join(self.game, "UnityPlayer.dll"), b"MZ" + b"\0" * 10)
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "Could not read UnityPlayer.dll")

    def test_missing_runtime_in_mod_folder(self):
        os.remove(os.path.join(self.mod, "runtime", "version.dll"))
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "runtime file")

    def test_foreign_version_dll_is_not_overwritten(self):
        foreign = b"MZ another loader"
        write(os.path.join(self.game, "version.dll"), foreign)
        before = tree(self.root)
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "different version.dll")
        self.assertEqual(tree(self.root), before)

    def test_identical_preexisting_version_dll_is_reused(self):
        write(os.path.join(self.game, "version.dll"), FAKE_RUNTIME)
        out = self.install(self.game)
        self.assertIn("pre-existing", out)
        m = self.manifest(self.game)
        self.assertEqual(m["preexisting"], [{"path": "version.dll", "sha256": sha(FAKE_RUNTIME)}])
        self.install(self.game)  # re-install keeps it pre-existing
        self.assertEqual(self.manifest(self.game)["preexisting"], m["preexisting"])
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 0, err)
        self.assertEqual(read(os.path.join(self.game, "version.dll")), FAKE_RUNTIME)

    def test_older_own_loader_is_replaced(self):
        self.install(self.game)
        write(os.path.join(self.mod, "runtime", "version.dll"), FAKE_RUNTIME_NEW)
        out = self.install(self.game)
        self.assertIn("(updated)", out)
        self.assertEqual(read(os.path.join(self.game, "version.dll")), FAKE_RUNTIME_NEW)
        self.assertEqual(self.manifest(self.game)["files"][0]["sha256"], sha(FAKE_RUNTIME_NEW))

    def test_missing_loader_is_restored(self):
        self.install(self.game)
        expected = tree(self.game)
        os.remove(os.path.join(self.game, "version.dll"))
        self.install(self.game)
        self.assertEqual(tree(self.game), expected)

    def _failing_on(self, target_name, real=ltm.atomic_write):
        def fake(path, data, mode=None):
            if os.path.basename(path) == target_name:
                raise OSError(28, "No space left on device")
            return real(path, data, mode)
        return mock.patch.object(ltm, "atomic_write", side_effect=fake)

    def test_rollback_on_write_failure_fresh(self):
        before = tree(self.root)
        with self._failing_on("version.dll"):
            rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "Install failed")
        self.assertIn("undone", err)
        self.assertEqual(tree(self.root), before)

    def test_rollback_on_write_failure_reinstall(self):
        self.install(self.game, "--scale", "1.3")
        write(os.path.join(self.mod, "runtime", "version.dll"), FAKE_RUNTIME_NEW)
        before = tree(self.root)
        with self._failing_on("version.dll"):
            rc, out, err = self.run_tool("install", "--game-dir", self.game, "--scale", "1.8")
        self.assertEqual(rc, 1)
        self.assertEqual(tree(self.root), before)

    def test_interrupted_install_can_be_uninstalled(self):
        before = tree(self.game)

        def interrupted(path, data, mode=None, real=ltm.atomic_write):
            if os.path.basename(path) == "version.dll":
                raise KeyboardInterrupt  # like a killed process: no rollback runs
            return real(path, data, mode)

        with mock.patch.object(ltm, "atomic_write", side_effect=interrupted):
            rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 130)
        self.assertTrue(os.path.exists(os.path.join(self.mod, "manifest.json")))
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 0, err)
        self.assertEqual(tree(self.game), before)

    def test_invalid_manifest_is_reported(self):
        write(os.path.join(self.mod, "manifest.json"), "{not json")
        rc, out, err = self.run_tool("install", "--game-dir", self.game)
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "manifest")


class EngineCheckTests(unittest.TestCase):
    def test_pe_parser_pe32_and_pe32plus(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "x.dll")
            for plus in (True, False):
                write(path, make_pe(("A.dll", "VERSION.dll", "b.dll"), pe32plus=plus))
                self.assertEqual(ltm.pe_imported_dlls(path), ["A.dll", "VERSION.dll", "b.dll"])
            write(path, b"not a pe file at all" * 10)
            with self.assertRaises(ValueError):
                ltm.pe_imported_dlls(path)

    def test_pe_parser_rejects_bad_rva(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "x.dll")
            data = bytearray(make_pe())
            struct.pack_into("<I", data, 0x40 + 24 + 112 + 8, 0x999999)  # import RVA outside sections
            write(path, bytes(data))
            with self.assertRaises(ValueError):
                ltm.pe_imported_dlls(path)

    def test_unity_version_from_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = make_fake_game(tmp, unity="6000.5.8f1")
            self.assertEqual(ltm.check_engine(game), "6000.5.8f1")


class ProcessDetectionTests(unittest.TestCase):
    def make_proc(self, tmp, processes):
        for pid, (comm, cmdline) in processes.items():
            write(os.path.join(tmp, str(pid), "comm"), comm + "\n")
            write(os.path.join(tmp, str(pid), "cmdline"), b"\0".join(a.encode() for a in cmdline) + b"\0")
        os.makedirs(os.path.join(tmp, "self"), exist_ok=True)

    def test_linux_detects_game_by_comm(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.make_proc(tmp, {100: ("bash", ["bash"]), 200: ("Tyrion1.exe", ["Z:\\x\\Tyrion1.exe"])})
            self.assertTrue(ltm._linux_game_running(tmp))

    def test_linux_detects_game_by_cmdline(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.make_proc(tmp, {300: ("wine64-preload", ["C:\\Games\\Tyrion\\TYRION1.EXE", "-arg"])})
            self.assertTrue(ltm._linux_game_running(tmp))

    def test_linux_no_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.make_proc(tmp, {100: ("bash", ["bash", "-c", "Tyrion1.exe.bak"]), 101: ("steam", ["steam"])})
            self.assertFalse(ltm._linux_game_running(tmp))

    def test_windows_tasklist(self):
        running = subprocess.CompletedProcess([], 0, b'"Tyrion1.exe","1234","Console","1","500,000 K"\r\n', b"")
        idle = subprocess.CompletedProcess([], 0, b"INFO: No tasks are running which match the criteria.\r\n", b"")
        with mock.patch.object(ltm.subprocess, "run", return_value=running) as run:
            self.assertTrue(ltm._windows_game_running())
            self.assertIn("IMAGENAME eq Tyrion1.exe", run.call_args[0][0])
        with mock.patch.object(ltm.subprocess, "run", return_value=idle):
            self.assertFalse(ltm._windows_game_running())
        with mock.patch.object(ltm.subprocess, "run", side_effect=OSError("no tasklist")):
            with self.assertRaises(ltm.ModError):
                ltm._windows_game_running()


if __name__ == "__main__":
    unittest.main()
