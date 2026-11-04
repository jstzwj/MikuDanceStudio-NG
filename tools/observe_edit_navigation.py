"""Freeze the original dialog EDIT's keyboard behavior, without showing windows.

Only resource data is mapped. No original code, desktop input, foreground change,
or read-only native edit rejection (which may beep) is used.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path

REFERENCE_SHA256 = "07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1"
TEXTS = [
    "", "abc def", "a.b,c-d_e", "a  b\tc", "  a  b  ", "1.5", "1,234.56",
    "a/b/c", "ab\\cd", "a+2", "1+a", "a$2", "a%2", "a(b)c", "a[b]c", "a{b}c",
    "a--b", "a..b", "hello, world!", "a\u00a0b",
    " a /b\\c;d:e+f=g!h?i(j)k[l]m{n}o@p#q$r%s^t&u*v_w-z",
    "\u65e5\u672c\u8a9eabc \u4e2d\u56fd", "\u4e2d\u6587\u6d4b\u8bd5",
    "\u3042\u3044\u30a2\u30a4\u6f22\u5b57\uff71\uff72\u3001\u3002",
    "\u300c\u65e5\u672c\u300d\u4e2d\u56fd", "a\u0301b \U0001f600c",
]
TEXTS += [left + punctuation + right
          for punctuation in '.,;:-_/\\+=$%*!?()[]{}@#^&"\''
          for left, right in (("ab", "cd"), ("12", "34"), ("ab", "12"), ("12", "cd"))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--multiline", action="store_true")
    args = parser.parse_args()
    if hashlib.sha256(args.original.read_bytes()).hexdigest() != REFERENCE_SHA256:
        raise RuntimeError("Unexpected original executable")
    user, kernel = c.WinDLL("user32"), c.WinDLL("kernel32")
    callback_type = c.WINFUNCTYPE(c.c_ssize_t, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
    # A hidden dialog must not give focus to any of its controls.
    callback = callback_type(lambda hwnd, msg, wp, lp: msg in (0x110, 0x28))
    kernel.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]
    kernel.LoadLibraryExW.restype = w.HMODULE
    kernel.FreeLibrary.argtypes = [w.HMODULE]
    user.CreateDialogParamA.argtypes = [w.HINSTANCE, c.c_void_p, w.HWND, callback_type, w.LPARAM]
    user.CreateDialogParamA.restype = w.HWND
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.SendMessageW.restype = c.c_ssize_t
    user.SendMessageA.argtypes = user.SendMessageW.argtypes
    user.SendMessageA.restype = user.SendMessageW.restype
    user.SetWindowTextW.argtypes = [w.HWND, w.LPCWSTR]
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.DestroyWindow.argtypes = [w.HWND]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.GetKeyboardState.argtypes = [c.c_void_p]
    user.SetKeyboardState.argtypes = [c.c_void_p]
    user.GetForegroundWindow.restype = user.GetFocus.restype = w.HWND
    previous = (c.c_ubyte * 256)()
    assert user.GetKeyboardState(previous)
    foreground, focus = user.GetForegroundWindow(), user.GetFocus()
    module = kernel.LoadLibraryExW(str(args.original.resolve()), None, 2 | 0x20)
    assert module
    records = []
    try:
        for template in (666, 665):
            dialog = user.CreateDialogParamA(module, c.c_void_p(template), None, callback, 0)
            assert dialog and not user.IsWindowVisible(dialog)
            try:
                control = 668 if args.multiline else 667
                edit = user.GetDlgItem(dialog, control)
                assert edit and not user.IsWindowVisible(edit)
                # ANSI multiline EDIT stores the host ACP's byte buffer, unlike
                # single-line EDIT's Unicode storage. Freeze portable ASCII here;
                # the editor's Unicode extension must not reproduce ACP loss.
                texts = ([s for s in TEXTS if s.isascii()] +
                         ["abc\ndef", "abc\n  def", "\nabc\n\ndef\n", "abc \ndef",
                          "abc   \n  def", " \n x", "abc\n   \ndef"]
                         if args.multiline else TEXTS)
                for text in texts:
                    units = list(memoryview(text.encode("utf-16le")).cast("H"))
                    length = len(units)
                    record = dict(template=template, control=control, text=text, left=[], right=[], selections=[],
                                  sequences=[])
                    ranges = [(i, i) for i in range(length + 1)]
                    ranges += [(0, length), (length, 0), (0, length // 2), (length // 2, 0)]
                    for range_index, (anchor, cursor) in enumerate(ranges):
                        for key, vk, char in (("left", 0x25, 0), ("right", 0x27, 0),
                                              ("all", 0x41, 1), ("backspace", 8, 127),
                                              ("delete", 0x2E, 0)):
                            # An actual caret cannot sit inside a surrogate pair.
                            if key in ("backspace", "delete") and any(
                                0 < p < length and 0xD800 <= units[p - 1] <= 0xDBFF
                                and 0xDC00 <= units[p] <= 0xDFFF for p in (anchor, cursor)
                            ):
                                continue
                            for shift in ((False, True) if key in ("left", "right") else (False,)):
                                state = (c.c_ubyte * 256)()
                                state[0x11], state[0x10] = 0x80, 0x80 if shift else 0
                                assert user.SetKeyboardState(state)
                                native_text = text.replace("\n", "\r\n")
                                assert user.SetWindowTextW(edit, native_text)
                                user.SendMessageW(edit, 0xB1, anchor + units[:anchor].count(10),
                                                  cursor + units[:cursor].count(10))
                                user.SendMessageA(edit, 0x100, vk, 1)
                                if char:
                                    user.SendMessageA(edit, 0x102, char, 1)
                                user.SendMessageA(edit, 0x101, vk, 0xC0000001)
                                value = c.create_unicode_buffer(256)
                                user.GetWindowTextW(edit, value, len(value))
                                first, last = w.DWORD(), w.DWORD()
                                user.SendMessageW(edit, 0xB0, c.addressof(first), c.addressof(last))
                                native_units = list(memoryview(value.value.encode("utf-16le")).cast("H"))
                                selection = [p - native_units[:p].count(13) for p in (first.value, last.value)]
                                result = dict(anchor=anchor, cursor=cursor, key=key, shift=shift,
                                              selection=selection, result=value.value.replace("\r\n", "\n"))
                                if not shift:
                                    del result['shift']
                                if result['result'] == text:
                                    del result['result']
                                if range_index <= length and key in ("left", "right") and not shift:
                                    assert first.value == last.value and value.value == native_text, (
                                        template, control, ascii(text), anchor, cursor, key,
                                        selection, ascii(value.value))
                                    record[key].append(selection[0])
                                else:
                                    record["selections"].append(result)
                    for start in sorted({0, length // 2, length}):
                        # Do not manufacture an initial caret inside a surrogate.
                        if 0 < start < length and 0xD800 <= units[start - 1] <= 0xDBFF:
                            continue
                        for keys in (("shift-left", "shift-left", "shift-right"),
                                     ("shift-right", "shift-right", "shift-left"),
                                     ("shift-left", "left", "right"),
                                     ("shift-right", "left", "shift-right"),
                                     ("shift-left", "all", "shift-left")):
                            assert user.SetWindowTextW(edit, text.replace("\n", "\r\n"))
                            native_start = start + units[:start].count(10)
                            user.SendMessageW(edit, 0xB1, native_start, native_start)
                            sequence = dict(start=start, keys=keys, selections=[])
                            for key in keys:
                                state = (c.c_ubyte * 256)()
                                state[0x11], state[0x10] = 0x80, 0x80 if key.startswith("shift-") else 0
                                assert user.SetKeyboardState(state)
                                vk = 0x25 if key.endswith("left") else 0x27 if key.endswith("right") else 0x41
                                user.SendMessageA(edit, 0x100, vk, 1)
                                if key == "all":
                                    user.SendMessageA(edit, 0x102, 1, 1)
                                user.SendMessageA(edit, 0x101, vk, 0xC0000001)
                                first, last = w.DWORD(), w.DWORD()
                                user.SendMessageW(edit, 0xB0, c.addressof(first), c.addressof(last))
                                native_units = list(memoryview(text.replace("\n", "\r\n").encode("utf-16le")).cast("H"))
                                sequence["selections"].append([p - native_units[:p].count(13) for p in (first.value, last.value)])
                            record["sequences"].append(sequence)
                    records.append(record)
            finally:
                user.DestroyWindow(dialog)
        assert (foreground, focus) == (user.GetForegroundWindow(), user.GetFocus())
    finally:
        user.SetKeyboardState(previous)
        kernel.FreeLibrary(module)
    # Keep one independently observed text corpus per line, without thousands
    # of formatting-only lines or platform-dependent non-ASCII console output.
    args.output.write_text(
        '{\n  "originalSha256": "' + REFERENCE_SHA256 + '",\n  "creationApi": "CreateDialogParamA",\n'
        '  "records": [\n' + ',\n'.join('    ' + json.dumps(r, ensure_ascii=True, separators=(',', ':'))
                                       for r in records) + '\n  ]\n}\n', encoding="utf-8")
    print(f"Recorded {len(records)} never-shown original edit corpora")


if __name__ == "__main__":
    main()
