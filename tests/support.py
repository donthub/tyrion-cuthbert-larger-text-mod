"""Shared helpers for the Python tool tests: fake game folders, PE files, release archives.

No real game files are used or touched. Everything lives in temporary folders.
"""

import contextlib
import hashlib
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import unittest
from unittest import mock

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL_DIR = os.path.join(REPO_ROOT, "tool")
SCRIPTS_DIR = os.path.join(REPO_ROOT, "scripts")
for _p in (TOOL_DIR, SCRIPTS_DIR):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import larger_text_mod as ltm  # noqa: E402
import package_release  # noqa: E402
import update_mod  # noqa: E402

MOD = ltm.MOD_DIR_NAME
FAKE_RUNTIME = b"MZ" + b"\x90" * 62 + b"fake runtime component v2.0.0"
FAKE_RUNTIME_NEW = b"MZ" + b"\x90" * 62 + b"fake runtime component v2.1.0"


def make_pe(imports=("KERNEL32.dll", "VERSION.dll"), pe32plus=True):
    """Builds a minimal PE file whose import table lists the given DLL names."""
    opt_size = 240 if pe32plus else 224
    pe_offset = 0x40
    section_table = pe_offset + 4 + 20 + opt_size
    raw_ptr = 0x200
    va = 0x1000
    descriptors_size = 20 * (len(imports) + 1)
    names = b""
    name_offsets = []
    for name in imports:
        name_offsets.append(descriptors_size + len(names))
        names += name.encode("ascii") + b"\0"
    section = bytearray(descriptors_size) + names
    for i, off in enumerate(name_offsets):
        struct.pack_into("<IIIII", section, 20 * i, 0, 0, 0, va + off, 0)
    section += b"\0" * ((-len(section)) % 0x200)

    data = bytearray(raw_ptr)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, pe_offset)
    data[pe_offset:pe_offset + 4] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, pe_offset + 4, 0x8664 if pe32plus else 0x14C, 1, 0, 0, 0, opt_size, 0x2022)
    opt = pe_offset + 24
    struct.pack_into("<H", data, opt, 0x20B if pe32plus else 0x10B)
    count_off, dirs_off = (108, 112) if pe32plus else (92, 96)
    struct.pack_into("<I", data, opt + count_off, 16)
    struct.pack_into("<II", data, opt + dirs_off + 8, va, descriptors_size)
    struct.pack_into("<8sIIII", data, section_table, b".idata", len(section), va, len(section), raw_ptr)
    return bytes(data) + bytes(section)


def unity_header(version="6000.5.8f1"):
    return b"UnityFS\0\0\0\0\x08" + b"5.x.x\0" + version.encode("ascii") + b"\0" + b"\0" * 64


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def read(path):
    with open(path, "rb") as f:
        return f.read()


def make_fake_game(root, unity="6000.5.8f1", imports=("KERNEL32.dll", "VERSION.dll"), il2cpp=True,
                   runtime=FAKE_RUNTIME, version="2.0.0"):
    """Creates <root>/game with fake game files and a mod folder. Returns the game folder."""
    game = os.path.join(root, "game")
    write(os.path.join(game, "Tyrion1.exe"), b"MZ fake game exe")
    write(os.path.join(game, "UnityPlayer.dll"), make_pe(imports))
    write(os.path.join(game, "Tyrion1_Data", "data.unity3d"), unity_header(unity))
    if il2cpp:
        write(os.path.join(game, "GameAssembly.dll"), b"MZ fake GameAssembly")
        write(os.path.join(game, "Tyrion1_Data", "il2cpp_data", "Metadata", "global-metadata.dat"),
              b"\xaf\x1b\xb1\xfa" + b"\0" * 32)
    mod = os.path.join(game, MOD)
    write(os.path.join(mod, "runtime", "version.dll"), runtime)
    write(os.path.join(mod, "VERSION"), version + "\n")
    write(os.path.join(mod, "larger_text_mod.py"), read(os.path.join(TOOL_DIR, "larger_text_mod.py")))
    return game


def tree(root):
    """{relative path: bytes} for every file below root (dirs as None), for before/after comparisons."""
    result = {}
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root).replace(os.sep, "/")
        if rel_dir != ".":
            result[rel_dir + "/"] = None
        for name in filenames:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            result[rel] = read(path)
    return result


