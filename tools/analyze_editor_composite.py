"""Review complete-scene visual equivalence and retain strict pixel diagnostics."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw


def digest(data):
    return hashlib.sha256(data).hexdigest()


def visual_difference(native, actual, width, height):
    """Mirror the complete-scene C++ gate, including Alpha on both backgrounds."""
    result = {'mean': 0., 'large_pixel_fraction': 0., 'block_mean_maximum': 0.,
              'alpha_differences': sum(native[i] != actual[i] for i in range(3, len(native), 4))}
    for background in (0., 255.):
        error_sum = block_maximum = 0.
        large_pixels = 0
        for y0 in range(0, height, 8):
            for x0 in range(0, width, 8):
                block, count = [0., 0., 0.], 0
                for y in range(y0, min(y0 + 8, height)):
                    for x in range(x0, min(x0 + 8, width)):
                        i, maximum = (y * width + x) * 4, 0.
                        for channel in range(3):
                            a = (native[i + channel] * native[i + 3] + background * (255 - native[i + 3])) / 255
                            b = (actual[i + channel] * actual[i + 3] + background * (255 - actual[i + 3])) / 255
                            delta = a - b
                            error_sum += abs(delta)
                            maximum = max(maximum, abs(delta))
                            block[channel] += delta
                        large_pixels += maximum > 3
                        count += 1
                block_maximum = max(block_maximum, *(abs(value) / count for value in block))
        result['mean'] = max(result['mean'], error_sum / (width * height * 3))
        result['large_pixel_fraction'] = max(result['large_pixel_fraction'], large_pixels / (width * height))
        result['block_mean_maximum'] = max(result['block_mean_maximum'], block_maximum)
    result['accepted'] = (result['mean'] <= .5 and result['large_pixel_fraction'] <= .0005
                          and result['block_mean_maximum'] <= 12)
    return result


def inspect(reference, actual, output, name):
    expected = Image.open(reference).convert('RGBA')
    width, height = expected.size
    raw = actual.read_bytes()
    if len(raw) != width * height * 4:
        raise ValueError(f'{actual}: RGBA byte count does not match {expected.size}')
    native = expected.tobytes()
    image = Image.frombytes('RGBA', expected.size, raw)
    overview = Image.new('RGB', (width * 2, height + 24), 'white')
    labels = ImageDraw.Draw(overview)
    for column, (source, title) in enumerate(((expected, 'DX9 reference'), (image, 'current backend'))):
        labels.text((column * width + 4, 5), title, fill='black')
        background = Image.new('RGBA', expected.size, 'white')
        overview.paste(Image.alpha_composite(background, source).convert('RGB'), (column * width, 24))
    overview.save(output / f'{name}-overview.png')
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
            'accepted': outside == 0 and coverage == 0, 'failed_pixels': pixels,
            'visual': visual_difference(native, raw, width, height)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=Path('tests/data/p4_composite'))
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--strict', action='store_true', help='Require the retained strict diagnostic profile')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'schema': 1, 'thresholds': {'rgb_code_values': 1, 'alpha_code_values': 0,
                                       'coverage_differences': 0},
              'visual_thresholds': {'mean_rgb_code_values': .5, 'large_pixel_fraction': .0005,
                                    'large_rgb_code_values': 3, 'block_size': 8,
                                    'block_mean_maximum': 12, 'backgrounds': [0, 255]},
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
        visual = result['visual']
        print(f"  visual: mean={visual['mean']:.6f}, "
              f"large_fraction={visual['large_pixel_fraction']:.9f}, "
              f"block_maximum={visual['block_mean_maximum']:.6f}, accepted={visual['accepted']}")
    report['strict_accepted'] = all(frame['accepted'] for frame in report['frames'].values())
    report['visual_accepted'] = all(frame['visual']['accepted'] for frame in report['frames'].values())
    report['policy'] = 'strict' if args.strict else 'visual scene composite'
    report['accepted'] = report['strict_accepted'] if args.strict else report['visual_accepted']
    (args.output / 'analysis.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return 0 if report['accepted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
