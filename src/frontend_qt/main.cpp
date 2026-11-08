#include "editor_window.hpp"
#include "export.hpp"
#include "editor_style.hpp"
#include "editor_font.hpp"
#include <QApplication>
#include <QIcon>
#include <QProxyStyle>
#include <QCommandLineParser>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QMediaDevices>
#include <QAudioDevice>
#include "mikudancestudio/shader_compiler.hpp"
#include <cstdio>
#include <iostream>
#ifdef __ANDROID__
#include "android_files.hpp"
#include "android_input.hpp"
#include "android_acceptance.hpp"
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#endif

namespace {
#ifdef __APPLE__
void AppleMessageHandler(QtMsgType, const QMessageLogContext& context, const QString& message) {
    // Qt 6.8.3's AppleUnifiedLogger retains a static QString bundle identifier.
    // Plugin cleanup can log after that object has been destroyed (observed as
    // an invalid free on macOS 26). Keep the sink alive through process exit,
    // with no static Qt objects. Category filtering and fatal handling remain Qt's.
    const auto utf8 = message.toUtf8();
    std::fprintf(stderr, "[%s] %s\n", context.category ? context.category : "default",
                 utf8.constData());
    std::fflush(stderr);
}
#endif
int DeploymentCheck(mikudancestudio::qt::EditorWindow& window, const QString& report,
                    const std::filesystem::path& assets, const QString& project,
                    bool mmeCheck = false) {
    using namespace mikudancestudio;
    const auto fail = [&](int code, const QString& message) {
        // Android does not forward ordinary stderr to logcat. Keep failure
        // diagnostics available even when the native process exits early.
        qCritical().noquote() << message;
        QFile file(report);
        if (file.open(QIODevice::WriteOnly))
            file.write(
                QJsonDocument(QJsonObject{{"state", "failed"},
                                          {"version", QCoreApplication::applicationVersion()},
                                          {"reason", message},
                                          {"exitCode", code}})
                    .toJson());
        return code;
    };
    if (project.isEmpty() || !window.Open(qt::Path(project))) {
        return fail(2, QString("deployment PMM: %1 (project: %2, exists: %3)")
                           .arg(window.LastError(), project)
                           .arg(QFileInfo::exists(project)));
    }
    QJsonArray languages;
    QJsonArray cancelLabels;
    for (const auto& language : qt::Language::Available()) {
        if (!window.SetLanguage(language.code))
            return fail(3, "deployment language: " + language.code);
        languages.append(language.code);
        cancelLabels.append(QCoreApplication::translate("QPlatformTheme", "Cancel"));
    }
    // Instantiate the multimedia backend without opening an audio output.
    const auto audioDevices = QMediaDevices::audioOutputs();
    rhi::BackendOptions options;
    const auto backend = qEnvironmentVariable("MDS_GPU_BACKEND");
    options.backend = backend == "d3d12"    ? rhi::Backend::D3D12
                      : backend == "d3d11"  ? rhi::Backend::D3D11
                      : backend == "vulkan" ? rhi::Backend::Vulkan
                      : backend == "metal"  ? rhi::Backend::Metal
                                            : rhi::DefaultBackend();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    QImage image;
    std::string reason;
    if (mmeCheck) {
        auto& session = window.Session();
        if (session.Document().models.empty() ||
            !session.ImportAccessory(qt::Path(project).parent_path() / "bound.x", reason) ||
            !session.AssignEffect({false, session.Document().models.front().slot, -1},
                                  assets / "effects/Material.fx", reason) ||
            !session.AssignEffect({true, session.Document().accessories.back().slot, -1},
                                  assets / "effects/Scene.fx", reason))
            return fail(10, "deployment MME assignment: " + QString::fromStdString(reason));
    }
    if (!qt::RenderImage(window.Session().Runtime(), window.Session().Assets(), assets, options, 96,
                         64, image, reason, window.Session().DisplayOptions(),
                         window.Session().Effects())) {
        return fail(4, "deployment render: " + QString::fromStdString(reason));
    }
    if (mmeCheck) {
        auto& session = window.Session();
        QImage basic;
        if (!qt::RenderImage(session.Runtime(), session.Assets(), assets, options, 96, 64, basic,
                             reason) ||
            basic == image)
            return fail(11, "deployment MME did not change the image: " +
                                QString::fromStdString(reason));
        const auto saved = qt::Path(report + ".pmm");
        if (!window.Save(saved) || !window.Open(saved) || session.Effects().assignments.size() != 2)
            return fail(12, "deployment MME save/reopen: " + window.LastError());
        const auto frames = session.Runtime().Counters().frames;
        if (!qt::ExportSequence(session, assets, options, qt::Path(report + ".sequence"), 0, 1, 96,
                                64, {}, reason))
            return fail(13, "deployment MME export: " + QString::fromStdString(reason));
        if (QImage(report + ".sequence/frame_00000000.png")
                    .convertToFormat(QImage::Format_RGBA8888) != image ||
            session.Runtime().Counters().frames != frames)
            return fail(14, "deployment MME export/CPU snapshot mismatch");
    }
    const bool runtimeCompiler = rhi::DefaultSpirvCompiler()->Available();
#ifndef __ANDROID__
    if (rhi::BackendAvailable(rhi::Backend::Vulkan) || rhi::BackendAvailable(rhi::Backend::Metal)) {
        rhi::ShaderModule shader;
        shader.stage = rhi::ShaderStage::Pixel;
        shader.entry = "main";
        shader.hlsl = "float4 main():SV_Target0{return float4(1,0,0,1);}";
        rhi::SnapshotBytes spirv;
        if (!rhi::DefaultSpirvCompiler()->Compile(shader, spirv, reason)) {
            return fail(5, "deployment compiler: " + QString::fromStdString(reason));
        }
    }
#endif
    if (window.isVisible() || image.isNull() || !image.save(report + ".png"))
        return fail(6, "deployment image capture failed");
    QJsonArray iconSizes;
    for (const auto& size : QApplication::windowIcon().availableSizes()) {
        if (QApplication::windowIcon().pixmap(size).isNull() ||
            window.windowIcon().pixmap(size).isNull())
            return fail(8, "deployment application icon failed");
        iconSizes.append(QJsonArray{size.width(), size.height()});
    }
    const auto proxy = qobject_cast<QProxyStyle*>(qApp->style());
    const auto styleBase = proxy ? proxy->baseStyle()->objectName() : qApp->style()->objectName();
    QJsonObject result{{"qt", qVersion()},
                       {"mme", mmeCheck},
                       {"shaderCompiler", qt::Path(rhi::DefaultSpirvCompiler()->Executable())},
                       {"version", QCoreApplication::applicationVersion()},
                       {"runtimeShaderCompiler", runtimeCompiler},
                       {"applicationIconSizes", iconSizes},
                       {"bundledFonts", mikudancestudio::qt::InstallEditorFonts()},
                       {"platform", QGuiApplication::platformName()},
                       {"styleBase", styleBase},
                       {"languages", languages},
                       {"standardCancelLabels", cancelLabels},
                       {"plugins", QLibraryInfo::path(QLibraryInfo::PluginsPath)},
                       {"libraries", QLibraryInfo::path(QLibraryInfo::LibrariesPath)},
                       {"translations", QLibraryInfo::path(QLibraryInfo::TranslationsPath)},
                       {"assets", qt::Path(assets)},
                       {"visible", false},
                       {"audioDevices", int(audioDevices.size())},
                       {"width", image.width()},
                       {"height", image.height()},
                       {"vulkan", rhi::BackendAvailable(rhi::Backend::Vulkan)},
                       {"metal", rhi::BackendAvailable(rhi::Backend::Metal)},
                       {"nativeMetal", options.backend == rhi::Backend::Metal}};
    QFile output(report);
    if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(result).toJson()) < 0)
        return fail(7, "deployment report write failed: " + output.errorString());
    std::cout << "Relocated editor: PMM, languages, multimedia and offscreen render passed\n";
    return 0;
}
}
int main(int argc, char** argv) {
#ifdef __APPLE__
    qInstallMessageHandler(AppleMessageHandler);
#endif
    QApplication app(argc, argv);
#ifdef __ANDROID__
    mikudancestudio::qt::InstallAndroidInput();
#endif
    QCoreApplication::setOrganizationName("MikuDanceStudio");
    QCoreApplication::setApplicationName("MikuDanceStudio-NG");
    QCoreApplication::setApplicationVersion(MDS_PRODUCT_VERSION);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));
    mikudancestudio::qt::InstallEditorStyle(app);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("project", "PMM project to open");
    QStringList languageCodes{"system"};
    for (const auto& language : mikudancestudio::qt::Language::Available())
        languageCodes.append(language.code);
    parser.addOption({"language", "UI language: " + languageCodes.join(", "), "language"});
    parser.addOption({"gpu-driver", "GPU driver: hardware or software", "driver"});
    parser.addOption({"assets", "Builtin asset directory", "directory",
                      QDir(QCoreApplication::applicationDirPath() + "/../Resources/assets").exists()
                          ? QCoreApplication::applicationDirPath() + "/../Resources/assets"
                          : QCoreApplication::applicationDirPath() + "/assets"});
    parser.addOption(
        {"deployment-check", "Check the installed editor without showing windows", "report"});
    parser.addOption({"mme-deployment-check",
                      "Check installed FX, save/reopen and export without showing windows",
                      "report"});
