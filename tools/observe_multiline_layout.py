"""Observe original MMD multiline geometry without showing or focusing windows."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path

from hidden_dialog_resource import create_hidden_dialog, hidden_input_guard

from observe_edit_navigation import REFERENCE_SHA256


@hidden_input_guard
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--hit-test', action='store_true',
                        help='freeze read-only EM_CHARFROMPOS samples in the client rectangle')
    args = parser.parse_args()
    if hashlib.sha256(args.original.read_bytes()).hexdigest() != REFERENCE_SHA256:
        raise RuntimeError('Unexpected original executable')
    user, kernel = c.WinDLL('user32'), c.WinDLL('kernel32')
    callback_type = c.WINFUNCTYPE(c.c_ssize_t, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
    callback = callback_type(lambda hwnd, message, wp, lp: message == 0x28)
    kernel.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]
    kernel.LoadLibraryExW.restype = w.HMODULE
    kernel.FreeLibrary.argtypes = [w.HMODULE]
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.SendMessageW.restype = c.c_ssize_t
    user.SetWindowTextW.argtypes = [w.HWND, w.LPCWSTR]
    user.GetClientRect.argtypes = user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.MapWindowPoints.argtypes = [w.HWND, w.HWND, c.POINTER(w.POINT), w.UINT]
    user.IsWindowVisible.argtypes = user.DestroyWindow.argtypes = [w.HWND]
    user.GetForegroundWindow.restype = user.GetFocus.restype = w.HWND
    focus = user.GetFocus()
    module = kernel.LoadLibraryExW(str(args.original.resolve()), None, 0x22)
    if not module:
        raise RuntimeError('Cannot map original resource data')
    records = []
    try:
        for template in (666, 665):
            dialog = create_hidden_dialog(user, kernel, module, template, callback)
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
                    record = dict(template=template, text=text,
                                  outer=[outer.right - outer.left, outer.bottom - outer.top],
                                  client=[client.right, client.bottom],
                                  origin=[origin.x - outer.left, origin.y - outer.top],
                                  format=[formatting.left, formatting.top,
                                          formatting.right, formatting.bottom],
                                  lines=user.SendMessageW(edit, 0xBA, 0, 0), positions=positions)
                    if args.hit_test:
                        native_text = text.replace('\n', '\r\n')
                        points = []
                        for y in range(0, client.bottom, 3):
                            for x in range(0, client.right, 3):
                                result = user.SendMessageW(edit, 0xD7, 0, x | (y << 16))
                                index = result & 0xFFFF
                                points.append([x, y, index - native_text[:index].count('\r')])
                        record['hitTest'] = points
                        del record['positions']
                    records.append(record)
            finally:
                user.DestroyWindow(dialog)
    finally:
        kernel.FreeLibrary(module)
    process = w.DWORD()
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    kernel.GetCurrentProcessId.restype = w.DWORD
    current_foreground = user.GetForegroundWindow()
    if current_foreground:
        user.GetWindowThreadProcessId(current_foreground, c.byref(process))
    if process.value == kernel.GetCurrentProcessId() or user.GetFocus() != focus:
        raise RuntimeError('Hidden observation activated its own window or changed thread focus')
    encoded = ',\n'.join(json.dumps(record) for record in records)
    args.output.write_text('{"originalSha256": ' + json.dumps(REFERENCE_SHA256) +
                           ', "creationApi": "CreateDialogIndirectParamA; outer WS_EX_NOACTIVATE", "records": [\n' +
                           encoded + '\n]}\n', encoding='utf-8')
    print(f'{len(records)} hidden original multiline layout records')


if __name__ == '__main__':
    main()
