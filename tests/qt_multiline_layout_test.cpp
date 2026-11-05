#include "numeric_dialog.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QImage>
#include "multiline_text_cases.hpp"
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
    for (const auto* code : {"ja", "zh_CN"}) {
        qt::Language language;
        if (!language.Set(code))
            return 1;
        Dialog dialog(language);
        auto* field = dialog.Field();
        const auto samples = MultilineUnicodeCases();
        for (unsigned variant = 0; variant < 2; ++variant) {
            field->setPlainText(samples[variant] + "\nrow3");
            QImage paint(field->size(), QImage::Format_RGB32);
            field->render(&paint);
            for (auto block = field->document()->begin(); block.isValid(); block = block.next()) {
                const auto caret =
                    field->cursorRect(QTextCursor(block)).translated(field->viewport()->pos());
                // Independently measured original MS UI Gothic EDIT profile:
                // first glyph/caret y=3, fixed 12-pixel rows, including fallback.
                if (caret.y() != 3 + 12 * block.blockNumber() || caret.height() != 12) {
                    std::cerr << code << " CJK fallback row=" << block.blockNumber()
                              << " y=" << caret.y() << " height=" << caret.height() << '\n';
                    for (int i = 0; i < block.layout()->lineCount(); ++i) {
                        const auto line = block.layout()->lineAt(i);
                        std::cerr << " line height=" << line.height() << " ascent=" << line.ascent()
                                  << " descent=" << line.descent() << " leading=" << line.leading()
                                  << " included=" << line.leadingIncluded() << '\n';
                    }
                    ++failures;
                }
                ++comparisons;
            }
        }
        if (dialog.isVisible())
            return 1;
    }
    std::cout << comparisons << " original multiline character/tab/line origins; " << failures
              << " mismatches; never shown\n";
    return !comparisons || failures ? 1 : 0;
}
