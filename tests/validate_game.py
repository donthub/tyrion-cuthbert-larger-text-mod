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


def validate_evidence_popup(assets, original_assets, scale):
    """Check the serialized shared notification and its layout at each scale."""
    prefab = 'Assets/Prefabs/GUI/Loaded GUIs/Add Item Popup.prefab'
    go = assets.container[prefab].read_typetree()

    def component(game_object, field):
        return next((c['component']['m_PathID'], data)
                    for c in game_object['m_Component']
                    if field in (data := assets.objects[c['component']['m_PathID']].read_typetree()))

    def rect_of(data):
        owner = assets.objects[data['m_GameObject']['m_PathID']].read_typetree()
        return component(owner, 'm_AnchorMin')[1]

    def parent(rect):
        return assets.objects[rect['m_Father']['m_PathID']].read_typetree()

    controller_id, controller = component(go, 'itemDisplay')
    # Both message variants, localization and display references must survive.
    assert assets.objects[controller_id].get_raw_data() == original_assets.objects[controller_id].get_raw_data()
    assert controller['itemUnlockHeader'] == 'OBTAINED EVIDENCE!'
    assert controller['itemUpdateHeader'] == 'EVIDENCE UPDATED!'
    display = controller['itemDisplay']
    text_ids = [controller['header']['m_PathID'], display['itemName']['m_PathID'],
                display['itemDescription']['m_PathID']]
    for field in display['fieldDisplays']:
        text_ids.extend(field[key]['m_PathID'] for key in ('fieldName', 'fieldValue'))
    for i in text_ids:
        before = original_assets.objects[i].read_typetree()
        after = assets.objects[i].read_typetree()
        size = before['m_fontSizeMax'] if before['m_enableAutoSizing'] else before['m_fontSize']
        assert after['m_fontSizeMax'] == mod.float32_tree(size * scale)
        assert after['m_text'] == before['m_text']

    name = assets.objects[display['itemName']['m_PathID']].read_typetree()
    name_rect = rect_of(name)
    info = parent(name_rect)
    top = parent(info)
    window = parent(top)
    assert window['m_AnchorMin']['x'] == mod.float32_tree(0.075)
    assert window['m_AnchorMax']['x'] == mod.float32_tree(0.925)
    def owner(rect):
        return assets.objects[rect['m_GameObject']['m_PathID']].read_typetree()

    # Unity computes the two columns and their heights at runtime. The icon
    # keeps a fixed 270-unit box and a 30-unit gap before the text column.
    horizontal = component(owner(top), 'm_Spacing')[1]
    assert horizontal['m_Spacing'] == 30.0
    assert horizontal['m_ChildControlWidth'] == horizontal['m_ChildControlHeight'] == 1
    assert horizontal['m_ChildForceExpandWidth'] == horizontal['m_ChildForceExpandHeight'] == 0
    icon_id = display['itemIcon']['m_PathID']
    icon = assets.objects[icon_id].read_typetree()
    icon_rect = rect_of(icon)
    assert icon_rect['m_SizeDelta'] == {'x': 270.0, 'y': 270.0}
    assert assets.objects[icon_id].get_raw_data() == original_assets.objects[icon_id].get_raw_data()
    icon_layout = component(owner(icon_rect), 'm_IgnoreLayout')[1]
    assert not icon_layout['m_IgnoreLayout']
    for axis in ('Width', 'Height'):
        assert icon_layout['m_Min' + axis] == icon_layout['m_Preferred' + axis] == 270.0
        assert icon_layout['m_Flexible' + axis] == 0.0
    for field in display['fieldDisplays']:
        field_text = assets.objects[field['fieldName']['m_PathID']].read_typetree()
        row = parent(rect_of(field_text))
        fields = parent(row)
        assert fields['m_AnchorMax']['x'] == 1.0
        assert row['m_SizeDelta']['y'] == mod.float32_tree(45.0 * scale)
    header = assets.objects[controller['header']['m_PathID']].read_typetree()
    assert header['m_enableAutoSizing']
    assert rect_of(header)['m_SizeDelta']['y'] < (1 - window['m_AnchorMax']['y']) * 1080
    description = assets.objects[display['itemDescription']['m_PathID']].read_typetree()
    description_rect = rect_of(description)
    assert parent(description_rect) == info
    children = [assets.objects[p['m_PathID']].read_typetree() for p in info['m_Children']]
    assert children == [name_rect, description_rect, fields]
    assert description_rect not in [assets.objects[p['m_PathID']].read_typetree()
                                    for p in window['m_Children']]
    # Each parent uses preferred heights, so descriptions wrap and push visible
    # metadata down; hidden field rows no longer leave a fixed empty region.
    for container in (info, window):
        layout = component(owner(container), 'm_Spacing')[1]
        assert layout['m_ChildControlHeight'] == 1
        assert layout['m_ChildForceExpandHeight'] == 0
    assert component(owner(window), 'm_VerticalFit')[1]['m_VerticalFit'] == 2
    assert not component(owner(description_rect), 'm_VerticalFit')[1]['m_Enabled']
    # Check both sides of every changed hierarchy/component link, including
    # added layout components, rather than accepting orphaned serialized data.
    for transform in (window, top, info, icon_rect, description_rect):
        game_object = owner(transform)
        for link in game_object['m_Component']:
            data = assets.objects[link['component']['m_PathID']].read_typetree()
            assert data['m_GameObject'] == transform['m_GameObject']
        for link in transform['m_Children']:
            child = assets.objects[link['m_PathID']].read_typetree()
            assert parent(child) == transform


