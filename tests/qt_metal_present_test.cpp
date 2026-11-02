#include "viewport.hpp"
#include "mikudancestudio/rhi_metal.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "material_sampling_fixture.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
int main(int argc, char** argv) try {
    CHECK(qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1");
    QApplication app(argc, argv);
    auto window = std::make_unique<QWindow>();
    window->resize(97, 73);
    window->show();
    const auto pump = [&] {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 100) {
            app.processEvents();
            QThread::msleep(2);
        }
    };
    pump();
    std::string reason;
    BackendOptions options;
    options.backend = Backend::Metal;
    options.surface = qt::QtSurface(*window, reason);
    CHECK(options.surface);
    std::weak_ptr<void> lease = options.surface->lease;
    SwapchainDesc desc;
    desc.window = options.surface->window;
    desc.present = PresentMode::Immediate;
    desc.width = qRound(window->width() * window->devicePixelRatio());
    desc.height = qRound(window->height() * window->devicePixelRatio());
    auto backend = CreateBackendDevice(desc, options);
    if (!backend)
        throw std::runtime_error(backend.reason);
    CHECK(backend.swapchain->Health() == SurfaceHealth::Ready);
    auto unsupported = desc;
    unsupported.samples = 4;
    CHECK(!backend.swapchain->Query(unsupported).supported);
    auto& device = *backend.device;
    SceneRenderer renderer(device);
    PreparedModelFrame prepared;
    CompletionToken token;
    for (auto logical : {QSize(97, 73), QSize(151, 103), QSize(97, 73)}) {
        window->resize(logical);
        pump();
        desc.width = qRound(window->width() * window->devicePixelRatio());
        desc.height = qRound(window->height() * window->devicePixelRatio());
        CHECK(backend.swapchain->Rebuild(desc, RebuildReason::Resize) == RebuildResult::Success);
        CHECK(!renderer.Submit(prepared, token));
        auto frame = sampling_fixture::Fixture(0, SamplerFilter::Point, 1, SamplerAddress::Clamp, 1,
                                               false, false);
        frame.finalColor = 1;
        for (auto& target : frame.targets)
            if (target.id != 3) {
                target.description.width = desc.width;
                target.description.height = desc.height;
            }
        auto result = renderer.Prepare(1, frame, prepared, FrameOutput::Presentation);
        if (!result)
            throw std::runtime_error(result.reason);
        const auto before = QueryMetalStats(device);
        for (unsigned n = 0; n < 8; ++n) {
            CHECK(renderer.Submit(prepared, token) && device.Present());
            CHECK(!device.Present());
        }
        const auto after = QueryMetalStats(device);
        CHECK(after.presentations == before.presentations + 8 &&
              after.readbacks == before.readbacks);
        RgbaImage presented;
        AttachmentReadback expected;
        CHECK(ReadBackendPresentation(Backend::Metal, device, presented));
        CHECK(renderer.ReadTarget(1, 1, expected));
        CHECK(presented.width == desc.width && presented.height == desc.height &&
              presented.pixels == expected.bytes);
    }
    CHECK(renderer.Unload(1) && renderer.Detach());
    // The window can disappear first; a moved-out device must keep its layer
    // lease until native command queues and textures have been destroyed.
    window->destroy();
    pump();
    CHECK(backend.swapchain->Health() == SurfaceHealth::Removed);
    window->create();
    CHECK(backend.swapchain->Health() == SurfaceHealth::Removed);
    window.reset();
    backend.swapchain.reset();
    auto moved = std::move(backend.device);
    options.surface.reset();
    CHECK(!lease.expired());
    moved.reset();
    CHECK(lease.expired());
    std::cout
        << "Metal Qt: physical pixels, exact BGRA presentation, zero implicit readbacks, resize and host lease passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
