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
PLAYER_DATA = Path('Tyrion1_Data/data.unity3d')
# Inspected Steam build 25968594. PlayerSettings has no embedded type tree;
# pin both the container and object before changing its runInBackground byte.
PLAYER_DATA_SHA256 = 'adf2c92b09edb41471cef1c93fd3d436615432690e2085ad690c7fc27edb7ec5'
PLAYER_SETTINGS_SHA256 = '10b1c6bfd36503544bd3e73b9700c28650d12e396cdb3d8bb097c9a649ce596c'
RUN_IN_BACKGROUND_OFFSET = 406
PATTERN = 'defaultlocalgroup_assets_default_*.bundle'
PREFABS = ('Assets/Prefabs/GUI/Dialogue Boxes/Dialogue Box.prefab',
           'Assets/Prefabs/GUI/Dialogue Boxes/Narrator Box Variant.prefab')
# UI coordinates use a nominal 1080p canvas: 15 units = 20 pixels at 1440p.
TOOLBAR_OFFSET_Y = 20.0 * 1080.0 / 1440.0
DIALOGUE_BACKGROUND_OPACITY = 0.99
# Leave 2.5% of the screen on each side of the normal dialogue panel.
DIALOGUE_SIDE_MARGIN = 0.025
# Clean files inspected in Steam build 25968594. Automatic backup migration is
# restricted to this exact bundle/catalog/hash combination, not just its Unity
# version: a partial update or another mod must never become our new baseline.
VERIFIED_UPDATES = {
    ('80d021717fbb258b6939b02fc87fbd0629a8536db0849a6f71dd9ec27d3feda8',
     'a79ded5b8ae6890b91d4c277dfbe424c7277b24b452f4eaf0fe10ef26e1aa9e4',
     '972423fe261f4d4f7044f0b0962d1f7ea52dd33375520fbe784080ac46f0ac91'): '25968594',
}


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


