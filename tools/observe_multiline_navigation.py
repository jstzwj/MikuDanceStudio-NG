"""Freeze original EDIT selection/page behavior; no shown window or desktop input."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path

from observe_edit_navigation import REFERENCE_SHA256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.original.read_bytes()).hexdigest() != REFERENCE_SHA256:
        raise RuntimeError('Unexpected original executable')
    user, kernel = c.WinDLL('user32'), c.WinDLL('kernel32')
    callback_type = c.WINFUNCTYPE(c.c_ssize_t, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
    callback = callback_type(lambda hwnd, msg, wp, lp: msg in (0x110, 0x28))
    kernel.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]
    kernel.LoadLibraryExW.restype = w.HMODULE
    kernel.FreeLibrary.argtypes = [w.HMODULE]
    user.CreateDialogParamA.argtypes = [w.HINSTANCE, c.c_void_p, w.HWND, callback_type, w.LPARAM]
    user.CreateDialogParamA.restype = w.HWND
    user.GetDlgItem.argtypes, user.GetDlgItem.restype = [w.HWND, c.c_int], w.HWND
    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.SendMessageW.restype = c.c_ssize_t
    user.SetWindowTextW.argtypes = [w.HWND, w.LPCWSTR]
    user.DestroyWindow.argtypes = user.IsWindowVisible.argtypes = [w.HWND]
    user.GetForegroundWindow.restype = user.GetFocus.restype = w.HWND
    keyboard = (c.c_ubyte * 256)()
    assert user.GetKeyboardState(keyboard)
    foreground, focus = user.GetForegroundWindow(), user.GetFocus()
    module = kernel.LoadLibraryExW(str(args.original.resolve()), None, 0x22)
    if not module:
        raise RuntimeError('Cannot map original resources')
    text = '\n'.join('row%02d abc def %s' % (row, 'x' * 40) for row in range(16))
    native_text = text.replace('\n', '\r\n')
    records = []
    try:
        for template in (666, 665):
            dialog = user.CreateDialogParamA(module, c.c_void_p(template), None, callback, 0)
            assert dialog and not user.IsWindowVisible(dialog)
            edit = user.GetDlgItem(dialog, 668)
            assert edit and not user.IsWindowVisible(edit)
            try:
                for row in (0, 4, 9, 15):
                    position = sum(len(line) + 1 for line in text.split('\n')[:row]) + 8
                    for key in (0x26, 0x28, 0x21, 0x22, 0x24, 0x23):
                        for control, shift in ((False, False), (True, False), (False, True), (True, True)):
                            for offset in (-3, 0, 3):
                                assert user.SetWindowTextW(edit, native_text)
                                native_position = position + row
                                user.SendMessageW(edit, 0xB1, native_position + offset, native_position)
                                # Explicitly scroll the unfocused control. Every
                                # start caret is visible; EM_SCROLLCARET itself
                                # is inactive when the EDIT does not own focus.
                                user.SendMessageW(edit, 0xB6, 0, max(0, row - 4))
                                before = user.SendMessageW(edit, 0xCE, 0, 0)
                                keys = (c.c_ubyte * 256)()
                                keys[0x11], keys[0x10] = (0x80 if control else 0), (0x80 if shift else 0)
                                assert user.SetKeyboardState(keys)
                                user.SendMessageW(edit, 0x100, key, 1)
                                user.SendMessageW(edit, 0x101, key, 0xC0000001)
                                first, last = w.DWORD(), w.DWORD()
                                user.SendMessageW(edit, 0xB0, c.addressof(first), c.addressof(last))
                                def portable(index):
                                    return index - native_text[:index].count('\r')
                                records.append(dict(template=template, position=position,
                                                    anchor=position + offset, key=key,
                                                    ctrl=control, shift=shift,
                                                    selection=[portable(first.value), portable(last.value)],
                                                    before=before,
                                                    firstVisible=user.SendMessageW(edit, 0xCE, 0, 0)))
            finally:
                user.DestroyWindow(dialog)
    finally:
        assert user.SetKeyboardState(keyboard)
        kernel.FreeLibrary(module)
    if user.GetForegroundWindow() != foreground or user.GetFocus() != focus:
        raise RuntimeError('Hidden observation changed foreground/focus')
    header = dict(originalSha256=REFERENCE_SHA256, creationApi='CreateDialogParamA',
                  scope='selection and explicit page scroll; not focus-driven scroll/caret paint',
                  text=text)
    encoded = json.dumps(header, ensure_ascii=False, indent=2)[:-2]
    encoded += ',\n  "records": [\n'
    encoded += ',\n'.join('    ' + json.dumps(record) for record in records) + '\n  ]\n}\n'
    args.output.write_text(encoded, encoding='utf-8')
    print(f'{len(records)} original multiline selection/page cases; never shown')


if __name__ == '__main__':
    main()