#ifdef __ANDROID__
    parser.addOption(
        {"android-acceptance", "Isolated emulator presentation and lifecycle check", "report"});
    parser.addOption(
        {"android-ui-report", "Observe ordinary editor UI state for emulator tests", "report"});
#endif
    parser.process(app);
    if (parser.isSet("gpu-driver")) {
        const auto driver = parser.value("gpu-driver");
        if (driver != "hardware" && driver != "software")
            parser.showHelp(2);
        qputenv("MDS_GPU_DRIVER", driver.toLatin1());
    }
    QString assetsPath = parser.value("assets");
#ifdef __ANDROID__
    QString reason;
    assetsPath = mikudancestudio::qt::PrepareAndroidAssets(reason);
    if (assetsPath.isEmpty()) {
        std::cerr << reason.toStdString() << '\n';
        return 9;
    }
#endif
    const auto assets = mikudancestudio::qt::Path(assetsPath);
    bool testing = parser.isSet("deployment-check") || parser.isSet("mme-deployment-check");
#ifdef __ANDROID__
    testing = testing || parser.isSet("android-acceptance");
#endif
    mikudancestudio::qt::EditorWindow window(assets, testing);
    if (parser.isSet("deployment-check"))
        return DeploymentCheck(window, parser.value("deployment-check"), assets,
                               parser.positionalArguments().value(0));
    if (parser.isSet("mme-deployment-check"))
        return DeploymentCheck(window, parser.value("mme-deployment-check"), assets,
                               parser.positionalArguments().value(0), true);
    if (parser.isSet("language"))
        window.SetLanguage(parser.value("language"));
    if (!parser.positionalArguments().empty())
        window.Open(mikudancestudio::qt::Path(parser.positionalArguments()[0]));
#ifdef __ANDROID__
    if (parser.isSet("android-acceptance"))
        return mikudancestudio::qt::RunAndroidAcceptance(app, window,
                                                         parser.value("android-acceptance"));
    if (parser.isSet("android-ui-report"))
        mikudancestudio::qt::StartAndroidUiReport(window, parser.value("android-ui-report"));
    if (!window.menuBar()->actions().isEmpty()) {
        auto* menu = window.menuBar()->actions().front()->menu();
        auto* action = menu->addAction(QObject::tr("Import workspace folder…"));
        QObject::connect(action, &QAction::triggered, &window, [&window] {
            QString error;
            if (!mikudancestudio::qt::ImportAndroidWorkspace(&window, error))
                QMessageBox::warning(&window, QObject::tr("Workspace import"), error);
        });
    }
#endif
    window.show();
    return app.exec();
}
