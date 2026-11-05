"""Exercise visible Android controls with real emulator touch coordinates.

Never inject host desktop input. ANDROID_SERIAL selects the owned emulator.
Coordinates come from Android's accessibility hierarchy, not a test-only Qt
event or an offset that could hide a broken platform coordinate conversion.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGE = 'io.github.jstzwj.mikudancestudiong'
ACTIVITY = PACKAGE + '/' + PACKAGE + '.EditorActivity'


def adb(*args, **kwargs):
    return subprocess.run(['adb', *args], check=True, timeout=45, **kwargs)


def text(*args):
    return adb(*args, capture_output=True, text=True, encoding='utf-8', errors='replace').stdout


def hierarchy(output, name):
    adb('shell', 'uiautomator', 'dump', '/sdcard/mds-ui.xml', capture_output=True)
    value = text('shell', 'cat', '/sdcard/mds-ui.xml')
    (output / (name + '.xml')).write_text(value, encoding='utf-8')
    return ET.fromstring(value)


def bounds(node):
    return tuple(map(int, re.findall(r'-?\d+', node.attrib['bounds'])))


def find(tree, label):
    for node in tree.iter('node'):
        labels = (node.get('text', ''), node.get('content-desc', ''))
        if any(value.replace('\u2026', '...').replace('\ufffd', '...') == label for value in labels) and node.get('enabled') == 'true':
            return node
    raise AssertionError('Visible control not found: ' + label)


def tap(node):
    x0, y0, x1, y1 = bounds(node)
    assert x1 > x0 and y1 > y0, node.attrib
    adb('shell', 'input', 'tap', str((x0 + x1) // 2), str((y0 + y1) // 2))
    time.sleep(.4)


def capture(output, name):
    with (output / (name + '.png')).open('wb') as image:
        adb('exec-out', 'screencap', '-p', stdout=image)


def launch(driver):
    adb('shell', 'am', 'force-stop', PACKAGE)
    arguments = shlex.quote('--language en --gpu-driver ' + driver)
    adb('shell', 'am', 'start', '-W', '-n', ACTIVITY, '--es', 'applicationArguments', arguments,
        capture_output=True)
    time.sleep(5)


def touch_controls(output):
    deadline = time.monotonic() + 35
    while True:
        tree = hierarchy(output, 'editor')
        try:
            find(tree, 'file(F)')
            break
        except AssertionError:
            if time.monotonic() >= deadline:
                capture(output, 'startup-failure')
                raise
            time.sleep(1)
    tap(find(tree, 'file(F)'))
    menu = hierarchy(output, 'file-menu')
    tap(find(menu, 'Preferences...'))
    # v0.0.1 selects Exit instead. A live process alone is insufficient:
    # require the actual language and dialog buttons after the physical tap.
    dialog = hierarchy(output, 'preferences')
    find(dialog, 'English')
    find(dialog, 'Apply')
    capture(output, 'preferences')
    tap(find(dialog, 'Cancel'))
    editor = hierarchy(output, 'preferences-closed')
    tap(find(editor, 'Go'))
    assert text('shell', 'pidof', PACKAGE).strip(), 'Menu/button touch terminated the editor'
    capture(output, 'editor')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('out/android-ui'))
    parser.add_argument('--driver', choices=('hardware', 'software'), default='hardware')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    assert text('shell', 'getprop', 'ro.kernel.qemu').strip() == '1', 'Use an owned emulator'
    original_size = text('shell', 'wm', 'size')
    original_density = text('shell', 'wm', 'density')
    try:
        adb('shell', 'wm', 'size', '1280x800')
        adb('shell', 'wm', 'density', '160')
        time.sleep(3)
        launch(args.driver)
        touch_controls(args.output)
        (args.output / 'ui.json').write_text(json.dumps({'touch': 'passed'}, indent=2) + '\n')
    finally:
        for command, value in [('size', original_size), ('density', original_density)]:
            override = re.search(r'Override (?:size|density):\s*(\S+)', value)
            adb('shell', 'wm', command, override.group(1) if override else 'reset')


if __name__ == '__main__':
    main()
