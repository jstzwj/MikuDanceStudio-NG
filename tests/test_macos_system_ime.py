"""Qualify Kotoeri input in test-owned windows on an isolated macOS CI runner."""
import argparse
import ctypes as c
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import cpu_import_fixture


class MacInput:
    def __init__(self):
        self.cf = c.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
        self.tis = c.CDLL('/System/Library/Frameworks/Carbon.framework/Frameworks/HIToolbox.framework/HIToolbox')
        self.cg = c.CDLL('/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics')
        signatures = [
            (self.cf, 'CFArrayGetCount', c.c_long, [c.c_void_p]),
            (self.cf, 'CFArrayGetValueAtIndex', c.c_void_p, [c.c_void_p, c.c_long]),
            (self.cf, 'CFStringGetCString', c.c_bool, [c.c_void_p, c.c_void_p, c.c_long, c.c_uint]),
            (self.cf, 'CFRelease', None, [c.c_void_p]),
            (self.tis, 'TISCreateInputSourceList', c.c_void_p, [c.c_void_p, c.c_bool]),
            (self.tis, 'TISGetInputSourceProperty', c.c_void_p, [c.c_void_p, c.c_void_p]),
            (self.tis, 'TISCopyCurrentKeyboardInputSource', c.c_void_p, []),
            (self.tis, 'TISEnableInputSource', c.c_int, [c.c_void_p]),
            (self.tis, 'TISSelectInputSource', c.c_int, [c.c_void_p]),
            (self.cg, 'CGPreflightPostEventAccess', c.c_bool, []),
            (self.cg, 'CGEventCreateKeyboardEvent', c.c_void_p, [c.c_void_p, c.c_ushort, c.c_bool]),
            (self.cg, 'CGEventPostToPid', None, [c.c_int, c.c_void_p]),
        ]
        for library, name, result, arguments in signatures:
            function = getattr(library, name)
            function.restype, function.argtypes = result, arguments
        self.sources = self.tis.TISCreateInputSourceList(None, True)
        self.previous = self.tis.TISCopyCurrentKeyboardInputSource()
        self.available = []
        self.japanese = None
        key = c.c_void_p.in_dll(self.tis, 'kTISPropertyInputSourceID')
        for index in range(self.cf.CFArrayGetCount(self.sources)):
            source = self.cf.CFArrayGetValueAtIndex(self.sources, index)
            value = self.tis.TISGetInputSourceProperty(source, key)
            buffer = c.create_string_buffer(2048)
            if value and self.cf.CFStringGetCString(value, buffer, len(buffer), 0x08000100):
                identifier = buffer.value.decode('utf-8')
                self.available.append(identifier)
                if 'Kotoeri' in identifier and identifier.endswith('.Japanese'):
                    self.japanese = source

    def select(self):
        if not self.japanese:
            raise RuntimeError(f'Japanese Hiragana input mode unavailable: {self.available}')
        if self.tis.TISEnableInputSource(self.japanese) or self.tis.TISSelectInputSource(self.japanese):
            raise RuntimeError('Cannot enable/select the installed Japanese input mode')

    def key(self, pid, code):
        if not self.cg.CGPreflightPostEventAccess():
            raise RuntimeError('The isolated runner has no macOS event-posting permission')
        for down in (True, False):
            event = self.cg.CGEventCreateKeyboardEvent(None, code, down)
            if not event:
                raise RuntimeError('Cannot create a native keyboard event')
            self.cg.CGEventPostToPid(pid, event)
            self.cf.CFRelease(event)
        time.sleep(.12)

    def type_japanese(self, pid):
        for code in (45, 34, 4, 31, 45, 45):  # physical US keys: nihonn
            self.key(pid, code)

    def close(self):
        if self.previous:
            self.tis.TISSelectInputSource(self.previous)
            self.cf.CFRelease(self.previous)
        if self.sources:
            self.cf.CFRelease(self.sources)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if (sys.platform != 'darwin' or os.environ.get('GITHUB_ACTIONS') != 'true' or
            os.environ.get('MDS_QT_ISOLATED_DISPLAY') != '1'):
        raise RuntimeError('Requires an isolated macOS GitHub runner; never uses a developer desktop')
    report = {'accepted': False, 'engine': 'Kotoeri', 'profiles': []}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    native = MacInput()
    client = None
    try:
        report['availableSources'] = native.available
        with tempfile.TemporaryDirectory(prefix='mds-kotoeri-') as temporary:
            root = Path(temporary)
            (root / 'english.pmd').write_bytes(cpu_import_fixture.pmd())
            for language in ('en', 'ja', 'zh_CN'):
                status, request = root / 'status.json', root / 'command.json'
                status.unlink(missing_ok=True)
                request.unlink(missing_ok=True)
                sequence = 0
                env = dict(os.environ, QT_QPA_PLATFORM='cocoa', MDS_SYSTEM_IME='kotoeri')
                with args.report.with_suffix(f'.{language}.log').open('w', encoding='utf-8') as log:
                    client = subprocess.Popen([str(args.test.resolve()), str(root / 'english.pmd'),
                                               language, str(status), str(request)], env=env,
                                              stdout=log, stderr=subprocess.STDOUT)

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

                    wait(lambda s: s['active'], 'startup')
                    command('select')
                    wait(lambda s: s['focus'] and s['command'] == sequence, 'native focus')
                    native.select()
                    time.sleep(.5)
                    native.type_japanese(client.pid)
                    preedit = wait(lambda s: s['preedit'], 'native Hiragana preedit')
                    if preedit['name'] != 'before' or preedit['commits']:
                        raise RuntimeError(f'Composition executed an editor command: {preedit}')
                    native.key(client.pid, 49)  # Space: candidate conversion
                    converted = wait(lambda s: '\u65e5\u672c' in s['preedit'], 'Japanese candidate conversion')
                    native.key(client.pid, 36)  # Return: IME commit
                    committed = wait(lambda s: s['commits'] == 1 and not s['preedit'], 'candidate commit')
                    if committed['field'] != '\u65e5\u672c' or committed['name'] != 'before':
                        raise RuntimeError(f'IME commit changed the scene: {committed}')
                    native.key(client.pid, 36)
                    applied = wait(lambda s: s['name'] == '\u65e5\u672c' and s['active'], 'editor Enter')
                    command('select')
                    wait(lambda s: s['command'] == sequence, 'second selection')
                    native.type_japanese(client.pid)
                    wait(lambda s: s['preedit'], 'second composition')
                    native.key(client.pid, 36)
                    ime_return = wait(lambda s: s['commits'] == 2 and not s['preedit'], 'IME Enter')
                    if ime_return['name'] != '\u65e5\u672c' or not ime_return['active']:
                        raise RuntimeError(f'IME Enter also executed a command: {ime_return}')
                    command('select')
                    wait(lambda s: s['command'] == sequence, 'third selection')
                    native.type_japanese(client.pid)
                    wait(lambda s: s['preedit'], 'third composition')
                    native.key(client.pid, 53)  # Escape: cancel composition
                    cancelled = wait(lambda s: not s['preedit'] and s['active'], 'IME Escape')
                    if cancelled['name'] != '\u65e5\u672c' or cancelled['commits'] != 2:
                        raise RuntimeError(f'IME cancellation changed the scene: {cancelled}')
                    report['profiles'].append({'language': language, 'preedit': preedit,
                                               'conversion': converted, 'commit': committed,
                                               'apply': applied, 'imeReturn': ime_return,
                                               'cancel': cancelled})
                    command('finish')
                    if client.wait(timeout=10):
                        raise RuntimeError('Input client failed')
                    client = None
            report['accepted'] = True
    finally:
        if client is not None and client.poll() is None:
            client.terminate()
            client.wait(timeout=10)
        native.close()
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Native Kotoeri preedit, conversion, commit and cancellation passed in three UI languages')


if __name__ == '__main__':
    main()
