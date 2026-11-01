"""CPU editor preparation only; no native window, GPU or audio device."""
import argparse
import subprocess
import tempfile
from pathlib import Path
import cpu_import_fixture

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--assets', required=True)
parser.add_argument('--report', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='mds-editor-performance-') as root:
    cpu_import_fixture.write(root)
    model = Path(root) / 'performance.pmx'
    model.write_bytes(cpu_import_fixture.pmx(copies=2730))
    subprocess.run([args.test, str(model), args.assets, args.report], check=True, timeout=60)