def expand_ui(assets, scale, modified, opacity=DIALOGUE_BACKGROUND_OPACITY):
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

    def component_id(p, field):
        matches = [c['component']['m_PathID'] for c in data[paths[p]]['m_Component']
                   if field in data.get(c['component']['m_PathID'], {})]
        if len(matches) != 1:
            raise RuntimeError(f'Expected one {field} component at {p}.')
        return matches[0]

    def component(p, field):
        return change(component_id(p, field))

    def add_component(p, template, field):
        # Reuse an inspected component type and script reference. Allocate stable
        # IDs from the original inventory so repeated installation is identical.
        source = component_id(template, field)
        i = 1
        while i in assets.objects:
            i += 1
        obj = copy.copy(assets.objects[source])
        obj.path_id = i
        obj.data = assets.objects[source].get_raw_data()
        assets.objects[i] = obj
        modified[i] = copy.deepcopy(data[source])
        modified[i]['m_GameObject']['m_PathID'] = paths[p]
        change(paths[p])['m_Component'].append({'component': {'m_FileID': 0, 'm_PathID': i}})
        return modified[i]

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
        component(base, 'm_Texture')['m_Color']['a'] = opacity
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

    # Both obtained and updated evidence use this popup, with the controller
    # replacing the localized heading. Use a fixed-size icon beside a vertical
    # text column: item name, description, then any visible metadata rows.
    popup = 'Add Item Popup/Window'
    panel = rect(popup)
    panel['m_AnchorMin']['x'] = 0.075
    panel['m_AnchorMax']['x'] = 0.925
    header = popup + '/Header'
    text(header)
    heading = component(header, 'm_fontSize')
    # The heading starts large. Allow long translations to fit the wider panel
    # without shrinking below its original size or extending above the screen.
    heading['m_enableAutoSizing'] = True
    heading['m_fontSizeMin'] = heading['m_fontSize'] / scale
    rect(header)['m_SizeDelta']['y'] = min(100.0 * scale, 175.0)
    text_changes[-1]['auto_size'] = True

    info = popup + '/Top/Item Info'
    text(info + '/Item Name', keep_minimum=True)
    rect(info + '/Item Name')['m_SizeDelta']['y'] *= scale
    fields = info + '/Item Fields'
    # Originally the fields occupy only 66% of the area beside the icon.
    rect(fields)['m_AnchorMax']['x'] = 1.0
    for suffix in ('', ' (1)', ' (2)', ' (3)', ' (4)'):
        row = fields + '/Item Field' + suffix
        rect(row)['m_SizeDelta']['y'] *= scale
        for label in ('Item Field Names', 'Item Field Values'):
            text(row + '/' + label, keep_minimum=True)
    description = popup + '/Description'
    text(description)
    fitter = component(description, 'm_VerticalFit')
    if fitter['m_VerticalFit'] != 2:
        raise RuntimeError('Evidence popup must retain preferred-height fitting.')
    # The parent now uses TMP's preferred height directly; a second height
    # controller on the description would fight the layout group.
    fitter['m_Enabled'] = 0
    description_id = transforms[paths[description]]
    children = rect(popup)['m_Children']
    children.remove({'m_FileID': 0, 'm_PathID': description_id})
    rect(info)['m_Children'].insert(1, {'m_FileID': 0, 'm_PathID': description_id})
    rect(description)['m_Father']['m_PathID'] = transforms[paths[info]]
    rect(description)['m_Pivot']['y'] = 1.0

    column = add_component(info, fields, 'm_Spacing')
    column['m_Spacing'] = 15.0
    column['m_ChildAlignment'] = 0
    column['m_ChildControlHeight'] = 1
    row = add_component(popup + '/Top', 'Dialogue Box/Container/Buttons', 'm_Spacing')
    row['m_Spacing'] = 30.0
    row['m_ChildAlignment'] = 0
    row['m_ChildControlWidth'] = row['m_ChildControlHeight'] = 1
    row['m_ChildForceExpandWidth'] = row['m_ChildForceExpandHeight'] = 0
    icon = add_component(popup + '/Top/Icon', header, 'm_IgnoreLayout')
    icon['m_IgnoreLayout'] = 0
    icon['m_MinWidth'] = icon['m_PreferredWidth'] = 270.0
    icon['m_MinHeight'] = icon['m_PreferredHeight'] = 270.0
    icon['m_FlexibleWidth'] = icon['m_FlexibleHeight'] = 0.0
    component(popup, 'm_Spacing')['m_ChildControlHeight'] = 1
    component(popup, 'm_VerticalFit')['m_VerticalFit'] = 2
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


def build(original, catalog, scale, opacity=DIALOGUE_BACKGROUND_OPACITY):
    import UnityPy
    from UnityPy.streams import EndianBinaryReader
    if UnityPy.__version__ != '1.25.4':
        raise RuntimeError('This patch requires UnityPy 1.25.4.')
    env = UnityPy.load(original)
    bundle = env.file
    assets = env.assets[0]
    if assets.unity_version != '6000.5.8f1':
        raise RuntimeError('Unrecognized Unity version; refusing to patch.')
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

    ui_changes = expand_ui(assets, scale, modified, opacity)
    # Write each staged object once, even when multiple layout adjustments touch it.
    for i, data in modified.items():
        obj = assets.objects[i]
        obj.save_typetree(data)
    # Reparenting the description and adding layout components changes object
    # lengths. Rebuild offsets, then verify every untouched object's raw bytes.
    replacement = EndianBinaryReader(assets.save())
    replacement.flags = assets.flags
    bundle.files[assets.name] = replacement
    patched = bundle.save(packer='lz4')
    check = UnityPy.load(patched)
    after = check.assets[0]
    if set(after.objects) != set(assets.objects):
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
    report = {'revision': 11, 'scale': scale, 'opacity': opacity,
              'changes': changes, 'ui_text_changes': ui_changes,
              'verified_objects': len(after.objects), 'added_objects': len(after.objects) - len(originals),
              'modified_objects': len(modified), 'original_crc': old_crc, 'patched_crc': new_crc,
              'catalog_crc_offset': offset}
    return patched, patched_catalog, hash_token, report


def load_state(folder):
    path = folder / 'state.json'
    return json.loads(path.read_text()) if path.exists() else None


