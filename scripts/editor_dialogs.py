"""Generate numeric-dialog geometry and labels; verify original binary templates."""
import argparse
import json
from pathlib import Path
import re
import struct
from dialog_rc import parse_res

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'include/mikudancestudio/mmd_numeric_dialogs.hpp'
NAMES = [('bone', 'en', 663), ('bone', 'ja', 648),
         ('camera', 'en', 664), ('camera', 'ja', 649),
         ('screen', 'en', 653), ('screen', 'ja', 607),
         ('edge', 'en', 662), ('edge', 'ja', 645),
         ('gravity', 'en', 803), ('gravity', 'ja', 802),
         ('morph_scale', 'en', 661), ('morph_scale', 'ja', 643),
         ('camera_scale', 'en', 657), ('camera_scale', 'ja', 623),
         ('bone_scale', 'en', 660), ('bone_scale', 'ja', 636),
         ('physics_keys', 'en', 813), ('physics_keys', 'ja', 812),
         ('draw_order', 'en', 805), ('draw_order', 'ja', 804),
         ('calculation_order', 'en', 811), ('calculation_order', 'ja', 810)]
NAMES += [('ground_shadow', 'en', 658), ('ground_shadow', 'ja', 624)]
NAMES += [('center_bias', 'en', 651), ('center_bias', 'ja', 600)]
NAMES += [('accessory_order', 'en', 659), ('accessory_order', 'ja', 627)]
NAMES += [('frame_shift', 'en', 655), ('frame_shift', 'ja', 615)]
NAMES += [('blink', 'en', 656), ('blink', 'ja', 617)]
NAMES += [('english_name', 'en', 666), ('english_name', 'ja', 665)]


def source_dialogs():
    source = (ROOT / 'res/templates/dialog/dialogs.rc').read_text(encoding='utf-16')
    result = {}
    for kind, language, ident in NAMES:
        command = {'screen': 'SCREEN_SIZE', 'edge': 'EDGE_THICKNESS', 'gravity': 'GRAVITY',
                   'morph_scale': 'MORPH_MULTIPLY',
                   'camera_scale': 'CAMERA_FRAME_MULTIPLY',
                   'bone_scale': 'BONE_FRAME_MULTIPLY',
                   'physics_keys': 'PHYSICS_FRAME_CONVERSION',
                   'draw_order': 'MODEL_DRAW_ORDER',
                   'ground_shadow': 'GROUND_SHADOW_COLOR',
                   'center_bias': 'CENTER_POSITION_BIAS',
                   'accessory_order': 'ACCESSORY_ORDER',
                   'frame_shift': 'FRAME_SHIFT',
                   'blink': 'REGISTER_BLINKING',
                   'english_name': 'ENGLISH_NAMES',
                   'calculation_order': 'MODEL_CALCULATE_ORDER'}.get(kind, kind.upper() + '_NUMERIC_INPUT')
        tag = f'MDS_DLG_{command}_{"EN" if language == "en" else "JP"}'
        block = source.split(tag + ' DIALOGEX ', 1)[1].split('\n  END', 1)[0]
        geometry = list(map(int, block.splitlines()[0].split(', ')))
        style = int(re.search(r'STYLE (0x[0-9A-F]+)', block)[1], 16)
        title = re.search(r'CAPTION "([^"]*)"', block)[1]
        font = re.search(r'FONT (\d+), "([^"]*)", (\d+), (\d+), (\d+)', block)
        controls = []
        for text, id_, cls, flags, x, y, w, h, ex in re.findall(
                r'CONTROL "([^"]*)", (-?\d+), "([^"]*)", (0x[0-9A-F]+), '
                r'(-?\d+), (-?\d+), (\d+), (\d+), (0x[0-9A-F]+)', block):
            controls.append(dict(text=text, id=int(id_), kind=cls,
                                 style=int(flags, 16), ex=int(ex, 16),
                                 rect=list(map(int, (x, y, w, h)))))
        result[(kind, language)] = dict(id=ident, rect=geometry, style=style, ex=0,
                                       title=title, point=int(font[1]), font=font[2],
                                       weight=int(font[3]), italic=int(font[4]),
                                       charset=int(font[5]), controls=controls)
    return result


