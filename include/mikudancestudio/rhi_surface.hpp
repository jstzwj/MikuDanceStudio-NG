#pragma once
#include "rhi_swapchain.hpp"
#include <functional>
#include <string>

namespace mikudancestudio::rhi {
// The host owns the native window/display/layer. Physical pixel dimensions are
// supplied separately by SwapchainDesc; a lease survives all native GPU users.
// Cocoa handle = CAMetalLayer*, Win32 = HWND, Xcb/Wayland = window/surface token.
// No graphics API entry points or instance extensions belong to this contract.
struct WindowSurfaceSource {
    NativeWindow window;
    std::uintptr_t display = 0;
    std::shared_ptr<void> lease;
    std::function<SurfaceHealth()> health;
};
inline bool ValidWindowSurface(const WindowSurfaceSource& source) {
    const auto system = source.window.system;
    return source.window.handle && source.lease && source.health &&
           (system == WindowSystem::Win32 || system == WindowSystem::Cocoa ||
            ((system == WindowSystem::Xcb || system == WindowSystem::Wayland) && source.display));
}
inline std::shared_ptr<const WindowSurfaceSource>
CreateWindowSurface(NativeWindow window, std::uintptr_t display, std::shared_ptr<void> lease,
                    std::function<SurfaceHealth()> health, std::string& reason) {
    auto source = std::make_shared<WindowSurfaceSource>(
        WindowSurfaceSource{window, display, std::move(lease), std::move(health)});
    if (!ValidWindowSurface(*source)) {
        reason = "surface_requires_native_window_display_lease_and_health";
        return {};
    }
    reason.clear();
    return source;
}
}
