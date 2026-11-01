#include "relative_pointer.hpp"
#include "qwayland-relative-pointer-unstable-v1.h"
#include "qwayland-pointer-constraints-unstable-v1.h"
#include <QtWaylandClient/QWaylandClientExtension>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QCursor>
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>

namespace mikudancestudio::qt {
namespace {
class RelativeManager final : public QWaylandClientExtensionTemplate<RelativeManager>,
                              public QtWayland::zwp_relative_pointer_manager_v1 {
public:
    RelativeManager() : QWaylandClientExtensionTemplate(1) { initialize(); }
    ~RelativeManager() override {
        if (isInitialized())
            destroy();
    }
};
class Constraints final : public QWaylandClientExtensionTemplate<Constraints>,
                          public QtWayland::zwp_pointer_constraints_v1 {
public:
    Constraints() : QWaylandClientExtensionTemplate(1) { initialize(); }
    ~Constraints() override {
        if (isInitialized())
            destroy();
    }
};
class WaylandPointer final : public QObject,
                             public RelativePointer,
                             private QtWayland::zwp_relative_pointer_v1,
                             private QtWayland::zwp_locked_pointer_v1 {
public:
    WaylandPointer() {
        timeout_.setSingleShot(true);
        connect(&timeout_, &QTimer::timeout, this,
                [this] { Revoke("wayland-pointer-lock-not-granted"); });
        connect(&relative_, &QWaylandClientExtension::activeChanged, this, [this] {
            if (!relative_.isActive())
                Revoke("wayland-relative-pointer-removed");
        });
        connect(&constraints_, &QWaylandClientExtension::activeChanged, this, [this] {
            if (!constraints_.isActive())
                Revoke("wayland-pointer-constraints-removed");
        });
    }
    ~WaylandPointer() override { End(false); }
    bool Requested() const override { return QtWayland::zwp_locked_pointer_v1::isInitialized(); }
    bool Begin(QWindow& window, QPointF restore, std::function<void(QPointF)> motion,
               std::function<void(const std::string&)> lost, std::string& reason) override {
        End(false);
        if (!window.isExposed() || !relative_.isActive() || !constraints_.isActive()) {
            reason = "wayland-relative-pointer-unavailable";
            return false;
        }
        // The public native interface checks for an absent seat before accessing
        // the pointer. nativeResourceForIntegration("wl_pointer") does not.
        auto* native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        auto* pointer = native ? native->pointer() : nullptr;
        auto* api = QGuiApplication::platformNativeInterface();
        auto* surface =
            api ? static_cast<wl_surface*>(api->nativeResourceForWindow("surface", &window))
                : nullptr;
        if (!pointer || !surface) {
            reason = "wayland-pointer-or-surface-unavailable";
            return false;
        }
        window_ = &window;
        savedCursor_ = window.cursor();
        window.setCursor(Qt::BlankCursor);
        surface_ = surface;
        restore_ = restore;
        motion_ = std::move(motion);
        lost_ = std::move(lost);
        QtWayland::zwp_relative_pointer_v1::init(relative_.get_relative_pointer(pointer));
        QtWayland::zwp_locked_pointer_v1::init(constraints_.lock_pointer(
            surface, pointer, nullptr, QtWayland::zwp_pointer_constraints_v1::lifetime_oneshot));
        // Pointer locking is asynchronous; no nested dispatch or display roundtrip.
        timeout_.start(1000);
        reason.clear();
        return true;
    }
    void End(bool restore) override {
        timeout_.stop();
        locked_ = false;
        motion_ = {};
        lost_ = {};
        if (Requested()) {
            if (restore && window_ && surface_) {
                set_cursor_position_hint(wl_fixed_from_double(restore_.x()),
                                         wl_fixed_from_double(restore_.y()));
                // Hints are surface state. The compositor may choose to honor it;
                // Wayland intentionally supplies no client-side global warp.
                wl_surface_commit(surface_);
            }
            QtWayland::zwp_locked_pointer_v1::destroy();
        }
        if (QtWayland::zwp_relative_pointer_v1::isInitialized())
            QtWayland::zwp_relative_pointer_v1::destroy();
        if (window_)
            window_->setCursor(savedCursor_);
        window_.clear();
        surface_ = nullptr;
        ++generation_;
    }

private:
    void Revoke(const std::string& reason) {
        if (!Requested())
            return;
        locked_ = false;
        timeout_.stop();
        const auto generation = generation_;
        // Do not destroy a protocol listener from inside its own dispatch.
        QTimer::singleShot(0, this, [this, generation, reason] {
            if (generation != generation_)
                return;
            auto lost = lost_;
            End(false);
            if (lost)
                lost(reason);
        });
    }
    void zwp_locked_pointer_v1_locked() override {
        locked_ = true;
        timeout_.stop();
    }
    void zwp_locked_pointer_v1_unlocked() override { Revoke("wayland-pointer-lock-revoked"); }
    void zwp_relative_pointer_v1_relative_motion(uint32_t, uint32_t, wl_fixed_t dx, wl_fixed_t dy,
                                                 wl_fixed_t, wl_fixed_t) override {
        if (locked_ && motion_) {
            const auto motion = motion_; // consumer may cancel the request
            motion({wl_fixed_to_double(dx), wl_fixed_to_double(dy)});
        }
    }
    RelativeManager relative_;
    Constraints constraints_;
    QTimer timeout_;
    QPointer<QWindow> window_;
    wl_surface* surface_ = nullptr;
    QPointF restore_;
    QCursor savedCursor_;
    bool locked_ = false;
    unsigned generation_ = 0;
    std::function<void(QPointF)> motion_;
    std::function<void(const std::string&)> lost_;
};
}
std::unique_ptr<RelativePointer> CreateWaylandRelativePointer() {
    if (!QGuiApplication::platformName().startsWith("wayland"))
        return {};
    return std::make_unique<WaylandPointer>();
}
}
