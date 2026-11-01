#include "export.hpp"
#include "editor_window.hpp"
#include <QApplication>
#include <QFile>
#include <iostream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " reason=" << reason << '\n';        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 3);
    const auto root = std::filesystem::absolute(argv[1]),
               assets = std::filesystem::absolute(argv[2]);
    editor::EditorSession session;
    CHECK(session.ImportModel(root / "bone-only.pmd", reason));
    CHECK(session.SetBone(0, 0, {0, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(session.Register({{editor::Track::Bone, 0, 0, 0}}, reason));
    CHECK(session.SetBone(0, 0, {3, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(session.Register({{editor::Track::Bone, 0, 0, 3}}, reason));
    const auto revision = session.Revision(), updates = session.Runtime().Counters().frames;
    rhi::BackendOptions options;
    options.backend = rhi::Backend::Vulkan;
    if (qEnvironmentVariable("MDS_GPU_BACKEND") == "d3d11")
        options.backend = rhi::Backend::D3D11;
    options.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    CHECK(qt::ExportSequence(session, assets, options, root / "frames", 0, 3, 128, 96, {}, reason));
    CHECK(qt::ExportSequence(session, assets, options, root / "frames-repeat", 0, 3, 128, 96, {},
                             reason));
    for (unsigned i = 0; i < 4; ++i) {
        const auto name = QString("frame_%1.png").arg(i, 8, 10, QChar('0'));
        QFile a(qt::Path(root / "frames") + "/" + name),
            b(qt::Path(root / "frames-repeat") + "/" + name);
        CHECK(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
        CHECK(a.readAll() == b.readAll());
    }
    CHECK(!qt::ExportSequence(
        session, assets, options, root / "cancelled", 0, 3, 128, 96,
        [](unsigned done, unsigned) { return done < 2; }, reason));
    CHECK(reason == "export-cancelled");
    CHECK(!std::filesystem::exists(root / "cancelled"));
    CHECK(session.Revision() == revision && session.Runtime().Counters().frames == updates);
    CHECK(session.Save(
        root / "render.pmm",
        [](const auto& p, const auto& bytes, auto&) {
            QFile f(qt::Path(p));
            return f.open(QIODevice::WriteOnly) &&
                   f.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) ==
                       qint64(bytes.size());
        },
        reason));
    CHECK(session.Open(root / "render.pmm", reason));
    QImage image;
    CHECK(qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 128, 96, image,
                          reason));
    CHECK(image.size() == QSize(128, 96));
    std::cout
        << "Qt offscreen export: deterministic PNG, isolated scene, cancellation, save/reopen passed\n";
}
