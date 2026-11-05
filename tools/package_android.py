"""Verify/sign the Qt Release APK with Android build-tools, then checksum its payload."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from release_contract import digest, inspect_archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--build-tools', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('out/releases'))
    parser.add_argument('--tag', default='')
    args = parser.parse_args()
    import json
    import zipfile
    with zipfile.ZipFile(args.apk) as archive:
        info = json.loads(archive.read('assets/build-info.json'))
    badging = subprocess.check_output([str(args.build_tools / 'aapt'), 'dump', 'badging', str(args.apk)], text=True)
    expected = f"package: name='io.github.jstzwj.mikudancestudiong' versionCode='1' versionName='{info['version']}'"
    # Version code is monotonic; version name and package ID must match build-info.
    if not re.search(r"package: name='io.github.jstzwj.mikudancestudiong' versionCode='[0-9]+' versionName='" +
                     re.escape(info['version']) + "'", badging):
        raise ValueError(f'APK manifest and build identity disagree (expected {expected})')
    args.output.mkdir(parents=True, exist_ok=True)
    target = args.output / f"MikuDanceStudio-NG-v{info['version']}-android-{info['arch']}.apk"
    if args.tag:
        if 'application-debuggable' in badging or info.get('tag') != args.tag:
            raise ValueError('Cannot release a debuggable or mismatched APK')
    if args.tag or os.environ.get('ANDROID_KEYSTORE_PATH'):
        keystore = os.environ['ANDROID_KEYSTORE_PATH']
        for key in ('ANDROID_KEYSTORE_PASSWORD', 'ANDROID_KEY_ALIAS', 'ANDROID_KEY_PASSWORD'):
            if not os.environ.get(key):
                raise ValueError(f'Missing signing secret: {key}')
        with tempfile.TemporaryDirectory(prefix='mds-apk-sign-') as temporary:
            aligned = Path(temporary) / 'aligned.apk'
            subprocess.run([str(args.build_tools / 'zipalign'), '-P', '16', '-f', '4', str(args.apk), str(aligned)], check=True)
            subprocess.run([str(args.build_tools / 'apksigner'), 'sign', '--ks', keystore,
                            '--v4-signing-enabled', 'false',
                            '--ks-key-alias', os.environ['ANDROID_KEY_ALIAS'],
                            '--ks-pass', 'env:ANDROID_KEYSTORE_PASSWORD', '--key-pass', 'env:ANDROID_KEY_PASSWORD',
                            '--out', str(target), str(aligned)], check=True)
    else:
        shutil.copy2(args.apk, target)
    subprocess.run([str(args.build_tools / 'apksigner'), 'verify', '--verbose', '--print-certs', str(target)], check=True)
    checksum = digest(target)
    target.with_name(target.name + '.sha256').write_text(f'{checksum}  {target.name}\n', encoding='ascii')
    inspect_archive(target, args.tag or None)
    print(target)


if __name__ == '__main__':
    main()
