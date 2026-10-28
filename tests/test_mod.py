"""Backup migration and failure recovery tests; no game assets required."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import mod
import windows_setup


class UpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.folder = self.root / 'TyrionCuthbertLargerTextMod'
        self.paths = [mod.AA / 'StandaloneWindows64/defaultlocalgroup_assets_default_new.bundle',
                      mod.AA / 'catalog.bin', mod.AA / 'catalog.hash']
        self.clean = [b'new bundle', b'new catalog', b'new token']
        self.outputs = [b'patched bundle', b'patched catalog', b'patched token']
        for path, content in zip(self.paths, self.clean):
            mod.atomic_write(self.root / path, content)
        self.old = {'files': []}
        for i, path in enumerate(self.paths):
            old_path = str(path).replace('_new.bundle', '_old.bundle')
            original = f'old original {i}'.encode()
            backup = f'{i}-old'
            mod.atomic_write(self.folder / 'backup' / backup, original)
            self.old['files'].append({'path': old_path, 'backup': backup,
                                      'original_sha256': mod.digest(original),
                                      'installed_sha256': mod.digest(b'old patched')})
        mod.atomic_write(self.folder / 'state.json', json.dumps(self.old).encode())
        self.snapshot = self.files()
        known = {tuple(map(mod.digest, self.clean)): 'test-build'}
        self.known = patch.object(mod, 'VERIFIED_UPDATES', known)
        self.known.start()
        self.addCleanup(self.known.stop)
        report = {'scale': 1.5, 'ui_text_changes': [], 'verified_objects': 1, 'modified_objects': 1}
        self.builder = patch.object(mod, 'build', return_value=(*self.outputs, report))
        self.build = self.builder.start()
        self.addCleanup(self.builder.stop)

    def files(self):
        return {p.relative_to(self.root): p.read_bytes()
                for p in self.root.rglob('*') if p.is_file()}

    def test_migrate_repeat_adjust_and_uninstall(self):
        mod.prepare_install(self.root, self.folder)
        self.assertEqual(self.snapshot, self.files(), 'preflight must be read-only')
        mod.install(self.root, self.folder, 1.5)
        state = mod.load_state(self.folder)
        self.assertEqual(state['game_build'], 'test-build')
        archive = list((self.folder / 'backup').glob('state-*.json'))
        self.assertEqual(len(archive), 1)
        self.assertEqual(json.loads(archive[0].read_text()), self.old)
        for item in self.old['files']:
            path = Path('TyrionCuthbertLargerTextMod/backup') / item['backup']
            self.assertEqual(self.files()[path], self.snapshot[path])
        for scale in (1.5, 1.3):
            mod.install(self.root, self.folder, scale)
            self.build.assert_called_with(self.clean[0], self.clean[1], scale, 0.99)
        mod.uninstall(self.root, self.folder)
        self.assertEqual([(self.root / p).read_bytes() for p in self.paths], self.clean)

    def test_unknown_or_partial_update_does_not_write(self):
        for index in range(3):
            with self.subTest(file=index):
                path = self.root / self.paths[index]
                mod.atomic_write(path, b'unknown or partially updated')
                before = self.files()
                with self.assertRaisesRegex(RuntimeError, 'verified clean update'):
                    mod.install(self.root, self.folder, 1.5)
                self.assertEqual(self.files(), before)
                mod.atomic_write(path, self.clean[index])
        self.build.assert_not_called()

    def test_opacity_adjustment_uses_original_backups(self):
        for opacity in (0.0, 0.8, 1.0, 0.99):
            with self.subTest(opacity=opacity):
                mod.install(self.root, self.folder, 1.3, opacity)
                self.build.assert_called_with(self.clean[0], self.clean[1], 1.3, opacity)
        mod.uninstall(self.root, self.folder)
        self.assertEqual([(self.root / p).read_bytes() for p in self.paths], self.clean)

    def test_invalid_opacity_does_not_write(self):
        for opacity in (-0.01, 1.01, float('nan'), float('inf'), -float('inf')):
            with self.subTest(opacity=opacity):
                with self.assertRaisesRegex(ValueError, '--opacity'):
                    mod.install(self.root, self.folder, 1.5, opacity)
                self.assertEqual(self.files(), self.snapshot)
        self.build.assert_not_called()

    def test_failed_build_preserves_all_files(self):
        self.build.side_effect = RuntimeError('unsupported layout')
        with self.assertRaisesRegex(RuntimeError, 'unsupported layout'):
            mod.install(self.root, self.folder, 1.5)
        self.assertEqual(self.files(), self.snapshot)

    def test_corrupt_backup_is_not_treated_as_update(self):
        mod.atomic_write(self.folder / 'backup' / '0-old', b'corrupt')
        before = self.files()
        with self.assertRaisesRegex(RuntimeError, 'Backup verification failed'):
            mod.install(self.root, self.folder, 1.5)
        self.assertEqual(self.files(), before)

    def test_write_failure_restores_new_game_and_previous_state(self):
        write = mod.atomic_write

        def fail_catalog(path, content):
            if path == self.root / self.paths[1] and content == self.outputs[1]:
                raise OSError('simulated write failure')
            write(path, content)

        with patch.object(mod, 'atomic_write', side_effect=fail_catalog):
            with self.assertRaisesRegex(OSError, 'simulated write failure'):
                mod.install(self.root, self.folder, 1.5)
        self.assertEqual(mod.load_state(self.folder), self.old)
        for path in self.paths:
            self.assertEqual((self.root / path).read_bytes(), self.snapshot[path])
        # Retained new backups do not prevent retrying migration.
        mod.install(self.root, self.folder, 1.5)
        mod.check_files(self.root, self.folder, mod.load_state(self.folder))

    def test_uninstall_cannot_restore_previous_build_over_update(self):
        with self.assertRaisesRegex(RuntimeError, 'game update removed'):
            mod.uninstall(self.root, self.folder)
        self.assertEqual(self.files(), self.snapshot)

    def test_existing_install_and_fresh_install_need_no_update_allowlist(self):
        self.known.stop()
        for item, path, content in zip(self.old['files'], self.paths, self.clean):
            item['path'] = path.as_posix()
            item['original_sha256'] = mod.digest(content)
            mod.atomic_write(self.folder / 'backup' / item['backup'], content)
        mod.atomic_write(self.folder / 'state.json', json.dumps(self.old).encode())
        mod.install(self.root, self.folder, 1.5)
        self.assertEqual(mod.load_state(self.folder)['files'][0]['backup'], '0-old')
        self.assertFalse(list((self.folder / 'backup').glob('state-*.json')))
        mod.uninstall(self.root, self.folder)
        (self.folder / 'state.json').unlink()
        mod.install(self.root, self.folder, 1.5)
        mod.uninstall(self.root, self.folder)
        self.assertEqual([(self.root / p).read_bytes() for p in self.paths], self.clean)


class ArgumentTests(unittest.TestCase):
    def test_opacity_cli_default_and_custom_values(self):
        for options, expected in (([], 0.99), (['--opacity', '0'], 0.0),
                                  (['--opacity', '0.8'], 0.8), (['--opacity', '1'], 1.0)):
            with self.subTest(options=options), patch('sys.argv', ['mod.py', 'install', *options]), \
                    patch.object(mod, 'game_closed'), patch.object(mod, 'install') as install:
                mod.main()
                self.assertEqual(install.call_args.args[2:], (1.5, expected))

    def test_invalid_opacity_rejected_before_game_access_or_setup(self):
        for entrypoint in (mod, windows_setup):
            for value in ('-0.1', '1.1', 'nan', 'inf', '-inf', 'invalid'):
                with self.subTest(entrypoint=entrypoint.__name__, value=value), \
                        patch('sys.platform', 'win32'), \
                        patch('sys.argv', ['script.py', 'install', '--opacity=' + value]), \
                        patch.object(mod, 'game_closed') as closed, \
                        patch.object(windows_setup, 'prepare_python') as setup, \
                        contextlib.redirect_stderr(io.StringIO()) as error:
                    with self.assertRaises(SystemExit) as exit_result:
                        entrypoint.main()
                    self.assertEqual(exit_result.exception.code, 2)
                    self.assertIn('--opacity', error.getvalue())
                    closed.assert_not_called()
                    setup.assert_not_called()

    def test_windows_forwards_opacity(self):
        with patch('sys.platform', 'win32'), \
                patch('sys.argv', ['windows_setup.py', 'install', '--opacity', '0.8']), \
                patch.object(Path, 'is_file', return_value=True), \
                patch.object(mod, 'game_closed'), patch.object(mod, 'prepare_install'), \
                patch.object(windows_setup, 'prepare_python', return_value=Path('python.exe')), \
                patch.object(windows_setup.subprocess, 'run') as run:
            run.return_value.returncode = 0
            self.assertEqual(windows_setup.main(), 0)
            self.assertEqual(run.call_args.args[0][-4:], ['--scale', '1.5', '--opacity', '0.8'])


if __name__ == '__main__':
    unittest.main()
