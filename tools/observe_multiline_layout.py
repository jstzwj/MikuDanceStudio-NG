"""Observe original MMD multiline geometry without showing or focusing windows."""
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
    callback = callback_type(lambda hwnd, message, wp, lp: message in (0x110, 0x28))
    kernel.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]
    kernel.LoadLibraryExW.restype = w.HMODULE
    kernel.FreeLibrary.argtypes = [w.HMODULE]
    user.CreateDialogParamA.argtypes = [w.HINSTANCE, c.c_void_p, w.HWND, callback_type, w.LPARAM]
    user.CreateDialogParamA.restype = w.HWND
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.SendMessageW.restype = c.c_ssize_t
    user.SetWindowTextW.argtypes = [w.HWND, w.LPCWSTR]
    user.GetClientRect.argtypes = user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.MapWindowPoints.argtypes = [w.HWND, w.HWND, c.POINTER(w.POINT), w.UINT]
    user.IsWindowVisible.argtypes = user.DestroyWindow.argtypes = [w.HWND]
    user.GetForegroundWindow.restype = user.GetFocus.restype = w.HWND
    foreground, focus = user.GetForegroundWindow(), user.GetFocus()
    module = kernel.LoadLibraryExW(str(args.original.resolve()), None, 0x22)
    if not module:
        raise RuntimeError('Cannot map original resource data')
    records = []
    try:
        for template in (666, 665):
            dialog = user.CreateDialogParamA(module, c.c_void_p(template), None, callback, 0)
            if not dialog:
                raise RuntimeError('Cannot create hidden original dialog')
            try:
                edit = user.GetDlgItem(dialog, 668)
                assert edit and not user.IsWindowVisible(edit) and not user.IsWindowVisible(dialog)
                client, outer, formatting, origin = w.RECT(), w.RECT(), w.RECT(), w.POINT()
                assert user.GetClientRect(edit, c.byref(client))
                assert user.GetWindowRect(edit, c.byref(outer))
                user.MapWindowPoints(edit, None, c.byref(origin), 1)
                user.SendMessageW(edit, 0xB2, 0, c.addressof(formatting))
                # ASCII avoids the original ANSI multiline control's ACP loss.
                for text in ('abc def', '1.5\n-12.5\n999', '  first\n\n last', '\t1\t2\tend',
                             'x' * 90, '\n'.join('row ' + str(i) for i in range(14))):
                    assert user.SetWindowTextW(edit, text.replace('\n', '\r\n'))
                    positions = []
                    for index in range(len(text)):
                        native_index = index + text[:index].count('\n')
                        position = user.SendMessageW(edit, 0xD6, native_index, 0)
                        if position == -1:
                            continue
                        x = c.c_short(position & 0xFFFF).value + origin.x - outer.left
                        y = c.c_short((position >> 16) & 0xFFFF).value + origin.y - outer.top
                        positions.append([index, x, y])
                    records.append(dict(template=template, text=text,
                                        outer=[outer.right - outer.left, outer.bottom - outer.top],
                                        client=[client.right, client.bottom],
                                        origin=[origin.x - outer.left, origin.y - outer.top],
                                        format=[formatting.left, formatting.top,
                                                formatting.right, formatting.bottom],
                                        lines=user.SendMessageW(edit, 0xBA, 0, 0), positions=positions))
            finally:
                user.DestroyWindow(dialog)
    finally:
        kernel.FreeLibrary(module)
    if user.GetForegroundWindow() != foreground or user.GetFocus() != focus:
        raise RuntimeError('Hidden observation changed foreground/focus')
    encoded = ',\n'.join(json.dumps(record) for record in records)
    args.output.write_text('{"originalSha256": ' + json.dumps(REFERENCE_SHA256) +
                           ', "creationApi": "CreateDialogParamA", "records": [\n' +
                           encoded + '\n]}\n', encoding='utf-8')
    print(f'{len(records)} hidden original multiline layout records')


if __name__ == '__main__':
    main()
