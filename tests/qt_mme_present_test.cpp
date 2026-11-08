#include "viewport.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <fstream>
#include <iostream>
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
    CHECK(qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1");
#endif
    QApplication app(argc, argv);
#ifdef _WIN32
    CHECK(QGuiApplication::platformName() == "windows");
#endif
    CHECK(argc == 3);
    const auto root = std::filesystem::absolute(argv[1]);
    std::filesystem::copy(MDS_EDITOR_EFFECTS_DIR, root / "effects",
                          std::filesystem::copy_options::recursive);
    editor::EditorSession session;
    CHECK(session.ImportModel(root / "triangle.pmx", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    const editor::EffectAddress model{false, 0, -1}, carrier{true, 0, -1};
    CHECK(session.AssignEffect(model, root / "effects/Material.fx", reason));
    CHECK(session.AssignEffect(carrier, root / "effects/Scene.fx", reason));
    qt::Viewport viewport(session, std::filesystem::absolute(argv[2]));
    rhi::BackendOptions options;
    const auto api = qEnvironmentVariable("MDS_GPU_BACKEND");
    options.backend = api == "d3d12"    ? rhi::Backend::D3D12
                      : api == "d3d11"  ? rhi::Backend::D3D11
                      : api == "vulkan" ? rhi::Backend::Vulkan
                      : api == "metal"  ? rhi::Backend::Metal
                                        : rhi::DefaultBackend();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    options.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
    viewport.SetOptions(options);
    viewport.resize(127, 95);
    const auto expose = [&] {
#ifndef _WIN32
        viewport.show();
        QElapsedTimer deadline;
        deadline.start();
        while (!viewport.isExposed() && deadline.elapsed() < 5000) {
            app.processEvents();
            QThread::msleep(5);
        }
#endif
    };
    expose();
    for (unsigned n = 0; n < 3; ++n) {
        CHECK(session.Seek(n, reason));
        const auto updates = session.Runtime().Counters().frames;
        viewport.resize(127 + 2 * n, 95 + 2 * n);
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.EffectReport().find("submitted-and-committed") == 0);
        const auto submissions = viewport.Submissions();
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.Submissions() == submissions + 1);
        CHECK(session.Runtime().Counters().frames == updates);
        CHECK(viewport.Reset(reason));
        CHECK(viewport.Render(reason, true));
    }
    viewport.destroy();
    expose();
    CHECK(viewport.Render(reason, true));
    std::ofstream(root / "effects/broken.fx") << "unsupported syntax";
    CHECK(session.AssignEffect(model, root / "effects/broken.fx", reason));
    const auto updates = session.Runtime().Counters().frames;
    const auto beforeError = viewport.Submissions();
    CHECK(!viewport.Render(reason, true) && !reason.empty());
    // A new clear-only error surface was submitted; the old scene was not
    // reused, and the failed schedule was never reported as committed.
    CHECK(viewport.Submissions() == beforeError + 1);
    CHECK(viewport.EffectReport().find("submitted-and-committed") != 0);
    CHECK(!viewport.Render(reason, true));
    CHECK(viewport.Submissions() == beforeError + 2);
    CHECK(session.Runtime().Counters().frames == updates);
    std::filesystem::copy_file(root / "effects/Material.fx", root / "effects/broken.fx",
                               std::filesystem::copy_options::overwrite_existing);
    CHECK(session.ReloadEffect(model, reason));
    CHECK(viewport.Render(reason, true));
    CHECK(viewport.EffectReport().find("submitted-and-committed") == 0);
    CHECK(session.Runtime().Counters().frames == updates);
    CHECK(session.UnloadModel(0, reason));
    CHECK(viewport.Render(reason, true));
#ifdef _WIN32
    CHECK(!viewport.isVisible());
#endif
    std::cout
        << "Qt MME native presentation: submit/commit, resize, seek, reset, surface recreation, error surface, reload, unload passed\n";
}
