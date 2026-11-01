"""Qt menus versus original binary resources; never opens a visible window."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import cpu_import_fixture

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import editor_menu

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--assets', required=True)
args = parser.parse_args()
editor_menu.run(check=True)
with tempfile.TemporaryDirectory(prefix='mds-menu-') as temporary:
    root = Path(temporary)
    cpu_import_fixture.write(root)
    reference = root / 'original-menus.json'
    reference.write_text(json.dumps(editor_menu.reference_menus(), ensure_ascii=False), encoding='utf-8')
    subprocess.run([args.test, str(root), args.assets, str(reference)], check=True, timeout=60)
