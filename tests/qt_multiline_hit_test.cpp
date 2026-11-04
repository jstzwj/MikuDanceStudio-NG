#include "numeric_dialog.hpp"
#include "multiline_hit.hpp"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QScrollBar>
#include <algorithm>
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
    const auto records = reference["records"].toArray();
    if (records.size() != 12)
        return 1;
    unsigned comparisons = 0, mouseComparisons = 0, dragComparisons = 0, failures = 0;
    for (const auto& value : records) {
        const auto record = value.toObject();
        for (const char* code : {"en", "ja", "zh_CN"}) {
            if ((record["template"] == 666) != (QString::fromLatin1(code) == "en"))
                continue;
            qt::Language language;
            if (!language.Set(code))
                return 1;
            Dialog dialog(language);
            auto* field = dialog.Field();
            field->setPlainText(record["text"].toString());
            field->setReadOnly(true); // Selection only; no software input panel request.
            QImage paint(field->size(), QImage::Format_RGB32);
            field->render(&paint); // Polish actual layout without exposing a window.
            const auto origin = record["origin"].toArray();
            const auto delta =
                QPoint(origin[0].toInt(), origin[1].toInt()) - field->viewport()->pos();
            unsigned recordFailures = 0;
            for (const auto& sample : record["hitTest"].toArray()) {
                const auto point = sample.toArray();
                const auto position = QPoint(point[0].toInt(), point[1].toInt()) + delta;
                const auto cursor = qt::MultilineCursorForPosition(*field, position);
                if (cursor.position() != point[2].toInt()) {
                    ++failures;
                    if (recordFailures++ < 4)
                        std::cerr << code << " client=" << point[0].toInt() << ','
                                  << point[1].toInt() << " cursor=" << cursor.position()
                                  << " expected=" << point[2].toInt() << '\n';
                }
                ++comparisons;
                if (comparisons % 97 == 0 && field->viewport()->rect().contains(position)) {
                    // Address only the never-shown widget. No SendInput,
                    // cursor movement, activation or hardware mouse events.
                    field->setTextCursor(QTextCursor(field->document()));
                    field->verticalScrollBar()->setValue(0);
                    field->horizontalScrollBar()->setValue(0);
                    const auto send = [&](QEvent::Type type, Qt::MouseButton button,
                                          Qt::MouseButtons buttons) {
                        QMouseEvent event(type, position, position, position, button, buttons,
                                          Qt::ShiftModifier,
                                          Qt::MouseEventSynthesizedByApplication);
                        QCoreApplication::sendEvent(field->viewport(), &event);
                    };
                    send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
                    send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
                    const auto selected = field->textCursor();
                    if (selected.anchor() != 0 || selected.position() != point[2].toInt()) {
                        if (failures++ < 16)
                            std::cerr << code << " addressed Shift-click=" << selected.anchor()
                                      << ',' << selected.position() << " expected=0,"
                                      << point[2].toInt() << '\n';
                    }
                    field->verticalScrollBar()->setValue(0);
                    field->horizontalScrollBar()->setValue(0);
                    ++mouseComparisons;
                }
            }
            // Use two independently frozen native hits as the drag endpoints.
            // This exercises the real production press/move/release handlers,
            // including both directions; no synthetic OS input or capture.
            const auto samples = record["hitTest"].toArray();
            for (int index = 13; index < samples.size(); index += 137) {
                const auto a = samples[index].toArray();
                const auto b = samples[(index + samples.size() / 3) % samples.size()].toArray();
                const auto start = QPoint(a[0].toInt(), a[1].toInt()) + delta;
                const auto end = QPoint(b[0].toInt(), b[1].toInt()) + delta;
                if (!field->viewport()->rect().contains(start) ||
                    !field->viewport()->rect().contains(end))
                    continue;
                for (bool reverse : {false, true}) {
                    field->moveCursor(QTextCursor::Start);
                    field->verticalScrollBar()->setValue(0);
                    field->horizontalScrollBar()->setValue(0);
                    const auto send = [&](QEvent::Type type, QPoint point, Qt::MouseButton button,
                                          Qt::MouseButtons buttons) {
                        QMouseEvent event(type, point, point, point, button, buttons,
                                          Qt::NoModifier, Qt::MouseEventSynthesizedByApplication);
                        QCoreApplication::sendEvent(field->viewport(), &event);
                    };
                    send(QEvent::MouseButtonPress, reverse ? end : start, Qt::LeftButton,
                         Qt::LeftButton);
                    send(QEvent::MouseMove, reverse ? start : end, Qt::NoButton, Qt::LeftButton);
                    send(QEvent::MouseButtonRelease, reverse ? start : end, Qt::LeftButton,
                         Qt::NoButton);
                    const auto selected = field->textCursor();
                    const int anchor = (reverse ? b : a)[2].toInt();
                    const int position = (reverse ? a : b)[2].toInt();
                    if (selected.anchor() != anchor || selected.position() != position) {
                        if (failures++ < 16)
                            std::cerr << code << " addressed drag=" << selected.anchor() << ','
                                      << selected.position() << " expected=" << anchor << ','
                                      << position << '\n';
                    }
                    ++dragComparisons;
                }
            }
            std::cout << code << " text-size=" << field->toPlainText().size()
                      << " mismatches=" << recordFailures << '\n';
            if (dialog.isVisible() || field->isVisible() ||
                field->toPlainText() != record["text"].toString())
                return 1;
        }
    }
    std::cout << comparisons << " original multiline client hit-test comparisons; " << failures
              << " mismatches; " << mouseComparisons
              << " addressed Shift-click comparisons; no desktop input or window exposure\n";
    std::cout << dragComparisons << " independently frozen native drag endpoint comparisons\n";
    return comparisons != 50232 || !mouseComparisons || !dragComparisons || failures ? 1 : 0;
}
