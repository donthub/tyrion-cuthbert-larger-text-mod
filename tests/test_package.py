"""Release packaging: explicit file lists, line endings, modes, determinism, updater acceptance."""

import contextlib
import io
import os
import tarfile
import tempfile
import unittest
import zipfile

from support import FAKE_RUNTIME_NEW, MOD, make_source_root, package_release, read, update_mod, write


class PackageTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.src = make_source_root(self._tmp.name)
        # Simulate a Windows checkout: CRLF in shell scripts and LF in batch files must be fixed.
        for name in ("Install.sh", "Update.sh"):
            path = os.path.join(self.src, "tool", name)
            write(path, read(path).replace(b"\n", b"\r\n"))
        for name in ("Install.bat",):
            path = os.path.join(self.src, "tool", name)
            write(path, read(path).replace(b"\r\n", b"\n"))
        self.dll = os.path.join(self._tmp.name, "version.dll")
        write(self.dll, FAKE_RUNTIME_NEW)
        self.out = os.path.join(self._tmp.name, "dist")

    def build(self, version="2.1.0"):
        return package_release.package(version, self.dll, self.out, self.src)

    def test_archive_names_and_contents(self):
        win, lin = self.build()
        self.assertEqual(os.path.basename(win), "TyrionCuthbertLargerTextMod-v2.1.0-windows.zip")
        self.assertEqual(os.path.basename(lin), "TyrionCuthbertLargerTextMod-v2.1.0-linux.tar.gz")
        with zipfile.ZipFile(win) as zf:
            names = sorted(zf.namelist())
            self.assertEqual(names, sorted(MOD + "/" + r for r in update_mod.allowed_files("windows")))
            self.assertEqual(zf.read(MOD + "/VERSION"), b"2.1.0\n")
            self.assertEqual(zf.read(MOD + "/runtime/version.dll"), FAKE_RUNTIME_NEW)
            self.assertEqual(zf.read(MOD + "/licenses/MinHook-LICENSE.txt"), b"MinHook licence (fake)\n")
            for name in names:
                data = zf.read(name)
                if name.endswith(".bat"):
                    self.assertEqual(data.count(b"\n"), data.count(b"\r\n"), name)
                    self.assertGreater(data.count(b"\r\n"), 5)
                elif not name.endswith(".dll"):
                    self.assertNotIn(b"\r\n", data, name)
        with tarfile.open(lin, "r:gz") as tf:
            members = {m.name: m for m in tf.getmembers()}
            self.assertEqual(sorted(members), sorted(MOD + "/" + r for r in update_mod.allowed_files("linux")))
            for name, member in members.items():
                self.assertTrue(member.isreg(), name)
                self.assertEqual((member.uid, member.gid, member.mtime), (0, 0, package_release.FIXED_MTIME))
                data = tf.extractfile(member).read()
                if name.endswith(".sh"):
                    self.assertEqual(member.mode, 0o755, name)
                    self.assertNotIn(b"\r", data, name)
                    self.assertTrue(data.startswith(b"#!/bin/sh\n"), name)
                else:
                    self.assertEqual(member.mode, 0o644, name)

    def test_nothing_else_is_shipped(self):
        write(os.path.join(self.src, "tool", "config.ini"), "x")
        write(os.path.join(self.src, "tool", "manifest.json"), "x")
        write(os.path.join(self.src, "tool", "runtime.log"), "x")
        win, lin = self.build()
        with zipfile.ZipFile(win) as zf:
            for name in zf.namelist():
                for banned in ("AGENTS", "tests/", "config.ini", "manifest.json", "runtime.log", ".sh"):
                    self.assertNotIn(banned, name)

    def test_deterministic(self):
        first = [read(p) for p in self.build()]
        second = [read(p) for p in self.build()]
        self.assertEqual(first, second)

    def test_updater_accepts_packaged_archives(self):
        win, lin = self.build()
        for path, platform in ((win, "windows"), (lin, "linux")):
            files = update_mod.read_archive(read(path), platform)
            self.assertEqual(set(files), update_mod.allowed_files(platform))
            self.assertEqual(files["VERSION"], b"2.1.0\n")

    def test_prerelease_version(self):
        win, lin = self.build("2.1.0-rc.1")
        self.assertTrue(win.endswith("TyrionCuthbertLargerTextMod-v2.1.0-rc.1-windows.zip"))

    def test_invalid_inputs(self):
        for version in ("v2.1.0", "2.1", "2.1.0 ", "latest"):
            with self.assertRaises(package_release.PackageError):
                self.build(version)
        os.remove(self.dll)
        with self.assertRaises(package_release.PackageError):
            self.build()
        write(self.dll, b"not a dll")
        with self.assertRaises(package_release.PackageError):
            self.build()

    def test_main_reports_errors(self):
        with contextlib.redirect_stderr(io.StringIO()) as err:
            rc = package_release.main(["--version", "2.1.0", "--runtime", os.path.join(self._tmp.name, "missing.dll"),
                                       "--out", self.out, "--source-root", self.src])
        self.assertEqual(rc, 1)
        self.assertTrue(err.getvalue().startswith("ERROR: cannot read"))


if __name__ == "__main__":
    unittest.main()
