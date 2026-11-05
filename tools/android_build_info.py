"""Embed immutable source/build identity in Qt's Android package assets."""
import argparse
import json
from pathlib import Path
import subprocess
from release_contract import PRODUCT, version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--abi', choices=('arm64-v8a', 'x86_64'), required=True)
    parser.add_argument('--qt', required=True)
    parser.add_argument('--abis', nargs='+', default=[])
    parser.add_argument('--tag', default='')
    args = parser.parse_args()
    value = version((args.source / 'VERSION').read_text().strip())
    def git(*arguments):
        return subprocess.check_output(['git', '-C', str(args.source), *arguments], text=True).strip()
    commit = git('rev-parse', 'HEAD')
    modified = bool(git('status', '--porcelain', '--untracked-files=no'))
    if args.tag and (args.tag != 'v' + value or modified or
                     git('rev-parse', f'refs/tags/{args.tag}^{{commit}}') != commit):
        raise ValueError('Android release requires an unmodified checkout of the matching version tag')
    info = dict(schema=1, product=PRODUCT, format='apk', version=value, system='Android', qt=args.qt,
                arch='universal' if len(args.abis) > 1 else ('arm64' if args.abi == 'arm64-v8a' else 'x64'),
                abis=args.abis or [args.abi], commit=commit,
                tag=args.tag or None, modified=modified, development=not bool(args.tag),
                vulkan=True, metal=False, runtimeShaderCompiler=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
