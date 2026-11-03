#include "viewport.hpp"
#include "surface_lease.hpp"
#include <QGuiApplication>
#include <QPointer>
#if !defined(_WIN32) && !defined(__APPLE__)
#include <qpa/qplatformnativeinterface.h>
#endif
namespace mikudancestudio::qt {
#ifndef __APPLE__
std::shared_ptr<const rhi::WindowSurfaceSource> QtSurface(QWindow& window, std::string& reason) {
#ifdef _WIN32
    if (QGuiApplication::platformName() != "windows") {
        reason = "qt-platform-has-no-native-surface";
        return {};
    }
#endif
    window.create();
    auto lease = std::make_shared<QtWindowLease>(window);
    rhi::NativeWindow native;
    std::uintptr_t display = 0;
#ifdef _WIN32
    native = {rhi::WindowSystem::Win32, std::uintptr_t(window.winId())};
#else
    auto* api = QGuiApplication::platformNativeInterface();
    if (QGuiApplication::platformName() == "xcb") {
        native = {rhi::WindowSystem::Xcb, std::uintptr_t(window.winId())};
        display = reinterpret_cast<std::uintptr_t>(api->nativeResourceForIntegration("connection"));
    } else if (QGuiApplication::platformName().startsWith("wayland")) {
        native = {
            rhi::WindowSystem::Wayland,
            reinterpret_cast<std::uintptr_t>(api->nativeResourceForWindow("surface", &window))};
        display = reinterpret_cast<std::uintptr_t>(api->nativeResourceForIntegration("display"));
    } else {
        reason = "qt-platform-has-no-native-surface";
        return {};
    }
#endif
    const auto health = [lease] {
        if (!lease->window || !lease->nativeAlive)
            return rhi::SurfaceHealth::Removed;
        return lease->window->width() > 0 && lease->window->height() > 0
                   ? rhi::SurfaceHealth::Ready
                   : rhi::SurfaceHealth::Suspended;
    };
    return rhi::CreateWindowSurface(native, display, lease, health, reason);
}
#endif
}
