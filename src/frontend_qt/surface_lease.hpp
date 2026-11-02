#pragma once
#include <QPointer>
#include <QPlatformSurfaceEvent>
#include <QWindow>
namespace mikudancestudio::qt {
// A QWindow can destroy/recreate its native surface without being deleted.
// Each lease belongs to one generation; an old backend must never revive when
// the same QWindow later acquires a different native handle.
class QtWindowLease : public QObject {
public:
    explicit QtWindowLease(QWindow& value) : window(&value) { value.installEventFilter(this); }
    QPointer<QWindow> window;
    bool nativeAlive = true;

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::PlatformSurface &&
            static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
                QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            nativeAlive = false;
        return false;
    }
};
}
