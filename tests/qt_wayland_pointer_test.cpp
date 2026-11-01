// A private compositor and child client. Never connects to a user's display.
#include "viewport.hpp"
#include "wayland-relative-pointer-unstable-v1-server-protocol.h"
#include "wayland-pointer-constraints-unstable-v1-server-protocol.h"
#include <QtWaylandCompositor/QWaylandCompositor>
#include <QtWaylandCompositor/QWaylandOutput>
#include <QtWaylandCompositor/QWaylandSeat>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QtWaylandCompositor/QWaylandView>
#include <QtWaylandCompositor/QWaylandXdgShell>
#include <QApplication>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QFile>
#include <QMouseEvent>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <wayland-server.h>
#include <cmath>
#include <cstdio>
#include <iostream>
using namespace mikudancestudio;
namespace {
void Check(bool ok, const char* expression, int line) {
    if (!ok) {
        std::cerr << "line " << line << ": " << expression << '\n';
        std::exit(1);
    }
}
#define CHECK(x) Check(bool(x), #x, __LINE__)
template <class F> bool Wait(F predicate, int timeout = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout)
        QTest::qWait(5);
    return predicate();
}
void Command(const char* command) {
    std::cout << "TEST " << command << std::endl;
}

// Minimal protocol server. Qt supplies the real registry, seat, wl_pointer,
// xdg_surface and event queue; only these two optional protocols are scripted.
struct Globals {
    wl_display* display;
    wl_global *relativeGlobal = nullptr, *constraintsGlobal = nullptr;
    wl_resource *relative = nullptr, *locked = nullptr;
    unsigned locks = 0, lockDestroys = 0, relativeDestroys = 0, hints = 0;
    QPointF hint;
    bool grant;
    Globals(wl_display* d, bool supported, bool allow) : display(d), grant(allow) {
        if (!supported)
            return;
        relativeGlobal = wl_global_create(
            d, &zwp_relative_pointer_manager_v1_interface, 1, this,
            [](wl_client* client, void* data, uint32_t version, uint32_t id) {
                auto* resource = wl_resource_create(
                    client, &zwp_relative_pointer_manager_v1_interface, version, id);
                static const struct zwp_relative_pointer_manager_v1_interface implementation = {
                    [](wl_client*, wl_resource* r) { wl_resource_destroy(r); },
                    [](wl_client* c, wl_resource* r, uint32_t id, wl_resource* pointer) {
                        auto& self = *static_cast<Globals*>(wl_resource_get_user_data(r));
                        CHECK(pointer && !self.relative);
                        self.relative =
                            wl_resource_create(c, &zwp_relative_pointer_v1_interface, 1, id);
                        static const struct zwp_relative_pointer_v1_interface implementation = {
                            [](wl_client*, wl_resource* r) { wl_resource_destroy(r); }};
                        wl_resource_set_implementation(
                            self.relative, &implementation, &self, [](wl_resource* r) {
                                auto& self = *static_cast<Globals*>(wl_resource_get_user_data(r));
                                self.relative = nullptr;
                                ++self.relativeDestroys;
                            });
                    }};
                wl_resource_set_implementation(resource, &implementation, data, nullptr);
            });
        constraintsGlobal = wl_global_create(
            d, &zwp_pointer_constraints_v1_interface, 1, this,
            [](wl_client* client, void* data, uint32_t version, uint32_t id) {
                auto* resource =
                    wl_resource_create(client, &zwp_pointer_constraints_v1_interface, version, id);
                static const struct zwp_pointer_constraints_v1_interface implementation = {
                    [](wl_client*, wl_resource* r) { wl_resource_destroy(r); },
                    [](wl_client* c, wl_resource* r, uint32_t id, wl_resource* surface,
                       wl_resource* pointer, wl_resource* region, uint32_t lifetime) {
                        auto& self = *static_cast<Globals*>(wl_resource_get_user_data(r));
                        CHECK(surface && pointer && !region && !self.locked);
                        CHECK(QWaylandSurface::fromResource(surface));
                        CHECK(lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
                        ++self.locks;
                        self.locked =
                            wl_resource_create(c, &zwp_locked_pointer_v1_interface, 1, id);
                        static const struct zwp_locked_pointer_v1_interface implementation = {
                            [](wl_client*, wl_resource* r) { wl_resource_destroy(r); },
                            [](wl_client*, wl_resource* r, wl_fixed_t x, wl_fixed_t y) {
                                auto& self = *static_cast<Globals*>(wl_resource_get_user_data(r));
                                self.hint = {wl_fixed_to_double(x), wl_fixed_to_double(y)};
                                ++self.hints;
                            },
                            [](wl_client*, wl_resource*, wl_resource*) {}};
                        wl_resource_set_implementation(
                            self.locked, &implementation, &self, [](wl_resource* r) {
                                auto& self = *static_cast<Globals*>(wl_resource_get_user_data(r));
                                self.locked = nullptr;
                                ++self.lockDestroys;
                            });
                        if (self.grant)
                            zwp_locked_pointer_v1_send_locked(self.locked);
                    },
                    [](wl_client*, wl_resource*, uint32_t, wl_resource*, wl_resource*, wl_resource*,
                       uint32_t) {
                        CHECK(false); // This editor requests locking, never confinement.
                    }};
                wl_resource_set_implementation(resource, &implementation, data, nullptr);
            });
    }
    ~Globals() {
        if (relativeGlobal)
            wl_global_destroy(relativeGlobal);
        if (constraintsGlobal)
            wl_global_destroy(constraintsGlobal);
    }
    void Motion() {
        CHECK(relative && locked);
        for (unsigned i = 0; i < 8; ++i)
            // Deliberately different unaccelerated values: normal desktop
            // navigation consumes accelerated surface-coordinate motion.
            zwp_relative_pointer_v1_send_relative_motion(
                relative, 0, i + 1, wl_fixed_from_double(.125), wl_fixed_from_double(.25),
                wl_fixed_from_double(5), wl_fixed_from_double(9));
        wl_display_flush_clients(display);
    }
};

int Client(const QString& mode) {
    CHECK(qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1");
    CHECK(QGuiApplication::platformName().startsWith("wayland"));
    editor::EditorSession session;
    qt::Viewport viewport(session, {});
    viewport.resize(640, 480);
    std::string error;
    viewport.changed = [&](bool ok, const std::string& reason) {
        if (!ok)
            error = reason;
    };
    viewport.show(); // Visible only to the private, memory-only compositor.
    CHECK(Wait([&] { return viewport.isExposed(); }));
    QTest::qWait(50); // Let registry/seat and keyboard-focus events be dispatched.
    const auto initial = session.Document().cameraState[5];
    const auto frames = session.Runtime().Counters().frames;
    const auto begin = [&] { return viewport.BeginHeaderDrag(1, {100, 100}, {40, 20}); };
    if (mode == "missing") {
        CHECK(!begin());
        CHECK(error == "wayland-relative-pointer-unavailable");
        CHECK(!viewport.HeaderDragging() && viewport.cursor().shape() != Qt::BlankCursor);
    } else if (mode == "denied") {
        CHECK(begin());
        CHECK(Wait([&] { return !viewport.HeaderDragging(); }));
        CHECK(error == "wayland-pointer-lock-not-granted");
        CHECK(viewport.cursor().shape() != Qt::BlankCursor);
    } else {
        CHECK(begin());
        CHECK(viewport.cursor().shape() == Qt::BlankCursor);
        QMouseEvent absolute(QEvent::MouseMove, QPointF(400, 400), QPointF(400, 400), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&viewport, &absolute);
        CHECK(session.Document().cameraState[5] == initial);
        QTest::qWait(30);
        Command("motion");
        CHECK(Wait([&] { return session.Document().cameraState[5] != initial; }));
        QTest::qWait(30);
        CHECK(std::abs(session.Document().cameraState[5] -
                       (initial - .2f * viewport.devicePixelRatio())) < 1e-5f);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(100, 100), QPointF(100, 100),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&viewport, &release);
        CHECK(!viewport.HeaderDragging() && viewport.cursor().shape() != Qt::BlankCursor);
        CHECK(begin());
        QTest::qWait(30);
        Command("revoke");
        CHECK(Wait([&] { return !viewport.HeaderDragging(); }));
        CHECK(error == "wayland-pointer-lock-revoked");
        CHECK(viewport.cursor().shape() != Qt::BlankCursor);
        const auto distance = session.Document().cameraState[5];
        CHECK(begin());
        QFocusEvent focus(QEvent::FocusOut);
        QApplication::sendEvent(&viewport, &focus);
        CHECK(!viewport.HeaderDragging());
        CHECK(begin());
        viewport.SetOptions(viewport.Options());
        CHECK(!viewport.HeaderDragging());
        CHECK(begin());
        viewport.destroy();
        CHECK(!viewport.HeaderDragging() && viewport.cursor().shape() != Qt::BlankCursor);
        CHECK(session.Document().cameraState[5] == distance);
    }
    CHECK(session.Runtime().Counters().frames == frames && !session.CanUndo());
    QTest::qWait(30);
    Command("done");
    return 0;
}
int Server(const QString& mode, const QString& scale) {
    CHECK(QGuiApplication::platformName() == "offscreen");
    QTemporaryDir directory;
    CHECK(directory.isValid());
    QFile::setPermissions(directory.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    qputenv("XDG_RUNTIME_DIR", directory.path().toUtf8());
    QWaylandCompositor compositor;
    compositor.setSocketName("mds-pointer-test");
    compositor.setUseHardwareIntegrationExtension(false);
    compositor.create();
    QWaylandOutput output(&compositor, nullptr);
    const QWaylandOutputMode outputMode(QSize(1024, 768), 60000);
    output.addMode(outputMode, true);
    output.setCurrentMode(outputMode);
    compositor.setDefaultOutput(&output);
    QWaylandXdgShell shell(&compositor);
    QObject::connect(&shell, &QWaylandXdgShell::toplevelCreated, &compositor,
                     [&](QWaylandXdgToplevel* top, QWaylandXdgSurface* xdg) {
                         auto* view = new QWaylandView(nullptr, xdg);
                         view->setSurface(xdg->surface());
                         output.surfaceEnter(xdg->surface());
                         top->sendConfigure(QSize(640, 480),
                                            QList<QWaylandXdgToplevel::State>{
                                                QWaylandXdgToplevel::ActivatedState});
                         compositor.defaultSeat()->setKeyboardFocus(xdg->surface());
                         compositor.defaultSeat()->sendMouseMoveEvent(view, {100, 100}, {100, 100});
                     });
    Globals globals(compositor.display(), mode != "missing", mode != "denied");
    QProcess client;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("WAYLAND_DISPLAY", compositor.socketName());
    env.insert("QT_QPA_PLATFORM", "wayland");
    env.insert("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
    env.insert("QT_SCALE_FACTOR", scale);
    env.insert("MDS_QT_ISOLATED_DISPLAY", "1");
    client.setProcessEnvironment(env);
    client.setProgram(QCoreApplication::applicationFilePath());
    client.setArguments({"--client", mode});
    bool done = false;
    QByteArray buffer;
    QObject::connect(&client, &QProcess::readyReadStandardOutput, [&] {
        buffer += client.readAllStandardOutput();
        while (buffer.contains('\n')) {
            const auto index = buffer.indexOf('\n');
            const auto command = buffer.left(index).trimmed();
            buffer.remove(0, index + 1);
            if (command == "TEST motion")
                globals.Motion();
            if (command == "TEST revoke") {
                CHECK(globals.locked);
                zwp_locked_pointer_v1_send_unlocked(globals.locked);
                wl_display_flush_clients(compositor.display());
            }
            if (command == "TEST done")
                done = true;
        }
    });
    QObject::connect(&client, &QProcess::readyReadStandardError,
                     [&] { std::cerr << client.readAllStandardError().constData(); });
    client.start();
    CHECK(Wait([&] { return client.state() == QProcess::NotRunning; }, 10000));
    CHECK(client.exitStatus() == QProcess::NormalExit && client.exitCode() == 0 && done);
    CHECK(!globals.locked && !globals.relative);
    if (mode == "normal") {
        CHECK(globals.locks == 5 && globals.lockDestroys == 5 && globals.relativeDestroys == 5);
        CHECK(globals.hints == 1 && globals.hint == QPointF(40, 20));
    } else if (mode == "denied") {
        CHECK(globals.locks == 1 && globals.lockDestroys == 1 && globals.hints == 0);
    } else
        CHECK(globals.locks == 0);
    std::cout << "Private Wayland pointer protocol: " << mode.toStdString() << ", scale "
              << scale.toStdString() << " passed\n";
    return 0;
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc == 3 && QString(argv[1]) == "--client")
        return Client(argv[2]);
    CHECK(argc == 3);
    return Server(argv[1], argv[2]);
}
