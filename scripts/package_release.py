"""Build platform-specific release archives using only Python's standard library."""
from __future__ import annotations

import argparse
import io
from pathlib import Path
import re
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FOLDER = 'TyrionCuthbertLargerTextMod'
# Explicit file lists keep local backups, game assets, and environments out.
COMMON_FILES = ('mod.py', 'README.md')
WINDOWS_FILES = COMMON_FILES + ('Install.bat', 'Uninstall.bat', 'windows_setup.py')
LINUX_FILES = COMMON_FILES + ('Install.sh', 'Uninstall.sh')


def release_tag(value):
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?', value):
        raise argparse.ArgumentTypeError('Use a tag such as v1.0.0 or v1.0.0-rc.1.')
    return value


def file_bytes(name):
    data = (ROOT / name).read_bytes()
    # Ensure launchers work even when packaging from a Windows checkout.
    if name.endswith('.sh'):
        data = data.replace(b'\r\n', b'\n')
    elif name.endswith('.bat'):
        data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag', type=release_tag)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'dist')
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    prefix = f'{FOLDER}-{args.tag}'

    windows = args.output_dir / f'{prefix}-windows.zip'
    with zipfile.ZipFile(windows, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name in WINDOWS_FILES:
            archive.writestr(f'{FOLDER}/{name}', file_bytes(name))

    linux = args.output_dir / f'{prefix}-linux.tar.gz'
    with tarfile.open(linux, 'w:gz') as archive:
        for name in LINUX_FILES:
            data = file_bytes(name)
            info = tarfile.TarInfo(f'{FOLDER}/{name}')
            info.size = len(data)
            info.mode = 0o755 if name.endswith('.sh') else 0o644
            archive.addfile(info, io.BytesIO(data))

    print(windows)
    print(linux)


if __name__ == '__main__':
    main()
