#include "multiline_native.hpp"
int main(int argc, char** argv) try {
    // Validate BEFORE QApplication or any native window is created. This test
    // is deliberately absent from the ordinary local CTest suite.
    Check(qEnvironmentVariable("GITHUB_ACTIONS") == "true" &&
              qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1",
          "actual multiline input requires an isolated GitHub runner");
    QApplication app(argc, argv);
    Check(argc == 2 && QGuiApplication::platformName() == "windows", "Windows QPA and output");
    app.setCursorFlashTime(0);
    MouseDelivery delivery;
    Check(SetCursorPos(0, 0), "park isolated runner cursor");
    const QString output = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(output);
    QJsonArray paintRecords, mouseRecords;
    unsigned paintFailures = 0, mouseFailures = 0, caretFailures = 0;
    for (const auto* code : {"en", "ja", "zh_CN"}) {
        qt::Language language;
        Check(language.Set(code), "UI language");
        Dialog dialog(language);
        auto* field = dialog.Field();
        dialog.move(400, 32);
        dialog.show();
        // Actual unmodified RT_DIALOG. No hidden observer hook/style adaption.
        const auto native =
            CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(dialog.Template().id),
                               nullptr, Reference, 0);
        Check(native != nullptr, "original multiline reference dialog");
        const auto control = GetDlgItem(native, 668);
        Check(control != nullptr, "original multiline EDIT");
        SetWindowPos(native, nullptr, 32, 32, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_SHOWWINDOW);
        QTest::qWait(50);
        const QString texts[]{"abc def",
                              "1.5\n-12.5\n999",
                              "  first\n\n last",
                              "\t1\t2\tend",
                              QString(90, 'x'),
                              "row0 abc def\nrow1 abc def\nrow2 abc def\nrow3 abc def\n"
                              "row4 abc def\nrow5 abc def\nrow6 abc def\nrow7 abc def"};
        for (unsigned variant = 0; variant < std::size(texts); ++variant) {
            const auto text = texts[variant];
            const auto caption = QString(text).replace("\n", "\r\n").toStdWString();
            for (unsigned state = 0; state < 4; ++state) {
                const int first = state == 0 ? 0 : state == 1 ? 1 : text.size();
                const int last = state < 2 ? first : state == 2 ? 0 : text.size() / 2;
                Check(SetWindowTextW(control, caption.c_str()), "reference multiline contents");
                NativeFocus(native, control);
                const auto index = [&](int value) { return value + text.left(value).count('\n'); };
                SendMessageW(control, EM_SETSEL, index(first), index(last));
                if (state == 3)
                    SendMessageW(control, EM_LINESCROLL, 3, 2);
                RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                QCoreApplication::processEvents();
                Check(GetFocus() == control, "native actual focus before capture");
                const auto expected = Client(control);
                GUITHREADINFO info{sizeof info};
                Check(GetGUIThreadInfo(GetCurrentThreadId(), &info), "native caret information");
                Check(info.hwndCaret == control, "native reference owns actual caret");
                const auto caret = QJsonArray{int(info.rcCaret.left), int(info.rcCaret.top),
                                              int(info.rcCaret.right), int(info.rcCaret.bottom)};
                RECT formatting{};
                SendMessageW(control, EM_GETRECT, 0, LPARAM(&formatting));
                const auto origin = SendMessageW(control, EM_POSFROMCHAR, 0, 0);
                const auto vertical = SendMessageW(control, EM_GETFIRSTVISIBLELINE, 0, 0);
                field->setPlainText(text);
                auto cursor = field->textCursor();
                cursor.setPosition(first);
                cursor.setPosition(last, QTextCursor::KeepAnchor);
                field->setTextCursor(cursor);
                dialog.activateWindow();
                SetActiveWindow(reinterpret_cast<HWND>(dialog.winId()));
                QApplication::setActiveWindow(&dialog);
                field->setFocus(Qt::OtherFocusReason);
                QCoreApplication::processEvents();
                Check(field->hasFocus(), "Qt actual multiline focus");
                field->verticalScrollBar()->setValue(int(vertical));
                field->horizontalScrollBar()->setValue(formatting.left - short(LOWORD(origin)));
                QImage actual(field->size(), QImage::Format_RGB32);
                field->render(&actual);
                actual = actual.copy(2, 2, expected.width(), expected.height());
                unsigned different = 0;
                for (int y = 0; y < expected.height(); ++y)
                    for (int x = 0; x < expected.width(); ++x)
                        different +=
                            (expected.pixel(x, y) & 0xffffff) != (actual.pixel(x, y) & 0xffffff);
                const auto stem = QString("%1-text%2-state%3").arg(code).arg(variant).arg(state);
                expected.save(output + '/' + stem + "-native.png");
                actual.save(output + '/' + stem + "-qt.png");
                const auto qtCaret =
                    field->cursorRect().translated(field->viewport()->pos() - QPoint(2, 2));
                const QJsonArray actualCaret{qtCaret.x(), qtCaret.y(), qtCaret.right() + 1,
                                             qtCaret.bottom() + 1};
                caretFailures += actualCaret != caret;
                paintRecords.append(QJsonObject{{"case", stem},
                                                {"different", int(different)},
                                                {"nativeCaret", caret},
                                                {"qtCaret", actualCaret}});
                paintFailures += different != 0;
            }
            // Real click, drag in both directions, Shift-click and double click.
            for (unsigned gesture = 0; gesture < 5; ++gesture) {
                Check(SetWindowTextW(control, caption.c_str()), "native mouse contents");
                NativeFocus(native, control);
                SendMessageW(control, EM_SETSEL, 0, 0);
                SendMessageW(control, EM_LINESCROLL, -10000, -10000);
                QTest::qWait(30);
                const QPoint first(5, 6), last(35, 20);
                const auto start = gesture == 2 ? last : first;
                const auto end = gesture == 1 ? last : gesture == 2 ? first : start;
                const auto nativeHit = [&](QPoint point) {
                    const auto value =
                        SendMessageW(control, EM_CHARFROMPOS, 0, MAKELPARAM(point.x(), point.y()));
                    const auto nativeText = QString(text).replace("\n", "\r\n");
                    return int(LOWORD(value)) - nativeText.left(LOWORD(value)).count('\r');
                };
                const QJsonArray nativeHits{nativeHit(start), nativeHit(end)};
                POINT nativeOrigin{};
                ClientToScreen(control, &nativeOrigin);
                Gesture(native, start + QPoint(nativeOrigin.x, nativeOrigin.y),
                        end + QPoint(nativeOrigin.x, nativeOrigin.y), gesture == 4, gesture == 3);
                const auto expected = Selection(control, text);
                dialog.activateWindow();
                SetForegroundWindow(reinterpret_cast<HWND>(dialog.winId()));
                QApplication::setActiveWindow(&dialog);
                field->setPlainText(text);
                field->moveCursor(QTextCursor::Start);
                field->setFocus(Qt::OtherFocusReason);
                field->verticalScrollBar()->setValue(0);
                field->horizontalScrollBar()->setValue(0);
                QTest::qWait(30);
                const auto qtOrigin = field->mapToGlobal(QPoint(2, 2));
                const auto qtHit = [&](QPoint point) {
                    return qt::MultilineCursorForPosition(*field, point + QPoint(2, 2) -
                                                                      field->viewport()->pos())
                        .position();
                };
                const QJsonArray qtHits{qtHit(start), qtHit(end)};
                Gesture(reinterpret_cast<HWND>(dialog.winId()), start + qtOrigin, end + qtOrigin,
                        gesture == 4, gesture == 3);
                const auto selected = field->textCursor();
                const QJsonArray actual{selected.selectionStart(), selected.selectionEnd()};
                mouseFailures += actual != expected;
                mouseRecords.append(QJsonObject{{"language", code},
                                                {"text", int(variant)},
                                                {"gesture", int(gesture)},
                                                {"nativeHitsBefore", nativeHits},
                                                {"qtHitsBefore", qtHits},
                                                {"native", expected},
                                                {"qt", actual}});
            }
        }
        DestroyWindow(native);
        dialog.close();
        QCoreApplication::processEvents();
    }
    QFile report(output + "/report.json");
    Check(report.open(QIODevice::WriteOnly), "write active multiline report");
    report.write(QJsonDocument(QJsonObject{{"windowsShown", true},
                                           {"actualOsMouseCapture", true},
                                           {"paintFailures", int(paintFailures)},
                                           {"mouseFailures", int(mouseFailures)},
                                           {"caretFailures", int(caretFailures)},
                                           {"paint", paintRecords},
                                           {"mouse", mouseRecords}})
                     .toJson());
    std::cout << paintRecords.size() << " actual focused client paints; " << paintFailures
              << " differences; " << mouseRecords.size() << " OS mouse gestures; " << mouseFailures
              << " differences\n";
    return paintFailures || mouseFailures || caretFailures ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
