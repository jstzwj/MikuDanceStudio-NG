#pragma once

#include <Windows.h>
#include <cstring>
#include <stdexcept>
#include <vector>

// WS_EX_NOACTIVATE governs mouse activation, not explicit SetActiveWindow or
// SetFocus from USER32 dialog/control processing. Veto both in this test thread
// before creating any reference windows. This does not hook other threads,
// change the user's keyboard state or restore/change desktop foreground.
class HiddenDialogInputGuard final {
public:
    explicit HiddenDialogInputGuard(bool enabled = true) {
        if (enabled) {
            hook_ = SetWindowsHookExW(WH_CBT, Callback, nullptr, GetCurrentThreadId());
            if (!hook_)
                throw std::runtime_error("install hidden observer input guard");
        }
    }
    ~HiddenDialogInputGuard() {
        if (hook_)
            UnhookWindowsHookEx(hook_);
    }
    HiddenDialogInputGuard(const HiddenDialogInputGuard&) = delete;
    HiddenDialogInputGuard& operator=(const HiddenDialogInputGuard&) = delete;

private:
    HHOOK hook_ = nullptr;
    static LRESULT CALLBACK Callback(int code, WPARAM wParam, LPARAM lParam) {
        if (code == HCBT_ACTIVATE || code == HCBT_SETFOCUS)
            return 1;
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
};

// Test-only adaptation of the OUTER dialog, before USER32 creates any HWND.
// All child styles, fonts, sizes and template data remain byte-identical.
// Set the flag before creation and keep HiddenDialogInputGuard alive while
// using the result. Shown/focus oracles use the original
// resource directly, without this adaptation.
inline HWND CreateHiddenResourceDialog(HINSTANCE instance, int resourceId, DLGPROC procedure,
                                       LPARAM parameter = 0) {
    const auto resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_DIALOG);
    const auto size = resource ? SizeofResource(instance, resource) : 0;
    const auto* source =
        resource ? static_cast<const BYTE*>(LockResource(LoadResource(instance, resource)))
                 : nullptr;
    if (!source || size < 16)
        throw std::runtime_error("invalid hidden dialog resource");
    const bool extended = source[2] == 0xff && source[3] == 0xff;
    const auto styleOffset = extended ? 12 : 0;
    const auto exStyleOffset = extended ? 8 : 4;
    DWORD style{}, exStyle{};
    std::memcpy(&style, source + styleOffset, sizeof style);
    if (style & WS_VISIBLE)
        throw std::runtime_error("hidden reference resource must not show a window");
    // DWORD alignment also satisfies DLGTEMPLATEEX and its embedded items.
    std::vector<DWORD> storage((size + sizeof(DWORD) - 1) / sizeof(DWORD));
    auto* copy = reinterpret_cast<BYTE*>(storage.data());
    std::memcpy(copy, source, size);
    std::memcpy(&exStyle, copy + exStyleOffset, sizeof exStyle);
    exStyle |= WS_EX_NOACTIVATE;
    std::memcpy(copy + exStyleOffset, &exStyle, sizeof exStyle);
    return CreateDialogIndirectParamW(instance, reinterpret_cast<const DLGTEMPLATE*>(copy), nullptr,
                                      procedure, parameter);
}
