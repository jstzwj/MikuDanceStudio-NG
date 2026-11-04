"""Create a never-shown resource dialog without creation-time activation.

Only the outer extended style is adapted. Child resource bytes stay unchanged;
shown/focus oracles must continue to use the unmodified original resource.
"""
import ctypes as c
from ctypes import wintypes as w
import struct
from functools import wraps


def hidden_input_guard(function):
    """Veto explicit activation/focus in this observer's thread, with cleanup."""
    @wraps(function)
    def guarded(*args, **kwargs):
        user, kernel = c.WinDLL('user32'), c.WinDLL('kernel32')
        hook_type = c.WINFUNCTYPE(c.c_ssize_t, c.c_int, w.WPARAM, w.LPARAM)
        user.SetWindowsHookExW.argtypes = [c.c_int, hook_type, w.HINSTANCE, w.DWORD]
        user.SetWindowsHookExW.restype = w.HANDLE
        user.UnhookWindowsHookEx.argtypes = [w.HANDLE]
        user.CallNextHookEx.argtypes = [w.HANDLE, c.c_int, w.WPARAM, w.LPARAM]
        user.CallNextHookEx.restype = c.c_ssize_t
        kernel.GetCurrentThreadId.restype = w.DWORD

        @hook_type
        def callback(code, wp, lp):
            if code in (5, 9):  # HCBT_ACTIVATE, HCBT_SETFOCUS
                return 1
            return user.CallNextHookEx(None, code, wp, lp)

        user.GetForegroundWindow.restype = user.GetFocus.restype = w.HWND
        user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
        kernel.GetCurrentProcessId.restype = w.DWORD
        foreground, focus = user.GetForegroundWindow(), user.GetFocus()
        hook = user.SetWindowsHookExW(5, callback, None, kernel.GetCurrentThreadId())  # WH_CBT
        if not hook:
            raise RuntimeError('Cannot guard hidden observer activation')
        try:
            result = function(*args, **kwargs)
            current = user.GetForegroundWindow()
            process = w.DWORD()
            if current:
                user.GetWindowThreadProcessId(current, c.byref(process))
            if user.GetFocus() != focus or (current != foreground and process.value == kernel.GetCurrentProcessId()):
                raise RuntimeError('Hidden observer activated a test window or changed thread focus')
            return result
        finally:
            user.UnhookWindowsHookEx(hook)
    return guarded


def create_hidden_dialog(user, kernel, module, template, callback):
    kernel.FindResourceW.argtypes = [w.HMODULE, c.c_void_p, c.c_void_p]
    kernel.FindResourceW.restype = w.HANDLE
    kernel.SizeofResource.argtypes = [w.HMODULE, w.HANDLE]
    kernel.SizeofResource.restype = w.DWORD
    kernel.LoadResource.argtypes = [w.HMODULE, w.HANDLE]
    kernel.LoadResource.restype = w.HANDLE
    kernel.LockResource.argtypes = [w.HANDLE]
    kernel.LockResource.restype = c.c_void_p
    resource = kernel.FindResourceW(module, template, 5)  # RT_DIALOG
    size = kernel.SizeofResource(module, resource) if resource else 0
    source = kernel.LockResource(kernel.LoadResource(module, resource)) if resource else None
    if not source or size < 16:
        raise RuntimeError('Invalid hidden dialog resource')
    data = bytearray(c.string_at(source, size))
    extended = data[2:4] == b'\xff\xff'
    style_offset, extended_offset = (12, 8) if extended else (0, 4)
    if struct.unpack_from('<I', data, style_offset)[0] & 0x10000000:  # WS_VISIBLE
        raise RuntimeError('Hidden reference must not show a window')
    extended_style = struct.unpack_from('<I', data, extended_offset)[0]
    struct.pack_into('<I', data, extended_offset, extended_style | 0x08000000)  # NOACTIVATE
    storage = (w.DWORD * ((len(data) + 3) // 4))()
    c.memmove(storage, bytes(data), len(data))
    user.CreateDialogIndirectParamA.argtypes = [w.HINSTANCE, c.c_void_p, w.HWND,
                                              type(callback), w.LPARAM]
    user.CreateDialogIndirectParamA.restype = w.HWND
    # FALSE from WM_INITDIALOG prevents USER32's default initial SetFocus.
    return user.CreateDialogIndirectParamA(module, storage, None, callback, 0)