def build_focus_patch(original):
    """Disable background updates, preserving every other uncompressed byte."""
    import UnityPy
    from UnityPy.streams import EndianBinaryReader
    if UnityPy.__version__ != '1.25.4':
        raise RuntimeError('This patch requires UnityPy 1.25.4.')
    if digest(original) != PLAYER_DATA_SHA256:
        raise RuntimeError('Unrecognized player data; refusing to patch background input.')
    env = UnityPy.load(original)
    assets = next(a for a in env.assets if a.name == 'globalgamemanagers')
    obj = assets.objects[1]
    raw = obj.get_raw_data()
    if (assets.unity_version != '6000.5.8f1' or obj.type.name != 'PlayerSettings'
            or digest(raw) != PLAYER_SETTINGS_SHA256 or raw[RUN_IN_BACKGROUND_OFFSET] != 1):
        raise RuntimeError('Unexpected PlayerSettings layout; refusing to patch.')
    # UnityPy's fallback type tree omits four trailing bytes in this build.
    # Change only the inspected boolean directly, retaining that unknown tail,
    # all object offsets, metadata, and the other files in data.unity3d.
    before = assets.reader.bytes
    offset = obj.byte_start + RUN_IN_BACKGROUND_OFFSET
    changed = before[:offset] + b'\x00' + before[offset + 1:]
    # Avoid reserializing stripped assets with fallback type trees.
    for name, item in list(env.file.files.items()):
        content = item.reader.bytes if hasattr(item, 'reader') else item.bytes
        replacement = EndianBinaryReader(changed if name == assets.name else content)
        replacement.flags = item.flags
        env.file.files[name] = replacement
    patched = env.file.save(packer='lz4')
    check = UnityPy.load(patched)
    if set(env.file.files) != set(check.file.files):
        raise RuntimeError('Player data file inventory changed.')
    for name, item in check.file.files.items():
        actual = item.reader.bytes if hasattr(item, 'reader') else item.bytes
        source = env.file.files[name]
        expected = source.reader.bytes if hasattr(source, 'reader') else source.bytes
        if actual != expected:
            raise RuntimeError('Player data verification failed: ' + name)
    return patched


def include_player_data(root, state, originals):
    """Extend old three-file installations without replacing their backups."""
    paths = [item['path'] for item in state['files']]
    if PLAYER_DATA.as_posix() not in paths:
        original = (root / PLAYER_DATA).read_bytes()
        if digest(original) != PLAYER_DATA_SHA256:
            raise RuntimeError('Unrecognized player data; refusing to patch background input.')
        state['files'].append({'path': PLAYER_DATA.as_posix(),
                               'backup': PLAYER_DATA_SHA256 + '/3-data.unity3d',
                               'original_sha256': PLAYER_DATA_SHA256})
        originals.append(original)
    return state, originals


def check_backups(folder, state):
    for item in state['files']:
        backup = folder / 'backup' / item['backup']
        if digest(backup.read_bytes()) != item['original_sha256']:
            raise RuntimeError('Backup verification failed: ' + str(backup))


def check_files(root, folder, state):
    check_backups(folder, state)
    for item in state['files']:
        path = root / item['path']
        if not path.is_file():
            raise RuntimeError('A game update removed ' + item['path'] +
                               '. Run install for a supported update. See README.md.')
        current = digest(path.read_bytes())
        if current not in {item['original_sha256'], item.get('installed_sha256')}:
            raise RuntimeError('A game update or another mod changed ' + item['path'] +
                               '. Refusing to overwrite it. See README.md.')


