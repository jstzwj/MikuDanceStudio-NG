"""Exercise visible Android controls with real emulator touch coordinates.

Never inject host desktop input. ANDROID_SERIAL selects the owned emulator.
Coordinates come from Android's accessibility hierarchy, not a test-only Qt
event or an offset that could hide a broken platform coordinate conversion.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGE = 'io.github.jstzwj.mikudancestudiong'
ACTIVITY = PACKAGE + '/' + PACKAGE + '.EditorActivity'
FILES = '/data/user/0/' + PACKAGE + '/files'
REPORT = FILES + '/ui-state.json'


def adb(*args, **kwargs):
    try:
        return subprocess.run(['adb', *args], check=True, timeout=45, **kwargs)
    except subprocess.CalledProcessError as error:
        if error.stdout:
            print(error.stdout, flush=True)
        if error.stderr:
            print(error.stderr, flush=True)
        raise


def text(*args):
    return adb(*args, capture_output=True, text=True, encoding='utf-8', errors='replace').stdout


def hierarchy(output, name, ime_only=False):
    if os.environ.get('MDS_ANDROID_UI_OBSERVER') == '1':
        request = str(time.monotonic_ns())
        observer = 'io.github.jstzwj.mds.testime'
        def request_snapshot():
            adb('shell', 'am', 'broadcast', '--receiver-foreground', '-a', observer + '.DUMP', '-p', observer,
                '--es', 'request', request, '--ez', 'imeOnly', str(ime_only).lower(), capture_output=True)
        # Qt services each node property on its GUI thread. A complex private
        # scene on a CPU Vulkan driver can take several seconds per subtree;
        # keep the same real-node/nonce checks while allowing that traversal.
        deadline = time.monotonic() + 60
        next_request = 0
        while True:
            if time.monotonic() >= next_request:
                request_snapshot()
                next_request = time.monotonic() + 2
            try:
                value = text('shell', 'cat', '/data/user/0/' + observer + '/files/hierarchy.xml')
                if ET.fromstring(value).get('request') == request:
                    break
            except (subprocess.CalledProcessError, ET.ParseError):
                pass
            assert time.monotonic() < deadline, 'Android accessibility observer timed out'
            time.sleep(.2)
    else:
        adb('shell', 'uiautomator', 'dump', '/sdcard/mds-ui.xml', capture_output=True)
        value = text('shell', 'cat', '/sdcard/mds-ui.xml')
    (output / (name + '.xml')).write_text(value, encoding='utf-8')
    return ET.fromstring(value)


def bounds(node):
    return tuple(map(int, re.findall(r'-?\d+', node.attrib['bounds'])))


def find(tree, label):
    label = label.replace('\u2026', '...').replace('\ufffd', '...')
    parents = {child: parent for parent in tree.iter() for child in parent}
    for node in tree.iter('node'):
        labels = (node.get('text', ''), node.get('content-desc', ''))
        if any(value.replace('\u2026', '...').replace('\ufffd', '...') == label for value in labels) and node.get('enabled') == 'true':
            x0, y0, x1, y1 = bounds(node)
            x, y = (x0 + x1) // 2, (y0 + y1) // 2
            parent = parents.get(node)
            clipped = False
            while parent is not None:
                if parent.get('resource-id', '').endswith('.qt_scrollarea_viewport'):
                    left, top, right, bottom = bounds(parent)
                    clipped |= not (left <= x < right and top <= y < bottom)
                parent = parents.get(parent)
            if clipped:
                continue
            return node
    raise AssertionError('Visible control not found: ' + label)


def scroll_control(output, label):
    for _ in range(10):
        tree = hierarchy(output, 'scroll-' + label.replace(' ', '-'))
        try:
            return find(tree, label)
        except AssertionError:
            areas = [n for n in tree.iter('node') if
                     n.get('resource-id', '').endswith('.QStackedWidget.QScrollArea.qt_scrollarea_viewport')]
            if not areas:
                # Native focus/window replacement can briefly expose no root.
                time.sleep(.4)
                continue
            left, top, right, bottom = bounds(areas[-1])
            x = (left + right) // 2
            adb('shell', 'input', 'swipe', str(x), str(top + (bottom-top)*4//5),
                str(x), str(top + (bottom-top)//5), '500')
            time.sleep(.4)
    raise AssertionError('Control is unreachable by scrolling: ' + label)


def wait_tree(output, name, expected, ime_only=False):
    deadline = time.monotonic() + 45
    while True:
        tree = hierarchy(output, name, ime_only)
        try:
            find(tree, expected)
            return tree
        except AssertionError:
            if time.monotonic() >= deadline:
                capture(output, name + '-failure')
                raise
            time.sleep(.4)


def tap(node):
    x0, y0, x1, y1 = bounds(node)
    assert x1 > x0 and y1 > y0, node.attrib
    adb('shell', 'input', 'tap', str((x0 + x1) // 2), str((y0 + y1) // 2))
    time.sleep(.4)


def capture(output, name):
    with (output / (name + '.png')).open('wb') as image:
        adb('exec-out', 'screencap', '-p', stdout=image)


def visible_control(output, name, node):
    """Require actual light control chrome and dark text, not only a11y nodes."""
    from PIL import Image
    deadline = time.monotonic() + 25
    while True:
        capture(output, name)
        with Image.open(output / (name + '.png')) as image:
            raw = image.convert('RGB').crop(bounds(node)).tobytes()
            pixels = list(zip(raw[0::3], raw[1::3], raw[2::3]))
        neutral = [p for p in pixels if max(p) - min(p) < 16]
        light = sum(min(p) > 180 for p in neutral)
        dark = sum(max(p) < 100 for p in neutral)
        if light > len(pixels) * .55 and dark > max(8, len(pixels) * .001):
            return
        assert time.monotonic() < deadline, 'Accessible control was not visibly painted: ' + repr(node.attrib)
        time.sleep(.4)


def launch(driver, observe=False):
    adb('shell', 'am', 'force-stop', PACKAGE)
    if observe:
        adb('shell', 'rm', '-f', REPORT)
    arguments = shlex.quote('--language en --gpu-driver ' + driver +
                           (' --android-ui-report ' + REPORT if observe else ''))
    adb('shell', 'am', 'start', '-W', '-n', ACTIVITY, '--es', 'applicationArguments', arguments,
        capture_output=True)
    time.sleep(5)


def wait_state(predicate, timeout=45):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = json.loads(text('shell', 'cat', REPORT))
            if predicate(last):
                return last
        except (json.JSONDecodeError, subprocess.CalledProcessError):
            pass
        time.sleep(.3)
    raise AssertionError('Editor state did not reach expected result: ' + repr(last))


def wait_submission(after):
    # A dialog/IME can replace the native Surface and its renderer. Its GPU
    # counter restarts at zero; compare against that new baseline, then require
    # an actual increase. A reset alone must never count as a presented frame.
    previous = after['submissions']
    def advanced(state):
        nonlocal previous
        current = state['submissions']
        increased = current > previous
        previous = current
        return increased
    return wait_state(advanced)


def stable_presentation():
    first = wait_state(lambda s: s['submissions'] > 3 and s['presentationState'] == 1)
    last = wait_state(lambda s: s['submissions'] >= first['submissions'] + 5 and s['presentationState'] == 1)
    assert last['surfaceRebuilds'] <= first['surfaceRebuilds'] + 1, 'Persistent SUBOPTIMAL rebuilt the swapchain every frame'
    return last


def label(language, key):
    path = Path(__file__).resolve().parents[1] / 'translations' / ('mikudancestudio_' + language + '.ts')
    return ET.parse(path).find(".//message[@id='" + key + "']/translation").text.replace('&', '')


def file_menu(output, language='en'):
    tree = wait_tree(output, 'editor', label(language, 'menu.file'))
    tap(find(tree, label(language, 'menu.file')))
    tree = wait_tree(output, 'file-menu', label(language, 'preferences.menu'))
    visible_control(output, 'visible-file-menu', find(tree, label(language, 'preferences.menu')))
    return tree


def choose_file(output, path):
    tree = wait_tree(output, 'file-dialog', 'File name:')
    edits = [n for n in tree.iter('node') if 'fileNameEdit' in n.get('resource-id', '')]
    assert edits, 'File dialog has no accessible file name editor'
    # Qt exposes the directory editor above and the file name below the list.
    tap(max(edits, key=lambda n: bounds(n)[1]))
    wait_state(lambda s: s['focus'] == 'fileNameEdit')
    if os.environ.get('MDS_ANDROID_UI_OBSERVER') == '1':
        wait_state(lambda s: s['imeVisible'])
        helper = 'io.github.jstzwj.mds.testime'
        adb('shell', 'am', 'broadcast', '--receiver-foreground', '-a', helper + '.PREPARE', '-p', helper,
            '--es', 'text', shlex.quote(path), capture_output=True)
        keyboard = wait_tree(output, 'path-keyboard', 'Path', ime_only=True)
        capture(output, 'path-keyboard')
        tap(find(keyboard, 'Path'))
        capture(output, 'path-committed')
    else:
        adb('shell', 'input', 'keycombination', '113', '29')  # Ctrl+A
        adb('shell', 'input', 'text', path)
    wait_state(lambda s: s['focusText'] == path)
    # Open must remain reachable with the keyboard up. Do not dismiss the IME
    # and then touch coordinates captured during its closing resize animation.
    tree = wait_tree(output, 'file-selected', 'Open')
    tap(find(tree, 'Open'))


def language_controls(output):
    initial = wait_state(lambda s: s['models'] > 0)
    current = 'en'
    names = {'en': 'English', 'ja': '\u65e5\u672c\u8a9e', 'zh_CN': '\u7b80\u4f53\u4e2d\u6587'}
    for code in ('zh_CN', 'ja', 'en'):
        tap(find(file_menu(output, current), label(current, 'preferences.menu')))
        native = names[current]
        dialog = wait_tree(output, 'language-' + current, native)
        visible_control(output, 'visible-language-' + current, find(dialog, native))
        visible_control(output, 'visible-apply-' + current, find(dialog, label(current, 'preferences.apply')))
        tap(find(dialog, native))
        choices = wait_tree(output, 'language-choices', names[code])
        visible_control(output, 'visible-choice-' + code, find(choices, names[code]))
        tap(find(choices, names[code]))
        dialog = wait_tree(output, 'language-selected', label(current, 'preferences.apply'))
        tap(find(dialog, label(current, 'preferences.apply')))
        state = wait_state(lambda s: s['language'] == code)
        assert state['models'] == initial['models'] and not state['error'], state
        dialog = wait_tree(output, 'language-applied-' + code, label(code, 'preferences.cancel'))
        capture(output, 'language-' + code)
        tap(find(dialog, label(code, 'preferences.cancel')))
        current = code
        wait_tree(output, 'language-closed-' + code, label(code, 'menu.file'))


def phone_controls(output, project, model, ime):
    print('Phone: camera touch drag', flush=True)
    tree = wait_tree(output, 'camera-start', 'Scene viewport')
    x0, y0, x1, y1 = bounds(find(tree, 'Scene viewport'))
    initial_camera = wait_state(lambda s: s['submissions'] > 0)
    adb('shell', 'input', 'swipe', str((x0+x1)//2), str((y0+y1)//2),
        str((x0+x1)//2+150), str((y0+y1)//2+50), '700')
    wait_state(lambda s: s['cameraX'] != initial_camera['cameraX'] or s['cameraY'] != initial_camera['cameraY'])
    capture(output, 'camera-drag')
    print('Phone: import PMM through File / Open', flush=True)
    tap(find(file_menu(output), 'open(O)'))
    # The preceding camera edit is a real document edit. Exercise the ordinary
    # discard confirmation instead of suppressing it with an editor test mode.
    tap(find(wait_tree(output, 'discard-camera', 'DISCARD'), 'DISCARD'))
    choose_file(output, project)
    initial = wait_state(lambda s: s['models'] > 0 and s['submissions'] > 0, 90)
    assert not initial['error'], initial
    wait_submission(initial)
    capture(output, 'import-pmm')
    print('Phone: switch languages with a loaded scene', flush=True)
    language_controls(output)
    tree = wait_tree(output, 'phone', 'Timeline')
    tap(find(tree, 'Timeline'))
    print('Phone: timeline touch drag', flush=True)
    capture(output, 'phone-timeline')
    # The timeline is custom-painted; its logical sidebar grid starts at x=91.
    # The enclosing accessibility bounds provide actual physical origin/scale.
    tree = wait_tree(output, 'phone-timeline', 'Timeline grid')
    timelines = [n for n in tree.iter('node') if n.get('content-desc') == 'Timeline grid']
    assert timelines, 'Timeline accessibility bounds are missing'
    x0, y0, x1, y1 = bounds(timelines[0])
    scale = (x1 - x0) / 295
    start = (round(x0 + 150 * scale), round(y0 + 8 * scale))
    end = (round(x0 + 220 * scale), start[1])
    adb('shell', 'input', 'swipe', *map(str, (*start, *end, 500)))
    wait_state(lambda s: s['frame'] != initial['frame'])
    tap(find(tree, 'Timeline'))
    tap(find(wait_tree(output, 'phone', 'Controls'), 'Controls'))
    tap(scroll_control(output, 'model manipulation'))
    tap(scroll_control(output, 'load'))
    print('Phone: import PMX through model controls', flush=True)
    choose_file(output, model)
    state = wait_state(lambda s: s['models'] == initial['models'] + 1, 90)
    assert not state['error'], state
    wait_submission(state)
    capture(output, 'import-pmx')
    tap(find(wait_tree(output, 'phone', 'Controls'), 'Controls'))
    if ime:
        print('Phone: native IME composition and Unicode', flush=True)
        ime_controls(output, ime)
    final = wait_state(lambda s: s['viewportWidth'] > 0 and s['viewportHeight'] > 0)
    return wait_submission(final)


def ime_controls(output, ime):
    keyboard = 'io.github.jstzwj.mds.testime/.TestKeyboard'
    previous = text('shell', 'settings', 'get', 'secure', 'default_input_method').strip()
    try:
        adb('shell', 'ime', 'enable', keyboard)
        adb('shell', 'ime', 'set', keyboard)
        # Save As can be cancelled without a dirty-document discard prompt.
        tap(find(file_menu(output), 'save as(A)'))
        tree = wait_tree(output, 'ime-file-dialog', 'File name:')
        edits = [n for n in tree.iter('node') if 'fileNameEdit' in n.get('resource-id', '')]
        tap(max(edits, key=lambda n: bounds(n)[1]))
        wait_state(lambda s: s['imeVisible'] and s['focus'] == 'fileNameEdit')
        for action, expected in [('Clear', ''), ('Compose', ''), ('Japanese', '\u65e5\u672c\u8a9e'),
                                 ('Chinese', '\u65e5\u672c\u8a9e\u4e2d\u6587'),
                                 ('Unicode', '\u65e5\u672c\u8a9e\u4e2d\u6587e\u0301\U0001f600')]:
            tap(find(hierarchy(output, 'ime-' + action, ime_only=True), action))
            state = wait_state(lambda s: s['focusText'] == expected and s['imeVisible'])
            (output / ('ime-' + action + '.json')).write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
        capture(output, 'ime-unicode')
        tap(find(hierarchy(output, 'ime-cancel'), 'Cancel'))
    finally:
        adb('shell', 'ime', 'set', previous)


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
    menu = wait_tree(output, 'file-menu', 'Preferences...')
    tap(find(menu, 'Preferences...'))
    # v0.0.1 selects Exit instead. A live process alone is insufficient:
    # require the actual language and dialog buttons after the physical tap.
    dialog = wait_tree(output, 'preferences', 'English')
    find(dialog, 'English')
    find(dialog, 'Apply')
    visible_control(output, 'preferences', find(dialog, 'English'))
    visible_control(output, 'preferences-apply', find(dialog, 'Apply'))
    tap(find(dialog, 'Cancel'))
    editor = wait_tree(output, 'preferences-closed', 'Go')
    tap(find(editor, 'Go'))
    wait_state(lambda s: any(e.get('text') == 'Go' and e.get('type') == 3 for e in s['input']))
    assert text('shell', 'pidof', PACKAGE).strip(), 'Menu/button touch terminated the editor'
    capture(output, 'editor')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('out/android-ui'))
    parser.add_argument('--driver', choices=('hardware', 'software'), default='hardware')
    parser.add_argument('--project', default=FILES + '/fixtures/relocated.pmm')
    parser.add_argument('--model', default=FILES + '/fixtures/triangle.pmx')
    parser.add_argument('--ime', type=Path)
    parser.add_argument('--phone', action='store_true')
    parser.add_argument('--phone-only', action='store_true', help='Local focused rerun; full Release gate also tests tablet')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    assert text('shell', 'getprop', 'ro.kernel.qemu').strip() == '1', 'Use an owned emulator'
    original_size = text('shell', 'wm', 'size')
    original_density = text('shell', 'wm', 'density')
    services = text('shell', 'settings', 'get', 'secure', 'enabled_accessibility_services').strip()
    enabled = text('shell', 'settings', 'get', 'secure', 'accessibility_enabled').strip()
    previous_ime = text('shell', 'settings', 'get', 'secure', 'default_input_method').strip()
    try:
        if args.ime:
            adb('install', '-r', str(args.ime))
            # Fresh API 35 images can deny sideloaded accessibility services
            # even when the secure enabled-service list is set. Authorize only
            # this read-only helper on the owned emulator, never the product.
            for operation in ('ACCESS_RESTRICTED_SETTINGS', 'BIND_ACCESSIBILITY_SERVICE'):
                adb('shell', 'cmd', 'appops', 'set', 'io.github.jstzwj.mds.testime', operation, 'allow')
            adb('shell', 'ime', 'enable', 'io.github.jstzwj.mds.testime/.TestKeyboard')
            adb('shell', 'ime', 'set', 'io.github.jstzwj.mds.testime/.TestKeyboard')
            observer = 'io.github.jstzwj.mds.testime/.UiObserver'
            # Android caches the installed accessibility-service list, including
            # an AppOps rejection from package installation. A component change
            # asks the framework to discover this helper after authorization.
            adb('shell', 'pm', 'disable', '--user', '0', observer, capture_output=True)
            adb('shell', 'pm', 'enable', '--user', '0', observer, capture_output=True)
            combined = observer if services in ('null', '') else services + ':' + observer
            adb('shell', 'settings', 'put', 'secure', 'enabled_accessibility_services', combined)
            adb('shell', 'settings', 'put', 'secure', 'accessibility_enabled', '1')
            # Package-added policy evaluation is asynchronous and may clear
            # the enabled list after install. Wait for the actual framework
            # binding, reapplying only if that list was cleared in the race.
            deadline = time.monotonic() + 30
            while not any('Bound services:' in line and 'label=NG emulator test keyboard,' in line
                          for line in text('shell', 'dumpsys', 'accessibility').splitlines()):
                assert time.monotonic() < deadline, 'Android did not bind the read-only observer'
                if text('shell', 'settings', 'get', 'secure', 'enabled_accessibility_services').strip() != combined:
                    adb('shell', 'settings', 'put', 'secure', 'enabled_accessibility_services', combined)
                    adb('shell', 'settings', 'put', 'secure', 'accessibility_enabled', '1')
                time.sleep(.5)
            os.environ['MDS_ANDROID_UI_OBSERVER'] = '1'
            hierarchy(args.output, 'observer-ready', ime_only=True)
        result = {}
        if not args.phone_only:
            print('Tablet: real menu, preferences and button touches', flush=True)
            adb('shell', 'am', 'force-stop', PACKAGE)
            adb('shell', 'wm', 'size', '1280x800')
            adb('shell', 'wm', 'density', '160')
            time.sleep(3)
            launch(args.driver, observe=True)
            wait_state(lambda s: s['submissions'] > 3 and s['viewportWidth'] > 0)
            stable_presentation()
            touch_controls(args.output)
            result['tabletTouch'] = 'passed'
        if args.phone or args.phone_only:
            adb('shell', 'am', 'force-stop', PACKAGE)
            adb('shell', 'wm', 'size', '1080x2400')
            adb('shell', 'wm', 'density', '420')
            time.sleep(3)
            launch(args.driver, observe=True)
            wait_state(lambda s: s['submissions'] > 3)
            stable_presentation()
            result['phone'] = phone_controls(args.output, args.project, args.model, args.ime)
        (args.output / 'ui.json').write_text(json.dumps(result, indent=2) + '\n')
    except Exception:
        if args.ime:
            (args.output / 'observer-accessibility.txt').write_text(
                text('shell', 'dumpsys', 'accessibility'), encoding='utf-8')
            (args.output / 'observer-appops.txt').write_text(
                text('shell', 'cmd', 'appops', 'get', 'io.github.jstzwj.mds.testime'), encoding='utf-8')
        capture(args.output, 'failure')
        try:
            (args.output / 'failure-state.json').write_text(text('shell', 'cat', REPORT), encoding='utf-8')
        except subprocess.CalledProcessError:
            pass
        raise
    finally:
        adb('shell', 'am', 'force-stop', PACKAGE)
        if args.ime:
            adb('shell', 'ime', 'set', previous_ime)
            adb('shell', 'ime', 'disable', 'io.github.jstzwj.mds.testime/.TestKeyboard')
            adb('shell', 'settings', 'put', 'secure', 'enabled_accessibility_services', services)
            adb('shell', 'settings', 'put', 'secure', 'accessibility_enabled', enabled)
            for operation in ('ACCESS_RESTRICTED_SETTINGS', 'BIND_ACCESSIBILITY_SERVICE'):
                adb('shell', 'cmd', 'appops', 'set', 'io.github.jstzwj.mds.testime', operation, 'default')
            os.environ.pop('MDS_ANDROID_UI_OBSERVER', None)
        for command, value in [('size', original_size), ('density', original_density)]:
            override = re.search(r'Override (?:size|density):\s*(\S+)', value)
            adb('shell', 'wm', command, override.group(1) if override else 'reset')


if __name__ == '__main__':
    main()
