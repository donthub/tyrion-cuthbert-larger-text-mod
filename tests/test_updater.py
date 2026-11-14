"""Self-updater, with a fake network layer. Release archives are built by package_release.py."""

import contextlib
import gzip
import io
import json
import os
import socket
import stat
import tarfile
import unittest
import urllib.error
import zipfile
from unittest import mock

from support import (FAKE_RUNTIME, FAKE_RUNTIME_NEW, MOD, FakeNetwork, ToolTestCase, build_release,
                     extract_release_into, ltm, make_fake_game, read, release_json, sha, tree, update_mod,
                     write)

API = update_mod.RELEASES_API_URL


def zip_bytes(entries):
    """entries: (name, data, unix mode or None). Raw zip without any validation."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        for name, data, mode in entries:
            info = zipfile.ZipInfo(name, date_time=(2000, 1, 1, 0, 0, 0))
            info.filename = name  # bypass zipfile's own name sanitizing (e.g. backslashes on Windows)
            if mode is not None:
                info.create_system = 3
                info.external_attr = mode << 16
            zf.writestr(info, data)
    return buf.getvalue()


def tar_bytes(members):
    """members: tarfile.TarInfo objects, or (TarInfo, data) pairs for regular files."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as tf:
        for item in members:
            info, data = item if isinstance(item, tuple) else (item, b"")
            if info.isreg():
                info.size = len(data)
                tf.addfile(info, io.BytesIO(data))
            else:
                tf.addfile(info)
    return gzip.compress(buf.getvalue())


def reg(name, data=b"x"):
    return tarfile.TarInfo(name), data


