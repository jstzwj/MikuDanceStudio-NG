"""Build a small emulator-only IME with SDK tools, without Gradle or product changes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('out/android-test-ime'))
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / 'tests/android_ime'
    args.output.mkdir(parents=True, exist_ok=True)
    sdk = args.sdk.resolve()
    tools = sdk / 'build-tools/35.0.0'
    platform = sdk / 'platforms/android-35/android.jar'
    java_home = Path(os.environ['JAVA_HOME']) / 'bin'
    executable = '.exe' if os.name == 'nt' else ''
    script = '.bat' if os.name == 'nt' else ''
    def run(*command):
        subprocess.run(list(map(str, command)), check=True, timeout=90, capture_output=True)
    with tempfile.TemporaryDirectory(prefix='mds-test-ime-') as temporary:
        tmp = Path(temporary)
        classes, dex = tmp / 'classes', tmp / 'dex'
        classes.mkdir(); dex.mkdir()
        compiled, unsigned = tmp / 'resources.zip', tmp / 'unsigned.apk'
        run(tools / ('aapt2' + executable), 'compile', '--dir', source / 'res', '-o', compiled)
        run(tools / ('aapt2' + executable), 'link', '-I', platform, '--manifest', source / 'AndroidManifest.xml',
            '-o', unsigned, compiled)
        run(java_home / ('javac' + executable), '-encoding', 'UTF-8', '-source', '8', '-target', '8',
            '-classpath', platform, '-d', classes, *source.glob('*.java'))
        run(tools / ('d8' + script), '--lib', platform, '--min-api', '33', '--output', dex,
            *classes.rglob('*.class'))
        with zipfile.ZipFile(unsigned, 'a') as apk:
            apk.write(dex / 'classes.dex', 'classes.dex')
        key = args.output.resolve() / 'test.keystore'
        if not key.exists():
            run(java_home / ('keytool' + executable), '-genkeypair', '-keystore', key, '-storepass', 'android',
                '-keypass', 'android', '-alias', 'test', '-dname', 'CN=Isolated emulator test IME',
                '-keyalg', 'RSA', '-validity', '2')
        target = args.output.resolve() / 'test-ime.apk'
        run(tools / ('apksigner' + script), 'sign', '--ks', key, '--ks-pass', 'pass:android',
            '--out', target, unsigned)
        print(target)


if __name__ == '__main__':
    main()
