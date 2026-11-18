"""Validate NG versions and tested release packages; never build legacy DX9."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from release_contract import SYSTEMS, digest, version, verify_packages


def verify_evidence(manifest, folder):
    support = []
    for package in manifest['packages']:
        target = SYSTEMS[package['system']] + '-' + package['arch']
        reports = folder / f'editor-media-{target}'
        symbols = folder / f'editor-symbols-{target}'
        record = json.loads((symbols / 'symbols.json').read_text(encoding='utf-8'))
        if (record['commit'], record['arch']) != (package['commit'], package['arch']):
            raise ValueError(f'{target}: symbol source or architecture mismatch')
        if not record['files'] or any(digest(symbols / name) != sha
                                      for name, sha in record['files'].items()):
            raise ValueError(f'{target}: missing or invalid debug symbols')
        mobile = target == 'android-universal'
        if mobile:
            native = {abi: value['binarySha256'] for abi, value in record['binaries'].items()}
            if native != package['binarySha256'] or record['packageSha256'] != package['sha256']:
                raise ValueError(f'{target}: symbols do not match the APK')
            report = json.loads((reports / 'media-qualification.json').read_text(encoding='utf-8'))
            passed = all(report.get(key) is True for key in
                         ('playedSilentWave', 'backgroundSeek', 'suspendResume', 'mediaProjectTouch'))
            passed &= sorted(report['verifiedCodecAbis']) == sorted(package['abis'])
            passed &= report['commit'] == manifest['commit']
            coverage = {key: report[key] for key in
                        ('verifiedCodecAbis', 'vulkanDevices', 'driver', 'runtimeFx')}
            coverage.update(desktopExport='unqualified', physicalArmGpu='unqualified')
        else:
            if record['binarySha256'] != package['binarySha256']:
                raise ValueError(f'{target}: symbols do not match the portable archive')
            if target.startswith('macos-') and record.get('sourceLineMappings') is not True:
                raise ValueError(f'{target}: dSYM has no verified source-line mappings')
            distribution, = reports.rglob('distribution-check.json')
            report = json.loads(distribution.read_text(encoding='utf-8'))
            media = report['releaseMediaEvidence']
            passed = report['build']['commit'] == manifest['commit'] and report['mme'] is True
            passed &= all(media.get(key) is True for key in ('failureRetry', 'settingsMigration', 'offscreen'))
            passed &= media['repeatPngFrames'] == 6 and media['exactPcmPulse'] == [960, 1439]
            passed &= media['audioFrames'] == 9600 and media['longWaveBytes'] > 64 * 1024 * 1024
            passed &= [m['frameNumbers'] for m in media['backgroundFrames']] == [[0, 0, 1, 2, 3, 4]] * 2
            coverage = dict(mme='packaged material and two-pass effect', gpu=report['gpu'],
                            media=media)
            native_report = next(reports.rglob('native-audio.json'), None)
            if native_report:
                audio = json.loads(native_report.read_text(encoding='utf-8-sig'))
                coverage['nativeAudio'] = {k: audio[k] for k in ('status', 'seconds', 'detail')}
        if not passed or report.get('packageSha256') != package['sha256']:
            raise ValueError(f'{target}: missing passing evidence for the exact package')
        support.append(dict(target=target, archive=package['archive'], sha256=package['sha256'],
                            mediaArtifact=reports.name, symbolArtifact=symbols.name,
                            symbols=record, coverage=coverage))
    return dict(schema=1, commit=manifest['commit'], tag=manifest['tag'], targets=support)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', help='Require this exact vMAJOR.MINOR.PATCH tag')
    parser.add_argument('--packages', type=Path, help='Validate all release archives')
    parser.add_argument('--candidate', action='store_true', help='Verify clean, untagged development packages')
    parser.add_argument('--evidence', type=Path, help='Match qualified media and symbols to the archives')
    args = parser.parse_args()
    value = version((ROOT / 'VERSION').read_text(encoding='utf-8').strip())
    if args.tag is not None and args.tag != f'v{value}':
        parser.error(f'Tag {args.tag!r} does not match VERSION (v{value})')
    if args.packages:
        if bool(args.tag) == args.candidate:
            parser.error('--packages requires one of --tag and --candidate')
        commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        manifest = verify_packages(args.packages, args.tag, commit,
                                   candidate_version=value if args.candidate else None)
        if args.evidence:
            report = verify_evidence(manifest, args.evidence)
            (args.packages / 'platform-support.json').write_text(
                json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'Qualified {len(manifest["packages"])} packages for {args.tag or value} ({commit})')
    else:
        print(value)


if __name__ == '__main__':
    main()