class UpdaterTestBase(ToolTestCase):
    current_version = "2.0.0"
    new_version = "2.1.0"

    def setUp(self):
        super().setUp()
        self.plat = "windows" if self.platform == "win32" else "linux"
        self.game = make_fake_game(self.root, version=self.current_version)
        self.mod = os.path.join(self.game, MOD)
        old = build_release(self.root, self.current_version, self.plat, runtime=FAKE_RUNTIME)
        extract_release_into(self.game, old, self.plat)
        self.net = FakeNetwork()
        p = mock.patch.object(update_mod.urllib.request, "urlopen", side_effect=self.net.urlopen)
        p.start()
        self.addCleanup(p.stop)

    def publish(self, releases):
        self.net.add(API, json.dumps(releases).encode("utf-8"))

    def publish_archive(self, data, tag=None, **kwargs):
        tag = tag or "v" + self.new_version
        rel = release_json(tag, self.plat, data, **kwargs)
        self.publish([rel])
        self.net.add(rel["assets"][0]["browser_download_url"], data)
        return rel

    def new_archive(self, **kwargs):
        return build_release(self.root, self.new_version, self.plat, **kwargs)

    def run_update(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = update_mod.main(["--game-dir", self.game] + list(argv))
        return rc, out.getvalue(), err.getvalue()

    def add_player_files(self):
        write(os.path.join(self.mod, "runtime.log"), "log\n")
        write(os.path.join(self.mod, "runtime.log.1"), "old log\n")
        write(os.path.join(self.mod, ".venv", "pyvenv.cfg"), "env\n")
        write(os.path.join(self.mod, "notes.txt"), "mine\n")

    def assertRejected(self, data, fragment, **kwargs):
        self.publish_archive(data, **kwargs)
        before = tree(self.root)
        rc, out, err = self.run_update()
        self.assertEqual(rc, 1, out + err)
        self.assertSingleError(err, fragment)
        self.assertEqual(tree(self.root), before)


class UpdateSuccessMixin:
    def test_update_success(self):
        self.do_update_success()

    def do_update_success(self):
        self.install(self.game, "--scale", "1.3")
        self.add_player_files()
        config = read(os.path.join(self.mod, "config.ini"))
        keep = {rel: data for rel, data in tree(self.game).items()
                if rel.startswith(MOD + "/") and rel.split("/", 1)[1] in
                ("config.ini", "runtime.log", "runtime.log.1", ".venv/pyvenv.cfg", "notes.txt")}
        data = self.new_archive()
        self.publish_archive(data)
        rc, out, err = self.run_update()
        self.assertEqual(rc, 0, out + err)
        self.assertIn("2.0.0 -> 2.1.0", out)
        self.assertIn("active at the next game start", out)
        expected = update_mod.read_archive(data, self.plat)
        for rel, content in expected.items():
            self.assertEqual(read(os.path.join(self.mod, *rel.split("/"))), content, rel)
        self.assertEqual(read(os.path.join(self.game, "version.dll")), FAKE_RUNTIME_NEW)
        now = tree(self.game)
        for rel, content in keep.items():
            self.assertEqual(now[rel], content, rel)
        self.assertEqual(read(os.path.join(self.mod, "config.ini")), config)
        m = self.manifest(self.game)
        self.assertEqual(m["mod_version"], "2.1.0")
        self.assertEqual(m["files"][0]["sha256"], sha(FAKE_RUNTIME_NEW))
        self.assertEqual(m["files"][1]["sha256"], sha(config))
        # Status agrees that the installation is complete.
        rc, out, err = self.run_tool("status", "--game-dir", self.game)
        self.assertIn("Status: INSTALLED (mod version 2.1.0)", out)
        # And uninstall still removes everything.
        rc, out, err = self.run_tool("uninstall", "--game-dir", self.game)
        self.assertEqual(rc, 0)
        self.assertFalse(os.path.exists(os.path.join(self.game, "version.dll")))
        return out


class WindowsUpdateTests(UpdaterTestBase, UpdateSuccessMixin):
    platform = "win32"

    def test_check_mode_makes_no_writes(self):
        self.install(self.game)
        self.publish_archive(self.new_archive())
        before = tree(self.root)
        rc, out, err = self.run_update("--check")
        self.assertEqual(rc, 0, err)
        self.assertIn("An update is available", out)
        self.assertIn("SHA-256 verified", out)
        self.assertEqual(tree(self.root), before)

    def test_up_to_date_does_not_download(self):
        self.publish_archive(build_release(self.root, "2.0.0", "windows"), tag="v2.0.0")
        rc, out, err = self.run_update()
        self.assertEqual(rc, 0, err)
        self.assertIn("already up to date", out)
        self.assertEqual(self.net.calls, [API])

    def test_force_reinstalls_same_version(self):
        self.install(self.game)
        data = build_release(self.root, "2.0.0", "windows", readme="# changed\n")
        self.publish_archive(data, tag="v2.0.0")
        rc, out, err = self.run_update("--force")
        self.assertEqual(rc, 0, err)
        self.assertEqual(read(os.path.join(self.mod, "README.md")), b"# changed\n")

    def test_refuses_while_game_runs(self):
        self.game_running = True
        before = tree(self.root)
        rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "is running")
        self.assertEqual(self.net.calls, [])
        self.assertEqual(tree(self.root), before)

    def test_not_installed_updates_mod_folder_only(self):
        data = self.new_archive()
        self.publish_archive(data)
        rc, out, err = self.run_update()
        self.assertEqual(rc, 0, err)
        self.assertIn("Run Install", out)
        self.assertFalse(os.path.exists(os.path.join(self.game, "version.dll")))
        self.assertEqual(read(os.path.join(self.mod, "runtime", "version.dll")), FAKE_RUNTIME_NEW)
        self.assertFalse(os.path.exists(os.path.join(self.mod, "manifest.json")))

    def test_preexisting_loader_is_not_touched(self):
        write(os.path.join(self.game, "version.dll"), FAKE_RUNTIME)
        self.install(self.game)
        self.publish_archive(self.new_archive())
        rc, out, err = self.run_update()
        self.assertEqual(rc, 0, err)
        self.assertEqual(read(os.path.join(self.game, "version.dll")), FAKE_RUNTIME)
        self.assertIn("pre-existing", out)

    def test_externally_changed_loader_is_not_replaced(self):
        self.install(self.game)
        write(os.path.join(self.game, "version.dll"), b"MZ other")
        self.publish_archive(self.new_archive())
        rc, out, err = self.run_update()
        self.assertEqual(rc, 0, err)
        self.assertEqual(read(os.path.join(self.game, "version.dll")), b"MZ other")
        self.assertIn("changed by something else", out)

    def test_release_without_digest_is_accepted(self):
        self.install(self.game)
        self.publish_archive(self.new_archive(), digest=False)
        rc, out, err = self.run_update()
        self.assertEqual(rc, 0, err)

    def test_bad_checksum(self):
        self.install(self.game)
        data = self.new_archive()
        rel = release_json("v2.1.0", "windows", data)
        rel["assets"][0]["digest"] = "sha256:" + "0" * 64
        self.publish([rel])
        self.net.add(rel["assets"][0]["browser_download_url"], data)
        before = tree(self.root)
        rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "SHA-256")
        self.assertEqual(tree(self.root), before)

    def test_bad_size(self):
        self.install(self.game)
        data = self.new_archive()
        rel = release_json("v2.1.0", "windows", data)
        rel["assets"][0]["size"] = len(data) + 1
        self.publish([rel])
        self.net.add(rel["assets"][0]["browser_download_url"], data)
        rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "bytes, but the release metadata says")

    def test_oversized_asset_in_metadata(self):
        data = self.new_archive()
        rel = release_json("v2.1.0", "windows", data)
        rel["assets"][0]["size"] = update_mod.MAX_ARCHIVE_BYTES + 1
        self.publish([rel])
        rc, out, err = self.run_update()
        self.assertSingleError(err, "larger than the limit")
        self.assertEqual(self.net.calls, [API])

    def test_oversized_response(self):
        self.net.add(API, b"[" + b" " * 100 + b"]")
        with mock.patch.object(update_mod, "MAX_API_BYTES", 50):
            rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "larger than the limit")
        # Without Content-Length, the limit is enforced while reading.
        self.net.add(API, b"[" + b" " * 100 + b"]", headers={})
        with mock.patch.object(update_mod, "MAX_API_BYTES", 50):
            rc, out, err = self.run_update()
        self.assertSingleError(err, "larger than the limit")

    def test_release_selection(self):
        def rel(tag, **kw):
            return release_json(tag, "windows", b"x", **kw)
        releases = [
            rel("v2.9.0"), rel("v2.10.0"), rel("v3.0.0", draft=True), rel("v4.0.0", prerelease=True),
            rel("v5.0.0-rc.1"), rel("5.0.0"), rel("v6.0"), {"tag_name": "v7.0.0"}, "junk",
        ]
        tag, release = update_mod.select_latest_release(releases)
        self.assertEqual(tag, "v2.10.0")
        with self.assertRaises(ltm.ModError):
            update_mod.select_latest_release([rel("v3.0.0", draft=True), rel("v4.0.0", prerelease=True)])
        with self.assertRaises(ltm.ModError):
            update_mod.select_latest_release({"message": "Not Found"})

    def test_no_stable_release(self):
        self.publish([release_json("v9.0.0", "windows", b"x", prerelease=True)])
        rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "No stable release")

    def test_missing_asset(self):
        data = self.new_archive()
        self.publish_archive(data, name="TyrionCuthbertLargerTextMod-v2.1.0-linux.tar.gz")
        rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "has no archive named TyrionCuthbertLargerTextMod-v2.1.0-windows.zip")

    def test_duplicate_asset(self):
        data = self.new_archive()
        rel = release_json("v2.1.0", "windows", data)
        rel["assets"].append(dict(rel["assets"][0]))
        self.publish([rel])
        rc, out, err = self.run_update()
        self.assertSingleError(err, "more than one")

    def test_wrong_download_url(self):
        for url in ("https://github.com/evil/fork/releases/download/v2.1.0/"
                    "TyrionCuthbertLargerTextMod-v2.1.0-windows.zip",
                    "http://github.com/donthub/tyrion-cuthbert-larger-text-mod/releases/download/v2.1.0/"
                    "TyrionCuthbertLargerTextMod-v2.1.0-windows.zip",
                    "https://github.com/donthub/tyrion-cuthbert-larger-text-mod/releases/download/v2.0.0/"
                    "TyrionCuthbertLargerTextMod-v2.1.0-windows.zip"):
            with self.subTest(url=url):
                self.net.calls.clear()
                self.publish([release_json("v2.1.0", "windows", b"x", url=url)])
                rc, out, err = self.run_update()
                self.assertEqual(rc, 1)
                self.assertSingleError(err, "not the expected one")
                self.assertEqual(self.net.calls, [API])

    def test_invalid_json(self):
        self.net.add(API, b"<html>")
        rc, out, err = self.run_update()
        self.assertSingleError(err, "not valid JSON")

    def test_network_errors(self):
        cases = (
            (urllib.error.URLError("Name or service not known"), "Network error"),
            (socket.timeout("timed out"), "Network error"),
            (ConnectionResetError(104, "reset"), "Network error"),
            (urllib.error.HTTPError(API, 404, "Not Found", {}, None), "HTTP 404"),
            (urllib.error.HTTPError(API, 403, "Forbidden", {}, None), "rate limit"),
            (urllib.error.HTTPError(API, 429, "Too Many Requests", {}, None), "rate limit"),
            (urllib.error.HTTPError(API, 500, "Server Error", {}, None), "HTTP 500"),
        )
        for error, fragment in cases:
            with self.subTest(error=error):
                self.net.add(API, error=error)
                rc, out, err = self.run_update()
                self.assertEqual(rc, 1)
                self.assertSingleError(err, fragment)

    def test_download_404(self):
        data = self.new_archive()
        rel = self.publish_archive(data)
        self.net.add(rel["assets"][0]["browser_download_url"],
                     error=urllib.error.HTTPError("u", 404, "Not Found", {}, None))
        rc, out, err = self.run_update()
        self.assertSingleError(err, "release archive was not found on GitHub (HTTP 404)")

    def test_rollback_after_failed_replacement(self):
        self.install(self.game)
        self.add_player_files()
        os.remove(os.path.join(self.mod, "licenses", "MinHook-LICENSE.txt"))
        os.rmdir(os.path.join(self.mod, "licenses"))  # a new folder will be created
        before = tree(self.game)
        calls = []
        real = ltm.atomic_write

        def flaky(path, data, mode=None):
            calls.append(path)
            if os.path.basename(path) == "manifest.json":
                raise OSError(13, "Permission denied")
            return real(path, data, mode)

        self.publish_archive(self.new_archive())
        with mock.patch.object(ltm, "atomic_write", side_effect=flaky):
            rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "restored to the previous version")
        self.assertGreater(len(calls), 3)
        self.assertEqual(tree(self.game), before)

    def test_failed_loader_write_rolls_back_mod_files(self):
        self.install(self.game)
        before = tree(self.game)
        real = ltm.atomic_write

        def flaky(path, data, mode=None):
            if os.path.normcase(path) == os.path.normcase(os.path.join(self.game, "version.dll")):
                raise OSError(32, "in use")
            return real(path, data, mode)

        self.publish_archive(self.new_archive())
        with mock.patch.object(ltm, "atomic_write", side_effect=flaky):
            rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertEqual(tree(self.game), before)

    def test_failed_rollback_names_files(self):
        self.install(self.game)
        real = ltm.atomic_write
        state = {"failed_once": False}

        def flaky(path, data, mode=None):
            name = os.path.basename(path)
            if name == "manifest.json" and not state["failed_once"]:
                state["failed_once"] = True
                raise OSError(13, "Permission denied")
            if name == "README.md" and state["failed_once"]:
                raise OSError(13, "Permission denied")  # restoring README fails
            return real(path, data, mode)

        self.publish_archive(self.new_archive())
        with mock.patch.object(ltm, "atomic_write", side_effect=flaky):
            rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "could not be restored")
        self.assertIn("README.md", err)
        self.assertIn("Hint: Download the archive", err)


