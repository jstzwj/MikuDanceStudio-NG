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


def check_audio_length(stream, expected, decoded):
    if stream['codec_name'].startswith('pcm_'):
        assert decoded == expected, ('PCM sample count', decoded, expected)
        return expected
    # MP4 edit lists use the movie clock (Qt's muxer uses 1 ms), separate
    # from the 48 kHz audio clock. New decoders honor the truncated final
    # edit; older decoders emit the remaining AAC packet padding instead.
    declared = Fraction(int(stream['duration_ts'])) * Fraction(stream['time_base']) * int(stream['sample_rate'])
    quantum = int(stream['sample_rate']) / 1000
    assert abs(declared - expected) <= quantum + 1, ('AAC edit duration', declared, expected)
    assert min(expected, math.floor(declared)) <= decoded <= max(expected, math.ceil(declared)) + 1024, ('AAC decoded tail', decoded, str(declared))
    return float(declared)


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
    audio_stream = next(s for s in info['streams'] if s['codec_type'] == 'audio')
    declared = check_audio_length(audio_stream, audio_count, len(left))
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
                decodedAudioFrames=len(left), declaredAudioSampleFrames=declared,
                audioPulseFirst=min(active), audioPulseLast=max(active),
                trailingSilence=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program')
    parser.add_argument('--assets')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--deployed-prefix', type=Path, help='Decode outputs of --media package qualification')
    args = parser.parse_args()
    for tool in ('ffmpeg', 'ffprobe'):
        if not shutil.which(tool):
            raise RuntimeError(f'{tool} is required for independent export qualification (test-only)')
    if args.deployed_prefix:
        result = []
        for suffix in ('.mp4', '.mkv'):
            movie = args.deployed_prefix.with_suffix(suffix)
            info = json.loads(run('ffprobe', '-v', 'error', '-count_frames', '-show_streams', '-of', 'json', movie))
            video = next(s for s in info['streams'] if s['codec_type'] == 'video')
            audio = next(s for s in info['streams'] if s['codec_type'] == 'audio')
            assert (video['width'], video['height'], int(video['nb_read_frames'])) == (96, 64, 2)
            assert int(audio['sample_rate']) == 48000 and int(audio['channels']) == 2
            samples = array.array('f')
            samples.frombytes(run('ffmpeg', '-v', 'error', '-i', movie, '-map', '0:a:0', '-f', 'f32le', '-'))
            declared = check_audio_length(audio, 3200, len(samples)//2)
            assert max(samples[::2]) > .2 and min(samples[::2]) < -.2
            assert max(abs(x) for x in samples[1::2]) < .001
            result.append(dict(file=movie.name, decodedFrames=2, audioFrames=len(samples)//2,
                               declaredAudioSampleFrames=declared,
                               videoCodec=video['codec_name'], audioCodec=audio['codec_name']))
        report = dict(schema=1, passed=True, packagedMedia=result)
        args.deployed_prefix.with_suffix('.decoded.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(report))
        return
    if not args.program or not args.assets:
        parser.error('--program and --assets are required unless --deployed-prefix is used')
    with tempfile.TemporaryDirectory(prefix='mds-export-oracle-') as temporary:
        root = Path(temporary)
        cpu_import_fixture.write(root)
        requested = args.output or os.environ.get('MDS_EXPORT_QUALIFICATION_DIR')
        output = Path(requested).resolve() if requested else root / 'evidence'
        if output.exists():
            raise RuntimeError(f'Refusing to overwrite previous evidence: {output}')
        env = os.environ.copy()
        env.update(QT_QPA_PLATFORM='offscreen', QT_MEDIA_BACKEND='ffmpeg', MDS_EXPORT_EVIDENCE=str(output))
        subprocess.run([args.program, str(root), args.assets], check=True, env=env, timeout=120)
        results = [inspect(output / name) for name in ('clock.mp4', 'clock.mkv', 'clock-ntsc.mp4')]
        original = run('ffmpeg', '-v', 'error', '-framerate', '30', '-i',
                       output/'sequence/frame_%08d.png', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
        assert len(original) == 3*160*96*3
        scene_quality = {}
        for name in ('scene.mp4', 'worker.mp4'):
            video = run('ffmpeg', '-v', 'error', '-i', output/name, '-map', '0:v:0',
                        '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
            assert len(video) == len(original)
            mse = sum((a-b)**2 for a, b in zip(original, video)) / len(video)
            psnr = 10*math.log10(255**2/mse) if mse else 100
            assert psnr >= 30, (name, psnr)
            scene_quality[name] = psnr
        report = dict(schema=1, backend=env.get('MDS_GPU_BACKEND', 'default'), passed=True, media=results,
                      sceneFrames=3, materialAndTwoPassMme=True, scenePsnrDb=scene_quality,
                      rawMmd30HzRegression='separate qt_mme strict gate',
                      ffmpegOracle=run('ffmpeg', '-version').decode().splitlines()[0])
        (output/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(report))


if __name__ == '__main__':
    main()
