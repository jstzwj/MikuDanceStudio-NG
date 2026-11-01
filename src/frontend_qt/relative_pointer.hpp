#pragma once
#include <QPointF>
#include <functional>
#include <memory>
#include <string>
class QWindow;
namespace mikudancestudio::qt {
// Surface-local logical motion, including the compositor's normal acceleration.
// A request owns the pointer until release, cancellation or compositor revocation.
class RelativePointer {
public:
    virtual ~RelativePointer() = default;
    virtual bool Begin(QWindow&, QPointF restore, std::function<void(QPointF)> motion,
                       std::function<void(const std::string&)> lost, std::string& reason) = 0;
    virtual void End(bool restore) = 0;
    virtual bool Requested() const = 0;
};
std::unique_ptr<RelativePointer> CreateWaylandRelativePointer();
}
