#include "editor_window.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QScreen>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

using namespace mikudancestudio;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
class DpiEvents final : public QAbstractNativeEventFilter {
public:
    unsigned changes = 0;
    HWND window = nullptr;
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
        const auto* native = static_cast<const MSG*>(message);
        if (native->hwnd == window && native->message == WM_DPICHANGED)
            ++changes;
        return false;
    }
};
}
int main(int argc, char** argv) try {
    // An opt-in self-hosted runner must be attached to its own disposable
    // interactive session. This target is never a local/ordinary CTest gate.
    Check(qEnvironmentVariable("GITHUB_ACTIONS") == "true" &&
              qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1",
          "physical monitor transitions require a dedicated isolated runner");
    for (const auto* variable :
         {"QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_FONT_DPI", "QT_DEVICE_PIXEL_RATIO"})
        Check(!qEnvironmentVariableIsSet(variable), "synthetic scale override cannot qualify DPI");
    QApplication app(argc, argv);
    Check(argc == 3 && QGuiApplication::platformName() == "windows",
          "Windows QPA, assets and output report required");
    const auto screens = app.screens();
    Check(screens.size() >= 2, "two actual OS displays are required; no synthetic fallback");
    auto* first = screens.front();
    auto different = std::find_if(screens.begin() + 1, screens.end(), [&](const auto* screen) {
        return qAbs(screen->devicePixelRatio() - first->devicePixelRatio()) > .1;
    });
    Check(different != screens.end(), "actual OS screens must have different device ratios");
    auto* second = *different;
    const QString output = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(QFileInfo(output).absolutePath());
    qt::EditorWindow editor(std::filesystem::absolute(argv[1]), true);
    editor.resize(900, 650);
    auto* viewport = editor.RenderWindow();
    auto options = viewport->Options();
    options.backend = rhi::Backend::D3D11;
    options.driver = rhi::BackendDriver::Software;
    viewport->SetOptions(options);
    editor.show();
    DpiEvents events;
    events.window = reinterpret_cast<HWND>(editor.winId());
    app.installNativeEventFilter(&events);
    const auto revision = editor.Session().Revision();
    const auto updates = editor.Session().Runtime().Counters().frames;
    QJsonArray records;
    for (const char* language : {"en", "ja", "zh_CN"}) {
        Check(editor.SetLanguage(language), "language switch at actual DPI");
        for (auto* target : {first, second, first}) {
            const auto geometry = target->availableGeometry();
            editor.move(geometry.topLeft() + QPoint(24, 24));
            QElapsedTimer wait;
            wait.start();
            while (wait.elapsed() < 5000) {
                QCoreApplication::processEvents();
                if (editor.windowHandle()->screen() == target && viewport->isExposed() &&
                    qAbs(viewport->devicePixelRatio() - target->devicePixelRatio()) < .01)
                    break;
                QTest::qWait(10);
            }
            Check(editor.windowHandle()->screen() == target && viewport->isExposed(),
                  "actual native window crossed to the requested OS monitor");
            const auto ratio = target->devicePixelRatio();
            const auto nativeDpi = GetDpiForWindow(events.window);
            Check(nativeDpi && qAbs(nativeDpi / 96. - ratio) < .01,
                  "USER32 effective DPI agrees with the actual target monitor");
            Check(qAbs(viewport->devicePixelRatio() - ratio) < .01 &&
                      GetDpiForWindow(reinterpret_cast<HWND>(viewport->winId())) == nativeDpi,
                  "embedded viewport follows native window DPI");
            std::string reason;
            const auto builds = viewport->FrameBuilds();
            Check(viewport->Render(reason), reason.c_str());
            const auto& frame = viewport->PreparedFrame();
            const auto color =
                std::find_if(frame.targets.begin(), frame.targets.end(),
                             [&](const auto& item) { return item.id == frame.finalColor; });
            Check(color != frame.targets.end() &&
                      color->description.width == unsigned(qRound(viewport->width() * ratio)) &&
                      color->description.height == unsigned(qRound(viewport->height() * ratio)),
                  "final color rebuilt at actual physical-pixel dimensions");
            Check(viewport->Render(reason), reason.c_str());
            const auto stableBuilds = viewport->FrameBuilds();
            Check(viewport->Render(reason) && viewport->FrameBuilds() == stableBuilds,
                  "stable monitor frame reuses prepared resources");
            auto* edit = qobject_cast<QLineEdit*>(editor.Control(panel::kCurrentFrameEdit));
            Check(edit != nullptr, "production frame input");
            edit->setText("17");
            edit->setCursorPosition(1);
            const auto caret = edit->inputMethodQuery(Qt::ImCursorRectangle).toRect();
            Check(caret.width() == 1 && caret.height() > 0 && edit->rect().intersects(caret),
                  "logical IME caret remains inside input");
            editor.grab().save(QFileInfo(output).absolutePath() + '/' +
                               QString("%1-%2.png").arg(language).arg(records.size()));
            records.append(QJsonObject{
                {"language", language},
                {"screen", target->name()},
                {"devicePixelRatio", ratio},
                {"nativeDpi", int(nativeDpi)},
                {"nativeDpiEvents", int(events.changes)},
                {"frameBuildsBefore", double(builds)},
                {"physicalWidth", int(color->description.width)},
                {"physicalHeight", int(color->description.height)},
                {"imeCaret", QJsonArray{caret.x(), caret.y(), caret.width(), caret.height()}}});
        }
    }
    Check(events.changes >= 6, "actual WM_DPICHANGED transitions must have occurred");
    Check(editor.Session().Revision() == revision &&
              editor.Session().Runtime().Counters().frames == updates,
          "language/monitor transition must not edit or advance the scene");
    app.removeNativeEventFilter(&events);
    editor.close();
    QFile report(output);
    Check(report.open(QIODevice::WriteOnly), "write physical DPI report");
    report.write(QJsonDocument(QJsonObject{{"syntheticScaling", false},
                                           {"nativeDpiEvents", int(events.changes)},
                                           {"records", records}})
                     .toJson());
    std::cout << records.size() << " real-monitor frames; " << events.changes
              << " native DPI transitions\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
