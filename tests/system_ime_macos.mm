#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <cstdint>
#include <string>

void ActivateCocoaInputWindow(std::uintptr_t handle) {
    auto *view = (__bridge NSView *)reinterpret_cast<void *>(handle);
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [view.window makeKeyAndOrderFront:nil];
    [view.window makeFirstResponder:view];
}
std::string ActivateCocoaJapanese() {
    const auto sources = TISCreateInputSourceList(nullptr, true);
    TISInputSourceRef method = nullptr, mode = nullptr;
    for (CFIndex n = 0; n < CFArrayGetCount(sources); ++n) {
        const auto source =
            static_cast<TISInputSourceRef>(const_cast<void *>(CFArrayGetValueAtIndex(sources, n)));
        const auto id =
            static_cast<CFStringRef>(TISGetInputSourceProperty(source, kTISPropertyInputSourceID));
        if (id && CFEqual(id, CFSTR("com.apple.inputmethod.Kotoeri.RomajiTyping")))
            method = source;
        if (id && CFEqual(id, CFSTR("com.apple.inputmethod.Kotoeri.RomajiTyping.Japanese")))
            mode = source;
    }
    const auto enabled = method ? TISEnableInputSource(method) : paramErr;
    const auto selected = mode ? TISSelectInputSource(mode) : paramErr;
    CFRelease(sources);
    return enabled || selected
               ? "Kotoeri activation: " + std::to_string(enabled) + "/" + std::to_string(selected)
               : std::string();
}
