#include "android_input.hpp"
#include <QCoreApplication>
#include <QComboBox>
#include <QAbstractItemView>
#include <QMouseEvent>
#include <QBackingStore>
#include <qpa/qplatformbackingstore.h>
#include <QApplication>
#include <QWidget>
#include <QTimer>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QJniEnvironment>
#include <QJniObject>
#include <QPointer>
#include <QPointingDevice>
#include <QScreen>
#include <QScopedValueRollback>
#include <QWindow>
#include <QVariant>
#include <private/qguiapplication_p.h>
#include <qpa/qplatforminputcontext.h>
#include <qpa/qplatformintegration.h>
#include <qpa/qplatformwindow.h>
#include <qpa/qwindowsysteminterface.h>
#include <cmath>
#include <vector>

namespace mikudancestudio::qt {
namespace {
// Qt's Android accessibility query can prevent the first EGL Surface from
// being created. Android's native Surface may become valid without another Qt
// expose event. Retry the actual backing-store flush, not just native readiness.
// Each raster window owns its retry and re-arms it whenever it is shown again.
class RasterSurfaceRetry final : public QObject {
public:
    explicit RasterSurfaceRetry(QWindow& window) : QObject(&window), window_(window) {
        setObjectName("android.rasterSurfaceRetry");
        connect(&timer_, &QTimer::timeout, this, [this] { Poll(); });
        Start();
    }
    void Start() {
        if (!polling_ && !timer_.isActive())
            timer_.start(100);
    }

private:
    void Poll() {
        const QScopedValueRollback polling(polling_, true);
        if (!window_.isVisible()) {
            timer_.stop();
            return;
        }
        if (!window_.isExposed())
            return;
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (widget->windowHandle() != &window_)
                continue;
            // repaint() refreshes the normal QWidget backing store first. The
            // explicit flush reports whether Qt could use the EGL Surface;
            // update() alone loses this result and can leave a reused popup blank.
            widget->repaint();
            // A normal repaint can fail makeCurrent and leave QRhi caching the
            // old window as current. Release it AFTER repaint, immediately before
            // the checked flush, so the check must acquire a live EGL Surface.
            if (auto* context = QOpenGLContext::currentContext())
                context->doneCurrent();
            auto* store = widget->backingStore();
            if (!store || !store->handle()->rhi(&window_))
                return;
            QPlatformTextureList textures; // editor windows contain raster widgets
            const auto result = store->handle()->rhiFlush(
                &window_, widget->devicePixelRatio(), QRegion(widget->rect()), QPoint(), &textures,
                widget->testAttribute(Qt::WA_TranslucentBackground));
            if (result == QPlatformBackingStore::FlushSuccess)
                timer_.stop();
            else if (result == QPlatformBackingStore::FlushFailedDueToLostDevice)
                store->handle()->graphicsDeviceReportedLost(&window_);
            return;
        }
        timer_.stop(); // no QWidget backing store (for example the RHI child)
    }
    QWindow& window_;
    QTimer timer_;
    bool polling_ = false;
};
class RasterWindowObserver final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* object, QEvent* event) override {
        // A normal backing-store update can lose makeCurrent while Android
        // queries accessibility, even without a new Surface/expose callback.
        // Re-arm after each requested root/dialog paint; the checked retry's
        // own repaint is excluded so success does not create a repaint loop.
        if (event->type() == QEvent::UpdateRequest || event->type() == QEvent::Resize)
            if (auto* widget = qobject_cast<QWidget*>(object);
                widget && widget->isWindow() && widget->windowHandle()) {
                auto* window = widget->windowHandle();
                if (auto* retry = window->findChild<QObject*>("android.rasterSurfaceRetry",
                                                              Qt::FindDirectChildrenOnly))
                    static_cast<RasterSurfaceRetry*>(retry)->Start();
                else
                    new RasterSurfaceRetry(*window);
            }
        if (event->type() == QEvent::TouchCancel)
            openingCombo_.clear();
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->source() == Qt::MouseEventSynthesizedByQt &&
                !QApplication::activePopupWidget()) {
                openingCombo_ = qobject_cast<QComboBox*>(object);
                openingPoint_ = mouse->globalPosition();
            }
        } else if (event->type() == QEvent::MouseButtonRelease && openingCombo_) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            auto* target = qobject_cast<QWidget*>(object);
            auto* popup = QApplication::activePopupWidget();
            if (target && popup && target->window() == popup &&
                openingCombo_->view()->window() == popup) {
                // Qt ignores the opening release for its double-click interval. A
                // native Surface can take longer, selecting/closing the combo
                // before the user's opening tap ends. Track the gesture instead
                // of wall time; a deliberate drag to another item still selects.
                const bool openingTap =
                    (mouse->globalPosition() - openingPoint_).manhattanLength() <
                    QApplication::startDragDistance();
                openingCombo_.clear();
                if (openingTap) {
                    event->accept();
                    return true;
                }
            }
        }
        if (event->type() == QEvent::Show || event->type() == QEvent::Expose ||
            event->type() == QEvent::FocusIn)
            if (auto* window = qobject_cast<QWindow*>(object);
                window && window->surfaceType() != QSurface::VulkanSurface) {
                // Qt 6.8.3 blocks requestActivateWindow for any window other
                // than the modal dialog, including that dialog's own popup.
                // A reused combo popup therefore stays below its parent after
                // Preferences is raised again. Raise a shown popup explicitly;
                // its position and ownership remain Qt's, with no input offset.
                if (event->type() == QEvent::Show && window->type() == Qt::Popup)
                    QTimer::singleShot(0, window, [window] {
                        if (window->isVisible())
                            window->raise();
                    });
                auto* retry = window->findChild<QObject*>("android.rasterSurfaceRetry",
                                                          Qt::FindDirectChildrenOnly);
                if (retry)
                    static_cast<RasterSurfaceRetry*>(retry)->Start();
                else
                    new RasterSurfaceRetry(*window);
            }
        return false;
    }

