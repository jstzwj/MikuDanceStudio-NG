#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#elif defined(__ANDROID__)
#include <android/native_window.h>
#define VK_USE_PLATFORM_ANDROID_KHR
#else
#ifdef MDS_SURFACE_XCB
#include <xcb/xcb.h>
#define VK_USE_PLATFORM_XCB_KHR
#endif
#ifdef MDS_SURFACE_WAYLAND
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "mikudancestudio/vulkan_surface.hpp"
#include <cstring>

namespace mikudancestudio::rhi {
std::shared_ptr<const VulkanSurfaceSource>
CreateNativeVulkanSurface(NativeWindow window, std::uintptr_t display, std::shared_ptr<void> lease,
                          std::function<SurfaceHealth()> health, std::string& reason) {
    auto host = CreateWindowSurface(window, display, std::move(lease), std::move(health), reason);
    return host ? CreateVulkanSurface(std::move(host), reason) : nullptr;
}
std::shared_ptr<const VulkanSurfaceSource>
CreateVulkanSurface(std::shared_ptr<const WindowSurfaceSource> host, std::string& reason) {
    if (!host || !ValidWindowSurface(*host)) {
        reason = "invalid_window_surface";
        return {};
    }
    const auto window = host->window;
    const auto display = host->display;
    auto source = std::make_shared<VulkanSurfaceSource>();
    static_cast<WindowSurfaceSource&>(*source) = *host;
    source->instanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME};
    const char* extension = nullptr;
    std::function<VkResult(VkInstance, const VulkanProcLookup&, VkSurfaceKHR&)> create;
#ifdef _WIN32
    if (window.system == WindowSystem::Win32) {
        extension = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
        create = [window](VkInstance instance, const VulkanProcLookup& lookup, VkSurfaceKHR& out) {
            auto fn =
                reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(lookup("vkCreateWin32SurfaceKHR"));
            if (!fn)
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
            info.hwnd = reinterpret_cast<HWND>(window.handle);
            info.hinstance =
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(info.hwnd, GWLP_HINSTANCE));
            return fn(instance, &info, nullptr, &out);
        };
    }
#elif defined(__APPLE__)
    if (window.system == WindowSystem::Cocoa) {
        extension = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
        create = [window](VkInstance instance, const VulkanProcLookup& lookup, VkSurfaceKHR& out) {
            auto fn =
                reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(lookup("vkCreateMetalSurfaceEXT"));
            if (!fn)
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            VkMetalSurfaceCreateInfoEXT info{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
            info.pLayer = reinterpret_cast<const CAMetalLayer*>(window.handle);
            return fn(instance, &info, nullptr, &out);
        };
    }
#elif defined(__ANDROID__)
    if (window.system == WindowSystem::Android) {
        extension = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
        create = [window](VkInstance instance, const VulkanProcLookup& lookup, VkSurfaceKHR& out) {
            auto fn = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
                lookup("vkCreateAndroidSurfaceKHR"));
            if (!fn)
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
            info.window = reinterpret_cast<ANativeWindow*>(window.handle);
            return fn(instance, &info, nullptr, &out);
        };
    }
#else
#ifdef MDS_SURFACE_XCB
    if (window.system == WindowSystem::Xcb && display) {
        extension = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
        create = [window, display](VkInstance instance, const VulkanProcLookup& lookup,
                                   VkSurfaceKHR& out) {
            auto fn = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(lookup("vkCreateXcbSurfaceKHR"));
            if (!fn)
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            VkXcbSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
            info.connection = reinterpret_cast<xcb_connection_t*>(display);
            info.window = xcb_window_t(window.handle);
            return fn(instance, &info, nullptr, &out);
        };
    }
#endif
#ifdef MDS_SURFACE_WAYLAND
    if (window.system == WindowSystem::Wayland && display) {
        extension = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
        create = [window, display](VkInstance instance, const VulkanProcLookup& lookup,
                                   VkSurfaceKHR& out) {
            auto fn = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
                lookup("vkCreateWaylandSurfaceKHR"));
            if (!fn)
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            VkWaylandSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
            info.display = reinterpret_cast<wl_display*>(display);
            info.surface = reinterpret_cast<wl_surface*>(window.handle);
            return fn(instance, &info, nullptr, &out);
        };
    }
#endif
#endif
    if (!create) {
        reason = "native_surface_provider_not_built";
        return {};
    }
    source->instanceExtensions.push_back(extension);
    source->create = [create](std::uintptr_t instance, const VulkanProcLookup& lookup,
                              std::uint64_t& out) {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const auto result = create(reinterpret_cast<VkInstance>(instance), lookup, surface);
        if (result == VK_SUCCESS) {
            out = 0;
            std::memcpy(&out, &surface, sizeof surface);
        }
        return std::int32_t(result);
    };
    reason.clear();
    return source;
}
} // namespace mikudancestudio::rhi
