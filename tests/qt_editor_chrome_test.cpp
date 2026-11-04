#include "editor_window.hpp"
#include "mikudancestudio/mmd_editor_layout.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QPainter>
#include <iostream>

using namespace mikudancestudio;
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) try {
    QApplication app(argc, argv);
    Check(argc == 3, "usage: assets output-directory");
    Check(QGuiApplication::platformName() == "offscreen" ||
              QGuiApplication::platformName() == "windows",
          "offscreen or never-shown Windows only");
    const auto assets = std::filesystem::absolute(argv[1]);
    const auto output = std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(output);
    QJsonArray profiles;
    for (const char* language : {"en", "ja", "zh_CN"})
        for (int width : {1024, 1920}) {
            qt::EditorWindow window(assets, true);
            Check(window.SetLanguage(language), "UI language");
            // The supplied 1920x1028 screenshot includes a 23-pixel OS title bar.
            window.resize(width, width == 1920 ? 1005 : 768);
            window.ensurePolished();
            window.layout()->activate();
            auto& session = window.Session();
            std::string reason;
            Check(session.SetRenderSize(1280, 720, reason), "16:9 output setting");
            auto camera = session.Runtime().Session()->Pose().camera;
            camera.fov = 30;
            Check(session.SetCamera(camera, reason), "original screenshot camera state");
            session.SetSidebarWidth(width == 1920 ? 415 : 250);
            window.Refresh();
            auto* canvas = window.centralWidget();
            const auto viewport =
                ui::MmdViewport(canvas->width(), canvas->height(),
                                session.Document().envelope.settings.editPanelWidth, 1280, 720);
            Check(window.RenderWindow()->size() == QSize(viewport.width, viewport.height),
                  "output aspect and native render surface agree");
            const auto revision = session.Revision();
            QImage backdrop(canvas->size(), QImage::Format_RGB32);
            canvas->render(&backdrop, {}, {}, QWidget::DrawWindowBackground);
            Check(backdrop.pixelColor(8, 97) == QColor(Qt::white) &&
                      backdrop.pixelColor(8, 122) == QColor(Qt::white) &&
                      backdrop.pixelColor(8, 120) == QColor(Qt::black),
                  "independent original TimelineDC empty waveform rows");
            QImage view;
            auto options = window.RenderWindow()->Options();
#ifdef _WIN32
            options.backend = rhi::Backend::D3D11;
            options.driver = rhi::BackendDriver::Software;
#endif
            window.RenderWindow()->SetOptions(options);
            Check(window.RenderWindow()->PrepareFrame(unsigned(viewport.width),
                                                      unsigned(viewport.height), reason),
                  "production viewport frame including editor overlays");
            auto backend = rhi::CreateOffscreenBackendDevice(unsigned(viewport.width),
                                                             unsigned(viewport.height), options);
            Check(bool(backend), "offscreen device");
            rhi::SceneRenderer renderer(*backend.device);
            rhi::PreparedModelFrame prepared;
            rhi::CompletionToken token;
            const auto& frame = window.RenderWindow()->PreparedFrame();
            Check(bool(renderer.Prepare(1, frame, prepared)) &&
                      bool(renderer.Submit(prepared, token)),
                  "submit own prepared viewport offscreen");
            rhi::AttachmentReadback pixels;
            Check(bool(renderer.ReadTarget(1, frame.finalColor, pixels)),
                  "read own offscreen attachment");
            // A native viewport presents RGB opaquely. Its clear alpha is not
            // QWidget transparency; preserve that presentation in this composite.
            view = QImage(pixels.bytes.data(), viewport.width, viewport.height, viewport.width * 4,
                          QImage::Format_RGBX8888)
                       .copy();
            QImage image(window.size(), QImage::Format_RGB32);
            window.render(&image);
            QPainter painter(&image);
            painter.drawImage(canvas->mapTo(&window, QPoint(viewport.x, viewport.y)), view);
            painter.end();
            const auto name = QString("%1-%2.png").arg(language).arg(width);
            Check(image.save(qt::Path(output / name.toStdString())),
                  "write own offscreen UI capture");
            Check(!window.isVisible() && !canvas->isVisible() && session.Revision() == revision,
                  "capture never shows a window or edits scene state");
            profiles.append(QJsonObject{{"language", language},
                                        {"width", width},
                                        {"visible", false},
                                        {"renderWidth", viewport.width},
                                        {"renderHeight", viewport.height},
                                        {"image", name}});
        }
    QFile report(qt::Path(output / "report.json"));
    Check(report.open(QIODevice::WriteOnly) && report.write(QJsonDocument(profiles).toJson()) > 0,
          "write profile report");
    std::cout << "Six own-window chrome/viewport captures; no shown window or desktop input\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
