#include "viewport.hpp"
#include "surface_lease.hpp"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <android/native_window_jni.h>

namespace mikudancestudio::qt {
namespace {
struct AndroidLease : QtWindowLease {
    using QtWindowLease::QtWindowLease;
    QJniObject surface;
    ANativeWindow* native = nullptr;
    ~AndroidLease() override {
        if (native)
            ANativeWindow_release(native);
    }
};
}
std::shared_ptr<const rhi::WindowSurfaceSource> QtSurface(QWindow& window, std::string& reason) {
    window.create();
    auto lease = std::make_shared<AndroidLease>(window);
    // Qt 6.8.3's Android WId is a retained QtWindow jobject. Ask its existing
    // container for a SurfaceView; Qt remains responsible for its UI lifetime.
    // This small pinned adapter never creates a competing EGL/Vulkan instance.
    const QJniObject view(reinterpret_cast<jobject>(window.winId()));
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([view, lease] {
        QJniEnvironment env;
        const auto surfaceView = env.findClass("android/view/SurfaceView");
        const auto count = view.callMethod<jint>("getChildCount");
        for (jint index = 0; index < count; ++index) {
            const auto child = view.callObjectMethod("getChildAt", "(I)Landroid/view/View;", index);
            if (!child.isValid() || !env->IsInstanceOf(child.object(), surfaceView))
                continue;
            const auto holder =
                child.callObjectMethod("getHolder", "()Landroid/view/SurfaceHolder;");
            auto surface = holder.callObjectMethod("getSurface", "()Landroid/view/Surface;");
            if (surface.isValid() && surface.callMethod<jboolean>("isValid"))
                lease->surface = std::move(surface);
            return;
        }
        // QtWindow.createSurface(bool, int, bool, int); 0 selects SurfaceView.
        view.callMethod<void>("createSurface", "(ZIZI)V", jboolean(false), jint(32), jboolean(true),
                              jint(0));
    }).waitForFinished();
    if (!lease->surface.isValid()) {
        reason = "android-surface-pending";
        return {};
    }
    QJniEnvironment env;
    lease->native = ANativeWindow_fromSurface(env.jniEnv(), lease->surface.object());
    const auto health = [lease] {
        if (!lease->window || !lease->nativeAlive ||
            !lease->surface.callMethod<jboolean>("isValid"))
            return rhi::SurfaceHealth::Removed;
        return QGuiApplication::applicationState() == Qt::ApplicationActive
                   ? rhi::SurfaceHealth::Ready
                   : rhi::SurfaceHealth::Suspended;
    };
    return rhi::CreateWindowSurface(
        {rhi::WindowSystem::Android, reinterpret_cast<std::uintptr_t>(lease->native)}, 0, lease,
        health, reason);
}
}
