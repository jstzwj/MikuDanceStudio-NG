#include "viewport.hpp"
#include <QPointer>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
namespace mikudancestudio::qt {
namespace {
struct Lease { QPointer<QWindow> window; CAMetalLayer* __strong layer; };
}
std::shared_ptr<const rhi::WindowSurfaceSource> QtSurface(QWindow& window,std::string& reason) {
    window.create(); NSView* view=(__bridge NSView*)reinterpret_cast<void*>(window.winId());
    if(!view) { reason="qt-cocoa-view-unavailable"; return {}; }
    auto lease=std::make_shared<Lease>(); lease->window=&window;
    lease->layer=[CAMetalLayer layer]; lease->layer.contentsScale=window.devicePixelRatio();
    view.wantsLayer=YES; view.layer=lease->layer;
    rhi::NativeWindow native{rhi::WindowSystem::Cocoa,reinterpret_cast<std::uintptr_t>((__bridge void*)lease->layer)};
    return rhi::CreateWindowSurface(native,0,lease,[lease] {
        if(!lease->window) return rhi::SurfaceHealth::Removed;
        lease->layer.contentsScale=lease->window->devicePixelRatio();
        return lease->window->width()>0&&lease->window->height()>0?rhi::SurfaceHealth::Ready:rhi::SurfaceHealth::Suspended;
    },reason);
}
}