class LinuxUpdateTests(UpdaterTestBase, UpdateSuccessMixin):
    platform = "linux"

    def test_linux_success_details(self):
        script = os.path.join(self.mod, "Install.sh")
        os.chmod(script, 0o644)
        self.do_update_success()
        if os.name == "posix":
            self.assertTrue(os.stat(os.path.join(self.mod, "Update.sh")).st_mode & stat.S_IXUSR)
            self.assertTrue(os.stat(script).st_mode & stat.S_IXUSR)

    def test_proton_note_after_update(self):
        self.install(self.game)
        self.publish_archive(self.new_archive())
        rc, out, err = self.run_update()
        self.assertEqual(rc, 0, err)
        self.assertIn("WINEDLLOVERRIDES", out)

    @unittest.skipUnless(hasattr(os, "symlink") and os.name == "posix", "symlinks need POSIX")
    def test_refuses_to_replace_link(self):
        self.install(self.game)
        target = os.path.join(self.root, "elsewhere.txt")
        write(target, "outside")
        readme = os.path.join(self.mod, "README.md")
        os.remove(readme)
        os.symlink(target, readme)
        self.publish_archive(self.new_archive())
        rc, out, err = self.run_update()
        self.assertEqual(rc, 1)
        self.assertSingleError(err, "link or not a regular file")
        self.assertEqual(read(target), b"outside")


