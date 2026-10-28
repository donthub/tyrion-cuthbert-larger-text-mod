"""Validate against local game files in a temporary copy, leaving the game intact.

Run with the installer's UnityPy environment:
    python tests/validate_game.py --game-dir "path/to/game"
"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mod


def validate(game):
    source_folder = game / 'TyrionCuthbertLargerTextMod'
    state, originals, previous_state = mod.prepare_install(game, source_folder)
    paths = [Path(item['path']) for item in state['files']]
    source_hashes = [mod.digest((game / p).read_bytes()) for p in paths]
    reports = []
    with tempfile.TemporaryDirectory(prefix='tyrion-mod-validation-') as temporary:
        root = Path(temporary)
        folder = root / 'TyrionCuthbertLargerTextMod'
        # Copy the live files and metadata to exercise the real upgrade path.
        for path in paths:
            mod.atomic_write(root / path, (game / path).read_bytes())
        if previous_state is not None:
            mod.atomic_write(folder / 'state.json', (source_folder / 'state.json').read_bytes())
            for item in previous_state['files']:
                backup = Path('backup') / item['backup']
                (folder / backup).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source_folder / backup, folder / backup)

        default_hashes = None
        for scale in (1.5, 1.5, 1.3, 1.1, 2.0):
            mod.install(root, folder, scale)
            installed = mod.load_state(folder)
            report = installed['report']
            assert len(report['changes']) == 2
            assert len(report['ui_text_changes']) == 13
            for change in report['changes']:
                assert change['font_size'] == round(26.0 * scale, 2)
            hashes = [mod.digest((root / p).read_bytes()) for p in paths]
            assert hashes == [i['installed_sha256'] for i in installed['files']]
            if scale == 1.5:
                if default_hashes is not None:
                    assert hashes == default_hashes, 'Repeat install changed output'
                default_hashes = hashes
            reports.append(report)

        mod.uninstall(root, folder)
        assert [(root / p).read_bytes() for p in paths] == originals
        if previous_state is not None:
            for item in previous_state['files']:
                backup = Path('backup') / item['backup']
                assert (folder / backup).read_bytes() == (source_folder / backup).read_bytes()
            if state['files'] != previous_state['files']:
                archives = list((folder / 'backup').glob('state-*.json'))
                assert any(json.loads(p.read_text()) == previous_state for p in archives)
    assert [mod.digest((game / p).read_bytes()) for p in paths] == source_hashes
    print('PASS: install, repeat, adjustment, scale limits, backup preservation, exact uninstall.')
    print('Source game files unchanged.')
    return reports


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    args = parser.parse_args()
    validate(args.game_dir.resolve())
