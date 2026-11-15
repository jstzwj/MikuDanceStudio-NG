"""Independent decoded-content/timestamp oracle. FFmpeg tools are test-only."""
import argparse
import array
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import cpu_import_fixture


def run(*command):
    return subprocess.run([str(x) for x in command], check=True, capture_output=True, timeout=120).stdout


def inspect(path):
    info = json.loads(run('ffprobe', '-v', 'error', '-count_frames', '-show_streams', '-show_frames', '-of', 'json', path))
    stream = next(s for s in info['streams'] if s['codec_type'] == 'video')
    long = 'ntsc' in path.name
    rate = Fraction(30000, 1001) if long else Fraction(60, 1)
    count = 3597 if long else 60
    frames = [f for f in info['frames'] if f['media_type'] == 'video']
    assert int(stream['nb_read_frames']) == count
    assert Fraction(stream['r_frame_rate']) == rate
    assert abs(float(Fraction(stream['avg_frame_rate']) - rate)) < .001
    times = [float(f['best_effort_timestamp_time']) for f in frames]
    timing_error = max(abs(t - float(n / rate)) for n, t in enumerate(times))
    assert timing_error <= .0011, timing_error  # Matroska uses a 1 ms container timebase.
    video = run('ffmpeg', '-v', 'error', '-i', path, '-map', '0:v:0', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
    assert len(video) == count * 160 * 96 * 3
    color_errors = []
    for n in range(count):
        for x, y in ((32, 24), (32, 72), (128, 24), (128, 72)):
            offset = ((n * 96 + y) * 160 + x) * 3
            expected = ((n * 3) % 256, y * 2, 32 if x < 80 else 224)
            color_errors.extend(abs(video[offset+c] - expected[c]) for c in range(3))
        for bit in range(6):
            offset = ((n*96+88)*160+bit*16+8)*3
            assert (video[offset] > 128) == bool(n & (1 << bit)), ('wrong decoded frame number', n, bit)
    # Lossy video is evaluated by decoded frame identity and image quality,
    # never compared as compressed bytes. Raw export stays byte-exact.
    mse = sum(e*e for e in color_errors) / len(color_errors)
    psnr = 10 * math.log10(255**2 / mse) if mse else 100
    assert psnr >= 30, psnr
    if path.suffix == '.mkv':
        assert max(color_errors) <= 10, max(color_errors)
    samples = array.array('f')
    samples.frombytes(run('ffmpeg', '-v', 'error', '-i', path, '-map', '0:a:0', '-f', 'f32le', '-'))
    left, right = samples[::2], samples[1::2]
    audio_count = int(count / rate * 48000)
    assert audio_count <= len(left) <= audio_count + 1024  # AAC final packet padding.
    active = [n for n, value in enumerate(left) if abs(value) > .2]
    pulse = 5724000 if long else 12000
    assert abs(min(active) - 12000) <= 48 and abs(max(active) - (pulse+479)) <= 48
    assert max(abs(x) for x in left[(pulse+24000 if long else 30000):]) < .001
    assert min(right[12048:12432]) < -.4 and max(left[12048:12432]) > .6
    if path.suffix == '.mkv':
        assert len(left) == 48000
        assert all(abs(left[n] - .75) < 1/32768 and abs(right[n] + .5) < 1/32768 for n in range(12000, 12480))
    return dict(file=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                videoCodec=stream['codec_name'], decodedFrames=len(frames),
                maxTimestampErrorSeconds=timing_error, maxRgbByteError=max(color_errors),
                colorPatchPsnrDb=psnr, frameNumbersVerified=True,
                decodedAudioFrames=len(left), audioPulseFirst=min(active), audioPulseLast=max(active),
                trailingSilence=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', required=True)
    parser.add_argument('--assets', required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    for tool in ('ffmpeg', 'ffprobe'):
        if not shutil.which(tool):
            raise RuntimeError(f'{tool} is required for independent export qualification (test-only)')
    with tempfile.TemporaryDirectory(prefix='mds-export-oracle-') as temporary:
        root = Path(temporary)
        cpu_import_fixture.write(root)
        output = args.output.resolve() if args.output else root / 'evidence'
        if output.exists():
            raise RuntimeError(f'Refusing to overwrite previous evidence: {output}')
        env = os.environ.copy()
        env.update(QT_QPA_PLATFORM='offscreen', QT_MEDIA_BACKEND='ffmpeg', MDS_EXPORT_EVIDENCE=str(output))
        subprocess.run([args.program, str(root), args.assets], check=True, env=env, timeout=120)
        results = [inspect(output / name) for name in ('clock.mp4', 'clock.mkv', 'clock-ntsc.mp4')]
        video = run('ffmpeg', '-v', 'error', '-i', output/'scene.mp4', '-map', '0:v:0', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
        assert len(video) == 3*160*96*3
        report = dict(schema=1, backend=env.get('MDS_GPU_BACKEND', 'default'), passed=True, media=results,
                      sceneFrames=3, rawMmd30HzRegression='separate qt_mme strict gate',
                      ffmpegOracle=run('ffmpeg', '-version').decode().splitlines()[0])
        (output/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(report))


if __name__ == '__main__':
    main()
