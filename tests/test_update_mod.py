"""Updater download, archive safety, and failure recovery tests; no network needed."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError, URLError
import zipfile

from scripts import package_release
import update_mod


class UpdaterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        for name in ('mod.py', 'state.json', 'backup/original', '.venv-linux/marker',
                     '.mod-tools/marker', 'custom.txt'):
            path = self.folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'previous ' + name.encode())

    def snapshot(self):
        return {p.relative_to(self.folder).as_posix(): p.read_bytes()
                for p in self.folder.rglob('*') if p.is_file()}

    def archive(self, platform, extra=(), omit=()):
        output = io.BytesIO()
        members = [(f'{update_mod.FOLDER}/{name}', b'new ' + name.encode())
                   for name in sorted(update_mod.REQUIRED[platform]) if name not in omit]
        members.extend(extra)
        if platform == 'win32':
            with zipfile.ZipFile(output, 'w') as archive:
                for name, data in members:
                    archive.writestr(name, data)
        else:
            with tarfile.open(fileobj=output, mode='w:gz') as archive:
                for name, data in members:
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    archive.addfile(info, io.BytesIO(data))
        return output.getvalue()

    def release(self, platform, data):
        suffix = 'Windows.zip' if platform == 'win32' else 'Linux-SteamDeck.tar.gz'
        name = f'{update_mod.FOLDER}-v1.2.3-{suffix}'
        return {'tag_name': 'v1.2.3', 'draft': False, 'prerelease': False,
                'assets': [{'name': name, 'size': len(data),
                            'digest': 'sha256:' + hashlib.sha256(data).hexdigest(),
                            'browser_download_url': f'https://github.com/{update_mod.REPOSITORY}'
                                                    f'/releases/download/v1.2.3/{name}'}]}

    def run_update(self, platform, data, release=None, args=()):
        release = self.release(platform, data) if release is None else release
        with patch.object(update_mod, 'HERE', self.folder), patch('sys.platform', platform), \
                patch('sys.argv', ['update_mod.py', *args]), \
                patch.object(update_mod, 'download', side_effect=[json.dumps(release).encode(), data]), \
                contextlib.redirect_stdout(io.StringIO()):
            return update_mod.main()

    def test_update_both_platforms_preserves_state_backups_and_environments(self):
        original = self.snapshot()
        for platform in ('win32', 'linux'):
            with self.subTest(platform=platform):
                self.assertEqual(self.run_update(platform, self.archive(platform)), 0)
                self.assertEqual((self.folder / 'mod.py').read_bytes(), b'new mod.py')
                for name, data in original.items():
                    if name != 'mod.py':
                        self.assertEqual((self.folder / name).read_bytes(), data)
                if platform == 'linux' and sys.platform != 'win32':
                    self.assertTrue((self.folder / 'Install.sh').stat().st_mode & stat.S_IXUSR)

    def test_check_downloads_and_validates_without_writing(self):
        before = self.snapshot()
        self.assertEqual(self.run_update('win32', self.archive('win32'), args=['--check']), 0)
        self.assertEqual(self.snapshot(), before)

    def test_bad_checksum_or_size_never_changes_files(self):
        data = self.archive('win32')
        for field, value in (('digest', 'sha256:bad'), ('size', len(data) + 1)):
            release = self.release('win32', data)
            release['assets'][0][field] = value
            before = self.snapshot()
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                self.run_update('win32', data, release)
            self.assertEqual(self.snapshot(), before)

    def test_older_release_without_digest_or_updater_is_supported(self):
        data = self.archive('linux')
        release = self.release('linux', data)
        release['assets'][0].pop('digest')
        self.run_update('linux', data, release)
        self.assertEqual((self.folder / 'mod.py').read_bytes(), b'new mod.py')

    def test_invalid_release_or_missing_platform_asset(self):
        for field, value in (('draft', True), ('prerelease', True),
                             ('tag_name', '../bad'), ('assets', [])):
            release = self.release('win32', b'')
            release[field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                update_mod.release_asset(release, 'win32')
        release = self.release('win32', b'')
        release['assets'][0]['browser_download_url'] = 'https://example.com/mod.zip'
        with self.assertRaises(RuntimeError):
            update_mod.release_asset(release, 'win32')

    def test_unsafe_duplicate_or_incomplete_archives_never_change_files(self):
        before = self.snapshot()
        for platform in ('win32', 'linux'):
            for name in ('../outside', '/absolute', f'{update_mod.FOLDER}/../outside',
                         f'{update_mod.FOLDER}/state.json', f'{update_mod.FOLDER}/backup/original',
                         f'{update_mod.FOLDER}/mod.py', f'{update_mod.FOLDER}/sub/new.py'):
                with self.subTest(platform=platform, name=name):
                    with self.assertWarns(UserWarning) if name.endswith('/mod.py') and platform == 'win32' else contextlib.nullcontext():
                        data = self.archive(platform, [(name, b'bad')])
                    with self.assertRaises(RuntimeError):
                        self.run_update(platform, data)
                    self.assertEqual(self.snapshot(), before)
            with self.assertRaisesRegex(RuntimeError, 'missing'):
                update_mod.archive_files(self.archive(platform, omit=['mod.py']), platform)

    def test_archive_links_are_rejected(self):
        output = io.BytesIO()
        with zipfile.ZipFile(output, 'w') as archive:
            link = zipfile.ZipInfo(f'{update_mod.FOLDER}/mod.py')
            link.create_system = 3
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(link, '../outside')
        with self.assertRaisesRegex(RuntimeError, 'link'):
            update_mod.archive_files(output.getvalue(), 'win32')
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE):
            output = io.BytesIO()
            with tarfile.open(fileobj=output, mode='w:gz') as archive:
                link = tarfile.TarInfo(f'{update_mod.FOLDER}/mod.py')
                link.type = kind
                link.linkname = '../outside'
                archive.addfile(link)
            with self.assertRaisesRegex(RuntimeError, 'link'):
                update_mod.archive_files(output.getvalue(), 'linux')

    def test_failed_replacement_restores_old_files_and_removes_new_ones(self):
        before = self.snapshot()
        write = update_mod.atomic_write

        def fail(path, data, mode):
            if path.name == 'Install.bat':
                raise OSError('simulated write failure')
            write(path, data, mode)

        with patch.object(update_mod, 'atomic_write', side_effect=fail), \
                self.assertRaisesRegex(RuntimeError, 'restored'):
            update_mod.replace_files(self.folder, {'mod.py': b'updated', 'README.md': b'new',
                                                  'Install.bat': b'failure'})
        self.assertEqual(self.snapshot(), before)

    def test_network_errors_and_oversized_response(self):
        for error in (HTTPError(update_mod.API_URL, 404, 'missing', {}, None),
                      HTTPError(update_mod.API_URL, 403, 'rate limited', {}, None),
                      URLError('offline'), TimeoutError()):
            with patch.object(update_mod, 'urlopen', side_effect=error), self.assertRaises(RuntimeError):
                update_mod.download(update_mod.API_URL)
        with patch.object(update_mod, 'urlopen', return_value=io.BytesIO(b'12345')), \
                self.assertRaisesRegex(RuntimeError, 'size limit'):
            update_mod.download(update_mod.API_URL, limit=4)

    def test_packaged_releases_include_updater_and_are_accepted(self):
        subprocess.run([sys.executable, str(package_release.ROOT / 'scripts/package_release.py'),
                        'v1.2.3', '--output-dir', str(self.folder)], check=True, capture_output=True)
        for platform, suffix, launcher in (('win32', 'Windows.zip', 'Update.bat'),
                                           ('linux', 'Linux-SteamDeck.tar.gz', 'Update.sh')):
            archive = self.folder / f'{update_mod.FOLDER}-v1.2.3-{suffix}'
            files = update_mod.archive_files(archive.read_bytes(), platform)
            self.assertIn('update_mod.py', files)
            self.assertIn(launcher, files)
            if platform == 'linux':
                self.assertNotIn(b'\r', files[launcher])
                with tarfile.open(archive) as package:
                    self.assertEqual(package.getmember(f'{update_mod.FOLDER}/{launcher}').mode, 0o755)


if __name__ == '__main__':
    unittest.main()
