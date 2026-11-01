#include "editor_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QDir>
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("MikuDanceStudio");
    QCoreApplication::setApplicationName("MikuDanceStudio-NG");
    app.setStyle(QStyleFactory::create("Fusion"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("project", "PMM project to open");
    parser.addOption({"language", "UI language: en, ja, zh_CN", "language"});
    parser.addOption({"assets", "Builtin asset directory", "directory",
                      QCoreApplication::applicationDirPath() + "/assets"});
    parser.process(app);
    mikudancestudio::qt::EditorWindow window(mikudancestudio::qt::Path(parser.value("assets")));
    if (parser.isSet("language"))
        window.SetLanguage(parser.value("language"));
    if (!parser.positionalArguments().empty())
        window.Open(mikudancestudio::qt::Path(parser.positionalArguments()[0]));
    window.show();
    return app.exec();
}