def reference_dialogs():
    resources = parse_res((ROOT / 'res/gen_resources.res').read_bytes())
    result = {}
    for kind, language, ident in NAMES:
        blob, = resources[(('ord', 5), ('ord', ident))]
        version, signature, help_, ex, style, count, x, y, w, h = struct.unpack_from('<HHIIIHhhhh', blob)
        assert (version, signature, help_) == (1, 0xffff, 0)
        offset = 26

        def name():
            nonlocal offset
            first, = struct.unpack_from('<H', blob, offset)
            if first == 0xffff:
                value, = struct.unpack_from('<H', blob, offset + 2)
                offset += 4
                return value
            end = offset
            while blob[end:end + 2] != b'\0\0':
                end += 2
            value = blob[offset:end].decode('utf-16-le')
            offset = end + 2
            return value

        assert name() == '' and name() == ''
        title = name()
        point, weight, italic, charset = struct.unpack_from('<HHBB', blob, offset)
        offset += 6
        font = name()
        controls = []
        for _ in range(count):
            offset = (offset + 3) & ~3
            help_, cex, flags, cx, cy, cw, ch, cid = struct.unpack_from('<IIIhhhhI', blob, offset)
            offset += 24
            assert help_ == 0
            cls = name()
            if isinstance(cls, int):
                cls = {0x80: 'Button', 0x81: 'Edit', 0x82: 'Static', 0x83: 'Listbox', 0x85: 'Combobox'}[cls]
            text = name()
            extra, = struct.unpack_from('<H', blob, offset)
            offset += 2
            assert extra == 0
            controls.append(dict(text=text, id=-1 if cid == 0xffffffff else cid,
                                 kind=cls, style=flags, ex=cex, rect=[cx, cy, cw, ch]))
        assert offset == len(blob)
        result[(kind, language)] = dict(id=ident, rect=[x, y, w, h], style=style, ex=ex,
                                       title=title, point=point, font=font, weight=weight,
                                       italic=italic, charset=charset, controls=controls)
    return result


def message(kind, index=None):
    return f'dialog.numeric.{kind}.' + ('title' if index is None else f'control.{index}')


def header(dialogs):
    out = ['// Generated by scripts/editor_dialogs.py; verified against original RT_DIALOG bytes.',
           '#pragma once', '#include <cstddef>', 'namespace mikudancestudio::ui {',
           'enum class NumericControlKind {', '    Static,', '    Edit,', '    Multiline,', '    Groupbox,', '    Button,', '    Slider,', '    Checkbox,', '    Combobox,', '    Listbox', '};',
           'struct NumericControl {', '    int id, x, y, width, height;',
           '    NumericControlKind kind;', '    const char* message;', '    unsigned style;', '};',
           'struct NumericTemplate {', '    int id, width, height, pointSize;',
           '    const char* fontFamily;',
           '    const char* title;', '    const NumericControl* controls;',
           '    std::size_t count;', '};']
    for (kind, language), dialog in dialogs.items():
        name = kind.title().replace('_', '') + language.title()
        out.append(f'inline constexpr NumericControl kNumeric{name}Controls[] = {{')
        for i, control in enumerate(dialog['controls']):
            values = ', '.join(map(str, [control['id']] + control['rect']))
            key = message(kind, i) if control['text'] else ''
            kind_name = {'msctls_trackbar32': 'Slider'}.get(control['kind'], control['kind'])
            if kind_name == 'Button' and control['style'] & 15 == 3:
                kind_name = 'Checkbox'
            if kind_name == 'Button' and control['style'] & 15 == 7:
                kind_name = 'Groupbox'
            if kind_name == 'Edit' and control['style'] & 4:
                kind_name = 'Multiline'
            out.append(f'    {{{values}, NumericControlKind::{kind_name}, "{key}", 0x{control["style"]:08x}u}},')
        out.append('};')
        font = {'\uff2d\uff33 \uff30\u30b4\u30b7\u30c3\u30af': 'MS PGothic'}.get(dialog['font'], dialog['font'])
        out.append(f'inline constexpr NumericTemplate kNumeric{name} = {{\n'
                   f'    {dialog["id"]}, {dialog["rect"][2]}, {dialog["rect"][3]}, {dialog["point"]}, '
                   f'"{font}", '
                   f'"{message(kind)}", kNumeric{name}Controls, {len(dialog["controls"])}}};')
    return '\n'.join(out + ['}', ''])


