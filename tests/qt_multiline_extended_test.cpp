#include "multiline_native.hpp"
#include "multiline_text_cases.hpp"
#include <QTextBoundaryFinder>
#include <vector>

namespace {
QJsonObject NativeState(HWND control, const QString& text) {
    RECT formatting{};
    SendMessageW(control, EM_GETRECT, 0, LPARAM(&formatting));
    const auto origin = SendMessageW(control, EM_POSFROMCHAR, 0, 0);
    return {{"selection", Selection(control, text)},
            {"firstVisible", int(SendMessageW(control, EM_GETFIRSTVISIBLELINE, 0, 0))},
            {"horizontal", int(formatting.left) - short(LOWORD(origin))}};
}
QJsonObject QtState(QPlainTextEdit& field) {
    const auto cursor = field.textCursor();
    return {{"selection", QJsonArray{cursor.selectionStart(), cursor.selectionEnd()}},
            {"firstVisible", field.verticalScrollBar()->value()},
            {"horizontal", field.horizontalScrollBar()->value()}};
}
template <class State>
QJsonObject OutsideDrag(HWND root, QPoint start, QPoint end, State state, QJsonArray& samples) {
    QTest::qWait(int(GetDoubleClickTime()) + 30);
    Check(GetAncestor(GetForegroundWindow(), GA_ROOT) == root, "outside-drag owns foreground");
    MouseDelivery::Move(root, start);
    Button(true);
    Check(GetCapture() && GetAncestor(GetCapture(), GA_ROOT) == root,
          "outside-drag capture belongs to test dialog");
    MouseDelivery::Move(root, end);
    QElapsedTimer elapsed;
    elapsed.start();
    auto previous = state();
    unsigned stable = 0;
    do {
        QTest::qWait(100);
        const auto current = state();
        samples.append(QJsonObject{{"elapsedMs", int(elapsed.elapsed())}, {"state", current}});
        stable = current == previous ? stable + 1 : 0;
        previous = current;
    } while ((elapsed.elapsed() < 700 || stable < 4) && elapsed.elapsed() < 3500);
    Check(GetCapture() && GetAncestor(GetCapture(), GA_ROOT) == root,
          "outside-drag retains actual OS capture");
    Button(false);
    Check(!GetCapture(), "outside-drag releases actual OS capture");
    return state();
}
unsigned Different(const QImage& expected, const QImage& actual) {
    Check(expected.size() == actual.size(), "complete multiline client dimensions");
    unsigned different = 0;
    for (int y = 0; y < expected.height(); ++y)
        for (int x = 0; x < expected.width(); ++x)
            different += (expected.pixel(x, y) & 0xffffff) != (actual.pixel(x, y) & 0xffffff);
    return different;
}
QImage QtClient(QPlainTextEdit& field, QSize size) {
    QImage image(field.size(), QImage::Format_RGB32);
    field.render(&image);
    return image.copy(2, 2, size.width(), size.height());
}
struct CaretCycle {
    std::vector<QImage> images;
    QJsonArray samples;
};
template <class Capture, class State>
CaretCycle CaptureCaretCycle(Capture capture, State state, const QJsonObject& expected) {
    // Observe an entire blink cycle without changing native focus, position,
    // hide count or system settings. Compare the complete sets of actual RGB
    // frames, including every observed caret phase, rather than masking a caret column
    // or searching for a conveniently matching image. Extra states fail too.
    const auto limit = quint64(GetCaretBlinkTime()) * 2 + 100;
    CaretCycle cycle;
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        Check(state() == expected, "caret cycle preserves completed drag state");
        auto image = capture();
        auto found =
            std::find_if(cycle.images.begin(), cycle.images.end(),
                         [&](const QImage& frame) { return Different(frame, image) == 0; });
        const auto index = int(found - cycle.images.begin());
        if (found == cycle.images.end())
            cycle.images.push_back(std::move(image));
        cycle.samples.append(QJsonArray{int(elapsed.elapsed()), index});
        QTest::qWait(40);
    } while (quint64(elapsed.elapsed()) < limit);
    Check(state() == expected, "caret cycle retains final drag state");
    return cycle;
}
unsigned UnicodeEditing(QPlainTextEdit& field, const QString& text) {
    Check(field.hasFocus(), "Unicode editing retains real focus");
    Check(GetAncestor(GetForegroundWindow(), GA_ROOT) ==
              reinterpret_cast<HWND>(field.window()->winId()),
          "Unicode keyboard input owns foreground");
    const auto chord = [](WORD key) {
        Key(VK_CONTROL, true);
        Key(key, true);
        Key(key, false);
        Key(VK_CONTROL, false);
    };
    unsigned failures = 0;
    chord('A');
    failures += field.textCursor().selectionStart() != 0 ||
                field.textCursor().selectionEnd() != text.size();
    Key(VK_BACK, true);
    Key(VK_BACK, false);
    failures += !field.toPlainText().isEmpty();
    chord('Z');
    failures += field.toPlainText() != text;
    chord('Y');
    failures += !field.toPlainText().isEmpty();
    chord('Z');
    failures += field.toPlainText() != text;
    field.moveCursor(QTextCursor::Start);
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text);
    // Exercise the shared legacy Ctrl-word handler with actual OS keys.
    // No movement may split a surrogate, combining sequence or emoji cluster.
    for (unsigned step = 0; step < 32 && field.textCursor().position() < text.size(); ++step) {
        const auto before = field.textCursor().position();
        chord(VK_RIGHT);
        const auto after = field.textCursor().position();
        graphemes.setPosition(after);
        failures += after <= before || !graphemes.isAtBoundary();
        if (after <= before)
            break;
    }
    failures += field.textCursor().position() != text.size();
    return failures;
}
}
int main(int argc, char** argv) try {
    Check(qEnvironmentVariable("GITHUB_ACTIONS") == "true" &&
              qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1",
          "extended actual multiline input requires an isolated GitHub runner");
    QApplication app(argc, argv);
    Check(argc == 2 && QGuiApplication::platformName() == "windows", "Windows QPA and output");
    Check(GetCaretBlinkTime() > 0 && GetCaretBlinkTime() <= 2000,
          "isolated native caret has a bounded blink interval");
    MouseDelivery delivery;
    const QString output = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(output);
    QJsonArray dragRecords, textRecords;
    unsigned dragFailures = 0, textFailures = 0, unicodeRasterDifferences = 0,
             traditionalRasterFailures = 0;
    const auto checkpoint = [&] {
        QFile report(output + "/report.json");
        Check(report.open(QIODevice::WriteOnly), "write extended multiline report");
        report.write(
            QJsonDocument(
                QJsonObject{
                    {"windowsShown", true},
                    {"actualOsMouseCapture", true},
                    {"nativeCaretBlinkMs", int(GetCaretBlinkTime())},
                    {"captureCaretPhase",
                     "full native/Qt blink cycles; both complete RGB frame sets must match exactly"},
                    {"dragFailures", int(dragFailures)},
                    {"unicodeFailures", int(textFailures)},
                    {"unicodeRasterDifferences", int(unicodeRasterDifferences)},
                    {"traditionalRasterFailures", int(traditionalRasterFailures)},
                    {"unicodePolicy",
                     "native-supported traditional CJK remains raster exact; preserve complete readable complex/lossy Unicode and editing, retaining raster diagnostics"},
                    {"drags", dragRecords},
                    {"unicode", textRecords}})
                .toJson());
    };
    for (const auto* code : {"en", "ja", "zh_CN"}) {
        app.setCursorFlashTime(int(GetCaretBlinkTime()) * 2);
        qt::Language language;
        Check(language.Set(code), "UI language");
        Dialog dialog(language);
        auto* field = dialog.Field();
        dialog.move(440, 32);
        dialog.show();
        const auto native =
            CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(dialog.Template().id),
                               nullptr, Reference, 0);
        Check(native != nullptr, "unchanged original multiline reference");
        const auto control = GetDlgItem(native, 668);
        Check(control != nullptr && IsWindowUnicode(control), "Unicode original EDIT instance");
        SetWindowPos(native, nullptr, 32, 32, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_SHOWWINDOW);
        QTest::qWait(50);
        QString text;
        for (int row = 0; row < 16; ++row)
            text += QString("%1 abc def %2%3")
                        .arg(row, 2, 10, QLatin1Char('0'))
                        .arg(QString(56, 'x'), row == 15 ? "" : "\n");
        const auto caption = QString(text).replace("\n", "\r\n").toStdWString();
        for (unsigned direction = 0; direction < 6; ++direction) {
            Check(SetWindowTextW(control, caption.c_str()), "native drag document");
            NativeFocus(native, control);
            SendMessageW(control, EM_SETSEL, 0, 0);
            SendMessageW(control, EM_LINESCROLL, 4, 4);
            const auto initial = NativeState(control, text);
            RECT client{};
            GetClientRect(control, &client);
            const QPoint start(client.right / 2, client.bottom / 2);
            const QPoint endpoints[]{
                QPoint(start.x(), -8), QPoint(start.x(), client.bottom + 8),
                QPoint(-8, start.y()), QPoint(client.right + 8, start.y()),
                QPoint(-8, -8),        QPoint(client.right + 8, client.bottom + 8)};
            POINT origin{};
            ClientToScreen(control, &origin);
            QJsonArray nativeSamples, qtSamples;
            const auto expected = OutsideDrag(
                native, start + QPoint(origin.x, origin.y),
                endpoints[direction] + QPoint(origin.x, origin.y),
                [&] { return NativeState(control, text); }, nativeSamples);
            const auto nativeCycle = CaptureCaretCycle(
                [&] {
                    GUITHREADINFO gui{sizeof gui};
                    Check(GetGUIThreadInfo(0, &gui) && gui.hwndCaret == control &&
                              GetFocus() == control,
                          "native cycle owns the actual focused caret");
                    return Client(control);
                },
                [&] { return NativeState(control, text); }, expected);
            dialog.activateWindow();
            SetForegroundWindow(reinterpret_cast<HWND>(dialog.winId()));
            QApplication::setActiveWindow(&dialog);
            field->setPlainText(text);
            field->moveCursor(QTextCursor::Start);
            field->setFocus(Qt::OtherFocusReason);
            field->verticalScrollBar()->setValue(initial["firstVisible"].toInt());
            field->horizontalScrollBar()->setValue(initial["horizontal"].toInt());
            QTest::qWait(30);
            const auto qtOrigin = field->mapToGlobal(QPoint(2, 2));
            const auto actual = OutsideDrag(
                reinterpret_cast<HWND>(dialog.winId()), start + qtOrigin,
                endpoints[direction] + qtOrigin, [&] { return QtState(*field); }, qtSamples);
            const auto qtCycle = CaptureCaretCycle(
                [&] {
                    Check(field->hasFocus(), "Qt cycle retains actual focus");
                    return QtClient(*field, nativeCycle.images.front().size());
                },
                [&] { return QtState(*field); }, actual);
            QJsonArray matrix;
            // EDIT can intentionally hide the caret after an outside release.
            // Require identical complete frame sets, including cardinality;
            // an extra blinking Qt frame must still fail.
            bool phasesMatch = !nativeCycle.images.empty() && nativeCycle.images.size() <= 2 &&
                               nativeCycle.images.size() == qtCycle.images.size();
            for (const auto& nativeImage : nativeCycle.images) {
                QJsonArray differences;
                bool exact = false;
                for (const auto& qtImage : qtCycle.images) {
                    const auto count = Different(nativeImage, qtImage);
                    differences.append(int(count));
                    exact |= count == 0;
                }
                phasesMatch &= exact;
                matrix.append(differences);
            }
            const auto stem = QString("%1-drag%2").arg(code).arg(direction);
            for (unsigned phase = 0; phase < nativeCycle.images.size(); ++phase)
                nativeCycle.images[phase].save(output + '/' + stem +
                                               QString("-native-phase%1.png").arg(phase));
            for (unsigned phase = 0; phase < qtCycle.images.size(); ++phase)
                qtCycle.images[phase].save(output + '/' + stem +
                                           QString("-qt-phase%1.png").arg(phase));
            dragRecords.append(QJsonObject{{"language", code},
                                           {"direction", int(direction)},
                                           {"initial", initial},
                                           {"native", expected},
                                           {"qt", actual},
                                           {"caretPhaseSetsMatch", phasesMatch},
                                           {"nativePhases", int(nativeCycle.images.size())},
                                           {"qtPhases", int(qtCycle.images.size())},
                                           {"fullClientDifferenceMatrix", matrix},
                                           {"nativeCaretSamples", nativeCycle.samples},
                                           {"qtCaretSamples", qtCycle.samples},
                                           {"nativeSamples", nativeSamples},
                                           {"qtSamples", qtSamples}});
            dragFailures += expected != actual || !phasesMatch;
            checkpoint();
        }
        app.setCursorFlashTime(0);
        const auto texts = MultilineUnicodeCases();
        for (unsigned variant = 0; variant < std::size(texts); ++variant)
            for (unsigned selected = 0; selected < 2; ++selected) {
                const auto text = texts[variant];
                const auto nativeText = QString(text).replace("\n", "\r\n");
                Check(SetWindowTextW(control, nativeText.toStdWString().c_str()),
                      "Unicode native contents");
                std::wstring contents(size_t(GetWindowTextLengthW(control)) + 1, 0);
                contents.resize(GetWindowTextW(control, contents.data(), int(contents.size())));
                const bool nativePreserved = QString::fromStdWString(contents) == nativeText;
                NativeFocus(native, control);
                SendMessageW(control, EM_SETSEL, 0, selected ? -1 : 0);
                const auto expected = Client(control);
                dialog.activateWindow();
                SetForegroundWindow(reinterpret_cast<HWND>(dialog.winId()));
                QApplication::setActiveWindow(&dialog);
                field->setPlainText(text);
                field->setFocus(Qt::OtherFocusReason);
                field->moveCursor(QTextCursor::Start);
                if (selected)
                    field->selectAll();
                QTest::qWait(20);
                const auto actual = QtClient(*field, expected.size());
                const auto different = Different(expected, actual);
                const auto stem =
                    QString("%1-unicode%2-selected%3").arg(code).arg(variant).arg(selected);
                expected.save(output + '/' + stem + "-native.png");
                actual.save(output + '/' + stem + "-qt.png");
                const bool preserved = field->toPlainText() == text;
                const auto editingFailures = UnicodeEditing(*field, text);
                const bool strictRaster = variant < 2 && nativePreserved;
                textRecords.append(QJsonObject{{"case", stem},
                                               {"different", int(different)},
                                               {"nativeTextPreserved", nativePreserved},
                                               {"nativeText", QString::fromStdWString(contents)},
                                               {"inputText", nativeText},
                                               {"textPreserved", preserved},
                                               {"strictRaster", strictRaster},
                                               {"editingFailures", int(editingFailures)}});
                unicodeRasterDifferences += different != 0;
                traditionalRasterFailures += strictRaster && different != 0;
                textFailures += !preserved || editingFailures != 0;
                checkpoint();
            }
        DestroyWindow(native);
        dialog.close();
        QCoreApplication::processEvents();
    }
    checkpoint();
    std::cout << dragRecords.size() << " actual outside-client drags; " << dragFailures
              << " differences; " << textRecords.size() << " Unicode preservation/editing cases; "
              << textFailures << " failures; " << unicodeRasterDifferences
              << " raster diagnostics; " << traditionalRasterFailures
              << " traditional CJK raster failures\n";
    return dragFailures || textFailures || traditionalRasterFailures ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
