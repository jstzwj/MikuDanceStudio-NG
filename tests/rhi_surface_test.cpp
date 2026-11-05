#include "mikudancestudio/rhi_backend.hpp"
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
#include "mikudancestudio/vulkan_surface.hpp"
#endif
#include <iostream>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << '\n';                                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main() {
    struct Host {
        SurfaceHealth state = SurfaceHealth::Ready;
    };
    auto host = std::make_shared<Host>();
    std::weak_ptr<Host> weak = host;
    std::function<SurfaceHealth()> health = [host] { return host->state; };
    std::string reason;
    const NativeWindow window{WindowSystem::Cocoa, 17};
    CHECK(!CreateWindowSurface({}, 0, host, health, reason));
    CHECK(!CreateWindowSurface(window, 0, {}, health, reason));
    CHECK(!CreateWindowSurface(window, 0, host, {}, reason));
    CHECK(!CreateWindowSurface({WindowSystem::Xcb, 9}, 0, host, health, reason));
    CHECK(!CreateWindowSurface({WindowSystem::Wayland, 9}, 0, host, health, reason));
    CHECK(CreateWindowSurface({WindowSystem::Wayland, 9}, 10, host, health, reason));
    CHECK(CreateWindowSurface({WindowSystem::Android, 9}, 0, host, health, reason));
    auto source = CreateWindowSurface(window, 0, host, health, reason);
    CHECK(source && reason.empty() && source->health() == SurfaceHealth::Ready);
    host->state = SurfaceHealth::Suspended;
    CHECK(source->health() == SurfaceHealth::Suspended);
    host->state = SurfaceHealth::Removed;
    CHECK(source->health() == SurfaceHealth::Removed);
    BackendOptions options;
    options.backend = Backend::Null;
    options.surface = source;
    SwapchainDesc desc;
    desc.window = {WindowSystem::Cocoa, 18};
    desc.width = desc.height = 16;
    CHECK(CreateBackendDevice(desc, options).reason == "invalid_window_surface");
    options.surface.reset();
    host.reset();
    health = [keep = std::shared_ptr<Host>{}] { return SurfaceHealth::Unknown; };
    CHECK(!weak.expired());
    source.reset();
    CHECK(weak.expired());
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    auto lease = std::make_shared<int>(1);
#if defined(_WIN32)
    NativeWindow native{WindowSystem::Win32, 1};
#elif defined(__APPLE__)
    NativeWindow native{WindowSystem::Cocoa, 1};
#else
    NativeWindow native{WindowSystem::Xcb, 1};
#endif
    auto generic =
        CreateWindowSurface(native, 1, lease, [] { return SurfaceHealth::Ready; }, reason);
    auto adapter = CreateVulkanSurface(generic, reason);
    CHECK(adapter && adapter->create && adapter->instanceExtensions.size() == 2);
    CHECK(adapter->window.handle == generic->window.handle && adapter->lease == generic->lease);
#endif
    std::cout << "Window surface contract: validation, health and lease ownership passed\n";
}
