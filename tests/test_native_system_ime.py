"""Qualify native Japanese input in test-owned windows on isolated CI runners."""
import argparse
import contextlib
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
        self.appkit = c.CDLL('/System/Library/Frameworks/AppKit.framework/AppKit')
        self.objc = c.CDLL('/usr/lib/libobjc.A.dylib')
        self.process = c.CDLL('/System/Library/Frameworks/ApplicationServices.framework/'
                              'Frameworks/HIServices.framework/HIServices')
        class ProcessSerialNumber(c.Structure):
            _fields_ = [('high', c.c_uint32), ('low', c.c_uint32)]
        self.ProcessSerialNumber = ProcessSerialNumber
        self.process.GetFrontProcess.argtypes = [c.POINTER(ProcessSerialNumber)]
        self.process.GetProcessPID.argtypes = [c.POINTER(ProcessSerialNumber), c.POINTER(c.c_int)]
        self.objc.objc_getClass.argtypes, self.objc.objc_getClass.restype = [c.c_char_p], c.c_void_p
        self.objc.sel_registerName.argtypes, self.objc.sel_registerName.restype = [c.c_char_p], c.c_void_p
        self.send_object = c.CFUNCTYPE(c.c_void_p, c.c_void_p, c.c_void_p)(('objc_msgSend', self.objc))
        self.send_pid = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p)(('objc_msgSend', self.objc))
        set_policy = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.c_void_p, c.c_long)(('objc_msgSend', self.objc))
        application = self.send_object(self.objc.objc_getClass(b'NSApplication'),
                                       self.objc.sel_registerName(b'sharedApplication'))
        if not set_policy(application, self.objc.sel_registerName(b'setActivationPolicy:'), 2):
            raise RuntimeError('Cannot keep the input driver out of the foreground')
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
            (self.cg, 'CGEventPost', None, [c.c_uint, c.c_void_p]),
            (self.cg, 'CGEventSetFlags', None, [c.c_void_p, c.c_uint64]),
        ]
        for library, name, result, arguments in signatures:
            function = getattr(library, name)
            function.restype, function.argtypes = result, arguments
        self.sources = self.tis.TISCreateInputSourceList(None, True)
        self.previous = self.tis.TISCopyCurrentKeyboardInputSource()
        self.available = []
        self.japanese = None
        self.japanese_method = None
        key = c.c_void_p.in_dll(self.tis, 'kTISPropertyInputSourceID')
        for index in range(self.cf.CFArrayGetCount(self.sources)):
            source = self.cf.CFArrayGetValueAtIndex(self.sources, index)
            value = self.tis.TISGetInputSourceProperty(source, key)
            buffer = c.create_string_buffer(2048)
            if value and self.cf.CFStringGetCString(value, buffer, len(buffer), 0x08000100):
                identifier = buffer.value.decode('utf-8')
                self.available.append(identifier)
                if identifier == 'com.apple.inputmethod.Kotoeri.RomajiTyping':
                    self.japanese_method = source
                if identifier == 'com.apple.inputmethod.Kotoeri.RomajiTyping.Japanese':
                    self.japanese = source

    def select(self):
        if not self.japanese:
            raise RuntimeError(f'Japanese Hiragana input mode unavailable: {self.available}')
        # Modes are selectable children, not independently enableable input
        # methods. Enable RomajiTyping before selecting its Hiragana mode.
        enabled = self.tis.TISEnableInputSource(self.japanese_method) if self.japanese_method else -1
        selected = self.tis.TISSelectInputSource(self.japanese)
        if enabled or selected:
            raise RuntimeError(f'Cannot enable/select Japanese Romaji/Hiragana: {enabled}/{selected}')

    def activate(self, command, wait):
        sequence = command('activate-ime')
        wait(lambda s: s['command'] == sequence, 'native input-mode activation')

    def key(self, pid, code):
        if not self.cg.CGPreflightPostEventAccess():
            raise RuntimeError('The isolated runner has no macOS event-posting permission')
        deadline = time.monotonic() + 2
        while True:
            # NSWorkspace caches launch/activation notifications on its run loop;
            # this Python driver has no AppKit event loop between client launches.
            # Query the live process manager instead of retaining an old client PID.
            serial = self.ProcessSerialNumber()
            current = c.c_int()
            if (self.process.GetFrontProcess(c.byref(serial)) or
                    self.process.GetProcessPID(c.byref(serial), c.byref(current))):
                raise RuntimeError('Cannot verify native foreground process ownership')
            foreground = current.value
            if foreground == pid or time.monotonic() >= deadline:
                break
            time.sleep(.025)
        if foreground != pid:
            raise RuntimeError(f'Input refused: foreground application={foreground}; client={pid}')
        for down in (True, False):
            event = self.cg.CGEventCreateKeyboardEvent(None, code, down)
            if not event:
                raise RuntimeError('Cannot create a native keyboard event')
            self.cg.CGEventSetFlags(event, 0)
            # Leave character translation to the active keyboard/input method.
            # A Unicode override can bypass its handling of physical control keys.
            # Candidate panels are owned by the native IME service. HID delivery
            # must let that service route keys instead of bypassing it by PID.
            self.cg.CGEventPost(0, event)
            self.cf.CFRelease(event)
            time.sleep(.025)
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


