"""Install the same shared Qt SDK/modules as the portable editor CI."""
import argparse
from pathlib import Path
import platform
import subprocess
import sys

VERSION = '6.8.3'
HOSTS = {'Windows': ('windows', 'win64_msvc2022_64'),
         'Linux': ('linux', 'gcc_64'), 'Darwin': ('mac', 'clang_64')}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('out/qt'))
    args = parser.parse_args()
    host, arch = HOSTS[platform.system()]
    modules = ['qtmultimedia'] + (['qtwaylandcompositor'] if host == 'linux' else [])
    subprocess.run([sys.executable, '-m', 'aqt', 'install-qt', host, 'desktop', VERSION,
                    arch, '--outputdir', str(args.output.resolve()), '-m', *modules], check=True)

if __name__ == '__main__':
    main()