def run(check):
    source = source_dialogs()
    assert source == reference_dialogs(), 'Original numeric dialog template mismatch'
    files = {HEADER: header(source)}
    zh = {'numeric input(bone)': '\u6570\u503c\u8f93\u5165(\u9aa8\u9abc)',
          'numeric input(camera)': '\u6570\u503c\u8f93\u5165(\u76f8\u673a)',
          'position': '\u4f4d\u7f6e', 'center position': '\u4e2d\u5fc3\u4f4d\u7f6e',
          'pos X ': '\u4f4d\u7f6eX ', 'pos Y ': '\u4f4d\u7f6eY ', 'pos Z ': '\u4f4d\u7f6eZ ',
          'degree': '\u89d2\u5ea6(\u00b0)', 'axis X': 'X\u8f74', 'axis Y': 'Y\u8f74', 'axis Z': 'Z\u8f74',
          'distance': '\u8ddd\u79bb', 'OK': '\u786e\u5b9a', 'cancel': '\u53d6\u6d88',
          'frame number to shift': '\u79fb\u52a8\u5e27\u6570',
          'register blinking': '\u767b\u8bb0\u7728\u773c',
          'frame from': '\u5f00\u59cb\u5e27', 'frame to': '\u7ed3\u675f\u5e27',
          'screen size': '\u5c4f\u5e55\u5c3a\u5bf8', 'width': '\u5bbd\u5ea6', 'height': '\u9ad8\u5ea6',
          'thickness of edge line': '\u63cf\u8fb9\u7c97\u7ec6', 'thickness': '\u7c97\u7ec6',
          '0                     1                    2': '0                     1                    2',
          'gravity setting': '\u91cd\u529b\u8bbe\u7f6e', 'register': '\u767b\u8bb0', 'close': '\u5173\u95ed',
          'acceleration': '\u52a0\u901f\u5ea6', 'dir x': '\u65b9\u5411 x', 'y': 'y', 'z': 'z',
          'noize': '\u566a\u58f0', '-1                    0                    1': '-1                    0                    1',
          'multiply of facial expression ': '\u8868\u60c5\u5927\u5c0f\u4fee\u6b63',
          'degree *': '\u5927\u5c0f *', '+': '+',
          'multiply of camera frame position-angle': '\u76f8\u673a\u5173\u952e\u5e27\u4f4d\u7f6e\u89d2\u5ea6\u4fee\u6b63',
          'camera': '\u76f8\u673a\u4e2d\u5fc3', 'angle': '\u89d2\u5ea6',
          'X *': 'X *', 'Y *': 'Y *', 'Z *': 'Z *',
          'dist *': '\u8ddd\u79bb *', 'v-angle *': '\u89c6\u89d2 *',
          'multiply of bone position-angle': '\u9aa8\u9abc\u5173\u952e\u5e27\u4f4d\u7f6e\u89d2\u5ea6\u4fee\u6b63',
          'bone place': '\u9aa8\u9abc\u4f4d\u7f6e',
          'change physics ON/OFF frame': '\u7269\u7406\u5f00\u5173\u5e27\u8f6c\u6362',
          'Change all selected frame point(under physics)': '\u5c06\u6240\u9009\u7269\u7406\u9aa8\u9abc\u5173\u952e\u5e27',
          'to physics': '\u8bbe\u4e3a\u7269\u7406', '.': '\u72b6\u6001\u3002',
          'model disply order': '\u6a21\u578b\u7ed8\u5236\u987a\u5e8f',
          'model calculate order': '\u6a21\u578b\u8ba1\u7b97\u987a\u5e8f',
          'drawing order': '\u7ed8\u5236\u987a\u5e8f', 'calculate order': '\u8ba1\u7b97\u987a\u5e8f',
          'up': '\u4e0a', 'down': '\u4e0b',
          'ground shadow color': '\u5730\u9762\u9634\u5f71\u989c\u8272',
          'brightness': '\u4eae\u5ea6',
          'apply center position bias': '\u4e2d\u5fc3\u4f4d\u7f6e\u504f\u79fb',
          'accessory': '\u9644\u4ef6\u8bbe\u7f6e',
          "name(press 'Enter')": '\u540d\u79f0(\u6309Enter\u786e\u5b9a)',
          'over': '\u7b2c', 'th': '\u4e2a\u4e4b\u540e',
          'accessory described after models': '\u7684\u9644\u4ef6\u5728\u6a21\u578b\u4e4b\u540e\u7ed8\u5236',
          'If name is writen by Japanese Font,Rename it to English Font.': '\u82f1\u6587\u540d\u79f0\u7f16\u8f91',
          'Model Name': '\u6a21\u578b\u540d\u79f0', 'Model Info': '\u6a21\u578b\u8bf4\u660e',
          'Bone Name': '\u9aa8\u9abc\u540d\u79f0', 'Facial Name': '\u8868\u60c5\u540d\u79f0',
          'Division Name': '\u5206\u7ec4\u540d\u79f0', '<': '<', '>': '>',
          'X:': 'X:', 'Y:': 'Y:', 'Z:': 'Z:'}
    for language in ('en', 'ja', 'zh_CN'):
        path = ROOT / 'res/i18n' / (language + '.json')
        catalog = json.loads(path.read_text(encoding='utf-8'))
        for kind in dict.fromkeys(kind for kind, _, _ in NAMES):
            dialog = source[(kind, 'ja' if language == 'ja' else 'en')]
            labels = {message(kind): dialog['title']}
            labels.update({message(kind, i): control['text']
                           for i, control in enumerate(dialog['controls']) if control['text']})
            catalog.update({key: zh[value] if language == 'zh_CN' else value
                            for key, value in labels.items()})
        catalog['dialog.numeric.physics_keys.on'] = {'en': 'ON (X mark)', 'ja': 'ON (X\u5370)', 'zh_CN': '\u5f00 (X\u6807\u8bb0)'}[language]
        catalog['dialog.numeric.physics_keys.off'] = '\u5173' if language == 'zh_CN' else 'OFF'
        blink = {
            'en': ("You cannot register blinking because this model has not facial motion of 'blink'.",
                   'randomly register blinking', '%1 blinking is registerd', 'register blinking'),
            'ja': ('\u3053\u306e\u30e2\u30c7\u30eb\u306e\u8868\u60c5\u306b\u201c\u307e\u3070\u305f\u304d\u201c\u304c\u306a\u3044\u70ba\n\u81ea\u52d5\u30ea\u30c3\u30d7\u30b7\u30f3\u30af\u306f\u3067\u304d\u307e\u305b\u3093',
                   '\u307e\u3070\u305f\u304d\u30e9\u30f3\u30c0\u30e0\u767b\u9332',
                   '%1\u56de\u306e\u307e\u3070\u305f\u304d\u3092\u8ffd\u52a0\u3057\u307e\u3057\u305f', '\u307e\u3070\u305f\u304d\u8ffd\u52a0'),
            'zh_CN': ('\u6b64\u6a21\u578b\u6ca1\u6709\u201c\u307e\u3070\u305f\u304d\u201d\u8868\u60c5\uff0c\u65e0\u6cd5\u81ea\u52a8\u767b\u8bb0\u7728\u773c\u3002',
                      '\u968f\u673a\u767b\u8bb0\u7728\u773c', '\u5df2\u6dfb\u52a0 %1 \u6b21\u7728\u773c', '\u6dfb\u52a0\u7728\u773c')
        }[language]
        catalog.update(zip(('dialog.blink.missing', 'dialog.blink.missing_title',
                            'dialog.blink.report', 'dialog.blink.report_title'), blink))
        catalog['dialog.blink.capacity'] = {
            'en': "You cannot regist over 20000 point\nPlease execute 'delete unused frame'",
            'ja': '\u767b\u9332\u30dd\u30a4\u30f3\u30c8\u6570\u304c20000\u500b\u3092\u8d85\u3048\u307e\u3057\u305f\n\u3053\u308c\u4ee5\u4e0a\u306e\u767b\u9332\u306f\u884c\u3048\u307e\u305b\u3093\n\u300c\uff8c\uff9a\uff70\uff91\u7de8\u96c6\u300d\u306e\u300c\u4e0d\u7528\uff8c\uff9a\uff70\uff91\u524a\u9664\u300d\u3092\u5b9f\u884c\u3057\u3066\u4e0b\u3055\u3044',
            'zh_CN': '\u5173\u952e\u5e27\u6570\u91cf\u4e0d\u80fd\u8d85\u8fc7 20000\u3002\n\u8bf7\u6267\u884c\u201c\u5220\u9664\u65e0\u7528\u5e27\u201d\u3002'
        }[language]
        catalog['dialog.blink.capacity_title'] = {'en': 'register frame',
            'ja': '\uff8c\uff9a\uff70\uff91\u767b\u9332', 'zh_CN': '\u767b\u8bb0\u5173\u952e\u5e27'}[language]
        files[path] = json.dumps(catalog, ensure_ascii=False, indent=2) + '\n'
    for path, content in files.items():
        if check:
            assert path.read_text(encoding='utf-8') == content, f'Stale generated file: {path}'
        else:
            path.write_text(content, encoding='utf-8', newline='\n')
    print(f'Original numeric dialogs: {len(NAMES)} binary templates, control order/geometry and three catalogs verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    run(parser.parse_args().check)
