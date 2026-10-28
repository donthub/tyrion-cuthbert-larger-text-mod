"""Download the latest stable GitHub release into this mod folder (Python 3.10+)."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import sys
import tarfile
import tempfile
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
import zipfile

HERE = Path(__file__).resolve().parent
REPOSITORY = 'donthub/tyrion-cuthbert-larger-text-mod'
API_URL = f'https://api.github.com/repos/{REPOSITORY}/releases/latest'
FOLDER = 'TyrionCuthbertLargerTextMod'
MAX_BYTES = 32 * 1024 * 1024
REQUIRED = {
    'win32': {'mod.py', 'README.md', 'Install.bat', 'Uninstall.bat', 'windows_setup.py'},
    'linux': {'mod.py', 'README.md', 'Install.sh', 'Uninstall.sh'},
}
OPTIONAL = {'update_mod.py', 'AGENTS.md', 'image-modded.jpg', 'image-unmodded.jpg'}


def download(url, limit=MAX_BYTES):
    request = Request(url, headers={'User-Agent': FOLDER + '-updater',
                                    'Accept': 'application/vnd.github+json' if url == API_URL
                                    else 'application/octet-stream'})
    try:
        with urlopen(request, timeout=60) as response:
            data = response.read(limit + 1)
    except HTTPError as error:
        if error.code == 404:
            raise RuntimeError('No published release or matching download was found on GitHub.') from error
        if error.code in (403, 429):
            raise RuntimeError('GitHub refused the download or its rate limit was reached. Try again later.') from error
        raise RuntimeError(f'GitHub download failed (HTTP {error.code}).') from error
    except (URLError, TimeoutError) as error:
        raise RuntimeError(f'Download failed. Check your internet connection: {error}') from error
    if len(data) > limit:
        raise RuntimeError('GitHub response exceeds the updater size limit.')
    return data


def release_asset(release, platform):
    tag = release.get('tag_name', '')
    if release.get('draft') or release.get('prerelease') or not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+', tag):
        raise RuntimeError('GitHub did not return a supported stable release.')
    suffix = 'Windows.zip' if platform == 'win32' else 'Linux-SteamDeck.tar.gz'
    name = f'{FOLDER}-{tag}-{suffix}'
    assets = [asset for asset in release.get('assets', []) if asset.get('name') == name]
    if len(assets) != 1:
        raise RuntimeError(f'The latest release is missing the expected asset: {name}')
    asset = assets[0]
    expected_url = f'https://github.com/{REPOSITORY}/releases/download/{tag}/{name}'
    if asset.get('browser_download_url') != expected_url:
        raise RuntimeError('The release asset does not point to this mod\'s GitHub repository.')
    return tag, asset


def archive_files(data, platform):
    """Read only known release files; never extract paths or links from an archive."""
    required = REQUIRED[platform]
    allowed = required | OPTIONAL | {'Update.bat' if platform == 'win32' else 'Update.sh'}
    files = {}
    total = 0

    def add(name, size, regular, read):
        nonlocal total
        filename = name.removeprefix(FOLDER + '/')
        if name != f'{FOLDER}/{filename}' or filename not in allowed or not regular:
            raise RuntimeError(f'Unexpected file or link in release archive: {name}')
        if filename in files:
            raise RuntimeError(f'Duplicate file in release archive: {name}')
        total += size
        if size < 0 or total > MAX_BYTES:
            raise RuntimeError('Unpacked release exceeds the updater size limit.')
        files[filename] = read()

    if platform == 'win32':
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for item in archive.infolist():
                if item.is_dir() and item.filename == FOLDER + '/':
                    continue
                kind = stat.S_IFMT(item.external_attr >> 16)
                add(item.filename, item.file_size, kind in (0, stat.S_IFREG) and not item.is_dir(),
                    lambda: archive.read(item))
    else:
        with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
            for item in archive:
                if item.isdir() and item.name.rstrip('/') == FOLDER:
                    continue
                def read_member():
                    with archive.extractfile(item) as source:
                        return source.read()
                add(item.name, item.size, item.isfile(), read_member)
    missing = required - files.keys()
    if missing:
        raise RuntimeError('Incomplete release archive; missing: ' + ', '.join(sorted(missing)))
    return files


def atomic_write(path, data, mode):
    descriptor, name = tempfile.mkstemp(prefix='.mod-update-', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'wb') as output:
            output.write(data)
        temporary.chmod(mode)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def replace_files(folder, files):
    # Snapshot everything before writing. Backups, state and environments are
    # excluded by archive_files' explicit allowlist.
    previous = {}
    for name in files:
        path = folder / name
        if path.is_symlink() or (path.exists() and not path.is_file()):
            raise RuntimeError(f'Refusing to replace a link or non-file: {path}')
        previous[name] = (path.read_bytes(), stat.S_IMODE(path.stat().st_mode)) if path.exists() else None
    changed = []
    try:
        for name, data in files.items():
            old = previous[name]
            mode = 0o755 if name.endswith('.sh') else (old[1] if old else 0o644)
            atomic_write(folder / name, data, mode)
            changed.append(name)
    except (OSError, KeyboardInterrupt) as error:
        failed = []
        for name in reversed(changed):
            try:
                if previous[name] is None:
                    (folder / name).unlink()
                else:
                    atomic_write(folder / name, *previous[name])
            except OSError:
                failed.append(name)
        if failed:
            raise RuntimeError('Update and rollback failed for: ' + ', '.join(failed)
                               + '. Re-extract a release into this folder; keep backup and state.json.') from error
        raise RuntimeError('Update failed; previous mod files were restored.') from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true',
                        help='Download and validate the latest release without replacing any files.')
    args = parser.parse_args()
    if sys.version_info < (3, 10):
        parser.error('Python 3.10 or newer is required.')
    if sys.platform not in REQUIRED:
        parser.error('This updater supports Windows and Linux / Steam Deck.')
    print('Checking the latest stable GitHub release...', flush=True)
    release = json.loads(download(API_URL, limit=1024 * 1024))
    tag, asset = release_asset(release, sys.platform)
    print(f'Downloading {asset["name"]}...', flush=True)
    data = download(asset['browser_download_url'])
    if len(data) != asset.get('size'):
        raise RuntimeError('Download size does not match GitHub release metadata.')
    digest = asset.get('digest')
    if digest and digest != 'sha256:' + hashlib.sha256(data).hexdigest():
        raise RuntimeError('Download checksum does not match GitHub release metadata.')
    files = archive_files(data, sys.platform)
    if args.check:
        print(f'Latest release {tag} validated ({len(files)} files). No files changed.')
        return 0
    replace_files(HERE, files)
    installer = 'Install.bat' if sys.platform == 'win32' else 'bash Install.sh'
    print(f'Mod files updated to {tag}. Backups and state.json were preserved.')
    print(f'Close the game, then run {installer} with your preferred scale and opacity to apply the update.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
