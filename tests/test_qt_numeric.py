"""Compare never-desktop numeric dialogs with original binary resources."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import cpu_import_fixture
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import editor_dialogs

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--assets', required=True)
args = parser.parse_args()
editor_dialogs.run(check=True)
with tempfile.TemporaryDirectory(prefix='mds-numeric-') as temporary:
    root = Path(temporary)
    cpu_import_fixture.write(root)
    references = {kind + '.' + language: value
                  for (kind, language), value in editor_dialogs.reference_dialogs().items()}
    path = root / 'numeric-reference.json'
    path.write_text(json.dumps(references, ensure_ascii=False), encoding='utf-8')
    subprocess.run([args.test, str(root), args.assets, str(path)], check=True, timeout=60)