private:
    QPointer<QComboBox> openingCombo_;
    QPointF openingPoint_;
};
QWindow* Window(int id) {
    for (auto* window : QGuiApplication::allWindows()) {
        if (!window->handle())
            continue;
        // Android's WId is the retained QtWindow jobject, not its integer ID.
        const QJniObject view(reinterpret_cast<jobject>(window->winId()));
        if (view.isValid() && view.callMethod<jint>("getId") == id)
            return window;
    }
    return nullptr;
}
void NativeSurfaceChanged(JNIEnv*, jclass, jint id) {
    QMetaObject::invokeMethod(
        qApp,
        [id] {
            auto* window = Window(id);
            if (!window)
                return;
            // A SurfaceView may replace its buffer queue while the QWindow,
            // Java Surface and dimensions survive. Retire every native lease
            // acquired before this lifecycle callback, including Vulkan's.
            window->setProperty("android.surfaceGeneration",
                                window->property("android.surfaceGeneration").toULongLong() + 1);
            if (window->surfaceType() == QSurface::VulkanSurface)
                return;
            if (auto* retry = window->findChild<QObject*>("android.rasterSurfaceRetry",
                                                          Qt::FindDirectChildrenOnly))
                static_cast<RasterSurfaceRetry*>(retry)->Start();
            else
                new RasterSurfaceRetry(*window);
        },
        Qt::QueuedConnection);
}
const QPointingDevice* TouchDevice() {
    static QPointer<QPointingDevice> device;
    if (!device) {
        device = new QPointingDevice(
            QStringLiteral("Android editor touchscreen"), 0x4d4453,
            QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger,
            QInputDevice::Capability::Position | QInputDevice::Capability::Area |
                QInputDevice::Capability::Pressure | QInputDevice::Capability::NormalizedPosition,
            10, 0, QString(), QPointingDeviceUniqueId(), qApp);
        QWindowSystemInterface::registerInputDevice(device);
    }
    return device;
}
void SendTouch(JNIEnv* env, jclass, jint windowId, jlong time, jint action, jint changed,
               jintArray identifiers, jfloatArray coordinates) {
    const auto count = env->GetArrayLength(identifiers);
    if (count < 1 || count > 10 || env->GetArrayLength(coordinates) != count * 6)
        return;
    std::vector<jint> ids(count);
    std::vector<jfloat> values(count * 6);
    env->GetIntArrayRegion(identifiers, 0, count, ids.data());
    env->GetFloatArrayRegion(coordinates, 0, count * 6, values.data());
    // The Java UI thread never waits on rendering or reads QWidget geometry.
    // Resolve the live native window on the GUI thread and discard a retired
    // window's queued events, preserving surface replacement ordering.
    QMetaObject::invokeMethod(
        qApp,
        [windowId, time, action, changed, ids, values] {
            auto* window = Window(windowId);
            if (!window)
                return;
            const auto* device = TouchDevice();
            if (action == 3) { // Android ACTION_CANCEL
                QWindowSystemInterface::handleTouchCancelEvent(window, ulong(time), device);
                return;
            }
            QList<QWindowSystemInterface::TouchPoint> points;
            const auto screen = window->screen();
            const auto size = screen->size() * screen->devicePixelRatio();
            // Android view coordinates are local native pixels. Qt's virtual
            // desktop starts inside the available content area, which need not
            // share MotionEvent.getRawX/Y's physical-screen origin.
            const auto origin = window->handle()->mapToGlobal(QPoint());
            for (std::size_t i = 0; i < ids.size(); ++i) {
                const auto* p = values.data() + i * 6;
                QWindowSystemInterface::TouchPoint point;
                point.id = ids[i] + 1;
                point.state = QEventPoint::State::Stationary;
                if ((action == 0 || action == 5) && int(i) == changed)
                    point.state = QEventPoint::State::Pressed;
                else if ((action == 1 || action == 6) && int(i) == changed)
                    point.state = QEventPoint::State::Released;
                else if (action == 2)
                    point.state = QEventPoint::State::Updated;
                point.pressure = p[2];
                point.rotation = p[5] * 180. / 3.14159265358979323846;
                const QPointF global = origin + QPointF(p[0], p[1]);
                point.area = QRectF(global.x() - p[4] / 2., global.y() - p[3] / 2., p[4], p[3]);
                point.normalPosition = {global.x() / size.width(), global.y() / size.height()};
                points.append(point);
            }
            if (action == 0)
                if (auto* input = QGuiApplicationPrivate::platformIntegration()->inputContext())
                    QMetaObject::invokeMethod(input, "touchDown",
                                              Q_ARG(int, qRound(origin.x() + values[0])),
                                              Q_ARG(int, qRound(origin.y() + values[1])));
            // QPA takes global native pixels. It performs the one conversion into
            // Qt logical/window coordinates, including DPI, parents and safe areas.
            QWindowSystemInterface::handleTouchEvent(window, ulong(time), device, points);
        },
        Qt::QueuedConnection);
}
void LongPress(JNIEnv*, jclass, jint windowId, jfloat x, jfloat y) {
    QMetaObject::invokeMethod(
        qApp,
        [windowId, x, y] {
            if (auto* window = Window(windowId))
                if (auto* input = QGuiApplicationPrivate::platformIntegration()->inputContext()) {
                    const auto origin = window->handle()->mapToGlobal(QPoint());
                    QMetaObject::invokeMethod(input, "longPress",
                                              Q_ARG(int, qRound(origin.x() + x)),
                                              Q_ARG(int, qRound(origin.y() + y)));
                }
        },
        Qt::QueuedConnection);
}
}
void InstallAndroidInput() {
    qApp->installEventFilter(new RasterWindowObserver(qApp));
    const JNINativeMethod methods[]{
        {"nativeSurfaceChanged", "(I)V", reinterpret_cast<void*>(NativeSurfaceChanged)},
        {"sendTouch", "(IJII[I[F)V", reinterpret_cast<void*>(SendTouch)},
        {"longPress", "(IFF)V", reinterpret_cast<void*>(LongPress)}};
    QJniEnvironment env;
    if (!env.registerNativeMethods("org/qtproject/qt/android/EditorTouchAdapter", methods, 3))
        qFatal("Cannot register the Qt 6.8.3 Android touch adapter");
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject::callStaticMethod<void>("org/qtproject/qt/android/EditorTouchAdapter",
                                           "enableSurfaceObserver", "()V");
    });
}
}
