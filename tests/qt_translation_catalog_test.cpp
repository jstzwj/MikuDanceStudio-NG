#include "language.hpp"
#include "../translations/messages.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QXmlStreamReader>
#include <iostream>
#include <stdexcept>

using namespace mikudancestudio::qt;
int main(int argc, char** argv) try {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        throw std::runtime_error("expected translations directory");
    QSet<QString> required;
    for (const auto& entry : kTranslationMessages)
        required.insert(QString::fromUtf8(entry.id));
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
        QSet<QString> seen;
        QString id, expected;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"message")
                id = xml.attributes().value("id").toString();
            else if (xml.isStartElement() && xml.name() == u"translation")
                expected = xml.readElementText();
            else if (xml.isEndElement() && xml.name() == u"message") {
                if (!required.contains(id) || seen.contains(id))
                    throw std::runtime_error("unexpected or duplicate translation ID: " +
                                             info.code.toStdString() + "/" + id.toStdString());
                seen.insert(id);
                if (language.Text(id) != expected)
                    throw std::runtime_error("TS/QM mismatch: " + info.code.toStdString() + "/" +
                                             id.toStdString());
                ++count;
            }
        }
        if (xml.hasError())
            throw std::runtime_error("invalid TS XML");
        if (seen != required)
            throw std::runtime_error("incomplete catalog coverage: " + info.code.toStdString());
        language.Activate();
        if (qtTrId("preferences.title") != language.Text("preferences.title"))
            throw std::runtime_error("application translator not installed");
        const auto cancel = QCoreApplication::translate("QPlatformTheme", "Cancel");
        if (cancel.isEmpty() || (info.code != "en" && cancel == "Cancel"))
            throw std::runtime_error("missing Qt standard-dialog translation");
    }
    // The SDK does not ship Vietnamese. Check every entry of our independently
    // compiled context-based Qt widget supplement, including file/edit menus.
    QTranslator vietnamese;
    if (!vietnamese.load(":/i18n/qt/qtbase_vi.qm"))
        throw std::runtime_error("cannot load Vietnamese Qt supplement");
    QFile supplement(QDir(QString::fromLocal8Bit(argv[1])).filePath("qtbase_vi.ts"));
    if (!supplement.open(QIODevice::ReadOnly))
        throw std::runtime_error("cannot read Vietnamese Qt source");
    QXmlStreamReader xml(&supplement);
    QString context, source, expected;
    int standardCount = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == u"name")
                context = xml.readElementText();
            else if (xml.name() == u"source")
                source = xml.readElementText();
            else if (xml.name() == u"translation")
                expected = xml.readElementText();
        } else if (xml.isEndElement() && xml.name() == u"message") {
            if (vietnamese.translate(context.toUtf8().constData(), source.toUtf8().constData()) !=
                expected)
                throw std::runtime_error("Vietnamese Qt TS/QM mismatch: " + source.toStdString());
            ++standardCount;
        }
    }
    if (xml.hasError() || standardCount < 100)
        throw std::runtime_error("incomplete Vietnamese standard-widget coverage");
    std::cout << count << " compiled Qt translations match their TS sources\n";
    std::cout << standardCount << " Vietnamese Qt widget translations match their TS source\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