def prepare_install(root, folder):
    """Select originals without writing anything; also used by Windows preflight."""
    previous_state = load_state(folder)
    if previous_state is not None:
        # A broken backup is a separate problem, never evidence of a game update.
        check_backups(folder, previous_state)
        try:
            check_files(root, folder, previous_state)
        except RuntimeError:
            pass
        else:
            originals = [(folder / 'backup' / item['backup']).read_bytes()
                         for item in previous_state['files']]
            state, originals = include_player_data(root, copy.deepcopy(previous_state), originals)
            return state, originals, previous_state

    matches = list((root / AA / 'StandaloneWindows64').glob(PATTERN))
    if len(matches) != 1:
        raise RuntimeError('Expected exactly one default asset bundle. See README.md.')
    paths = [matches[0].relative_to(root), AA / 'catalog.bin', AA / 'catalog.hash']
    originals = [(root / path).read_bytes() for path in paths]
    hashes = tuple(digest(content) for content in originals)
    if previous_state is not None and hashes not in VERIFIED_UPDATES:
        raise RuntimeError('Game files differ from the previous installation and do not match '
                           'a verified clean update. Refusing to overwrite them. See README.md.')
    # Separate generations so an update never replaces the previous originals.
    state = {'files': []}
    for i, (path, checksum) in enumerate(zip(paths, hashes)):
        state['files'].append({'path': path.as_posix(),
                               'backup': hashes[0] + '/' + str(i) + '-' + path.name,
                               'original_sha256': checksum})
    if hashes in VERIFIED_UPDATES:
        state['game_build'] = VERIFIED_UPDATES[hashes]
    state, originals = include_player_data(root, state, originals)
    return state, originals, previous_state


def install(root, folder, scale, opacity=DIALOGUE_BACKGROUND_OPACITY):
    if not math.isfinite(opacity) or not 0.0 <= opacity <= 1.0:
        raise ValueError('--opacity must be between 0.0 and 1.0')
    state, originals, previous_state = prepare_install(root, folder)
    bundle, catalog, token, report = build(originals[0], originals[1], scale, opacity)
    outputs = [bundle, catalog, token, build_focus_patch(originals[3])]
    report['revision'] = 12
    report['run_in_background'] = False
    previous = [(root / item['path']).read_bytes() for item in state['files']]
    # Build and validate first. Only then persist the new backup and state.
    for item, content in zip(state['files'], originals):
        backup = folder / 'backup' / item['backup']
        if not backup.exists():
            atomic_write(backup, content)
        elif digest(backup.read_bytes()) != item['original_sha256']:
            raise RuntimeError('Backup verification failed: ' + str(backup))
    if previous_state is not None and state['files'] != previous_state['files']:
        archived = json.dumps(previous_state, indent=2).encode()
        atomic_write(folder / 'backup' / ('state-' + digest(archived) + '.json'), archived)
        print('Installation upgraded. Previous backups and state retained in backup/.')
    old_state = previous_state if previous_state is not None else copy.deepcopy(state)
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
    print(f'Dialogue box background opacity: {opacity:g} (0 = transparent, 1 = opaque).')
    print('Background input disabled: the game pauses while it is out of focus.')
    print(f'Also enlarged {len(report["ui_text_changes"])} labels and text fields in dialogue, text log and evidence.')
    print(f'Validated {report["verified_objects"]} objects; {report["modified_objects"]} UI objects patched.')
    print('Launch the game normally. Original files are backed up in TyrionCuthbertLargerTextMod/backup.')


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
    print(f'Restored all {len(state["files"])} original files exactly. Backups retained.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'uninstall', 'status'])
    parser.add_argument('--scale', type=float, default=1.5)
    parser.add_argument('--opacity', type=float, default=DIALOGUE_BACKGROUND_OPACITY,
                        help='Dialogue/narrator background opacity: 0 (transparent) to 1 (opaque); default: 0.99.')
    parser.add_argument('--game-dir', type=Path, default=HERE.parent)
    args = parser.parse_args()
    if not math.isfinite(args.scale) or not 1.1 <= args.scale <= 2.0:
        parser.error('--scale must be between 1.1 and 2.0')
    if not math.isfinite(args.opacity) or not 0.0 <= args.opacity <= 1.0:
        parser.error('--opacity must be between 0.0 and 1.0')
    root = args.game_dir.resolve()
    folder = root / 'TyrionCuthbertLargerTextMod'
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
                print('Dialogue box background opacity:', state['report'].get('opacity', 'not recorded'))
                print('Pause when unfocused:', state['report'].get('run_in_background') is False)
            else:
                raise RuntimeError('Partial installation: run the uninstall script to restore originals.')
    else:
        game_closed()
        if args.action == 'install':
            install(root, folder, args.scale, args.opacity)
        else:
            uninstall(root, folder)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('ERROR:', exc, file=sys.stderr)
        sys.exit(1)
