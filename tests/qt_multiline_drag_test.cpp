#include "numeric_dialog.hpp"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QScrollBar>
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
    unsigned failures = 0;
    for (const auto& value : records) {
        const auto record = value.toObject();
        qt::Language language;
        if (!language.Set(record["language"].toString()))
            return 1;
        Dialog dialog(language);
        auto* field = dialog.Field();
        field->setReadOnly(true);
        QString text;
        for (int row = 0; row < 16; ++row)
            text += QString("%1 abc def %2%3")
                        .arg(row, 2, 10, QLatin1Char('0'))
                        .arg(QString(56, 'x'), row == 15 ? "" : "\n");
        field->setPlainText(text);
        QImage paint(field->size(), QImage::Format_RGB32);
        field->render(&paint);
        const auto initial = record["initial"].toObject();
        field->verticalScrollBar()->setValue(initial["firstVisible"].toInt());
        field->horizontalScrollBar()->setValue(initial["horizontal"].toInt());
        const auto size = record["clientSize"].toArray();
        const int width = size[0].toInt(), height = size[1].toInt();
        const QPoint start(width / 2, height / 2);
        const QPoint endpoints[]{QPoint(start.x(), -8), QPoint(start.x(), height + 8),
                                 QPoint(-8, start.y()), QPoint(width + 8, start.y()),
                                 QPoint(-8, -8),        QPoint(width + 8, height + 8)};
        const auto delta = QPoint(2, 2) - field->viewport()->pos();
        const auto send = [&](QEvent::Type type, QPoint point, Qt::MouseButton button,
                              Qt::MouseButtons buttons) {
            point += delta;
            QMouseEvent event(type, point, point, point, button, buttons, Qt::NoModifier,
                              Qt::MouseEventSynthesizedByApplication);
            QCoreApplication::sendEvent(field->viewport(), &event);
        };
        send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        const auto end = endpoints[record["direction"].toInt()];
        send(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
        const auto cursor = field->textCursor();
        const QJsonObject actual{
            {"selection", QJsonArray{cursor.selectionStart(), cursor.selectionEnd()}},
            {"firstVisible", field->verticalScrollBar()->value()},
            {"horizontal", field->horizontalScrollBar()->value()}};
        if (actual != record["expected"].toObject()) {
            ++failures;
            std::cerr << record["language"].toString().toStdString()
                      << " outside=" << record["direction"].toInt() << " actual="
                      << QJsonDocument(actual).toJson(QJsonDocument::Compact).constData()
                      << " expected="
                      << QJsonDocument(record["expected"].toObject())
                             .toJson(QJsonDocument::Compact)
                             .constData()
                      << " viewport=" << field->viewport()->width() << ','
                      << field->viewport()->height()
                      << " average=" << field->fontMetrics().averageCharWidth()
                      << " frame=" << field->frameWidth() << " pos=" << field->viewport()->pos().x()
                      << " width=" << field->width() << " caret=" << field->cursorRect().x()
                      << '\n';
        }
        if (dialog.isVisible() || field->toPlainText() != text)
            return 1;
    }
    std::cout << records.size() << " frozen actual OS outside-client drag results; " << failures
              << " differences; no window exposure or desktop input\n";
    return records.size() != 18 || failures ? 1 : 0;
}