class WindowsInput:
    def __init__(self):
        from ctypes import wintypes as w
        self.available = ['Installed Windows Japanese TSF keyboard profiles']
        self.user = c.WinDLL('user32', use_last_error=True)

        class Keyboard(c.Structure):
            _fields_ = [('key', w.WORD), ('scan', w.WORD), ('flags', w.DWORD),
                        ('time', w.DWORD), ('extra', c.c_size_t)]

        class Mouse(c.Structure):
            _fields_ = [('x', w.LONG), ('y', w.LONG), ('data', w.DWORD),
                        ('flags', w.DWORD), ('time', w.DWORD), ('extra', c.c_size_t)]

        class Data(c.Union):
            _fields_ = [('keyboard', Keyboard), ('mouse', Mouse)]

        class Input(c.Structure):
            _fields_ = [('type', w.DWORD), ('data', Data)]

        self.Input = Input
        self.user.GetForegroundWindow.restype = w.HWND
        self.user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
        self.user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
        self.user.MapVirtualKeyW.argtypes = [w.UINT, w.UINT]
        self.user.SendInput.argtypes = [w.UINT, c.POINTER(Input), c.c_int]

    def activate(self, command, wait):
        sequence = command('activate-ime')
        wait(lambda s: s['command'] == sequence, 'native TSF profile activation')

    def key(self, pid, code):
        from ctypes import wintypes as w
        owner = w.DWORD()
        deadline = time.monotonic() + 2
        while True:
            foreground = self.user.GetForegroundWindow()
            self.user.GetWindowThreadProcessId(foreground, c.byref(owner))
            if owner.value == pid or time.monotonic() >= deadline:
                break
            # Candidate/prediction panel transitions can briefly leave no
            # foreground window. Never post input while ownership is uncertain.
            time.sleep(.025)
        if owner.value != pid:
            window_class = c.create_unicode_buffer(256)
            self.user.GetClassNameW(foreground, window_class, len(window_class))
            raise RuntimeError(f'Input refused: foreground owner={owner.value}, '
                               f'window={foreground}, class={window_class.value!r}; client={pid}')
        virtual = {45: ord('N'), 34: ord('I'), 4: ord('H'), 31: ord('O'),
                   49: 0x20, 44: 0x1C, 36: 0x0D, 53: 0x1B}[code]
        scan = self.user.MapVirtualKeyW(virtual, 0)
        for down in (True, False):
            event = self.Input()
            event.type = 1
            event.data.keyboard.scan = scan
            event.data.keyboard.key = 0 if scan else virtual
            event.data.keyboard.flags = (8 if scan else 0) | (0 if down else 2)
            if self.user.SendInput(1, c.byref(event), c.sizeof(event)) != 1:
                raise RuntimeError(f'Native key input failed: {c.get_last_error()}')
            time.sleep(.025)
        time.sleep(.12)

    def type_japanese(self, pid):
        for code in (45, 34, 4, 31, 45, 45):
            self.key(pid, code)

    def close(self):
        pass  # The client activates TSF only on its own GUI thread.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if (sys.platform not in ('darwin', 'win32') or os.environ.get('GITHUB_ACTIONS') != 'true' or
            os.environ.get('MDS_QT_ISOLATED_DISPLAY') != '1'):
        raise RuntimeError('Requires an isolated native GitHub runner; never uses a developer desktop')
    engine = 'Kotoeri' if sys.platform == 'darwin' else 'Microsoft Japanese IME'
    report = {'accepted': False, 'engine': engine, 'driverPid': os.getpid(), 'profiles': []}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    native = MacInput() if sys.platform == 'darwin' else WindowsInput()
    client = None

    def stop_client():
        if client is not None and client.poll() is None:
            client.terminate()
            client.wait(timeout=10)

    try:
        report['availableSources'] = native.available
        with tempfile.TemporaryDirectory(prefix='mds-native-ime-') as temporary, contextlib.ExitStack() as lifetime:
            # Terminate the reader before TemporaryDirectory removes Windows
            # files. A failed assertion must still leave usable diagnostics.
            lifetime.callback(stop_client)
            root = Path(temporary)
            (root / 'english.pmd').write_bytes(cpu_import_fixture.pmd())
            for language in ('en', 'ja', 'zh_CN'):
                status, request = root / 'status.json', root / 'command.json'
                status.unlink(missing_ok=True)
                request.unlink(missing_ok=True)
                sequence = 0
                env = dict(os.environ, QT_QPA_PLATFORM='cocoa' if sys.platform == 'darwin' else 'windows',
                           MDS_SYSTEM_IME='kotoeri' if sys.platform == 'darwin' else 'ms-ime',
                           MDS_IME_DIAGNOSTIC_PATH=str(args.report.with_suffix(f'.{language}.hang.dmp').resolve()),
                           QT_LOGGING_RULES='qt.qpa.input.methods=true;qt.qpa.input.keys=true')
                with args.report.with_suffix(f'.{language}.log').open('w', encoding='utf-8') as log:
                    client = subprocess.Popen([str(args.test.resolve()), str(root / 'english.pmd'),
                                               language, str(status), str(request)], env=env,
                                              stdout=log, stderr=subprocess.STDOUT)

                    def wait(predicate, label):
                        deadline = time.monotonic() + 12
                        latest = {}
                        while time.monotonic() < deadline and client.poll() is None:
                            if status.exists():
                                try:
                                    latest = json.loads(status.read_text(encoding='utf-8'))
                                except (PermissionError, FileNotFoundError):
                                    # QSaveFile replaces the snapshot atomically. A
                                    # Windows reader can race its brief rename lock.
                                    time.sleep(.025)
                                    continue
                                report['lastStatus'] = latest
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
                        deadline = time.monotonic() + 3
                        while True:
                            try:
                                staged.replace(request)
                                break
                            except PermissionError:
                                if time.monotonic() >= deadline:
                                    raise
                                time.sleep(.025)
                        return sequence

                    def finish_composition(key, commits, name, label):
                        states = []
                        # Native candidate/prediction UIs may consume one key
                        # to close a panel before committing/cancelling preedit.
                        # Never send another key after preedit has ended: that
                        # would exercise an editor command instead of the IME.
                        for _ in range(3):
                            native.key(client.pid, key)
                            time.sleep(.35)
                            state = wait(lambda s: True, label)
                            states.append(state)
                            if not state['active'] or state['name'] != name:
                                raise RuntimeError(f'IME key executed an editor command: {state}')
                            if not state['preedit']:
                                if state['commits'] != commits:
                                    raise RuntimeError(f'Unexpected native commit/cancel: {state}')
                                return state, states
                        raise RuntimeError(f'{label}: native preedit did not end: {states}')

                    wait(lambda s: s['active'], 'startup')
                    command('select')
                    wait(lambda s: s['focus'] and s['command'] == sequence, 'native focus')
                    native.activate(command, wait)
                    time.sleep(.5)
                    native.type_japanese(client.pid)
                    preedit = wait(lambda s: s['preedit'], 'native Hiragana preedit')
                    if preedit['name'] != 'before' or preedit['commits']:
                        raise RuntimeError(f'Composition executed an editor command: {preedit}')
                    # Kotoeri's default live conversion can already have selected
                    # Japan. Space then advances to another candidate (e.g. its
                    # flag emoji), rather than starting conversion as on Windows.
                    live_conversion = '\u65e5\u672c' in preedit['preedit']
                    if not live_conversion:
                        # Use the native JIS Henkan key on Windows. A prediction
                        # panel can treat Space as finalization or a full-width
                        # space; Henkan explicitly requests candidate conversion.
                        native.key(client.pid, 44 if sys.platform == 'win32' else 49)
                    converted = wait(lambda s: '\u65e5\u672c' in s['preedit'] or
                                     (s['commits'] == 1 and s['committed'] == '\u65e5\u672c'),
                                     'Japanese candidate conversion')
                    if converted['name'] != 'before' or not converted['active']:
                        raise RuntimeError(f'Conversion executed an editor command: {converted}')
                    if converted['commits']:
                        # MS-IME prediction can finalize on the conversion key.
                        # Require evidence that native preedit contained the
                        # candidate before its native commit. Never send Return
                        # after composition already ended: it would apply text.
                        inputs = [e for e in converted['events'] if e['type'] == 'input']
                        candidates = [i for i, e in enumerate(inputs) if e['preedit'] == '\u65e5\u672c']
                        commits = [i for i, e in enumerate(inputs) if e['commit'] == '\u65e5\u672c']
                        if (converted['commits'] != 1 or converted['preedit'] or
                                not candidates or not commits or candidates[0] >= commits[0]):
                            raise RuntimeError(f'Native candidate/commit order differs: {converted}')
                        committed, commit_keys = converted, []
                    else:
                        committed, commit_keys = finish_composition(36, 1, 'before', 'candidate commit')
                    if committed['field'] != '\u65e5\u672c' or committed['name'] != 'before':
                        raise RuntimeError(f'IME commit changed the scene: {committed}')
                    native.key(client.pid, 36)
                    applied = wait(lambda s: s['name'] == '\u65e5\u672c' and s['active'], 'editor Enter')
                    command('select')
                    wait(lambda s: s['command'] == sequence, 'second selection')
                    native.type_japanese(client.pid)
                    wait(lambda s: s['preedit'], 'second composition')
                    ime_return, return_keys = finish_composition(36, 2, '\u65e5\u672c', 'IME Enter')
                    if ime_return['name'] != '\u65e5\u672c' or not ime_return['active']:
                        raise RuntimeError(f'IME Enter also executed a command: {ime_return}')
                    command('select')
                    wait(lambda s: s['command'] == sequence, 'third selection')
                    native.type_japanese(client.pid)
                    wait(lambda s: s['preedit'], 'third composition')
                    cancelled, cancel_keys = finish_composition(53, 2, '\u65e5\u672c', 'IME Escape')
                    if cancelled['name'] != '\u65e5\u672c' or cancelled['commits'] != 2:
                        raise RuntimeError(f'IME cancellation changed the scene: {cancelled}')
                    report['profiles'].append({'language': language, 'preedit': preedit,
                                               'conversion': converted, 'commit': committed,
                                               'apply': applied, 'imeReturn': ime_return,
                                               'cancel': cancelled, 'candidateKeys': commit_keys,
                                               'conversionMode': 'live' if live_conversion else
                                                   'henkan' if sys.platform == 'win32' else 'space',
                                               'candidateCommitKey': 'return' if commit_keys else 'conversion',
                                               'returnKeys': return_keys, 'cancelKeys': cancel_keys})
                    command('finish')
                    try:
                        result = client.wait(timeout=10)
                    finally:
                        # Retain the final acknowledgement even when native
                        # platform teardown hangs after the scenario passed.
                        if status.exists():
                            report['shutdownStatus'] = json.loads(status.read_text(encoding='utf-8'))
                    if result:
                        raise RuntimeError(f'Input client failed: {result}')
                    client = None
            report['accepted'] = True
    finally:
        stop_client()
        native.close()
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'Native {engine} preedit, conversion, commit and cancellation passed in three UI languages')


if __name__ == '__main__':
    main()
