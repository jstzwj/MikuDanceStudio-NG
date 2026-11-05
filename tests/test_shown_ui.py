"""Run the shown UI matrix with public generated model inputs on isolated CI."""
import argparse
import os
import subprocess
import tempfile

import cpu_import_fixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--test', required=True)
    parser.add_argument('--assets', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('MDS_QT_ISOLATED_DISPLAY') != '1':
        parser.error('shown UI requires an isolated GitHub runner')
    with tempfile.TemporaryDirectory(prefix='mds-shown-ui-') as root:
        cpu_import_fixture.write(root)
        subprocess.run([args.test, args.assets, args.output, root], check=True, timeout=180)


if __name__ == '__main__':
    main()
