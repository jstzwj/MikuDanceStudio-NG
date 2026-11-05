"""Qualify actual Vulkan screen orientation and lifecycle on an owned emulator."""
import argparse
import json
from pathlib import Path
import re
import time
from PIL import Image
from test_android_ui import adb, text, launch, capture, PACKAGE, ACTIVITY


def orientation(path):
    image = Image.open(path).convert('RGB')
    w, h = image.size
    green_columns, green_rows = [0] * w, [0] * h
    red_columns, red_rows = [0] * w, [0] * h
    # MSAA can cover only one of four samples on a one-pixel world axis.
    # Identify its hue after resolve (e.g. red becomes 255/191/191), while
    # retaining the long-line/direction checks that reject rotated frames.
    pixels = image.get_flattened_data() if hasattr(image, 'get_flattened_data') else image.getdata()
    for index, (r, g, b) in enumerate(pixels):
        x, y = index % w, index // w
        if g > 200 and g - r > 20 and g - b > 20:
            green_columns[x] += 1
            green_rows[y] += 1
        if r > 200 and r - g > 20 and r - b > 20:
            red_columns[x] += 1
            red_rows[y] += 1
    vertical, horizontal = max(green_columns), max(red_rows)
    assert vertical > 60 and vertical > max(green_rows) * 3, 'Green world axis is not vertical'
    assert horizontal > 60 and horizontal > max(red_columns) * 3, 'Red world axis is not horizontal'
    x = green_columns.index(vertical)
    red_y = red_rows.index(horizontal)
    upper = sum(1 for y in range(red_y) if (lambda c: c[1] > 200 and c[1] - c[0] > 20 and c[1] - c[2] > 20)(image.getpixel((x, y))))
    assert upper > vertical * .9, 'World axes are upside down'
    return {'greenVerticalPixels': vertical, 'redHorizontalPixels': horizontal}


def screenshot(output, name):
    deadline = time.monotonic() + 45
    while True:
        capture(output, name)
        try:
            return orientation(output / (name + '.png'))
        except AssertionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('out/android-presentation'))
    parser.add_argument('--driver', choices=('hardware', 'software'), default='hardware')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    assert text('shell', 'getprop', 'ro.kernel.qemu').strip() == '1', 'Use an owned emulator'
    original = {k: text('shell', 'wm', k) for k in ('size', 'density')}
    rotation = {k: text('shell', 'settings', 'get', 'system', k).strip()
                for k in ('accelerometer_rotation', 'user_rotation')}
    fixed_rotation = text('shell', 'wm', 'fixed-to-user-rotation').strip()
    acceleration = re.search(r'acceleration\s*=\s*(\S+)', text('emu', 'sensor', 'get', 'acceleration')).group(1)
    results = {}
    try:
        adb('shell', 'am', 'force-stop', PACKAGE)
        adb('shell', 'wm', 'size', '1080x2400')
        adb('shell', 'wm', 'density', '420')
        time.sleep(3)
        launch(args.driver)
        results['phoneColdStart'] = screenshot(args.output, 'phone-cold-start')
        adb('shell', 'wm', 'fixed-to-user-rotation', 'disabled')
        adb('shell', 'wm', 'user-rotation', 'free')
        adb('shell', 'settings', 'put', 'system', 'accelerometer_rotation', '1')
        # Drive actual accelerometer events. A shell rotation lock can be
        # immediately reversed by SystemUI's rotation-button observer on API35.
        # Sensor rotation follows the shipping Activity's sensorLandscape policy.
        for angle, gravity in (('3', '-9.8:0:0'), ('1', '9.8:0:0')):
            adb('emu', 'sensor', 'set', 'acceleration', gravity)
            deadline = time.monotonic() + 15
            while True:
                display = text('shell', 'dumpsys', 'window', 'displays')
                if re.search(r'\bmRotation=' + angle + r'\b', display):
                    break
                assert time.monotonic() < deadline, 'Display did not actually rotate'
                time.sleep(.5)
            (args.output / ('rotation-' + angle + '.txt')).write_text(display, encoding='utf-8')
            assert re.search(r'\bmRotation=' + angle + r'\b', display), 'Display did not actually rotate'
            results['rotation' + angle] = screenshot(args.output, 'rotation-' + angle)
        adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
        time.sleep(2)
        adb('shell', 'am', 'start', '-W', '-n', ACTIVITY, capture_output=True)
        time.sleep(3)
        results['resume'] = screenshot(args.output, 'phone-resume')
        # A cold start re-creates both QtWindow and the Vulkan surface.
        launch(args.driver)
        results['surfaceReplacement'] = screenshot(args.output, 'phone-replacement')
        (args.output / 'presentation.json').write_text(json.dumps(results, indent=2) + '\n')
    finally:
        adb('shell', 'am', 'force-stop', PACKAGE)
        adb('emu', 'sensor', 'set', 'acceleration', acceleration)
        for command, value in original.items():
            override = re.search(r'Override (?:size|density):\s*(\S+)', value)
            adb('shell', 'wm', command, override.group(1) if override else 'reset')
        for key, value in rotation.items():
            if value == 'null':
                adb('shell', 'settings', 'delete', 'system', key)
            else:
                adb('shell', 'settings', 'put', 'system', key, value)
        adb('shell', 'wm', 'fixed-to-user-rotation', fixed_rotation)


if __name__ == '__main__':
    main()
