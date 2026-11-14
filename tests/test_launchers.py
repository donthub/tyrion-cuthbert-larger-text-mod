"""Launchers: structure checks on every platform, and real runs on the matching platform."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from support import TOOL_DIR, read, write

ACTIONS = ("Install", "Uninstall", "Update", "Status")


def run(cmd, **kwargs):
    env = dict(os.environ, LTM_NO_PAUSE="1")
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                          env=env, timeout=120, **kwargs)


class LauncherStructureTests(unittest.TestCase):
    def test_all_launchers_exist(self):
        for action in ACTIONS:
            for ext in (".bat", ".sh"):
                self.assertTrue(os.path.isfile(os.path.join(TOOL_DIR, action + ext)), action + ext)

    def test_shell_launchers_are_one_block(self):
        for action in ACTIONS:
            text = read(os.path.join(TOOL_DIR, action + ".sh")).replace(b"\r\n", b"\n").decode()
            self.assertTrue(text.startswith("#!/bin/sh\n"), action)
            body = [l for l in text.splitlines() if l and not l.startswith("#")]
            self.assertEqual(body[0], "{", action)
            self.assertEqual(body[-1], "}", action)
            self.assertEqual(body[-2], 'exit "$rc"', action)
            self.assertIn("uname -s", text)
            self.assertIn("python3", text)

    def test_batch_update_runs_from_one_block(self):
        for action in ACTIONS:
            text = read(os.path.join(TOOL_DIR, action + ".bat")).replace(b"\r\n", b"\n").decode()
            start = text.index("\n(\n")
            end = text.index("\n)\n", start)
            block = text[start:end]
            self.assertIn("%LTM_PY%", block)
            self.assertTrue(block.rstrip().endswith("call exit /b %%LTM_RC%%"), action)
            before = text[:start]
            self.assertNotIn("%LTM_PY%", before)
            self.assertIn("pause", text)
            self.assertIn("winget install Python.Python.3.12", text)


@unittest.skipUnless(sys.platform == "win32", "Windows only")
class WindowsLauncherTests(unittest.TestCase):
    def bat(self, name, *args, cwd=TOOL_DIR):
        return run(["cmd", "/c", os.path.join(cwd, name)] + list(args))

    def test_help_and_bad_arguments(self):
        for action in ACTIONS:
            r = self.bat(action + ".bat", "--help")
            self.assertEqual(r.returncode, 0, r.stdout)
            self.assertIn(b"Usage: " + action.encode() + b".bat", r.stdout)
        self.assertEqual(self.bat("Install.bat", "1", "2", "3").returncode, 2)
        self.assertEqual(self.bat("Uninstall.bat", "x").returncode, 2)
        self.assertEqual(self.bat("Status.bat", "x").returncode, 2)
        self.assertEqual(self.bat("Update.bat", "--nope").returncode, 2)

    def test_failure_exit_code_and_error_line(self):
        r = self.bat("Install.bat", "5")
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn(b"ERROR: Invalid scale '5'", r.stdout)

    def test_update_survives_self_replacement(self):
        with tempfile.TemporaryDirectory() as tmp:
            shutil.copy(os.path.join(TOOL_DIR, "Update.bat"), tmp)
            write(os.path.join(tmp, "update_mod.py"),
                  "import os, sys\n"
                  "p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'Update.bat')\n"
                  "with open(p, 'w', newline='\\r\\n') as f:\n"
                  "    f.write('@echo off\\n' + 'echo REPLACED-FILE-WAS-EXECUTED\\n' * 40 + 'exit /b 42\\n')\n"
                  "print('fake updater done')\n"
                  "sys.exit(0)\n")
            r = self.bat("Update.bat", cwd=tmp)
            self.assertIn(b"fake updater done", r.stdout)
            self.assertNotIn(b"REPLACED-FILE-WAS-EXECUTED", r.stdout)
            self.assertEqual(r.returncode, 0, r.stdout)


@unittest.skipUnless(sys.platform.startswith("linux") and shutil.which("sh"), "Linux only")
class LinuxLauncherTests(unittest.TestCase):
    def sh(self, path, *args):
        return run(["sh", path] + list(args))

    def test_help_and_bad_arguments(self):
        for action in ACTIONS:
            r = self.sh(os.path.join(TOOL_DIR, action + ".sh"), "--help")
            self.assertEqual(r.returncode, 0, r.stdout)
            self.assertIn(b"Usage: ./" + action.encode() + b".sh", r.stdout)
        self.assertEqual(self.sh(os.path.join(TOOL_DIR, "Install.sh"), "1", "2", "3").returncode, 2)
        self.assertEqual(self.sh(os.path.join(TOOL_DIR, "Status.sh"), "x").returncode, 2)
        self.assertEqual(self.sh(os.path.join(TOOL_DIR, "Update.sh"), "--nope").returncode, 2)

    def test_failure_exit_code_and_error_line(self):
        r = self.sh(os.path.join(TOOL_DIR, "Install.sh"), "5")
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn(b"ERROR: Invalid scale '5'", r.stdout)

    def test_update_survives_self_replacement(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = os.path.join(tmp, "Update.sh")
            shutil.copy(os.path.join(TOOL_DIR, "Update.sh"), script)
            write(os.path.join(tmp, "update_mod.py"),
                  "import os, sys\n"
                  "p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'Update.sh')\n"
                  "tmp = p + '.new'\n"
                  "open(tmp, 'w').write('#!/bin/sh\\n' + 'echo REPLACED-FILE-WAS-EXECUTED\\n' * 40 + 'exit 42\\n')\n"
                  "os.replace(tmp, p)\n"
                  "print('fake updater done')\n"
                  "sys.exit(0)\n")
            r = self.sh(script)
            self.assertIn(b"fake updater done", r.stdout)
            self.assertNotIn(b"REPLACED-FILE-WAS-EXECUTED", r.stdout)
            self.assertEqual(r.returncode, 0, r.stdout)

    def test_in_place_overwrite_is_also_survived(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = os.path.join(tmp, "Update.sh")
            shutil.copy(os.path.join(TOOL_DIR, "Update.sh"), script)
            write(os.path.join(tmp, "update_mod.py"),
                  "import os\n"
                  "p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'Update.sh')\n"
                  "open(p, 'w').write('#!/bin/sh\\n' + 'echo REPLACED-FILE-WAS-EXECUTED\\n' * 400 + 'exit 42\\n')\n"
                  "print('fake updater done')\n")
            r = self.sh(script)
            self.assertNotIn(b"REPLACED-FILE-WAS-EXECUTED", r.stdout)
            self.assertEqual(r.returncode, 0, r.stdout)


if __name__ == "__main__":
    unittest.main()
