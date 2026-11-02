#include "viewport.hpp"
#include "editor_window.hpp"
#include "mikudancestudio/menu_commands.h"
#include "portable_material_history_fixture.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QPointer>
#include <QCloseEvent>
#include <QKeyEvent>
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
    unsigned backends = 0;
    const auto requested = qEnvironmentVariable("MDS_GPU_BACKEND");
    for (const auto [backend, name] :
         {std::pair{rhi::Backend::D3D11, "d3d11"}, std::pair{rhi::Backend::D3D12, "d3d12"},
          std::pair{rhi::Backend::Vulkan, "vulkan"}, std::pair{rhi::Backend::Metal, "metal"}}) {
        if (!requested.isEmpty() && requested != name)
            continue;
        if (!rhi::BackendAvailable(backend))
            continue;
        std::cout << "Qt native backend: " << name << '\n';
        const auto revision = session.Revision();
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
            const auto builds = viewport.FrameBuilds();
            CHECK(viewport.Render(reason, true));
            CHECK(viewport.FrameBuilds() == builds);
            CHECK(viewport.Submissions() == submissions + 2);
            CHECK(session.Runtime().Counters().frames == updates);
            if (i == 2)
                CHECK(viewport.Reset(reason));
        }
        const auto navigationFrames = session.Runtime().Counters().frames;
        const auto* snapshot = session.Runtime().Snapshot(0);
        const auto beforeNavigation = viewport.FrameBuilds();
        CHECK(session.DragView(false, 13, -7, false, false, reason));
        CHECK(session.DragView(true, -5, 11, true, false, reason));
        CHECK(session.WheelView(-120, reason));
        const auto cameraState = session.Document().cameraState;
        CHECK(session.Runtime().Counters().frames == navigationFrames);
        CHECK(session.Runtime().Snapshot(0) == snapshot);
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.FrameBuilds() == beforeNavigation + 1);
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.FrameBuilds() == beforeNavigation + 1);
        CHECK(viewport.Reset(reason));
        CHECK(viewport.Render(reason, true));
        CHECK(session.Document().cameraState == cameraState);
        CHECK(session.Runtime().Counters().frames == navigationFrames);
        CHECK(session.Runtime().Snapshot(0) == snapshot);
        // Selection and cursor mode do not change the document revision or
        // deform the scene, but must invalidate editor overlay geometry.
        const auto selectionRevision = session.Revision();
        CHECK(session.SelectBones({}, 0, reason));
        CHECK(viewport.Render(reason, true));
        const auto selectionBuilds = viewport.FrameBuilds();
        CHECK(session.SelectBones({0}, 0, reason));
        CHECK(session.Revision() == selectionRevision);
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.FrameBuilds() == selectionBuilds + 1);
        session.SetEditMode(2);
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.FrameBuilds() == selectionBuilds + 2);
        session.SetEditMode(0);
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.FrameBuilds() == selectionBuilds + 3);
        CHECK(session.Runtime().Counters().frames == navigationFrames);
        CHECK(session.SelectModel(255, reason));
        CHECK(viewport.Render(reason, true));
        CHECK(session.SelectModel(0, reason));
        CHECK(viewport.Render(reason, true));
        CHECK(session.Runtime().Counters().frames == navigationFrames);
        // Destroy the actual native surface, then recreate it on the same Qt
        // object. No GPU ownership may survive SurfaceAboutToBeDestroyed.
        viewport.destroy();
        CHECK(viewport.Submissions() == 0);
#ifndef _WIN32
        viewport.show();
        pump();
#endif
        CHECK(viewport.Render(reason, true));
        const auto recreated = viewport.FrameBuilds();
        CHECK(session.UnloadModel(0, reason));
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.FrameBuilds() == recreated + 1);
        CHECK(!session.CanUndo());
        CHECK(session.ImportModel(root / "triangle.pmx", reason));
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.FrameBuilds() == recreated + 2);
        CHECK(session.Edit(
            "capture-history",
            [](auto& d, auto&) {
                d.view.captureMode = 1;
                return true;
            },
            reason));
        CHECK(viewport.Render(reason, true));
        const auto captured = viewport.FrameBuilds();
        CHECK(viewport.Render(reason, true));
        CHECK(viewport.FrameBuilds() == captured + 1);
        CHECK(session.Edit(
            "capture-off",
            [](auto& d, auto&) {
                d.view.captureMode = 0;
                return true;
            },
            reason));
        editor::EditorSession materialSession;
        CHECK(MaterialHistoryScene(materialSession, root, reason));
        qt::Viewport materialViewport(materialSession, std::filesystem::absolute(argv[2]));
        materialViewport.SetOptions(options);
        materialViewport.resize(127, 91);
