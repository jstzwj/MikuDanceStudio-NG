"""Fetch the unmodified OFL Noto CJK collection used by the Qt distribution."""
import argparse
import hashlib
from pathlib import Path
import shutil
import tempfile
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    target = args.output / 'NotoSansCJK-Regular.ttc'
    expected = 'b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a'
    if target.is_file():
        if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Existing Noto CJK file has an unexpected hash: {target}')
        return
    url = ('https://raw.githubusercontent.com/notofonts/noto-cjk/'
           '523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/OTC/NotoSansCJK-Regular.ttc')
    with tempfile.TemporaryDirectory(prefix='noto-download-', dir=args.output) as temporary:
        staged = Path(temporary) / target.name
        with urllib.request.urlopen(url, timeout=60) as response, staged.open('wb') as output:
            shutil.copyfileobj(response, output)
        if hashlib.sha256(staged.read_bytes()).hexdigest() != expected:
            raise RuntimeError('Downloaded Noto CJK collection failed SHA-256 verification')
        staged.replace(target)
    print(f'Pinned Noto CJK collection: {target}')


if __name__ == '__main__':
    main()