class ArchiveValidationTests(UpdaterTestBase):
    """Unsafe, duplicate, incomplete and link archives are rejected without writes."""
    platform = "win32"

    def good_entries(self, version="2.1.0"):
        files = update_mod.read_archive(self.new_archive(), "windows")
        return [(MOD + "/" + rel, data, 0o100644) for rel, data in sorted(files.items())]

    def test_packaged_archive_is_accepted(self):
        files = update_mod.read_archive(self.new_archive(), "windows")
        self.assertEqual(set(files), update_mod.allowed_files("windows"))

    def test_path_traversal(self):
        self.assertRejected(zip_bytes(self.good_entries() + [(MOD + "/../evil.txt", b"x", None)]), "unsafe path")

    def test_absolute_path(self):
        self.assertRejected(zip_bytes(self.good_entries() + [("/" + MOD + "/x", b"x", None)]), "unsafe path")

    def test_backslash_path(self):
        self.assertRejected(zip_bytes(self.good_entries() + [(MOD + "\\README.md", b"x", None)]), "unsafe path")

    def test_duplicate_entry(self):
        entries = self.good_entries()
        with self.assertWarns(UserWarning):  # zipfile warns about the duplicate name
            data = zip_bytes(entries + [entries[0]])
        self.assertRejected(data, "duplicate entry")

    def test_unknown_file(self):
        self.assertRejected(zip_bytes(self.good_entries() + [(MOD + "/evil.exe", b"x", None)]), "unknown file")

    def test_wrong_top_level(self):
        self.assertRejected(zip_bytes(self.good_entries() + [("Other/README.md", b"x", None)]),
                            "unexpected top-level")

    def test_unexpected_directory(self):
        self.assertRejected(zip_bytes(self.good_entries() + [(MOD + "/plugins/", b"", None)]),
                            "unexpected directory")

    def test_expected_directories_are_allowed(self):
        entries = self.good_entries() + [(MOD + "/", b"", None), (MOD + "/runtime/", b"", None)]
        self.assertEqual(len(update_mod.read_archive(zip_bytes(entries), "windows")), 10)

    def test_incomplete(self):
        entries = [e for e in self.good_entries() if not e[0].endswith("runtime/version.dll")]
        self.assertRejected(zip_bytes(entries), "incomplete (missing runtime/version.dll)")

    def test_wrong_platform_launchers(self):
        entries = self.good_entries() + [(MOD + "/Install.sh", b"#!/bin/sh\n", None)]
        self.assertRejected(zip_bytes(entries), "unknown file")

    def test_zip_symlink(self):
        entries = self.good_entries() + [(MOD + "/notes.txt", b"/etc/passwd", 0o120777)]
        self.assertRejected(zip_bytes(entries), "is a link")

    def test_not_a_zip(self):
        self.assertRejected(b"PK\x03\x04 garbage" * 10, "not a valid zip")

    def test_version_mismatch(self):
        entries = [(n, b"2.0.9\n" if n.endswith("/VERSION") else d, m) for n, d, m in self.good_entries()]
        self.assertRejected(zip_bytes(entries), "VERSION file says")

    def test_tar_links_and_specials(self):
        files = update_mod.read_archive(build_release(self.root, "2.1.0", "linux"), "linux")
        base = [reg(MOD + "/" + rel, data) for rel, data in sorted(files.items())]
        self.assertEqual(len(update_mod.read_archive(tar_bytes(base), "linux")), 10)
        sym = tarfile.TarInfo(MOD + "/notes.txt")
        sym.type, sym.linkname = tarfile.SYMTYPE, "/etc/passwd"
        hard = tarfile.TarInfo(MOD + "/notes.txt")
        hard.type, hard.linkname = tarfile.LNKTYPE, MOD + "/README.md"
        fifo = tarfile.TarInfo(MOD + "/fifo")
        fifo.type = tarfile.FIFOTYPE
        for extra, fragment in ((sym, "is a link"), (hard, "is a link"), (fifo, "not a regular file"),
                                (reg(MOD + "/../x"), "unsafe path"), (reg(MOD + "/README.md"), "duplicate")):
            with self.subTest(fragment=fragment):
                with self.assertRaises(ltm.ModError) as cm:
                    update_mod.read_archive(tar_bytes(base + [extra]), "linux")
                self.assertIn(fragment, cm.exception.message)

    def test_tar_bomb_and_garbage(self):
        with mock.patch.object(update_mod, "MAX_UNPACKED_BYTES", 1000):
            with self.assertRaises(ltm.ModError) as cm:
                update_mod.read_archive(gzip.compress(b"\0" * 5000), "linux")
            self.assertIn("too large", cm.exception.message)
        with self.assertRaises(ltm.ModError):
            update_mod.read_archive(b"not gzip", "linux")
        with self.assertRaises(ltm.ModError):
            update_mod.read_archive(gzip.compress(b"x" * 1000), "linux")


if __name__ == "__main__":
    unittest.main()
