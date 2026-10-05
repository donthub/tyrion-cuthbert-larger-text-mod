"""Reversible dialogue accessibility patch for Tyrion1, Unity 6000.5.8f1."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib

HERE = Path(__file__).resolve().parent
# Keep direct invocation compatible with older Windows installs. New installers
# use an isolated venv, whose ABI-matched packages must take precedence.
if sys.platform == 'win32' and sys.prefix == sys.base_prefix:
    sys.path.insert(0, str(HERE.parent / '.mod-tools' / 'python'))
AA = Path('Tyrion1_Data/StreamingAssets/aa')
PATTERN = 'defaultlocalgroup_assets_default_*.bundle'
PREFABS = ('Assets/Prefabs/GUI/Dialogue Boxes/Dialogue Box.prefab',
           'Assets/Prefabs/GUI/Dialogue Boxes/Narrator Box Variant.prefab')
# UI coordinates use a nominal 1080p canvas: 15 units = 20 pixels at 1440p.
TOOLBAR_OFFSET_Y = 20.0 * 1080.0 / 1440.0
DIALOGUE_BACKGROUND_OPACITY = 0.99
# Leave 2.5% of the screen on each side of the normal dialogue panel.
DIALOGUE_SIDE_MARGIN = 0.025


def digest(data):
    return hashlib.sha256(data).hexdigest()


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.dialogue-mod-tmp')
    with temp.open('wb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temp, path)


def game_closed():
    if sys.platform == 'win32':
        result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                             "@(Get-Process | Where-Object { $_.ProcessName -eq 'Tyrion1' }).Count"],
                                capture_output=True, text=True, check=True,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        running = int(result.stdout.strip()) > 0
    elif sys.platform.startswith('linux'):
        # Proton/Wine exposes the Windows executable name in Linux's comm field.
        result = subprocess.run(['ps', '-eo', 'comm='],
                                capture_output=True, text=True, check=True)
        running = any(name.strip().lower() in ('tyrion1.exe', 'tyrion1')
                      for name in result.stdout.splitlines())
    else:
        raise RuntimeError('Supported systems: Windows and Linux (Steam Deck/Proton).')
    if running:
        raise RuntimeError('Close Tyrion Cuthbert before changing the mod.')


def unpack_blocks(bundle, raw):
    from UnityPy.streams import EndianBinaryReader
    reader = EndianBinaryReader(raw)
    reader.read_string_to_null()
    reader.read_u_int()
    reader.read_string_to_null()
    reader.read_string_to_null()
    return bundle.read_fs(reader)[1].bytes


def expand_ui(assets, scale, modified):
    """Change the inspected prefab paths; retain layout fitters and localization."""
    data = {i: o.read_typetree() for i, o in assets.objects.items()
            if o.type.name in ('GameObject', 'RectTransform', 'Transform', 'MonoBehaviour')}
    transforms = {d['m_GameObject']['m_PathID']: i for i, d in data.items() if 'm_Father' in d}

    def path(go):
        parent = data[transforms[go]]['m_Father']['m_PathID'] if go in transforms else 0
        prefix = path(data[parent]['m_GameObject']['m_PathID']) + '/' if parent else ''
        return prefix + data[go]['m_Name']

    paths = {path(i): i for i, d in data.items() if 'm_Component' in d}

    def change(i):
        if i not in modified:
            modified[i] = copy.deepcopy(data[i])
        return modified[i]

    def rect(p):
        return change(transforms[paths[p]])

    def component(p, field):
        matches = [c['component']['m_PathID'] for c in data[paths[p]]['m_Component']
                   if field in data.get(c['component']['m_PathID'], {})]
        if len(matches) != 1:
            raise RuntimeError(f'Expected one {field} component at {p}.')
        return change(matches[0])

    text_changes = []

    def text(p, factor=scale, keep_minimum=False):
        go = paths[p]
        candidates = [c['component']['m_PathID'] for c in data[go]['m_Component']
                      if any(k in data.get(c['component']['m_PathID'], {})
                             for k in ('m_fontSize', 'drawText'))]
        if len(candidates) != 1:
            raise RuntimeError(f'Expected one text component at {p}.')
        d = change(candidates[0])
        if 'm_fontSize' in d:
            original_size = d['m_fontSizeMax'] if d['m_enableAutoSizing'] else d['m_fontSize']
            target = original_size * factor
            d['m_fontSize'] = d['m_fontSizeBase'] = target
            d['m_fontSizeMax'] = target
            if not keep_minimum:
                d['m_fontSizeMin'] = min(target, d['m_fontSizeMin'] * factor)
        else:
            original_size = d['size']
            target = original_size * factor
            d['size'] = target
        text_changes.append({'path': p, 'original_size': original_size, 'target_size': target,
                             'auto_size': bool(d.get('m_enableAutoSizing', False))})

    for root in ('Dialogue Box', 'Narrator Box Variant'):
        base = root + '/Container'
        # Tint only the background image; child text and controls keep their alpha.
        component(base, 'm_Texture')['m_Color']['a'] = DIALOGUE_BACKGROUND_OPACITY
        panel_height = rect(base)['m_SizeDelta']['y']
        # The nameplate background is baked into the panel texture. Match its
        # vertical stretch and keep the original horizontal bounds on the artwork.
        name = base + '/Nameplate'
        rect(name)['m_SizeDelta']['y'] *= panel_height / 240.0
        text(name + '/Text')
        info = base + '/Character Info'
        rect(info)['m_AnchoredPosition']['y'] *= panel_height / 240.0
        rect(info)['m_SizeDelta']['y'] *= scale
        for label in ('Occupation', 'Age', 'Arcane Art'):
            text(info + '/' + label)
            rect(info + '/' + label)['m_SizeDelta']['y'] *= scale
        # Move the whole row so labels, icons and click targets stay aligned.
        # Always start from the original prefab; reinstalling cannot add drift.
        rect(base + '/Buttons')['m_AnchoredPosition']['y'] += TOOLBAR_OFFSET_Y
        if root == 'Dialogue Box':
            panel = rect(base)
            original_width = panel['m_SizeDelta']['x']
            if original_width != 1554.0:
                raise RuntimeError('Unexpected original dialogue panel width.')
            panel['m_AnchorMin']['x'] = DIALOGUE_SIDE_MARGIN
            panel['m_AnchorMax']['x'] = 1.0 - DIALOGUE_SIDE_MARGIN
            panel['m_AnchoredPosition']['x'] = 0.0
            panel['m_SizeDelta']['x'] = 0.0
            # Preserve each child's proportions on the background artwork.
            # Stretch rectangles instead of transforms so glyphs keep their shape.
            for child in ('Nameplate', 'Character Info', 'Dialogue', 'Buttons'):
                r = rect(base + '/' + child)
                x = r['m_AnchoredPosition']['x']
                width = r['m_SizeDelta']['x']
                pivot = r['m_Pivot']['x']
                r['m_AnchorMin']['x'] += (x - pivot * width) / original_width
                r['m_AnchorMax']['x'] += (x + (1.0 - pivot) * width) / original_width
                r['m_AnchoredPosition']['x'] = 0.0
                r['m_SizeDelta']['x'] = 0.0
    entry = 'Text Log/Scroll View/Viewport/Content/Text Entry'
    for label in ('Char Name', 'Text', 'Text 2', 'Large Text'):
        text(entry + '/' + label)
        # The log's SuperTextMesh prefabs add 0.2 em between characters.
        # Use the font's normal spacing for both log renderers and speaker names.
        spacing_field = 'characterSpacing' if label in ('Text', 'Text 2') else 'm_characterSpacing'
        component(entry + '/' + label, spacing_field)[spacing_field] = 0.0
        rect(entry + '/' + label)['m_SizeDelta']['y'] *= scale
        # These existing fitters recompute the full height after text changes.
        if component(entry + '/' + label, 'm_VerticalFit')['m_VerticalFit'] != 2:
            raise RuntimeError('Text log must retain preferred-height fitting.')
    component(entry, 'm_Spacing')['m_Spacing'] *= scale
    component('Text Log/Scroll View/Viewport/Content', 'm_Spacing')['m_Spacing'] *= scale
    viewport = rect('Text Log/Scroll View')
    viewport['m_AnchorMin']['x'] = 0.075
    viewport['m_AnchorMax']['x'] = 0.925

    # Clues, Spells and People share this description renderer. Keep their
    # original layouts, grid, headings, fields and buttons byte-for-byte intact.
    # Long descriptions retain the original lower auto-size bound to fit the
    # complete entry. Shorter descriptions use the increased maximum size.
    text('Item Menu/Container/Right/Container/Description', keep_minimum=True)
    return text_changes


def float32_tree(value):
    """Normalize every serialized float, including nested layout vectors."""
    if isinstance(value, float):
        return struct.unpack('<f', struct.pack('<f', value))[0]
    if isinstance(value, dict):
        return {k: float32_tree(v) for k, v in value.items()}
    if isinstance(value, list):
        return [float32_tree(v) for v in value]
    return value


def build(original, catalog, scale):
    import UnityPy
    from UnityPy.streams import EndianBinaryReader
    if UnityPy.__version__ != '1.25.4':
        raise RuntimeError('This patch requires UnityPy 1.25.4.')
    env = UnityPy.load(original)
    bundle = env.file
    assets = env.assets[0]
    if assets.unity_version != '6000.5.8f1':
        raise RuntimeError('Unrecognized Unity version; refusing to patch.')
    raw_assets = bytearray(assets.reader.bytes)
    originals = {i: o.get_raw_data() for i, o in assets.objects.items()}
    modified = {}
    changes = []

    def edit(obj, data):
        modified[obj.path_id] = data

    def components(go):
        return [assets.objects[c['component']['m_PathID']] for c in go['m_Component']]

    for prefab in PREFABS:
        go = assets.container[prefab].read_typetree()
        controller = next(o.read_typetree() for o in components(go)
                          if o.type.name == 'MonoBehaviour' and
                          'dialogueDisplay' in o.read_typetree())
        text_obj = assets.objects[controller['dialogueDisplay']['m_PathID']]
        text = text_obj.read_typetree()
        if text.get('size') != 26.0 or 'drawText' not in text:
            raise RuntimeError('Dialogue format does not match the supported build.')
        text['size'] = round(26.0 * scale, 2)
        edit(text_obj, text)
        text_go = assets.objects[text['m_GameObject']['m_PathID']].read_typetree()
        rect_obj = next(o for o in components(text_go) if o.type.name == 'RectTransform')
        rect = rect_obj.read_typetree()
        if rect['m_SizeDelta']['y'] != 70.0:
            raise RuntimeError('Unexpected original dialogue layout.')
        # More wrapping needs roughly scale squared as much vertical space.
        height = math.ceil(70.0 * scale * scale / 10.0) * 10.0
        rect['m_SizeDelta']['y'] = height
        edit(rect_obj, rect)
        panel_obj = assets.objects[rect['m_Father']['m_PathID']]
        panel = panel_obj.read_typetree()
        if panel['m_SizeDelta']['y'] != 240.0:
            raise RuntimeError('Unexpected original panel layout.')
        panel_height = 240.0 + height - 70.0 + 20.0 + 80.0 * (scale - 1.0)
        # Metadata begins 40/240 of the panel height below its top. Reserve
        # its scaled line height plus 12 units before the dialogue starts.
        minimum_height = 3.0 * (25.5 * scale + rect['m_AnchoredPosition']['y'] + height / 2.0 + 12.0)
        panel['m_SizeDelta']['y'] = max(panel_height, math.ceil(minimum_height / 10.0) * 10.0)
        edit(panel_obj, panel)
        changes.append({'prefab': prefab, 'original_size': 26, 'font_size': text['size'],
                        'text_height': height, 'panel_height': panel['m_SizeDelta']['y']})

    ui_changes = expand_ui(assets, scale, modified)
    # Write each staged object once, even when multiple layout adjustments touch it.
    for i, data in modified.items():
        obj = assets.objects[i]
        old = originals[i]
        new = obj.save_typetree(data)
        if len(old) != len(new):
            raise RuntimeError('Object size changed unexpectedly.')
        offset = obj.byte_start
        if raw_assets[offset:offset + len(old)] != old:
            raise RuntimeError('Asset offset validation failed.')
        raw_assets[offset:offset + len(old)] = new
    # Preserve the serialized file byte-for-byte except the intended UI objects.
    replacement = EndianBinaryReader(bytes(raw_assets))
    replacement.flags = assets.flags
    bundle.files[assets.name] = replacement
    patched = bundle.save(packer='lz4')
    check = UnityPy.load(patched)
    after = check.assets[0]
    if set(after.objects) != set(originals):
        raise RuntimeError('Asset object inventory changed.')
    for i, obj in after.objects.items():
        if i in modified:
            expected = float32_tree(modified[i])
            actual = obj.read_typetree()
            if actual != expected:
                differences = {k: (expected.get(k), actual.get(k)) for k in set(expected) | set(actual)
                               if expected.get(k) != actual.get(k)}
                raise RuntimeError(f'Patched object failed verification: {i}: {differences}')
        elif obj.get_raw_data() != originals[i]:
            raise RuntimeError(f'Unrelated object changed: {i}')
    for name, item in bundle.files.items():
        if name != assets.name and item.bytes != check.file.files[name].bytes:
            raise RuntimeError('Resource data changed.')

    old_crc = zlib.crc32(unpack_blocks(env.file, original))
    new_crc = zlib.crc32(unpack_blocks(check.file, patched))
    needle = struct.pack('<I', old_crc)
    if catalog.count(needle) != 1:
        raise RuntimeError('Cannot uniquely identify the bundle CRC in the catalog.')
    offset = catalog.index(needle)
    patched_catalog = catalog[:offset] + struct.pack('<I', new_crc) + catalog[offset + 4:]
    # The local catalog hash is a cache version token. Change it with the catalog.
    hash_token = hashlib.md5(patched_catalog).hexdigest().encode('ascii')
    report = {'revision': 7, 'scale': scale, 'changes': changes, 'ui_text_changes': ui_changes,
              'verified_objects': len(originals),
              'modified_objects': len(modified), 'original_crc': old_crc, 'patched_crc': new_crc,
              'catalog_crc_offset': offset}
    return patched, patched_catalog, hash_token, report


def load_state(folder):
    path = folder / 'state.json'
    return json.loads(path.read_text()) if path.exists() else None


def check_files(root, folder, state):
    for item in state['files']:
        backup = folder / 'backup' / item['backup']
        if digest(backup.read_bytes()) != item['original_sha256']:
            raise RuntimeError('Backup verification failed: ' + str(backup))
        current = digest((root / item['path']).read_bytes())
        if current not in {item['original_sha256'], item.get('installed_sha256')}:
            raise RuntimeError('A game update or another mod changed ' + item['path'] +
                               '. Refusing to overwrite it. See README.md.')


def install(root, folder, scale):
    state = load_state(folder)
    if state is None:
        matches = list((root / AA / 'StandaloneWindows64').glob(PATTERN))
        if len(matches) != 1:
            raise RuntimeError('Expected exactly one default asset bundle.')
        paths = [matches[0].relative_to(root), AA / 'catalog.bin', AA / 'catalog.hash']
        state = {'files': []}
        for i, path in enumerate(paths):
            content = (root / path).read_bytes()
            backup_name = str(i) + '-' + path.name
            atomic_write(folder / 'backup' / backup_name, content)
            state['files'].append({'path': path.as_posix(), 'backup': backup_name,
                                   'original_sha256': digest(content)})
        atomic_write(folder / 'state.json', json.dumps(state, indent=2).encode())
    check_files(root, folder, state)
    originals = [(folder / 'backup' / item['backup']).read_bytes() for item in state['files']]
    bundle, catalog, token, report = build(originals[0], originals[1], scale)
    outputs = [bundle, catalog, token]
    previous = [(root / item['path']).read_bytes() for item in state['files']]
    old_state = copy.deepcopy(state)
    for item, content in zip(state['files'], outputs):
        item['installed_sha256'] = digest(content)
    state['report'] = report
    # Record expected hashes first so uninstall can recover a partial installation.
    atomic_write(folder / 'state.json', json.dumps(state, indent=2).encode())
    try:
        for item, content in zip(state['files'], outputs):
            atomic_write(root / item['path'], content)
        check_files(root, folder, state)
    except Exception:
        for item, content in zip(state['files'], previous):
            atomic_write(root / item['path'], content)
        atomic_write(folder / 'state.json', json.dumps(old_state, indent=2).encode())
        raise
    print(f'Installed: dialogue size 26 -> {26 * scale:g} ({scale:.0%} of original).')
    print(f'Also enlarged {len(report["ui_text_changes"])} labels and text fields in dialogue, text log and evidence.')
    print(f'Validated {report["verified_objects"]} objects; {report["modified_objects"]} UI objects patched.')
    print('Launch the game normally. Original files are backed up in DialogueTextMod/backup.')


def uninstall(root, folder):
    state = load_state(folder)
    if state is None:
        print('No installation to restore.')
        return
    check_files(root, folder, state)
    for item in state['files']:
        content = (folder / 'backup' / item['backup']).read_bytes()
        atomic_write(root / item['path'], content)
        if digest((root / item['path']).read_bytes()) != item['original_sha256']:
            raise RuntimeError('Restore verification failed.')
    print('Restored all three original files exactly. Backups retained.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'uninstall', 'status'])
    parser.add_argument('--scale', type=float, default=1.5)
    parser.add_argument('--game-dir', type=Path, default=HERE.parent)
    args = parser.parse_args()
    if not math.isfinite(args.scale) or not 1.1 <= args.scale <= 2.0:
        parser.error('--scale must be between 1.1 and 2.0')
    root = args.game_dir.resolve()
    folder = root / 'DialogueTextMod'
    if args.action == 'status':
        state = load_state(folder)
        if state is None:
            print('Not installed.')
        else:
            check_files(root, folder, state)
            current = [digest((root / i['path']).read_bytes()) for i in state['files']]
            if all(h == i['original_sha256'] for h, i in zip(current, state['files'])):
                print('Original files active (mod uninstalled).')
            elif all(h == i.get('installed_sha256') for h, i in zip(current, state['files'])):
                print('Mod installed. Scale:', state['report']['scale'])
            else:
                raise RuntimeError('Partial installation: run the uninstall script to restore originals.')
    else:
        game_closed()
        if args.action == 'install':
            install(root, folder, args.scale)
        else:
            uninstall(root, folder)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('ERROR:', exc, file=sys.stderr)
        sys.exit(1)
