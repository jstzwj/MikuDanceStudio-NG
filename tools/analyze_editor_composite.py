"""Locate strict public-composite failures without changing images or acceptance rules."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inspect(reference, actual, output, name):
    expected = Image.open(reference).convert('RGBA')
    width, height = expected.size
    raw = actual.read_bytes()
    if len(raw) != width * height * 4:
        raise ValueError(f'{actual}: RGBA byte count does not match {expected.size}')
    native = expected.tobytes()
    image = Image.frombytes('RGBA', expected.size, raw)
    pixels = []
    elements = outside = coverage = alpha = changed = maximum = 0
    for index in range(width * height):
        offset = index * 4
        a, b = native[offset:offset+4], raw[offset:offset+4]
        errors = [abs(x-y) for x, y in zip(a, b)]
        elements += sum(e != 0 for e in errors)
        changed += any(errors)
        maximum = max(maximum, *errors)
        failed = sum(e > (0 if channel == 3 else 1) for channel, e in enumerate(errors))
        outside += failed
        covered = any(x != 255 for x in a[:3]) != any(x != 255 for x in b[:3])
        coverage += covered
        alpha += errors[3] != 0
        if failed or covered:
            pixels.append({'xy': [index % width, index // width], 'native': list(a),
                           'actual': list(b), 'absolute_error': errors,
                           'outside_tolerance_elements': failed,
                           'coverage_differs': covered})
    # Only diagnostic crops are rescaled; original input bytes and thresholds
    # remain untouched. Every failed coordinate, not just examples, is in JSON.
    examples = sorted(pixels, key=lambda p: max(p['absolute_error']), reverse=True)[:8]
    if examples:
        scale, radius, label, panel = 10, 6, 18, 130
        sheet = Image.new('RGB', (panel * 2, (panel + label) * len(examples)), 'white')
        draw = ImageDraw.Draw(sheet)
        for row, pixel in enumerate(examples):
            x, y = pixel['xy']
            box = (x-radius, y-radius, x+radius+1, y+radius+1)
            for column, (source, title) in enumerate(((expected, 'native'), (image, 'actual'))):
                crop = source.convert('RGB').crop(box).resize((panel, panel), Image.Resampling.NEAREST)
                position = (column * panel, row * (panel + label))
                draw.text(position, f'{title} ({x},{y})', fill='black')
                sheet.paste(crop, (position[0], position[1] + label))
                left, top = position[0] + radius * scale, position[1] + label + radius * scale
                draw.rectangle((left, top, left+scale-1, top+scale-1), outline='red')
        sheet.save(output / f'{name}-examples.png')
    return {'size': [width, height], 'native_png_sha256': digest(reference.read_bytes()),
            'native_rgba_sha256': digest(native), 'actual_rgba_sha256': digest(raw),
            'changed_elements': elements, 'changed_pixels': changed, 'maximum_code_error': maximum,
            'outside_tolerance_elements': outside, 'outside_tolerance_pixels': sum(
                p['outside_tolerance_elements'] != 0 for p in pixels),
            'alpha_differences': alpha, 'coverage_differences': coverage,
            'accepted': outside == 0 and coverage == 0, 'failed_pixels': pixels}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=Path('tests/data/p4_composite'))
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'schema': 1, 'thresholds': {'rgb_code_values': 1, 'alpha_code_values': 0,
                                       'coverage_differences': 0},
              'scope': 'Public native color reference only; no floating shadow or repeat qualification',
              'examples': 'Nearest-neighbor crops; red outline identifies the reported pixel',
              'frames': {}}
    for name in ('rest', 'posed'):
        result = inspect(args.reference / f'{name}.png', args.capture / f'{name}.rgba',
                         args.output, name)
        report['frames'][name] = result
        print(f"{name}: outside={result['outside_tolerance_pixels']} pixels / "
              f"{result['outside_tolerance_elements']} elements, "
              f"coverage={result['coverage_differences']}, alpha={result['alpha_differences']}")
    report['accepted'] = all(frame['accepted'] for frame in report['frames'].values())
    (args.output / 'analysis.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return 0 if report['accepted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