def validate(game):
    source_folder = game / 'TyrionCuthbertLargerTextMod'
    state, originals, previous_state = mod.prepare_install(game, source_folder)
    paths = [Path(item['path']) for item in state['files']]
    source_hashes = [mod.digest((game / p).read_bytes()) for p in paths]
    import UnityPy
    original_assets = UnityPy.load(originals[0]).assets[0]
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
            assert report['added_objects'] == 3
            assert report['run_in_background'] is False
            player_env = UnityPy.load((root / mod.PLAYER_DATA).read_bytes())
            managers = next(a for a in player_env.assets if a.name == 'globalgamemanagers')
            player_settings = managers.objects[1]
            assert player_settings.read_typetree(check_read=False)['runInBackground'] is False
            original_player = UnityPy.load(originals[3])
            original_managers = next(a for a in original_player.assets if a.name == 'globalgamemanagers')
            before = original_managers.objects[1].get_raw_data()
            after = player_settings.get_raw_data()
            assert len(before) == len(after)
            assert [i for i, (a, b) in enumerate(zip(before, after)) if a != b] == [406]
            # Loading bytes avoids holding a Windows file handle across uninstall.
            assets = UnityPy.load((root / paths[0]).read_bytes()).assets[0]
            validate_evidence_popup(assets, original_assets, scale)
            # Check the actual serialized image alpha in both prefab containers.
            for prefab in mod.PREFABS:
                go = assets.container[prefab].read_typetree()
                controller = next(assets.objects[c['component']['m_PathID']].read_typetree()
                                  for c in go['m_Component']
                                  if 'dialogueDisplay' in assets.objects[c['component']['m_PathID']].read_typetree())
                text_id = controller['dialogueDisplay']['m_PathID']
                text = assets.objects[text_id].read_typetree()
                # Re-enabling the dialogue after Text log must retain its read
                # position. Keep manual reading, timing, content and events intact.
                assert text['rememberReadPosition'] == 1, prefab
                original_text = original_assets.objects[text_id].read_typetree()
                assert text['autoRead'] == original_text['autoRead'] == 0
                assert {k: v for k, v in text.items() if k not in ('size', 'rememberReadPosition')} == {
                    k: v for k, v in original_text.items() if k not in ('size', 'rememberReadPosition')}
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
            assert len(report['ui_text_changes']) == 26
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
    print('PASS: install, repeat, adjustment, scale/opacity limits, dialogue retention settings, '
          'focus setting, backup preservation, exact uninstall.')
    print('Source game files unchanged.')
    return reports


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    args = parser.parse_args()
    validate(args.game_dir.resolve())
