#!/usr/bin/env python3
"""Builds the runtime component (version.dll) and runs its unit tests.

Uses a pinned Zig toolchain as a self-contained C/C++ cross compiler. On first
use it is downloaded over HTTPS to .tools/ and verified against a pinned
SHA-256. No game files are needed: the runtime talks to the game only through
the IL2CPP C API at run time.

    python scripts/build_runtime.py [--version 2.0.0] [--test] [--no-dll]

Output: build/release/version.dll, build/test/core_tests[.exe]
"""
from __future__ import annotations

import argparse
import hashlib
import io
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / '.tools'
BUILD = ROOT / 'build'
RUNTIME = ROOT / 'runtime'

ZIG_VERSION = '0.15.2'
ZIG = {
    ('windows', 'x86_64'): ('zip', '3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c'),
    ('linux', 'x86_64'): ('tar.xz', '02aa270f183da276e5b5920b1dac44a63f1a49e55050ebde3aecc9eb82f93239'),
}

CORE = ['config.cpp', 'layout.cpp', 'tx.cpp', 'mod_settings.cpp', 'features.cpp', 'framework.cpp']
ENGINE = ['bootstrap.cpp', 'il2cpp_api.cpp', 'unity.cpp', 'unity_ui.cpp', 'runtime.cpp']
MINHOOK = ['buffer.c', 'hook.c', 'trampoline.c', 'hde/hde64.c']


def host() -> tuple[str, str]:
    system = {'win32': 'windows', 'linux': 'linux'}.get(sys.platform)
    machine = platform.machine().lower()
    machine = {'amd64': 'x86_64', 'x64': 'x86_64'}.get(machine, machine)
    if (system, machine) not in ZIG:
        raise SystemExit(f'ERROR: no pinned Zig toolchain for {sys.platform}/{machine}')
    return system, machine


def zig() -> Path:
    system, machine = host()
    kind, sha = ZIG[(system, machine)]
    name = f'zig-{machine}-{system}-{ZIG_VERSION}'
    exe = TOOLS / name / ('zig.exe' if system == 'windows' else 'zig')
    if exe.exists():
        return exe
    url = f'https://ziglang.org/download/{ZIG_VERSION}/{name}.{kind}'
    print(f'Downloading {url}')
    with urllib.request.urlopen(url, timeout=300) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != sha:
        raise SystemExit('ERROR: Zig download failed its SHA-256 check')
    TOOLS.mkdir(exist_ok=True)
    if kind == 'zip':
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            archive.extractall(TOOLS)
    else:
        with tarfile.open(fileobj=io.BytesIO(data), mode='r:xz') as archive:
            archive.extractall(TOOLS, filter='data') if sys.version_info >= (3, 12) else archive.extractall(TOOLS)
    if system != 'windows':
        exe.chmod(0o755)
    return exe


def run(cmd: list[str]) -> None:
    print(' '.join(str(c) for c in cmd[:6]) + (' ...' if len(cmd) > 6 else ''))
    subprocess.run([str(c) for c in cmd], check=True, cwd=ROOT)


def build_dll(z: Path, version: str) -> Path:
    out = BUILD / 'release'
    obj = BUILD / 'obj'
    shutil.rmtree(obj, ignore_errors=True)
    obj.mkdir(parents=True)
    out.mkdir(parents=True, exist_ok=True)
    target = ['-target', 'x86_64-windows-gnu', '-O2', '-fno-sanitize=all']
    objects = []
    c_sources = [RUNTIME / 'src/loader/proxy.c'] + [RUNTIME / 'third_party/minhook/src' / f for f in MINHOOK]
    for src in c_sources:
        o = obj / (src.stem + '.c.o')
        run([z, 'cc', *target, '-std=c11', '-c', src, '-o', o])
        objects.append(o)
    cpp_sources = [RUNTIME / 'src/core' / f for f in CORE] + [RUNTIME / 'src/engine' / f for f in ENGINE]
    for src in cpp_sources:
        o = obj / (src.parent.name + '_' + src.stem + '.o')
        run([z, 'c++', *target, '-std=c++17', '-Wall', '-Wextra', f'-DLTM_VERSION="{version}"', '-c', src, '-o', o])
        objects.append(o)
    dll = out / 'version.dll'
    run([z, 'c++', *target, '-shared', '-s', '-o', dll, *objects])
    for leftover in out.glob('version.lib'):
        leftover.unlink()
    for leftover in out.glob('version.pdb'):
        leftover.unlink()
    print(f'Built {dll.relative_to(ROOT)} ({dll.stat().st_size} bytes)')
    return dll


def build_tests(z: Path) -> int:
    out = BUILD / 'test'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / ('core_tests.exe' if host()[0] == 'windows' else 'core_tests')
    sources = [RUNTIME / 'tests/test_main.cpp'] + [RUNTIME / 'src/core' / f for f in CORE]
    run([z, 'c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-o', exe, *sources])
    print('Running C++ unit tests')
    return subprocess.run([str(exe)], cwd=ROOT).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--version', default='0.0.0-dev', help='version embedded in the DLL')
    parser.add_argument('--test', action='store_true', help='also build and run the C++ unit tests')
    parser.add_argument('--no-dll', action='store_true', help='skip building version.dll')
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?', args.version):
        parser.error('--version must look like 2.0.0 or 2.0.0-rc.1')
    try:
        z = zig()
        if args.test and build_tests(z) != 0:
            print('ERROR: C++ unit tests failed')
            return 1
        if not args.no_dll:
            build_dll(z, args.version)
    except subprocess.CalledProcessError as e:
        print(f'ERROR: build step failed with exit code {e.returncode}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
