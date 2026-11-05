#include "viewport.hpp"
#include "surface_lease.hpp"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QFuture>
#include <QVariant>
#include <qpa/qplatformwindow.h>
#include <android/native_window_jni.h>

namespace mikudancestudio::qt {
void ResizeAndroidSurface(QWindow& window, QSize pixels) {
    const QJniObject view(reinterpret_cast<jobject>(window.winId()));
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([view, pixels] {
        QJniEnvironment env;
        const auto surfaceView = env.findClass("android/view/SurfaceView");
        const auto count = view.callMethod<jint>("getChildCount");
        for (jint index = 0; index < count; ++index) {
            const auto child = view.callObjectMethod("getChildAt", "(I)Landroid/view/View;", index);
            if (!child.isValid() || !env->IsInstanceOf(child.object(), surfaceView))
                continue;
            const auto holder =
                child.callObjectMethod("getHolder", "()Landroid/view/SurfaceHolder;");
            // Vulkan fixes buffer dimensions when creating its swapchain.
            // A later View layout alone can leave the old buffer extent in
            // Surface capabilities indefinitely. Resize the owned holder's
            // buffers before retrying the matching swapchain on the GUI thread.
            holder.callMethod<void>("setFixedSize", "(II)V", pixels.width(), pixels.height());
            return;
        }
    });
}
namespace {
struct NativeSurface {
    QJniObject surface, holder;
    QSize extent;
};
class SurfaceRequest final : public QtWindowLease {
public:
    explicit SurfaceRequest(QWindow& window) : QtWindowLease(window) {
        setParent(&window);
        setObjectName("android.surfaceRequest");
        generation = window.property("android.surfaceGeneration").toULongLong();
    }
    std::shared_ptr<NativeSurface> result = std::make_shared<NativeSurface>();
    QFuture<void> future;
    qulonglong generation = 0;
};
struct AndroidLease : QtWindowLease {
    using QtWindowLease::QtWindowLease;
    QJniObject surface;
    QJniObject holder;
    QSize extent;
    ANativeWindow* native = nullptr;
    qulonglong generation = 0;
    ~AndroidLease() override {
        if (native)
            ANativeWindow_release(native);
    }
};
}
std::shared_ptr<const rhi::WindowSurfaceSource> QtSurface(QWindow& window, std::string& reason) {
    window.create();
    // Qt 6.8.3's Android WId is a retained QtWindow jobject. Ask its existing
    // container for a SurfaceView; Qt remains responsible for its UI lifetime.
    // This small pinned adapter never creates a competing EGL/Vulkan instance.
    const QJniObject view(reinterpret_cast<jobject>(window.winId()));
    auto* request = static_cast<SurfaceRequest*>(
        window.findChild<QObject*>("android.surfaceRequest", Qt::FindDirectChildrenOnly));
    if (!request) {
        request = new SurfaceRequest(window);
        const auto result = request->result;
        request->future =
            QNativeInterface::QAndroidApplication::runOnAndroidMainThread([view, result] {
                QJniEnvironment env;
                const auto surfaceView = env.findClass("android/view/SurfaceView");
                const auto count = view.callMethod<jint>("getChildCount");
                for (jint index = 0; index < count; ++index) {
                    const auto child =
                        view.callObjectMethod("getChildAt", "(I)Landroid/view/View;", index);
                    if (!child.isValid() || !env->IsInstanceOf(child.object(), surfaceView))
                        continue;
                    // The root raster SurfaceView can be recreated after this
                    // child on resume. Equal-Z surfaces then follow creation
                    // order and cover a correctly rendering viewport. Media
                    // overlay (-1) is above that root (-2), below View/TextureView
                    // dialogs (0), independent of creation order.
                    child.callMethod<void>("setZOrderMediaOverlay", "(Z)V", jboolean(true));
                    const auto holder =
                        child.callObjectMethod("getHolder", "()Landroid/view/SurfaceHolder;");
                    result->holder = holder;
                    const auto frame =
                        holder.callObjectMethod("getSurfaceFrame", "()Landroid/graphics/Rect;");
                    result->extent = {frame.callMethod<jint>("width"),
                                      frame.callMethod<jint>("height")};
                    auto surface =
                        holder.callObjectMethod("getSurface", "()Landroid/view/Surface;");
                    if (surface.isValid() && surface.callMethod<jboolean>("isValid"))
                        result->surface = std::move(surface);
                    return;
                }
                // QtWindow.createSurface(bool, int, bool, int); 0 selects SurfaceView.
                view.callMethod<void>("createSurface", "(ZIZI)V", jboolean(true), jint(32),
                                      jboolean(true), jint(0));
            });
    }
    // Android accessibility and Surface callbacks can synchronously query Qt.
    // Waiting for Android's UI thread here deadlocks both event loops when a
    // dialog/IME closes. Poll the completed request on later render ticks.
    if (!request->future.isFinished()) {
        reason = "android-surface-pending";
        return {};
    }
    const auto result = request->result;
    const auto generation = request->generation;
    const bool alive = request->nativeAlive &&
                       generation == window.property("android.surfaceGeneration").toULongLong();
    delete request;
    if (!alive) {
        reason = "android-surface-pending";
        return {};
    }
    auto lease = std::make_shared<AndroidLease>(window);
    lease->surface = result->surface;
    lease->holder = result->holder;
    lease->extent = result->extent;
    lease->generation = generation;
    if (!lease->surface.isValid()) {
        reason = "android-surface-pending";
        return {};
    }
    if (lease->extent != window.handle()->geometry().size()) {
        ResizeAndroidSurface(window, window.handle()->geometry().size());
        reason = "android-surface-pending";
        return {};
    }
    QJniEnvironment env;
    lease->native = ANativeWindow_fromSurface(env.jniEnv(), lease->surface.object());
    const auto health = [lease] {
        if (!lease->window || !lease->nativeAlive ||
            lease->window->property("android.surfaceGeneration").toULongLong() !=
                lease->generation ||
            !lease->surface.callMethod<jboolean>("isValid"))
            return rhi::SurfaceHealth::Removed;
        // SurfaceView can replace its BufferQueue on resize without destroying
        // the QWindow or invalidating the Java Surface object. An acquired
        // ANativeWindow still names the old queue; never revive that lease.
        QJniEnvironment env;
        auto* current = ANativeWindow_fromSurface(env.jniEnv(), lease->surface.object());
        const bool same = current == lease->native;
        if (current)
            ANativeWindow_release(current);
        if (!same)
            return rhi::SurfaceHealth::Removed;
        const auto frame =
            lease->holder.callObjectMethod("getSurfaceFrame", "()Landroid/graphics/Rect;");
        if (QSize(frame.callMethod<jint>("width"), frame.callMethod<jint>("height")) !=
            lease->extent)
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
