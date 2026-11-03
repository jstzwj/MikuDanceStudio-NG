"""Qualify real Qt IBus/libpinyin input inside a private CI X11/DBus session."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

import cpu_import_fixture


def run(*command, env=None):
    return subprocess.run(command, check=True, text=True, encoding='utf-8',
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15, env=env).stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if (os.environ.get('GITHUB_ACTIONS') != 'true' or
            os.environ.get('MDS_QT_ISOLATED_DISPLAY') != '1' or
            not os.environ.get('DISPLAY') or not os.environ.get('DBUS_SESSION_BUS_ADDRESS')):
        raise RuntimeError('Requires isolated CI Xvfb and private DBus; never uses a developer display')
    env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_IM_MODULE='ibus',
               XMODIFIERS='@im=ibus')
    env.pop('IBUS_USE_PORTAL', None)
    report_path = args.report.resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    evidence = {'accepted': False, 'engine': 'libpinyin', 'profiles': []}
    with tempfile.TemporaryDirectory(prefix='mds-real-ime-') as temporary:
        root = Path(temporary)
        # Bypass desktop/portal discovery. The daemon, engine selector and Qt
        # client must connect to this test's socket on the private session bus.
        env['IBUS_ADDRESS'] = 'unix:path=' + str(root / 'ibus.socket')
        (root / 'english.pmd').write_bytes(cpu_import_fixture.pmd())
        with (report_path.with_suffix('.ibus.log')).open('w', encoding='utf-8') as log:
            daemon = subprocess.Popen(['ibus-daemon', '--replace', '--xim', '--panel=disable',
                                       '--address=' + env['IBUS_ADDRESS']],
                                      env=env, stdout=log, stderr=subprocess.STDOUT)
            client = None
            try:
                deadline = time.monotonic() + 15
                while True:
                    result = subprocess.run(['ibus', 'list-engine'], env=env,
                                            capture_output=True, text=True, timeout=5)
                    if result.returncode == 0 and 'libpinyin' in result.stdout:
                        break
                    if time.monotonic() >= deadline or daemon.poll() is not None:
                        raise RuntimeError(f'IBus/libpinyin unavailable ({result.returncode}): {result.stdout} {result.stderr}')
                    time.sleep(.1)
                for language in ('en', 'ja', 'zh_CN'):
                    status, request = root / 'status.json', root / 'command.json'
                    status.unlink(missing_ok=True)
                    request.unlink(missing_ok=True)
                    sequence = 0
                    with report_path.with_suffix(f'.{language}.log').open('w', encoding='utf-8') as client_log:
                        client = subprocess.Popen([str(args.test.resolve()), str(root / 'english.pmd'),
                                                   language, str(status), str(request)], env=env,
                                                  stdout=client_log, stderr=subprocess.STDOUT)

                        def wait(predicate, label):
                            deadline = time.monotonic() + 12
                            latest = {}
                            while time.monotonic() < deadline and client.poll() is None:
                                if status.exists():
                                    latest = json.loads(status.read_text(encoding='utf-8'))
                                    if latest.get('error') or not latest['sceneUnchanged']:
                                        raise RuntimeError(f'{language}: scene changed: {latest}')
                                    if predicate(latest):
                                        return latest
                                time.sleep(.025)
                            raise RuntimeError(f'{language}: {label}; exit={client.poll()}, status={latest}')

                        def command(action):
                            nonlocal sequence
                            sequence += 1
                            staged = request.with_suffix('.tmp')
                            staged.write_text(json.dumps({'sequence': sequence, 'action': action}), encoding='utf-8')
                            staged.replace(request)

                        wait(lambda s: s['active'], 'dialog startup')
                        window = run('xdotool', 'search', '--onlyvisible', '--name', f'^MDS-IBus-{language}$').splitlines()[-1]
                        run('xdotool', 'windowfocus', '--sync', window)
                        run('ibus', 'engine', 'libpinyin', env=env)
                        command('select')
                        wait(lambda s: s['focus'] and s['command'] == sequence, 'native focus')
                        run('xdotool', 'type', '--clearmodifiers', '--delay', '100', 'riben')
                        composing = wait(lambda s: s['preedit'], 'real platform preedit')
                        if composing['name'] != 'before' or composing['commits']:
                            raise RuntimeError(f'Preedit applied an editor command: {composing}')
                        run('xdotool', 'key', 'space')
                        committed = wait(lambda s: s['commits'] == 1 and not s['preedit'], 'candidate commit')
                        if committed['field'] != '\u65e5\u672c' or committed['name'] != 'before':
                            raise RuntimeError(f'Candidate commit/order differs: {committed}')
                        run('xdotool', 'key', 'Return')
                        applied = wait(lambda s: s['name'] == '\u65e5\u672c' and s['active'], 'editor Enter after candidate')
                        command('select')
                        wait(lambda s: s['command'] == sequence, 'second selection')
                        run('xdotool', 'type', '--clearmodifiers', '--delay', '100', 'riben')
                        wait(lambda s: s['preedit'], 'second composition')
                        run('xdotool', 'key', 'Return')
                        ime_return = wait(lambda s: s['commits'] == 2 and not s['preedit'], 'IME Enter commit')
                        if ime_return['name'] != '\u65e5\u672c' or not ime_return['active']:
                            raise RuntimeError(f'IME Enter also executed an editor command: {ime_return}')
                        command('select')
                        wait(lambda s: s['command'] == sequence, 'third selection')
                        run('xdotool', 'type', '--clearmodifiers', '--delay', '100', 'riben')
                        wait(lambda s: s['preedit'], 'third composition')
                        run('xdotool', 'key', 'Escape')
                        cancelled = wait(lambda s: not s['preedit'] and s['active'], 'IME Escape without closing dialog')
                        if cancelled['name'] != '\u65e5\u672c' or cancelled['commits'] != 2:
                            raise RuntimeError(f'Cancellation altered the scene: {cancelled}')
                        evidence['profiles'].append({'language': language, 'preedit': composing,
                                                     'commit': committed, 'apply': applied,
                                                     'imeReturn': ime_return,
                                                     'cancel': cancelled})
                        command('finish')
                        if client.wait(timeout=10):
                            raise RuntimeError(f'{language}: input client failed')
                        client = None
                evidence['accepted'] = True
            finally:
                if client is not None and client.poll() is None:
                    client.terminate()
                    client.wait(timeout=10)
                daemon.terminate()
                daemon.wait(timeout=10)
                report_path.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('Real IBus/libpinyin preedit, commit, command ordering and cancellation passed in three UI languages')


if __name__ == '__main__':
    main()
