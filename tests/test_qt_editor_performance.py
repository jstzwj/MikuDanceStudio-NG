"""Offscreen CPU preparation, optionally including native GPU completion."""
import argparse
import subprocess
import tempfile
from pathlib import Path
import cpu_import_fixture

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--assets', required=True)
parser.add_argument('--report', required=True)
parser.add_argument('--gpu', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='mds-editor-performance-') as root:
    cpu_import_fixture.write(root)
    model = Path(root) / 'performance.pmx'
    model.write_bytes(cpu_import_fixture.pmx(copies=2730, tile_copies=args.gpu))
    command = [args.test, str(model), args.assets, args.report]
    if args.gpu:
        command.append('--gpu')
    subprocess.run(command, check=True, timeout=180 if args.gpu else 60)
