"""Qualify scene-time video decode/composition without a visible window."""
import argparse
import array
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import wave
import cpu_import_fixture
import background_video_fixture as fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', required=True)
    parser.add_argument('--assets', required=True)
    parser.add_argument('--output')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mds-background-video-') as temporary:
        requested = args.output or os.environ.get('MDS_BACKGROUND_QUALIFICATION_DIR')
        root = Path(requested or temporary).resolve()
        if requested and root.exists():
            raise RuntimeError(f'Refusing to overwrite previous evidence: {root}')
        root.mkdir(parents=True, exist_ok=True)
        cpu_import_fixture.write(root)
        videos = fixture.write(root)
        # One hour of 48 kHz stereo PCM. Only the opening pulse is written;
        # RIFF metadata and the sparse data region exercise real file reads.
        with wave.open(str(root/'long-combined.wav'), 'wb') as pcm:
            pcm.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            pcm.writeframes(bytes(960 * 4) + b'\x00\x60\x00\xc0' * 480)
        import struct
        with (root/'long-combined.wav').open('r+b') as pcm:
            size = 48000 * 3600 * 4
            pcm.seek(4)
            pcm.write(struct.pack('<I', size + 36))
            pcm.seek(40)
            pcm.write(struct.pack('<I', size))
            pcm.truncate(44 + size)
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_MEDIA_BACKEND='ffmpeg')
        result = subprocess.run([args.program, str(root), args.assets], env=env, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=120)
        (root/'native.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'background test exit {result.returncode}\n'+result.stdout+result.stderr)
        encoded = fixture.run('ffmpeg', '-v', 'error', '-i', root/'background.mp4', '-map', '0:v:0',
                              '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
        pngs = fixture.run('ffmpeg', '-v', 'error', '-framerate', '60', '-i',
                           root/'mme-60/frame_%08d.png', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
        assert len(encoded) == len(pngs) == 12*160*96*3
        for n in range(12):
            frame = encoded[n*160*96*3:(n+1)*160*96*3]
            number = sum(1 << bit for bit in range(12) if frame[(88*160+bit*12+6)*3] > 128)
            assert number == n*500//1001, (n, number)
        mse = sum((a-b)**2 for a, b in zip(encoded, pngs)) / len(encoded)
        psnr = 100 if mse == 0 else 10*math.log10(255**2/mse)
        assert psnr >= 30, psnr
        info = json.loads(fixture.run('ffprobe', '-v', 'error', '-show_streams', '-of', 'json', root/'background.mp4'))
        assert all(s['codec_type'] != 'audio' for s in info['streams']), 'background audio leaked into export'
        hidden_psnr = {}
        for name, directory in [('hidden', 'hidden-sequence'), ('hidden-mme', 'hidden-mme-sequence')]:
            actual = fixture.run('ffmpeg', '-v', 'error', '-i', root/f'{name}.mp4',
                                 '-map', '0:v:0', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
            expected = fixture.run('ffmpeg', '-v', 'error', '-start_number', '24', '-i',
                                   root/directory/'frame_%08d.png', '-f', 'rawvideo',
                                   '-pix_fmt', 'rgb24', '-')
            assert len(actual) == len(expected) == 3*160*96*3, (name, len(actual), len(expected))
            error = sum((a-b)**2 for a, b in zip(actual, expected)) / len(actual)
            hidden_psnr[name] = 100 if error == 0 else 10*math.log10(255**2/error)
            assert hidden_psnr[name] >= 30, (name, hidden_psnr[name])
        combined = root/'combined-retry.mp4'
        samples = array.array('f')
        samples.frombytes(fixture.run('ffmpeg', '-v', 'error', '-i', combined,
                                      '-map', '0:a:0', '-f', 'f32le', '-'))
        assert len(samples) == 9600 * 2
        assert all(x == 0 for x in samples[:960*2] + samples[1440*2:])
        assert all(x == .75 for x in samples[960*2:1440*2:2])
        assert all(x == -.5 for x in samples[960*2+1:1440*2:2])
        recovered = fixture.run('ffmpeg', '-v', 'error', '-i', combined, '-map', '0:v:0',
                                '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
        assert len(recovered) == 6*160*96*3
        for n in range(6):
            frame = recovered[n*160*96*3:(n+1)*160*96*3]
            number = sum(1 << bit for bit in range(12) if frame[(88*160+bit*12+6)*3] > 128)
            assert number == max(0, n - 1), (n, number)
        budget = json.loads((root/'media-budget.json').read_text(encoding='utf-8'))
        report = dict(stage='P6.2', passed=True, backend=env.get('MDS_GPU_BACKEND', 'default'),
                      inputs=[dict(file=v['file'], frames=len(v['frames'])) for v in videos],
                      outputFrames=12, frameNumbers=True, videoPsnrDb=psnr, noBackgroundAudio=True,
                      hiddenVideoPsnrDb=hidden_psnr, hiddenSessionLifecycle=True,
                      combinedMediaRecovery=dict(frames=6, exactPcmPulse=[960, 1439],
                                                 audioFrames=9600, budget=budget),
                      checks=['forward/backward boundary and endpoint', 'VFR and NTSC B frames',
                              'rotation and sample aspect ratio', 'latest seek generation',
                              'PMM save/reopen', 'material and two-pass MME', 'PNG and video',
                              'cancel and missing media cleanup', 'hidden Qt language and toggle',
                              'session frozen frame and stale publication rejection',
                              'hidden source loss and retry', 'hidden cold reopen at saved cursor',
                              'hidden PNG/movie snapshot and two-pass MME',
                              'mode 3 target readback, resize and device replacement',
                              'capture mode switch and explicit refresh events'])
        (root/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(report))


if __name__ == '__main__':
    main()
