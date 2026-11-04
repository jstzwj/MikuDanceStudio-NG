"""Isolate OS Japanese IME shutdown from QApplication, on a disposable runner only."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from test_native_system_ime import WindowsInput


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if (sys.platform != 'win32' or os.environ.get('GITHUB_ACTIONS') != 'true' or
            os.environ.get('MDS_QT_ISOLATED_DISPLAY') != '1'):
        raise RuntimeError('Requires isolated Windows CI; never uses a developer desktop')
    report = {'accepted': False, 'scope': 'OS reference only; not editor qualification'}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    native = WindowsInput()
    client = None
    try:
        with tempfile.TemporaryDirectory(prefix='mds-native-shutdown-') as temporary:
            status, request = Path(temporary) / 'status.json', Path(temporary) / 'command.json'
            environment = dict(os.environ, MDS_IME_DIAGNOSTIC_PATH=str(
                args.report.with_suffix('.hang.dmp').resolve()))
            with args.report.with_suffix('.log').open('w', encoding='utf-8') as log:
                client = subprocess.Popen([str(args.test.resolve()), str(status), str(request)],
                                          stdout=log, stderr=subprocess.STDOUT, env=environment)
                sequence = 0

                def wait(predicate, label, timeout=15):
                    deadline = time.monotonic() + timeout
                    while client.poll() is None and time.monotonic() < deadline:
                        try:
                            state = json.loads(status.read_text(encoding='utf-8'))
                        except (PermissionError, FileNotFoundError):
                            time.sleep(.025)
                            continue
                        report['lastStatus'] = state
                        if state['error']:
                            raise RuntimeError(state['error'])
                        if predicate(state):
                            return state
                        time.sleep(.025)
                    raise RuntimeError(f'{label}: exit={client.poll()} status={report.get("lastStatus")}')

                def command(action):
                    nonlocal sequence
                    sequence += 1
                    staged = request.with_suffix('.tmp')
                    staged.write_text(json.dumps({'sequence': sequence, 'action': action}), encoding='utf-8')
                    deadline = time.monotonic() + 3
                    while True:
                        try:
                            staged.replace(request)
                            return sequence
                        except PermissionError:
                            if time.monotonic() >= deadline:
                                raise
                            time.sleep(.025)

                try:
                    wait(lambda s: s['focus'], 'native EDIT startup', 60)
                    command('select')
                    wait(lambda s: s['command'] == sequence, 'native selection')
                    native.activate(command, wait)
                    native.type_japanese(client.pid)
                    report['composition'] = wait(lambda s: bool(s['preedit']), 'native preedit')
                    native.key(client.pid, 44)
                    report['conversion'] = wait(
                        lambda s: '\u65e5\u672c' in s['preedit'] or s['committed'] == '\u65e5\u672c',
                        'native candidate conversion')
                    for _ in range(3):
                        state = wait(lambda s: True, 'native composition state')
                        if not state['preedit']:
                            break
                        native.key(client.pid, 36)
                        time.sleep(.35)
                    report['commit'] = wait(lambda s: not s['preedit'] and s['commits'] == 1,
                                            'native candidate commit')
                    command('select')
                    wait(lambda s: s['command'] == sequence, 'native second selection')
                    native.type_japanese(client.pid)
                    wait(lambda s: bool(s['preedit']), 'native second preedit')
                    for _ in range(3):
                        native.key(client.pid, 53)
                        time.sleep(.35)
                        if not wait(lambda s: True, 'native cancel state')['preedit']:
                            break
                    report['cancel'] = wait(lambda s: not s['preedit'] and s['commits'] == 1,
                                            'native cancellation')
                    command('finish')
                    report['exitCode'] = client.wait(timeout=10)
                    if report['exitCode']:
                        raise RuntimeError(f'Native reference exit={report["exitCode"]}')
                    report['accepted'] = True
                finally:
                    # Clean up even when teardown blocks, before deleting IPC files.
                    # Forced cleanup never qualifies as natural shutdown.
                    if client.poll() is None:
                        client.terminate()
                        client.wait(timeout=10)
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        native.close()
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
