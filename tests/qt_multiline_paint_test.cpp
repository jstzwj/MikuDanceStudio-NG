#include "numeric_dialog.hpp"
#include <QApplication>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QScrollBar>
#include <iostream>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

using namespace mikudancestudio;
namespace {
INT_PTR CALLBACK Hidden(HWND window, UINT message, WPARAM, LPARAM) {
    if (message == WM_INITDIALOG)
        SetWindowLongPtrW(window, GWL_EXSTYLE,
                          GetWindowLongPtrW(window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
    return message == WM_NEXTDLGCTL;
}
class Dialog final : public qt::TemplateDialog {
public:
    explicit Dialog(const qt::Language& language)
        : TemplateDialog(language.Code() == "en" ? ui::kNumericEnglishNameEn
                                                 : ui::kNumericEnglishNameJa,
                         language, nullptr) {
        setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    }
    QPlainTextEdit* Field() { return Multiline(668); }
};
QImage Client(HWND window) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    auto dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.right;
    info.bmiHeader.biHeight = -bounds.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap || !bits)
        throw std::runtime_error("native memory canvas");
    auto previous = SelectObject(dc, bitmap);
    FillRect(dc, &bounds, GetSysColorBrush(COLOR_BTNFACE));
    SendMessageW(window, WM_PRINTCLIENT, WPARAM(dc), PRF_CLIENT | PRF_ERASEBKGND);
    GdiFlush();
    auto image = QImage(static_cast<uchar*>(bits), bounds.right, bounds.bottom, bounds.right * 4,
                        QImage::Format_RGB32)
                     .copy();
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return image;
}
}
int main(int argc, char** argv) try {
    QApplication app(argc, argv);
    if (argc != 2 || QGuiApplication::platformName() != "windows")
        return 2;
    const QString output = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(output);
    QJsonArray records;
    unsigned cases = 0, failures = 0;
    for (const char* code : {"en", "ja", "zh_CN"}) {
        qt::Language language;
        if (!language.Set(code))
            return 1;
        Dialog dialog(language);
        auto* field = dialog.Field();
        const auto native = CreateDialogParamW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(dialog.Template().id), nullptr, Hidden, 0);
        const auto control = GetDlgItem(native, 668);
        if (!native || !control || IsWindowVisible(native))
            return 1;
        const QString texts[]{
            "abc def",          "1.5\n-12.5\n999",
            "  first\n\n last", "\t1\t2\tend",
            QString(90, 'x'),   "row0\nrow1\nrow2\nrow3\nrow4\nrow5\nrow6\nrow7\nrow8\nrow9"};
        for (unsigned variant = 0; variant < std::size(texts); ++variant)
            for (unsigned state = 0; state < 4; ++state)
                for (unsigned scroll = 0; scroll < 3; ++scroll)
                    for (unsigned selected = 0; selected < 2; ++selected) {
                        const bool enabled = !(state & 1), readOnly = state & 2;
                        field->setEnabled(enabled);
                        field->setReadOnly(readOnly);
                        field->setPlainText(texts[variant]);
                        field->moveCursor(QTextCursor::Start);
                        if (selected)
                            field->selectAll();
                        field->verticalScrollBar()->setValue(0);
                        field->horizontalScrollBar()->setValue(0);
                        EnableWindow(control, enabled);
                        SendMessageW(control, EM_SETREADONLY, readOnly, 0);
                        const auto text =
                            QString(texts[variant]).replace("\n", "\r\n").toStdWString();
                        SetWindowTextW(control, text.c_str());
                        SendMessageW(control, EM_SETSEL, 0, selected ? -1 : 0);
                        QImage actual(field->size(), QImage::Format_RGB32);
                        field->render(&actual); // Establish actual document scroll ranges.
                        if (scroll) {
                            SendMessageW(control, EM_LINESCROLL, scroll == 2 ? 3 : 0,
                                         scroll == 1 ? 2 : 0);
                            field->verticalScrollBar()->setValue(
                                int(SendMessageW(control, EM_GETFIRSTVISIBLELINE, 0, 0)));
                            RECT formatting{};
                            SendMessageW(control, EM_GETRECT, 0, LPARAM(&formatting));
                            const auto origin = SendMessageW(control, EM_POSFROMCHAR, 0, 0);
                            field->horizontalScrollBar()->setValue(formatting.left -
                                                                   short(LOWORD(origin)));
                        }
                        const auto expected = Client(control);
                        field->render(&actual);
                        actual = actual.copy(2, 2, expected.width(), expected.height());
                        unsigned difference = 0;
                        for (int y = 0; y < expected.height(); ++y)
                            for (int x = 0; x < expected.width(); ++x)
                                difference += (expected.pixel(x, y) & 0xffffff) !=
                                              (actual.pixel(x, y) & 0xffffff);
                        const auto stem = QString("%1-text%2-state%3-scroll%4-selected%5")
                                              .arg(code)
                                              .arg(variant)
                                              .arg(state)
                                              .arg(scroll)
                                              .arg(selected);
                        if (difference || (!variant && !state && !scroll && !selected)) {
                            expected.save(output + '/' + stem + "-native.png");
                            actual.save(output + '/' + stem + "-qt.png");
                        }
                        failures += difference != 0;
                        records.append(QJsonObject{{"case", stem}, {"different", int(difference)}});
                        ++cases;
                    }
        DestroyWindow(native);
        if (dialog.isVisible() || field->isVisible())
            return 1;
    }
    QFile report(output + "/report.json");
    if (!report.open(QIODevice::WriteOnly))
        return 1;
    report.write(QJsonDocument(QJsonObject{{"cases", int(cases)},
                                           {"failures", int(failures)},
                                           {"windowsShown", false},
                                           {"records", records}})
                     .toJson());
    std::cout << cases << " original multiline client paint states; " << failures
              << " mismatches; never shown\n";
    return cases != 432 || failures ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