#ifndef _WIN32
        materialViewport.show();
        pump();
#endif
        CHECK(materialViewport.Render(reason, true));
        const auto materialBuilds = materialViewport.FrameBuilds();
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.FrameBuilds() == materialBuilds + 1);
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.FrameBuilds() == materialBuilds + 1);
        CHECK(materialViewport.Reset(reason));
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.FrameBuilds() == materialBuilds + 3);
        CHECK(materialSession.ImportPicture(std::filesystem::absolute(argv[2]) / "hud_sprites.png",
                                            127, 91, reason));
        CHECK(materialViewport.Render(reason, true));
        const auto backgroundBuilds = materialViewport.FrameBuilds();
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.FrameBuilds() == backgroundBuilds);
        CHECK(materialSession.ShowPicture(false, reason));
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.FrameBuilds() == backgroundBuilds + 1);
        CHECK(materialSession.ShowPicture(true, reason));
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.FrameBuilds() == backgroundBuilds + 2);
        CHECK(materialViewport.Reset(reason));
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.FrameBuilds() == backgroundBuilds + 4);
        CHECK(materialViewport.Render(reason, true));
        CHECK(materialViewport.FrameBuilds() == backgroundBuilds + 4);
        // Use the production editor container lifecycle, not only a standalone
        // QWindow. Windows never exposes either top-level window; other systems
        // reach this test only in the isolated display configured above.
        qt::EditorWindow editor(std::filesystem::absolute(argv[2]), true);
        auto& live = editor.Session();
        CHECK(live.ImportModel(root / "articulated.pmd", reason));
        CHECK(live.SetBone(0, 0, {2, 0, 0}, {0, 0, 0, 1}, reason));
        editor.RenderWindow()->SetOptions(options);
        editor.resize(640, 540);
        editor.Refresh();
#ifndef _WIN32
        editor.show();
        pump();
