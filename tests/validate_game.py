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
        settings = ((1.5, 0.99), (1.5, 0.99), (1.3, 0.99), (1.1, 0.99), (2.0, 0.99),
                    (1.5, 0.0), (1.5, 0.8), (1.5, 1.0), (1.5, 0.99))
        for scale, opacity in settings:
            mod.install(root, folder, scale, opacity)
            installed = mod.load_state(folder)
            report = installed['report']
            assert report['opacity'] == opacity
            import UnityPy
            assets = UnityPy.load(str(root / paths[0])).assets[0]
            # Check the actual serialized image alpha in both prefab containers.
            for prefab in mod.PREFABS:
                go = assets.container[prefab].read_typetree()
                controller = next(assets.objects[c['component']['m_PathID']].read_typetree()
                                  for c in go['m_Component']
                                  if 'dialogueDisplay' in assets.objects[c['component']['m_PathID']].read_typetree())
                text = assets.objects[controller['dialogueDisplay']['m_PathID']].read_typetree()
                text_go = assets.objects[text['m_GameObject']['m_PathID']].read_typetree()
                rect = next(assets.objects[c['component']['m_PathID']].read_typetree()
                            for c in text_go['m_Component']
                            if assets.objects[c['component']['m_PathID']].type.name == 'RectTransform')
                panel = assets.objects[rect['m_Father']['m_PathID']].read_typetree()
                panel_go = assets.objects[panel['m_GameObject']['m_PathID']].read_typetree()
                image = next(assets.objects[c['component']['m_PathID']].read_typetree()
                             for c in panel_go['m_Component']
                             if 'm_Texture' in assets.objects[c['component']['m_PathID']].read_typetree())
                assert image['m_Color']['a'] == mod.float32_tree(opacity)
            assert len(report['changes']) == 2
            assert len(report['ui_text_changes']) == 13
            for change in report['changes']:
                assert change['font_size'] == round(26.0 * scale, 2)
            hashes = [mod.digest((root / p).read_bytes()) for p in paths]
            assert hashes == [i['installed_sha256'] for i in installed['files']]
            if (scale, opacity) == (1.5, 0.99):
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
    print('PASS: install, repeat, adjustment, scale/opacity limits, backup preservation, exact uninstall.')
    print('Source game files unchanged.')
    return reports


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    args = parser.parse_args()
    validate(args.game_dir.resolve())
