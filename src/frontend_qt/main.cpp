#include "editor_window.hpp"
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
#include <QMediaDevices>
#include <QAudioDevice>
#include "mikudancestudio/shader_compiler.hpp"
#include <cstdio>
#include <iostream>

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
                    const std::filesystem::path& assets, const QString& project) {
    using namespace mikudancestudio;
    if (project.isEmpty() || !window.Open(qt::Path(project))) {
        std::cerr << "deployment PMM: " << window.LastError().toStdString() << '\n';
        return 2;
    }
    QJsonArray languages;
    QJsonArray cancelLabels;
    for (const auto& language : qt::Language::Available()) {
        if (!window.SetLanguage(language.code))
            return 3;
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
    if (!qt::RenderImage(window.Session().Runtime(), window.Session().Assets(), assets, options, 96,
                         64, image, reason)) {
        std::cerr << "deployment render: " << reason << '\n';
        return 4;
    }
    if (rhi::BackendAvailable(rhi::Backend::Vulkan) || rhi::BackendAvailable(rhi::Backend::Metal)) {
        rhi::ShaderModule shader;
        shader.stage = rhi::ShaderStage::Pixel;
        shader.entry = "main";
        shader.hlsl = "float4 main():SV_Target0{return float4(1,0,0,1);}";
        rhi::SnapshotBytes spirv;
        if (!rhi::DefaultSpirvCompiler()->Compile(shader, spirv, reason)) {
            std::cerr << "deployment compiler: " << reason << '\n';
            return 5;
        }
    }
    if (window.isVisible() || image.isNull() || !image.save(report + ".png"))
        return 6;
    QJsonArray iconSizes;
    for (const auto& size : QApplication::windowIcon().availableSizes()) {
        if (QApplication::windowIcon().pixmap(size).isNull() ||
            window.windowIcon().pixmap(size).isNull())
            return 8;
        iconSizes.append(QJsonArray{size.width(), size.height()});
    }
    const auto proxy = qobject_cast<QProxyStyle*>(qApp->style());
    const auto styleBase = proxy ? proxy->baseStyle()->objectName() : qApp->style()->objectName();
    QJsonObject result{{"qt", qVersion()},
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
        return 7;
    std::cout << "Relocated editor: PMM, languages, multimedia and offscreen render passed\n";
    return 0;
}
}
int main(int argc, char** argv) {
#ifdef __APPLE__
    qInstallMessageHandler(AppleMessageHandler);
#endif
    QApplication app(argc, argv);
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
    parser.addOption({"assets", "Builtin asset directory", "directory",
                      QDir(QCoreApplication::applicationDirPath() + "/../Resources/assets").exists()
                          ? QCoreApplication::applicationDirPath() + "/../Resources/assets"
                          : QCoreApplication::applicationDirPath() + "/assets"});
    parser.addOption(
        {"deployment-check", "Check the installed editor without showing windows", "report"});
    parser.process(app);
    const auto assets = mikudancestudio::qt::Path(parser.value("assets"));
    mikudancestudio::qt::EditorWindow window(assets, parser.isSet("deployment-check"));
    if (parser.isSet("deployment-check"))
        return DeploymentCheck(window, parser.value("deployment-check"), assets,
                               parser.positionalArguments().value(0));
    if (parser.isSet("language"))
        window.SetLanguage(parser.value("language"));
    if (!parser.positionalArguments().empty())
        window.Open(mikudancestudio::qt::Path(parser.positionalArguments()[0]));
    window.show();
    return app.exec();
}