#endif
        CHECK(editor.RenderWindow()->Render(reason, true));
        QSize outputLimits;
        CHECK(editor.RenderWindow()->OutputLimits(outputLimits, reason));
        CHECK(outputLimits.width() >= 2048 && outputLimits.width() <= 4096);
        const auto* sizeRuntime = &live.Runtime();
        const auto sizeFrames = live.Runtime().Counters().frames;
        for (const auto size : {QSize(1001, 717), QSize(1280, 720), QSize(640, 480)}) {
            CHECK(live.SetRenderSize(unsigned(size.width()), unsigned(size.height()), reason));
            auto resetOptions = editor.RenderWindow()->Options();
            resetOptions.surface.reset();
            editor.RenderWindow()->SetOptions(resetOptions);
            editor.Refresh();
            pump();
            CHECK(editor.RenderWindow()->Render(reason, true));
            CHECK(editor.RenderWindow()->Render(reason, true));
            CHECK(sizeRuntime == &live.Runtime() && sizeFrames == live.Runtime().Counters().frames);
        }
        const auto* liveRuntime = &live.Runtime();
        const auto* liveSnapshot = live.Runtime().Snapshot(0);
        const auto liveBodies = live.Runtime().Bodies(0);
        const auto liveFrames = live.Runtime().Counters().frames;
        const auto liveRevision = live.Revision();
        const auto liveCamera = live.Document().cameraState;
        const auto command = [&](int id) -> QAction* {
            for (auto* action : editor.findChildren<QAction*>())
                if (action->property("mmdCommandId").toInt() == id)
                    return action;
            return nullptr;
        };
        for (unsigned cycle = 0; cycle < 3; ++cycle) {
            QPointer<qt::Viewport> former = editor.RenderWindow();
            command(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
            CHECK(former.isNull() && editor.SeparateWindow());
            CHECK(editor.RenderWindow()->Options().backend == backend);
            CHECK(editor.RenderWindow()->Submissions() == 0);
            editor.SeparateWindow()->resize(257 + cycle * 32, 249 + cycle * 24);
            editor.Refresh();
#ifndef _WIN32
            editor.SeparateWindow()->show();
            pump();
#endif
            CHECK(editor.RenderWindow()->Render(reason, true));
            CHECK(editor.RenderWindow()->Reset(reason));
            CHECK(editor.RenderWindow()->Render(reason, true));
            command(MDS_CMD_TOGGLE_ALWAYS_ON_TOP)->trigger();
#ifndef _WIN32
            editor.SeparateWindow()->show();
            pump();
#endif
            CHECK(editor.RenderWindow()->Render(reason, true));
            CHECK(editor.SetLanguage(cycle == 0 ? "en" : cycle == 1 ? "ja" : "zh_CN"));
            former = editor.RenderWindow();
            QPointer<QWidget> floating = editor.SeparateWindow();
            QCloseEvent close;
            QCoreApplication::sendEvent(floating, &close);
            CHECK(former.isNull() && !editor.SeparateWindow());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            CHECK(floating.isNull());
            pump();
            CHECK(editor.RenderWindow()->Render(reason, true));
            CHECK(&live.Runtime() == liveRuntime && live.Runtime().Snapshot(0) == liveSnapshot);
            CHECK(live.Runtime().Bodies(0) == liveBodies);
            CHECK(live.Runtime().Counters().frames == liveFrames &&
                  live.Revision() == liveRevision);
            CHECK(live.Document().cameraState == liveCamera && live.CanUndo());
#ifdef _WIN32
            CHECK(!editor.isVisible());
#endif
        }
        command(MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
#ifndef _WIN32
        editor.SeparateWindow()->show();
        pump();
#endif
        CHECK(editor.RenderWindow()->Render(reason, true));
        command(MDS_CMD_TOGGLE_FULL_SCREEN)->trigger();
        CHECK(editor.FullScreenViewport() && !editor.SeparateWindow());
        pump();
        CHECK(editor.RenderWindow()->Render(reason, true));
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(editor.RenderWindow(), &escape);
        pump();
        CHECK(!editor.FullScreenViewport() && editor.SeparateWindow());
#ifndef _WIN32
        editor.SeparateWindow()->show();
        pump();
#endif
        CHECK(editor.RenderWindow()->Render(reason, true));
        CHECK(&live.Runtime() == liveRuntime && live.Runtime().Snapshot(0) == liveSnapshot);
        CHECK(live.Runtime().Bodies(0) == liveBodies &&
              live.Runtime().Counters().frames == liveFrames);
        CHECK(live.Revision() == liveRevision);
#ifdef __APPLE__
        // Exercise the production menu transaction on the same live session.
        for (auto selected : {rhi::Backend::Metal, rhi::Backend::Vulkan, backend}) {
            if (selected != backend && qEnvironmentVariable("MDS_QT_TEST_BACKEND_SWITCH") != "1")
                continue;
            if (!rhi::BackendAvailable(selected))
                continue;
            QPointer<qt::Viewport> former = editor.RenderWindow();
            QAction* choice = nullptr;
            for (auto* action : editor.findChildren<QAction*>())
                if (action->property("rhiBackend").isValid() &&
                    action->property("rhiBackend").toInt() == int(selected))
                    choice = action;
            CHECK(choice && choice->isEnabled());
            choice->trigger();
            pump();
            CHECK(former.isNull() && editor.RenderWindow()->Options().backend == selected);
            CHECK(editor.RenderWindow()->Render(reason, true));
            CHECK(&live.Runtime() == liveRuntime && live.Runtime().Counters().frames == liveFrames);
            CHECK(live.Revision() == liveRevision && live.Runtime().Snapshot(0) == liveSnapshot);
        }
#endif
        CHECK(live.UnloadModel(0, reason));
        editor.Refresh();
        CHECK(editor.RenderWindow()->Render(reason, true));
        CHECK(editor.close());
        CHECK(!editor.isVisible() && !editor.SeparateWindow()->isVisible());
#ifdef _WIN32
        CHECK(!viewport.isVisible());
        CHECK(!materialViewport.isVisible());
#endif
    }
    CHECK(backends > 0);
    std::cout
        << "Qt native surfaces: resize, seek, device replacement, detach/dock, destroy/recreate and unload passed on "
        << backends << " backends\n";
}
