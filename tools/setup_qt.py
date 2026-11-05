"""Install the same shared Qt SDK/modules as the portable editor CI."""
import argparse
from pathlib import Path
import platform
import subprocess
import sys

VERSION = '6.8.3'
HOSTS = {'Windows': ('windows', 'win64_msvc2022_64'),
         'Linux': ('linux', 'linux_gcc_64'), 'Darwin': ('mac', 'clang_64')}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('out/qt'))
    parser.add_argument('--target', choices=('desktop', 'android'), default='desktop')
    parser.add_argument('--arch', choices=('native', 'x64', 'arm64'), default='native')
    args = parser.parse_args()
    host, arch = HOSTS[platform.system()]
    selected = platform.machine().lower() if args.arch == 'native' else args.arch
    if host == 'windows' and selected in ('arm64', 'aarch64'):
        arch = 'win64_msvc2022_arm64'
    if args.target == 'android':
        if host != 'linux':
            parser.error('The reproducible Android build profile currently uses a Linux x64 host')
        for target, sdk_arch in (('desktop', 'linux_gcc_64'), ('android', 'android_arm64_v8a'),
                                 ('android', 'android_x86_64')):
            subprocess.run([sys.executable, '-m', 'aqt', 'install-qt', host, target, VERSION,
                            sdk_arch, '--outputdir', str(args.output.resolve()), '-m', 'qtmultimedia'], check=True)
        return
    modules = ['qtmultimedia'] + (['qtwaylandcompositor'] if host == 'linux' else [])
    subprocess.run([sys.executable, '-m', 'aqt', 'install-qt', host, 'desktop', VERSION,
                    arch, '--outputdir', str(args.output.resolve()), '-m', *modules], check=True)

if __name__ == '__main__':
    main()
