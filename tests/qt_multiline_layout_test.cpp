#include "numeric_dialog.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCursor>
#include <QTextDocument>
#include <QImage>
#include <iostream>

using namespace mikudancestudio;
namespace {
class Dialog final : public qt::TemplateDialog {
public:
    explicit Dialog(const qt::Language& language)
        : TemplateDialog(language.Code() == "en" ? ui::kNumericEnglishNameEn
                                                 : ui::kNumericEnglishNameJa,
                         language, nullptr) {}
    QPlainTextEdit* Field() { return Multiline(668); }
};
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 2 || QGuiApplication::platformName() != "windows")
        return 2;
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly))
        return 1;
    const auto records = QJsonDocument::fromJson(file.readAll()).object()["records"].toArray();
    unsigned comparisons = 0, failures = 0;
    for (const auto& entry : records) {
        const auto record = entry.toObject();
        qt::Language language;
        if (!language.Set(record["template"] == 666 ? "en" : "ja"))
            return 1;
        Dialog dialog(language);
        auto* field = dialog.Field();
        if (!field || field->isVisible() || dialog.isVisible())
            return 1;
        const auto extent = record["outer"].toArray();
        if (field->size() != QSize(extent[0].toInt(), extent[1].toInt()) ||
            field->tabStopDistance() != 56)
            return 1;
        field->setPlainText(record["text"].toString());
        QImage paint(field->size(), QImage::Format_RGB32);
        field->render(&paint);
        if (field->document()->blockCount() != record["lines"].toInt())
            return 1;
        for (const auto& position : record["positions"].toArray()) {
            const auto expected = position.toArray();
            QTextCursor cursor(field->document());
            cursor.setPosition(expected[0].toInt());
            const auto actual = field->cursorRect(cursor).translated(field->viewport()->pos());
            if (actual.topLeft() != QPoint(expected[1].toInt(), expected[2].toInt())) {
                if (failures++ < 12)
                    std::cerr << "template=" << record["template"].toInt()
                              << " character=" << cursor.position() << " actual=" << actual.x()
                              << ',' << actual.y() << " expected=" << expected[1].toInt() << ','
                              << expected[2].toInt() << '\n';
            }
            ++comparisons;
        }
    }
    std::cout << comparisons << " original multiline character/tab/line origins; " << failures
              << " mismatches; never shown\n";
    return !comparisons || failures ? 1 : 0;
}
