#include "language.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QXmlStreamReader>
#include <iostream>
#include <stdexcept>

using namespace mikudancestudio::qt;
int main(int argc, char** argv) try {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        throw std::runtime_error("expected translations directory");
    int count = 0;
    Language language;
    for (const auto& info : Language::Available()) {
        if (!language.Set(info.code))
            throw std::runtime_error("cannot load QM: " + info.code.toStdString());
        QFile source(
            QDir(QString::fromLocal8Bit(argv[1])).filePath("mikudancestudio_" + info.code + ".ts"));
        if (!source.open(QIODevice::ReadOnly))
            throw std::runtime_error("cannot open TS");
        QXmlStreamReader xml(&source);
        QString id, expected;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"message")
                id = xml.attributes().value("id").toString();
            else if (xml.isStartElement() && xml.name() == u"translation")
                expected = xml.readElementText();
            else if (xml.isEndElement() && xml.name() == u"message") {
                if (language.Text(id) != expected)
                    throw std::runtime_error("TS/QM mismatch: " + info.code.toStdString() + "/" +
                                             id.toStdString());
                ++count;
            }
        }
        if (xml.hasError())
            throw std::runtime_error("invalid TS XML");
        language.Activate();
        if (qtTrId("preferences.title") != language.Text("preferences.title"))
            throw std::runtime_error("application translator not installed");
        const auto cancel = QCoreApplication::translate("QPlatformTheme", "Cancel");
        if (cancel.isEmpty() || (info.code != "en" && cancel == "Cancel"))
            throw std::runtime_error("missing Qt standard-dialog translation");
    }
    if (count != Language::Available().size() * 773)
        throw std::runtime_error("incomplete catalog coverage");
    std::cout << count << " compiled Qt translations match their TS sources\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
