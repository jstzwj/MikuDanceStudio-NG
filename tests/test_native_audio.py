"""Explicit silent native-device acceptance; unavailable is never a pass."""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--report', required=True)
parser.add_argument('--assets', default='res/assets')
parser.add_argument('--seconds', type=int, default=120)
parser.add_argument('--allow-unavailable', action='store_true')
args = parser.parse_args()
report = Path(args.report)
report.parent.mkdir(parents=True, exist_ok=True)
report.unlink(missing_ok=True)
environment = dict(os.environ, QT_QPA_PLATFORM='offscreen')
result = subprocess.run([
    args.test, '--native-silent', '--seconds', str(args.seconds),
    '--assets', str(Path(args.assets).resolve()), '--report', str(report.resolve()),
], env=environment, timeout=args.seconds + 45, check=False)
if not report.is_file():
    raise SystemExit('Native audio test did not produce a report')
data = json.loads(report.read_text(encoding='utf-8'))
if result.returncode == 77 and data['status'] == 'unavailable' and args.allow_unavailable:
    print('::warning::Native audio acceptance UNAVAILABLE: no output device. '
          'This platform has not passed native audio qualification.', flush=True)
elif result.returncode != 0 or data['status'] != 'passed':
    raise SystemExit(f'Native audio acceptance failed ({result.returncode}): {data}')
