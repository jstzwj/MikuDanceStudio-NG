#include "numeric_dialog.hpp"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCursor>
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
    const auto reference = QJsonDocument::fromJson(file.readAll()).object();
    if (reference["records"].toArray().size() != 576)
        return 1;
    unsigned comparisons = 0, mismatches = 0;
    for (const auto& value : reference["records"].toArray()) {
        const auto row = value.toObject();
        for (const char* code : {"en", "ja", "zh_CN"}) {
            if ((row["template"] == 666) != (QString::fromLatin1(code) == "en"))
                continue;
            qt::Language language;
            if (!language.Set(code))
                return 1;
            {
                Dialog dialog(language);
                auto* field = dialog.Field();
                field->setPlainText(reference["text"].toString());
                QImage layout(field->size(), QImage::Format_RGB32);
                field->render(&layout); // Polish real viewport/page metrics without exposure.
                auto cursor = field->textCursor();
                cursor.setPosition(row["anchor"].toInt());
                cursor.setPosition(row["position"].toInt(), QTextCursor::KeepAnchor);
                field->setTextCursor(cursor);
                field->verticalScrollBar()->setValue(row["before"].toInt());
                const auto before = field->verticalScrollBar()->value();
                const auto key = row["key"].toInt() == 0x26   ? Qt::Key_Up
                                 : row["key"].toInt() == 0x28 ? Qt::Key_Down
                                 : row["key"].toInt() == 0x21 ? Qt::Key_PageUp
                                 : row["key"].toInt() == 0x22 ? Qt::Key_PageDown
                                 : row["key"].toInt() == 0x24 ? Qt::Key_Home
                                                              : Qt::Key_End;
                const auto modifiers =
                    (row["ctrl"].toBool() ? Qt::ControlModifier : Qt::NoModifier) |
                    (row["shift"].toBool() ? Qt::ShiftModifier : Qt::NoModifier);
                QKeyEvent event(QEvent::KeyPress, key, modifiers);
                QCoreApplication::sendEvent(field, &event);
                cursor = field->textCursor();
                const auto selection = row["selection"].toArray();
                const auto firstVisible = field->verticalScrollBar()->value();
                // Unfocused EM_SCROLLCARET does not qualify focus-driven
                // scrolling for Home/End. Explicit native page scroll does.
                const bool page =
                    !row["ctrl"].toBool() && (key == Qt::Key_PageUp || key == Qt::Key_PageDown);
                if (cursor.selectionStart() != selection[0].toInt() ||
                    cursor.selectionEnd() != selection[1].toInt() ||
                    (page && (firstVisible != row["firstVisible"].toInt() ||
                              before != row["before"].toInt()))) {
                    if (mismatches++ < 16)
                        std::cerr << code << " key=" << int(key) << " ctrl=" << row["ctrl"].toBool()
                                  << " shift=" << row["shift"].toBool()
                                  << " start=" << row["position"].toInt()
                                  << " selection=" << cursor.selectionStart() << ','
                                  << cursor.selectionEnd() << '/' << selection[0].toInt() << ','
                                  << selection[1].toInt() << " page=" << before << ','
                                  << firstVisible << '/' << row["before"].toInt() << ','
                                  << row["firstVisible"].toInt() << '\n';
                }
                if (dialog.isVisible() || field->isVisible() ||
                    field->toPlainText() != reference["text"].toString())
                    return 1;
                ++comparisons;
            }
        }
    }
    std::cout << comparisons
              << " original multiline navigation/page comparisons in three languages; "
              << mismatches << " mismatches; never shown\n";
    return comparisons != 864 || mismatches ? 1 : 0;
}
