#include "numeric_dialog.hpp"
#include "editor_window.hpp"
#include "model_physics_dialog.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QScreen>
#include <QScrollBar>
#include <QTest>
#include <QTextLayout>
#include <QWindow>
#include <iostream>

using namespace mikudancestudio;
#ifdef Q_OS_MACOS
bool MoveShownPointer(int x, int y);
#endif
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void MovePointer(QPoint point) {
#ifdef Q_OS_MACOS
    // Warping the Cocoa cursor does not reliably dispatch a mouse-moved event.
    // This executable is guarded before QApplication and runs only in CI.
    Check(MoveShownPointer(point.x(), point.y()), "post native Cocoa mouse movement");
#else
    QCursor::setPos(point);
#endif
}
class Dialog final : public qt::TemplateDialog {
public:
    Dialog(const ui::NumericTemplate& description, const qt::Language& language)
        : TemplateDialog(description, language, nullptr) {}
};
QJsonArray Rectangle(QRect rect) {
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}
void Expose(QWidget& widget) {
    widget.show();
    widget.raise();
    widget.activateWindow();
    Check(QTest::qWaitForWindowExposed(&widget), "own native window becomes exposed");
    Check(QTest::qWaitForWindowActive(&widget), "own native window becomes active");
}
QJsonObject Capture(QWidget& widget, const QString& output, const QString& stem) {
    QCoreApplication::processEvents();
    const auto image = widget.grab();
    Check(!image.isNull() && widget.isVisible(), "capture actual shown test-owned widget");
    Check(image.save(output + '/' + stem + ".png"), "save shown UI image");
    return {{"image", stem + ".png"},
            {"logicalSize", QJsonArray{widget.width(), widget.height()}},
            {"physicalSize", QJsonArray{image.width(), image.height()}},
            {"dpr", widget.devicePixelRatioF()},
            {"visible", widget.isVisible()}};
}
}
int main(int argc, char** argv) try {
    // Never show, activate, hover, open popups or change focus on a working desktop.
    Check(qEnvironmentVariable("GITHUB_ACTIONS") == "true" &&
              qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1",
          "shown UI qualification requires an isolated GitHub runner");
    QApplication app(argc, argv);
    const auto platform = QGuiApplication::platformName();
    Check(argc == 4 && (platform == "xcb" || platform == "cocoa"),
          "shown UI qualification requires native X11 or Cocoa QPA, assets, output and fixtures");
    app.setCursorFlashTime(0);
    const QString output = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(output);
    const ui::NumericTemplate* english[]{
        &ui::kNumericBoneEn,        &ui::kNumericCameraEn,           &ui::kNumericScreenEn,
        &ui::kNumericEdgeEn,        &ui::kNumericGravityEn,          &ui::kNumericMorphScaleEn,
        &ui::kNumericCameraScaleEn, &ui::kNumericBoneScaleEn,        &ui::kNumericPhysicsKeysEn,
        &ui::kNumericDrawOrderEn,   &ui::kNumericCalculationOrderEn, &ui::kNumericGroundShadowEn,
        &ui::kNumericCenterBiasEn,  &ui::kNumericAccessoryOrderEn,   &ui::kNumericFrameShiftEn,
        &ui::kNumericBlinkEn,       &ui::kNumericEnglishNameEn,      &ui::kNumericToonEn,
        &ui::kNumericModelPhysicsEn};
    const ui::NumericTemplate* japanese[]{
        &ui::kNumericBoneJa,        &ui::kNumericCameraJa,           &ui::kNumericScreenJa,
        &ui::kNumericEdgeJa,        &ui::kNumericGravityJa,          &ui::kNumericMorphScaleJa,
        &ui::kNumericCameraScaleJa, &ui::kNumericBoneScaleJa,        &ui::kNumericPhysicsKeysJa,
        &ui::kNumericDrawOrderJa,   &ui::kNumericCalculationOrderJa, &ui::kNumericGroundShadowJa,
        &ui::kNumericCenterBiasJa,  &ui::kNumericAccessoryOrderJa,   &ui::kNumericFrameShiftJa,
        &ui::kNumericBlinkJa,       &ui::kNumericEnglishNameJa,      &ui::kNumericToonJa,
        &ui::kNumericModelPhysicsJa};
    QJsonArray records, menus;
    unsigned focused = 0, hovered = 0, popups = 0, listScrolls = 0, multilineScrolls = 0;
    const auto checkpoint = [&] {
        QFile report(output + "/report.json");
        Check(report.open(QIODevice::WriteOnly), "write shown UI report");
        report.write(
            QJsonDocument(
                QJsonObject{{"platform", platform},
                            {"actualShownWindows", true},
                            {"syntheticScale", qEnvironmentVariableIsSet("QT_SCALE_FACTOR")},
                            {"screenDpr", app.primaryScreen()->devicePixelRatio()},
                            {"focusedEdits", int(focused)},
                            {"hoveredButtons", int(hovered)},
                            {"comboPopups", int(popups)},
                            {"scrolledLists", int(listScrolls)},
                            {"scrolledMultiline", int(multilineScrolls)},
                            {"dialogs", records},
                            {"menus", menus}})
                .toJson());
    };
    for (const char* code : {"en", "ja", "zh_CN"}) {
        qt::Language language;
        Check(language.Set(code), "three-language shown UI");
        const auto& descriptions = language.Code() == "en" ? english : japanese;
        for (const auto* description : descriptions) {
            editor::EditorSession model;
            std::unique_ptr<qt::TemplateDialog> owned;
            const bool physics = description == &ui::kNumericModelPhysicsEn ||
                                 description == &ui::kNumericModelPhysicsJa;
            if (physics) {
                std::string reason;
                Check(
                    model.ImportModel(std::filesystem::absolute(argv[3]) / "triangle.pmd", reason),
                    "public generated physics-dialog model");
                auto actual = std::make_unique<qt::ModelPhysicsDialog>(model, language);
                Check(actual->Valid(), "production physics dialog is bound to a live model");
                owned = std::move(actual);
            } else {
                owned = std::make_unique<Dialog>(*description, language);
            }
            auto& dialog = *owned;
            dialog.move(40, 40);
            Expose(dialog);
            const auto stem = QString("%1-dialog%2").arg(code).arg(description->id);
            auto record = Capture(dialog, output, stem + "-normal");
            record["language"] = code;
            record["template"] = description->id;
            record["font"] = QFontInfo(dialog.font()).family();
            record["fontPixelSize"] = QFontInfo(dialog.font()).pixelSize();
            record["windowColor"] = dialog.palette().window().color().name();
            record["buttonColor"] = dialog.palette().button().color().name();
            record["highlightColor"] = dialog.palette().highlight().color().name();
            QJsonArray controls;
            QLineEdit* firstEdit = nullptr;
            QAbstractButton* firstButton = nullptr;
            for (auto* child :
                 dialog.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
                const auto id = child->property("mmdControlId");
                if (!id.isValid())
                    continue;
                controls.append(QJsonObject{{"id", id.toInt()},
                                            {"class", child->metaObject()->className()},
                                            {"visible", child->isVisible()},
                                            {"rect", Rectangle(child->geometry())},
                                            {"font", QFontInfo(child->font()).family()}});
                if (!child->isVisible())
                    continue;
                if (auto* label = qobject_cast<QLabel*>(child)) {
                    QTextLayout layout(label->text(), label->font());
                    QTextOption option;
                    option.setWrapMode(QTextOption::WordWrap);
                    layout.setTextOption(option);
                    qreal height = 0;
                    int consumed = 0, lines = 0;
                    bool complete = true;
                    layout.beginLayout();
                    while (true) {
                        auto line = layout.createLine();
                        if (!line.isValid())
                            break;
                        line.setLineWidth(label->contentsRect().width());
                        complete &= line.naturalTextWidth() <= label->contentsRect().width();
                        height += line.height();
                        consumed += line.textLength();
                        ++lines;
                    }
                    layout.endLayout();
                    complete &= consumed == label->text().size() &&
                                height <= label->contentsRect().height();
                    Check(label->wordWrap() && complete,
                          "shown static caption wraps completely inside original geometry");
                    auto caption = controls.last().toObject();
                    caption["text"] = label->text();
                    caption["fontStretch"] = label->font().stretch();
                    caption["lines"] = lines;
                    caption["complete"] = complete;
                    controls[controls.size() - 1] = caption;
                }
                if (auto* edit = qobject_cast<QLineEdit*>(child)) {
                    edit->setText("12.5");
                    if (!firstEdit && edit->isEnabled() && !edit->isReadOnly())
                        firstEdit = edit;
                }
                if (auto* button = qobject_cast<QAbstractButton*>(child))
                    if (!firstButton && button->isEnabled())
                        firstButton = button;
                if (auto* list = qobject_cast<QListWidget*>(child)) {
                    for (int row = 0; row < 40; ++row)
                        list->addItem(QString::fromUtf8("%1 Miku 初音 ミク").arg(row));
                    list->setCurrentRow(28);
                    list->scrollToItem(list->currentItem(), QAbstractItemView::PositionAtCenter);
                    list->setFocus(Qt::OtherFocusReason);
                    Check(list->hasFocus() && list->visualItemRect(list->currentItem())
                                                  .intersects(list->viewport()->rect()),
                          "shown list focus and scrolled selection");
                    Capture(dialog, output, stem + "-list-scroll");
                    ++listScrolls;
                }
                if (auto* text = qobject_cast<QPlainTextEdit*>(child)) {
                    QString contents;
                    for (int row = 0; row < 30; ++row)
                        contents +=
                            QString::fromUtf8("%1 初音ミク 中文 e\xcc\x81 abc def\n").arg(row);
                    text->setPlainText(contents);
                    text->setFocus(Qt::OtherFocusReason);
                    text->moveCursor(QTextCursor::End);
                    Check(text->hasFocus() && text->verticalScrollBar()->value() > 0,
                          "shown multiline focus and actual scrolling");
                    Check(text->toPlainText() == contents,
                          "shown multilingual multiline preserves contents");
                    Capture(dialog, output, stem + "-multiline-scroll");
                    ++multilineScrolls;
                }
                if (auto* combo = qobject_cast<QComboBox*>(child)) {
                    if (!physics) {
                        combo->clear();
                        combo->addItems({"first", QString::fromUtf8("初音ミク"),
                                         QString::fromUtf8("简体中文")});
                    }
                    if (!combo->isEnabled() || combo->count() == 0)
                        continue;
                    combo->setFocus(Qt::OtherFocusReason);
                    combo->showPopup();
                    QTest::qWait(30);
                    auto* popup = combo->view()->window();
                    Check(popup->isVisible() && combo->view()->isVisible(),
                          "actual combo popup exposed");
                    Capture(*popup, output, stem + QString("-combo%1-popup").arg(id.toInt()));
                    combo->hidePopup();
                    Check(!popup->isVisible(),
                          "combo popup dismissed without changing scene state");
                    ++popups;
                }
            }
            if (firstEdit) {
                firstEdit->setFocus(Qt::OtherFocusReason);
                firstEdit->selectAll();
                Check(firstEdit->hasFocus() && firstEdit->selectedText() == "12.5",
                      "shown field focus and selection");
                Capture(dialog, output, stem + "-edit-focus");
                ++focused;
            }
            if (firstButton) {
                MovePointer(dialog.mapToGlobal(QPoint(-10, -10)));
                QTest::qWait(20);
                MovePointer(firstButton->mapToGlobal(firstButton->rect().center()));
                Check(QTest::qWaitFor([&] { return firstButton->underMouse(); }, 1500),
                      "native pointer delivered to shown button");
                Capture(dialog, output, stem + "-button-hover");
                MovePointer(dialog.mapToGlobal(QPoint(-10, -10)));
                ++hovered;
            }
            record["controls"] = controls;
            if (physics) {
                auto* actual = static_cast<qt::ModelPhysicsDialog*>(&dialog);
                static_cast<QAbstractButton*>(actual->Control(801))->click();
                Check(actual->JointPage(), "production physics joint page");
                Capture(dialog, output, stem + "-joint-page");
                static_cast<QAbstractButton*>(actual->Control(800))->click();
                Check(!actual->JointPage(), "production physics rigid-body page");
            }
            records.append(record);
            checkpoint();
            dialog.close();
            QCoreApplication::processEvents();
        }
        qt::EditorWindow editor(std::filesystem::absolute(argv[1]), true);
        Check(editor.SetLanguage(code), "production editor shown language");
        editor.resize(1024, 768);
        Expose(editor);
        const auto revision = editor.Session().Revision();
        for (auto* action : editor.menuBar()->actions())
            if (auto* menu = action->menu()) {
                editor.activateWindow();
                Check(QTest::qWaitForWindowActive(&editor), "editor active before each menu");
                // Park away from menu bars before dispatching queued enter/move
                // events, which can otherwise switch the newly opened menu.
                MovePointer(editor.mapToGlobal(QPoint(editor.width() / 2, editor.height() / 2)));
                QTest::qWait(30);
                menu->popup(editor.menuBar()->mapToGlobal(
                    editor.menuBar()->actionGeometry(action).bottomLeft()));
                const bool exposed = QTest::qWaitForWindowExposed(menu, 3000);
                if (!menu->isVisible() || !exposed) {
                    std::cerr << code << " menu=" << menu->title().toStdString()
                              << " visible=" << menu->isVisible() << " exposed=" << exposed << '\n';
                    throw std::runtime_error("actual editor menu exposed");
                }
                auto record =
                    Capture(*menu, output, QString("%1-menu%2").arg(code).arg(menus.size()));
                record["title"] = menu->title();
                record["language"] = code;
                menus.append(record);
                checkpoint();
                menu->close();
                QTest::qWait(50);
            }
        Check(editor.Session().Revision() == revision && !editor.Session().Dirty(),
              "shown menus do not edit the production document");
        editor.close();
        QCoreApplication::processEvents();
    }
    checkpoint();
    Check(records.size() == 57 && focused > 0 && hovered > 0 && popups > 0 && listScrolls > 0 &&
              multilineScrolls > 0 && !menus.isEmpty(),
          "complete shown UI matrix");
    std::cout << records.size() << " actual shown three-language dialog profiles; " << menus.size()
              << " editor menus; focus, hover, popups and scrolling passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