def sha(data):
    return hashlib.sha256(data).hexdigest()


class ToolTestCase(unittest.TestCase):
    """Base class: temp folder, platform and game-process detection mocked."""

    platform = "win32"

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.game_running = False
        patches = [
            mock.patch.object(ltm, "current_platform", side_effect=lambda: self.platform),
            mock.patch.object(ltm, "is_game_running", side_effect=lambda plat: self.game_running),
        ]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)
        self.addCleanup(self._tmp.cleanup)

    def run_tool(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = ltm.main(list(argv))
        return rc, out.getvalue(), err.getvalue()

    def install(self, game, *extra):
        rc, out, err = self.run_tool("install", "--game-dir", game, *extra)
        self.assertEqual(rc, 0, out + err)
        return out

    def manifest(self, game):
        with open(os.path.join(game, MOD, "manifest.json"), encoding="utf-8") as f:
            return json.load(f)

    def assertSingleError(self, err, fragment):
        lines = [l for l in err.splitlines() if l.startswith("ERROR:")]
        self.assertEqual(len(lines), 1, err)
        self.assertIn(fragment, err)


# ---------------------------------------------------------------------------
# Release archives for updater tests
# ---------------------------------------------------------------------------

def make_source_root(root, readme="# README\n"):
    """A minimal source tree for package_release: README, tool/*, MinHook licence."""
    src = os.path.join(root, "src")
    write(os.path.join(src, "README.md"), readme)
    for name in os.listdir(TOOL_DIR):
        path = os.path.join(TOOL_DIR, name)
        if os.path.isfile(path) and not name.endswith(".pyc"):
            write(os.path.join(src, "tool", name), read(path))
    write(os.path.join(src, "runtime", "third_party", "minhook", "LICENSE.txt"), "MinHook licence (fake)\n")
    write(os.path.join(src, "AGENTS.md"), "not shipped\n")
    write(os.path.join(src, "tests", "test_x.py"), "not shipped\n")
    return src


def build_release(root, version, platform, runtime=FAKE_RUNTIME_NEW, readme=None):
    """Builds a release archive with package_release. Returns bytes."""
    src = make_source_root(os.path.join(root, "rel-" + version + "-" + platform),
                           readme or "# README {}\n".format(version))
    dll = os.path.join(src, "version.dll")
    write(dll, runtime)
    entries = package_release.collect_files(platform, version, dll, src)
    return package_release.build_zip(entries) if platform == "windows" else package_release.build_targz(entries)


def extract_release_into(game, data, platform):
    """Writes the archive's files into the game folder like a player extracting it."""
    files = update_mod.read_archive(data, platform)
    for rel, content in files.items():
        write(os.path.join(game, MOD, *rel.split("/")), content)
    return files


def release_json(tag, platform, data, draft=False, prerelease=False, digest=True, url=None, name=None):
    name = name or update_mod.archive_name(tag, platform)
    asset = {
        "name": name,
        "size": len(data),
        "browser_download_url": url or update_mod.DOWNLOAD_PREFIX + tag + "/" + name,
    }
    if digest:
        asset["digest"] = "sha256:" + sha(data)
    return {"tag_name": tag, "draft": draft, "prerelease": prerelease, "assets": [asset]}


class FakeResponse:
    def __init__(self, body, headers=None):
        self._buf = io.BytesIO(body)
        self.headers = headers if headers is not None else {"Content-Length": str(len(body))}

    def read(self, n=-1):
        return self._buf.read(n)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class FakeNetwork:
    """Replaces urllib.request.urlopen: maps URLs to bodies or exceptions and records calls."""

    def __init__(self):
        self.routes = {}
        self.calls = []

    def add(self, url, body=None, error=None, headers=None):
        self.routes[url] = (body, error, headers)

    def urlopen(self, request, timeout=None):
        url = request.full_url
        self.calls.append(url)
        assert timeout is not None, "requests must have a timeout"
        assert request.get_header("User-agent") == update_mod.USER_AGENT
        if url not in self.routes:
            raise AssertionError("unexpected request to " + url)
        body, error, headers = self.routes[url]
        if error is not None:
            raise error
        return FakeResponse(body, headers)


def copy_tree(src, dst):
    shutil.copytree(src, dst)
