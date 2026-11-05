"""Validate NG versions and tested release packages; never build legacy DX9."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from release_contract import version, verify_packages


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', help='Require this exact vMAJOR.MINOR.PATCH tag')
    parser.add_argument('--packages', type=Path, help='Validate all release archives')
    args = parser.parse_args()
    value = version((ROOT / 'VERSION').read_text(encoding='utf-8').strip())
    if args.tag is not None and args.tag != f'v{value}':
        parser.error(f'Tag {args.tag!r} does not match VERSION (v{value})')
    if args.packages:
        if not args.tag:
            parser.error('--packages requires --tag')
        commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        manifest = verify_packages(args.packages, args.tag, commit)
        print(f'Qualified {len(manifest["packages"])} packages for {args.tag} ({commit})')
    else:
        print(value)


if __name__ == '__main__':
    main()
