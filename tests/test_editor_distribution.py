"""Run the relocated production package with no SDK search paths or visible UI."""
import argparse
import array
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
import wave
import struct
import cpu_import_fixture
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from release_contract import inspect_archive


def qualify_release_media(report, fixtures, exe, env, root):
    import background_video_fixture as oracle
    samples = array.array('f')
    pcm = str(report) + '.pcm.mp4'
    samples.frombytes(oracle.run('ffmpeg', '-v', 'error', '-i', pcm,
                                 '-map', '0:a:0', '-f', 'f32le', '-'))
    assert len(samples) == 9600 * 2
    assert all(value == 0 for value in samples[:1920] + samples[2880:])
    assert all(value == .75 for value in samples[1920:2880:2])
    assert all(value == -.5 for value in samples[1921:2880:2])
    movies = []
    for suffix in ('.mp4', '.pcm.mp4'):
        path = str(report) + suffix
        rgb = oracle.run('ffmpeg', '-v', 'error', '-i', path, '-map', '0:v:0',
                         '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
        stride = 160 * 96 * 3
        assert len(rgb) == 6 * stride
        frame_numbers = []
        for n in range(6):
            pixels = rgb[n*stride:(n+1)*stride]
            numerals = sum(1 << b for b in range(12) if pixels[(88*160+b*12+6)*3] > 128)
            frame_numbers.append(numerals)
        assert frame_numbers == [0, 0, 1, 2, 3, 4], (suffix, frame_numbers)
        movies.append({'suffix': suffix, 'frameNumbers': frame_numbers})
    assert not list(root.glob('.mds-*')), 'Export left a staging directory'
    job = root / 'job.json'
    job.write_text(json.dumps(dict(version=1, project=str(report) + '.pmm', destination='job-frames',
                                   format='png', last=5, width=160, height=96,
                                   driver=env.get('MDS_GPU_DRIVER', 'hardware'),
                                   cancelFile='cancel-job')), encoding='utf-8')
    command = [str(exe), '--export-job', str(job)]
    (root / 'cancel-job').touch()
    cancelled = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=60)
    assert cancelled.returncode == 3 and json.loads(cancelled.stdout)['status'] == 'cancelled'
    (root / 'cancel-job').unlink()
    result = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=60)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert json.loads(result.stdout)['frames'] == 6
    originals = sorted(Path(str(report) + '.sequence').glob('frame_*.png'))
    replay = sorted((root / 'job-frames').glob('frame_*.png'))
    assert len(originals) == len(replay) == 6
    assert all(first.read_bytes() == second.read_bytes() for first, second in zip(originals, replay))
    return dict(backgroundFrames=movies, exactPcmPulse=[960, 1439], audioFrames=9600,
                longWaveBytes=(fixtures/'export.wav').stat().st_size, repeatPngFrames=6,
                failureRetry=True, settingsMigration=True, offscreen=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--dist', type=Path)
    source.add_argument('--archive', type=Path, help='Verify checksum, extract, then test the actual shipping archive')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--mme', action='store_true', help='Also qualify raw FX, save/reopen and export')
    parser.add_argument('--media', action='store_true', help='Also qualify packaged MME video and WAV encoding')
    parser.add_argument('--background', action='store_true', help='Also decode/compose scene-time background video with the bundled runtime')
    parser.add_argument('--release-media', action='store_true', help='Require P6 media and settings gates from the extracted package')
    args = parser.parse_args()
    if args.release_media:
        args.background = True
    if args.background:
        args.media = True
    if args.media:
        args.mme = True
    if args.archive:
        record = inspect_archive(args.archive)
        with tempfile.TemporaryDirectory(prefix='mds-shipping-archive-') as temporary:
            root = Path(temporary)
            if args.archive.name.endswith('.zip'):
                with zipfile.ZipFile(args.archive) as archive:
                    archive.extractall(root)
            else:
                with tarfile.open(args.archive) as archive:
                    archive.extractall(root, filter='data')
            package, = root.iterdir()
            report = args.report or args.archive.parent / 'distribution-check.json'
            subprocess.run([sys.executable, str(Path(__file__).resolve()), '--dist', str(package),
                            '--report', str(report.resolve())] + (['--release-media'] if args.release_media else ['--background'] if args.background else ['--media'] if args.media else ['--mme'] if args.mme else []), check=True)
            if args.release_media:
                checked = json.loads(report.read_text(encoding='utf-8'))
                checked.update(packageSha256=record['sha256'], archive=record['archive'])
                report.write_text(json.dumps(checked, indent=2) + '\n', encoding='utf-8')
        print(f'Shipping archive passed: {record["archive"]} ({record["arch"]})')
        return
    dist = args.dist.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='mds-relocated-package-') as temporary:
        # macOS exposes /var through /private/var. Compare canonical roots on
        # both sides before rejecting a dependency as external to the package.
        root = Path(temporary).resolve()
        package = root / 'package with spaces'
        shutil.copytree(dist, package, symlinks=True)
        fixtures = root / 'fixtures'
        fixtures.mkdir()
        cpu_import_fixture.write(fixtures)
        if args.background:
            import background_video_fixture
            background_video_fixture.write(fixtures)
        if args.media:
            with wave.open(str(fixtures / 'export.wav'), 'wb') as audio:
                audio.setnchannels(2); audio.setsampwidth(2); audio.setframerate(48000)
                if args.release_media:
                    audio.writeframes(bytes(960 * 4) + struct.pack('<hh', 24576, -16384) * 480)
                else:
                    audio.writeframes(b''.join(struct.pack('<hh', 12000 if n % 480 < 240 else -12000, 0) for n in range(4800)))
            if args.release_media:
                size = 48000 * 3600 * 4
                with (fixtures / 'export.wav').open('r+b') as audio:
                    audio.seek(4)
                    audio.write(struct.pack('<I', size + 36))
                    audio.seek(40)
                    audio.write(struct.pack('<I', size))
                    audio.truncate(44 + size)
        system = platform.system()
        if system == 'Windows':
            for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
                if not (package / name).is_file():
                    raise RuntimeError(f'Package depends on a preinstalled MSVC runtime: {name}')
        exe = package / ('MikuDanceStudioNG.exe' if system == 'Windows' else
                         'MikuDanceStudioNG.app/Contents/MacOS/MikuDanceStudioNG' if system == 'Darwin'
                         else 'bin/MikuDanceStudioNG')
        env = {}
        for key in ('SystemRoot', 'WINDIR', 'COMSPEC', 'TEMP', 'TMP', 'HOME', 'USERPROFILE',
                    'LANG', 'LC_ALL', 'XDG_RUNTIME_DIR'):
            if key in os.environ:
                env[key] = os.environ[key]
        env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32') if system == 'Windows' else '/usr/bin:/bin'
        env['QT_QPA_PLATFORM'] = 'offscreen'
        env['QT_DEBUG_PLUGINS'] = '1'
        if system == 'Windows':
            env['MDS_GPU_BACKEND'] = 'd3d11'
            env['MDS_GPU_DRIVER'] = 'software'
        elif system == 'Darwin':
            env['MTL_DEBUG_LAYER'] = '1'
        elif system == 'Linux':
            env['MDS_GPU_DRIVER'] = 'software'
        report = root / 'deployment.json'
        output = []
        profiles = ['offscreen'] if args.mme else (['offscreen'] * 3 if system == 'Darwin' else (['offscreen', 'windows'] if system == 'Windows' else ['offscreen']))
        profile_checks = []
        # Exercise actual process teardown repeatedly: Qt's Apple logging sink
        # previously failed intermittently while unloading plugins at exit.
        for attempt, qpa in enumerate(profiles):
            env['QT_QPA_PLATFORM'] = qpa
            result = subprocess.run([str(exe), '--release-deployment-check' if args.release_media else '--background-deployment-check' if args.background else '--media-deployment-check' if args.media else '--mme-deployment-check' if args.mme else '--deployment-check', str(report), str(fixtures / ('pmx.pmm' if args.release_media else 'v2.pmm'))],
                                    cwd=root, env=env, text=True, encoding='utf-8', errors='replace',
                                    capture_output=True, timeout=120,
                                    creationflags=subprocess.CREATE_NO_WINDOW if system == 'Windows' else 0)
            output.append(f'Process {attempt + 1}, exit {result.returncode}\n' + result.stdout + result.stderr)
            if result.returncode:
                break
            current = json.loads(report.read_text(encoding='utf-8'))
            if not all(size in current.get('applicationIconSizes', [])
                       for size in ([16, 16], [32, 32])):
                raise RuntimeError('Relocated package could not load its embedded application icon')
            if current.get('platform') != qpa or current.get('visible'):
                raise RuntimeError(f'Unexpected package platform/lifecycle: {current}')
            if qpa == 'windows' and current.get('styleBase') != 'windowsvista':
                raise RuntimeError('Relocated package did not load the native Windows control style')
            profile_checks.append({key: current[key] for key in ('platform', 'styleBase', 'visible')})
        diagnostic = args.report or dist.parent / 'distribution-check.json'
        diagnostic.parent.mkdir(parents=True, exist_ok=True)
        diagnostic.with_suffix('.log').write_text('\n'.join(output), encoding='utf-8')
        if result.returncode:
            print(result.stdout, result.stderr)
            # Keep a native stack for packaging-only crashes on isolated Apple
            # runners. The original failure remains fatal even if rerunning it
            # under the debugger succeeds; never turn this into a retry gate.
            if system == 'Darwin' and os.environ.get('GITHUB_ACTIONS') == 'true':
                try:
                    trace = subprocess.run(
                        ['/usr/bin/lldb', '--no-lldbinit', '--batch', '-o', 'run', '-k', 'thread backtrace all',
                         '--', str(exe), '--deployment-check', str(report), str(fixtures / 'v2.pmm')],
                        cwd=root, env=env, capture_output=True, text=True, encoding='utf-8',
                        errors='replace', timeout=60)
                    diagnostic.with_suffix('.lldb.log').write_text(trace.stdout + trace.stderr,
                                                                   encoding='utf-8')
                    print(trace.stdout, trace.stderr)
                except (OSError, subprocess.TimeoutExpired) as error:
                    print(f'Could not collect native crash stack: {error}')
                # A debugger can change timing or runtime enforcement. Preserve
                # the OS report from the original failed process as well.
                for folder in (Path.home() / 'Library/Logs/DiagnosticReports',
                               Path('/Library/Logs/DiagnosticReports')):
                    for crash in folder.glob('MikuDanceStudioNG*'):
                        if crash.is_file():
                            shutil.copy2(crash, diagnostic.parent /
                                         f'{diagnostic.stem}.{crash.name}')
            raise RuntimeError(f'Relocated distribution failed ({result.returncode}); see {diagnostic.with_suffix(".log")}')
        evidence = json.loads(report.read_text(encoding='utf-8'))
        if args.background and not evidence.get('backgroundVideo'):
            raise RuntimeError('Packaged scene-time background decoding was not executed')
        if args.media:
            if not evidence.get('mediaExport'):
                raise RuntimeError('Packaged video/audio export was not executed')
            for suffix in ('.mp4', '.pcm.mp4'):
                if Path(str(report) + suffix).stat().st_size < 1024:
                    raise RuntimeError('Packaged encoder did not publish a complete movie')
                shutil.copy2(str(report) + suffix, diagnostic.with_suffix(suffix))
        if args.mme:
            if not evidence.get('mme') or not Path(evidence['shaderCompiler']).resolve().is_relative_to(package):
                raise RuntimeError('MME did not use the packaged shader compiler')
            for name in ('Material.fx', 'Scene.fx', 'Common.fxh'):
                if not (Path(evidence['assets']) / 'effects' / name).is_file():
                    raise RuntimeError(f'Missing bundled raw FX/include: {name}')
        evidence['profiles'] = profile_checks
        if not evidence.get('bundledFonts') or not (package / 'licenses/NotoSansCJK/NotoSansCJK-OFL.txt').is_file():
            raise RuntimeError('Package is missing its redistributable CJK fonts or license')
        fonts = (package / 'MikuDanceStudioNG.app/Contents/Resources/fonts' if system == 'Darwin'
                 else exe.parent / 'fonts')
        with (fonts / 'NotoSansCJK-Regular.ttc').open('rb') as font:
            if hashlib.file_digest(font, 'sha256').hexdigest() != 'b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a':
                raise RuntimeError('Packaged Noto CJK collection is not the qualified unmodified version')
        for key in ('plugins', 'translations', 'assets'):
            path = Path(evidence[key]).resolve()
            if not path.is_relative_to(package) or not path.is_dir():
                raise RuntimeError(f'Package used an external {key} directory: {path}')
            evidence[key] = str(path.relative_to(package))
        expected_languages = ['en', 'ja', 'zh_CN', 'zh_TW', 'ko', 'fr', 'de', 'es',
                              'it', 'pt_BR', 'ru', 'nl', 'pl', 'tr', 'vi']
        size = (160, 96) if args.release_media else (96, 64)
        if evidence['visible'] or evidence['languages'] != expected_languages or (evidence['width'], evidence['height']) != size:
            raise RuntimeError(f'Incomplete package check: {evidence}')
        build = json.loads((package / 'build-info.json').read_text(encoding='utf-8'))
        if evidence.get('version') != build['version']:
            raise RuntimeError('Running application version differs from the package identity')
        evidence['build'] = build
        if args.release_media:
            assert evidence['releaseMedia']
            evidence['releaseMediaEvidence'] = qualify_release_media(report, fixtures, exe, env, root)
        if system == 'Darwin' and build.get('metal'):
            if not evidence.get('nativeMetal') or not (package / 'licenses/SPIRV-Cross/LICENSE').is_file():
                raise RuntimeError('macOS package did not use native Metal with its compiler notices')
        labels = evidence['standardCancelLabels']
        if labels[0] != 'Cancel' or any(not text or text == 'Cancel' for text in labels[1:]):
            raise RuntimeError(f'Qt standard dialog translations were not deployed: {labels}')
        diagnostic.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
        shutil.copy2(str(report) + '.png', diagnostic.with_suffix('.png'))
        print(result.stdout.strip())


if __name__ == '__main__':
    main()
