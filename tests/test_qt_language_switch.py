"""Exercise loaded-model language changes without showing any window or popup."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import cpu_import_fixture

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test', required=True)
parser.add_argument('--assets', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='mds-language-') as temporary:
    root = Path(temporary)
    cpu_import_fixture.write(root)
    for model in ('english.pmd', 'english.pmx'):
        for production in (False, True):
            command = [args.test, args.assets, str(root / model)]
            if production:
                command.append('production')
            subprocess.run(command, check=True, timeout=45)
