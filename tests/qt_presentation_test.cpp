#include "viewport.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
#include "mikudancestudio/rhi_vulkan.hpp"
#endif
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ": " << reason << '\n';                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
#ifndef _WIN32
    // This test may expose windows only on an explicitly isolated runner.
    CHECK(qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1");
#endif
    QApplication app(argc, argv);
    CHECK(argc == 3);
    const auto root = std::filesystem::absolute(argv[1]);
    editor::EditorSession session;
    CHECK(session.ImportModel(root / "triangle.pmx", reason));
    CHECK(session.SetBone(0, 0, {0, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(session.Register({{editor::Track::Bone, 0, 0, 0}}, reason));
    CHECK(session.SetBone(0, 0, {3, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(session.Register({{editor::Track::Bone, 0, 0, 10}}, reason));
    const auto revision = session.Revision();
    unsigned backends = 0;
    for (auto backend : {rhi::Backend::D3D11, rhi::Backend::D3D12, rhi::Backend::Vulkan}) {
        if (!rhi::BackendAvailable(backend))
            continue;
        ++backends;
        qt::Viewport viewport(session, std::filesystem::absolute(argv[2]));
        rhi::BackendOptions options;
        options.backend = backend;
        options.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
        if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
            options.driver = rhi::BackendDriver::Software;
        viewport.SetOptions(options);
        viewport.resize(127, 91);
        const auto pump = [&] {
            QElapsedTimer time;
            time.start();
            do {
                app.processEvents();
                QThread::msleep(2);
            } while (time.elapsed() < 50);
        };
#ifndef _WIN32
        viewport.show();
        pump();
        CHECK(viewport.isExposed());
#else
        CHECK(!viewport.isVisible());
#endif
        for (unsigned i = 0; i < 6; ++i) {
            CHECK(session.Seek(i, reason));
            viewport.resize(i % 2 ? 151 : 127, i % 2 ? 103 : 91);
            pump();
            const auto updates = session.Runtime().Counters().frames;
            const auto submissions = viewport.Submissions();
            CHECK(viewport.Render(reason, true));
            CHECK(viewport.Submissions() == submissions + 1);
            CHECK(session.Runtime().Counters().frames == updates);
            CHECK(session.Revision() == revision);
            if (i == 2)
                CHECK(viewport.Reset(reason));
        }
        // Destroy the actual native surface, then recreate it on the same Qt
        // object. No GPU ownership may survive SurfaceAboutToBeDestroyed.
        viewport.destroy();
        CHECK(viewport.Submissions() == 0);
#ifndef _WIN32
        viewport.show();
        pump();
#endif
        CHECK(viewport.Render(reason, true));
        CHECK(session.UnloadModel(0, reason));
        CHECK(viewport.Render(reason, true));
        CHECK(session.Undo(reason));
        CHECK(viewport.Render(reason, true));
#ifdef _WIN32
        CHECK(!viewport.isVisible());
#endif
    }
    CHECK(backends > 0);
    std::cout
        << "Qt native surfaces: resize, seek, device replacement, destroy/recreate and unload passed on "
        << backends << " backends\n";
}
