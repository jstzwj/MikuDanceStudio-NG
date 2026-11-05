#include "numeric_dialog.hpp"
#include "dialog_static_label.hpp"
#include "editor_font.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTextLayout>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "hidden_resource_dialog.hpp"
#endif

using namespace mikudancestudio;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
class Dialog final : public qt::TemplateDialog {
public:
    Dialog(const ui::NumericTemplate& description, const qt::Language& language)
        : TemplateDialog(description, language, nullptr) {
        setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    }
};
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
#ifdef _WIN32
INT_PTR CALLBACK Hidden(HWND, UINT message, WPARAM, LPARAM) {
    return message == WM_NEXTDLGCTL;
}
QImage Native(HWND control) {
    RECT bounds{};
    Check(GetClientRect(control, &bounds), "original STATIC bounds");
    auto dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.right;
    info.bmiHeader.biHeight = -bounds.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    Check(dc && bitmap && bits, "original STATIC memory canvas");
    const auto previous = SelectObject(dc, bitmap);
    FillRect(dc, &bounds, GetSysColorBrush(COLOR_BTNFACE));
    SendMessageW(control, WM_PRINTCLIENT, WPARAM(dc), PRF_CLIENT | PRF_ERASEBKGND);
    GdiFlush();
    auto result = QImage(static_cast<uchar*>(bits), bounds.right, bounds.bottom, bounds.right * 4,
                         QImage::Format_RGB32)
                      .copy();
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return result;
}
unsigned Difference(const QImage& a, const QImage& b) {
    Check(a.size() == b.size(), "STATIC dimensions remain original");
    unsigned count = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            count += (a.pixel(x, y) & 0xffffff) != (b.pixel(x, y) & 0xffffff);
    return count;
}
#endif
}
int main(int argc, char** argv) try {
#ifdef _WIN32
    const auto foregroundAtEntry = GetForegroundWindow(), focusAtEntry = GetFocus();
    HiddenDialogInputGuard inputGuard;
#endif
    QApplication app(argc, argv);
    const bool native = argc == 3 && QString::fromLocal8Bit(argv[2]) == "--native";
    Check(argc == 2 || native, "output directory and optional native observer");
    Check(QGuiApplication::platformName() == (native ? "windows" : "offscreen"),
          "never-shown Windows or offscreen qualification only");
    Check(qt::InstallEditorFonts(), "packaged open fonts available");
    const auto output = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(output);
    QJsonArray records;
    unsigned failures = 0;
    for (const auto* code : {"en", "ja", "zh_CN"}) {
        if (native && QString::fromLatin1(code) == "zh_CN")
            continue; // There is no original Simplified Chinese resource.
        qt::Language language;
        Check(language.Set(code), "three-language static captions");
        for (const auto* description : language.Code() == "en" ? english : japanese) {
            Dialog dialog(*description, language);
            if (!native && language.Code() != "en") {
                auto font = dialog.font();
                font.setFamilies(
                    {language.Code() == "ja" ? "Noto Sans CJK JP" : "Noto Sans CJK SC"});
                dialog.setFont(qt::MatchEditorFontMetrics(font));
            }
#ifdef _WIN32
            HWND reference = nullptr;
            std::vector<HWND> children;
            if (native) {
                reference =
                    CreateHiddenResourceDialog(GetModuleHandleW(nullptr), description->id, Hidden);
                Check(reference && !IsWindowVisible(reference), "never-shown original dialog");
                for (auto child = GetTopWindow(reference); child;
                     child = GetWindow(child, GW_HWNDNEXT))
                    children.push_back(child);
                Check(children.size() == description->count, "unchanged original child order");
            }
#endif
            for (unsigned i = 0; i < description->count; ++i) {
                const auto& control = description->controls[i];
                if (control.kind != ui::NumericControlKind::Static)
                    continue;
                auto* label = dialog.findChild<QLabel*>(QString("numeric.control.%1").arg(i));
                Check(label && label->text() == language.Text(control.message),
                      "complete original caption");
                Check(label->wordWrap(), "original SS_LEFT wrapping enabled");
                for (const bool enabled : {true, false}) {
                    label->setEnabled(enabled);
                    QImage actual(label->size(), QImage::Format_RGB32);
                    label->render(&actual); // Delivers pending resize without showing a window.
                    const auto stem = QString("%1-dialog%2-control%3-enabled%4")
                                          .arg(code)
                                          .arg(description->id)
                                          .arg(i)
                                          .arg(enabled);
                    unsigned different = 0;
                    bool complete = true;
                    int lines = 0;
#ifdef _WIN32
                    if (native) {
                        EnableWindow(children.at(i), enabled);
                        const auto expected = Native(children.at(i));
                        different = Difference(expected, actual);
                        expected.save(output + '/' + stem + "-native.png");
                    } else
#endif
                    {
                        QTextLayout layout(label->text(), label->font());
                        QTextOption option;
                        option.setWrapMode(QTextOption::WordWrap);
                        layout.setTextOption(option);
                        qreal height = 0;
                        int consumed = 0;
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
                    }
                    actual.save(output + '/' + stem + "-qt.png");
                    failures += different != 0 || !complete;
                    if (different || !complete)
                        std::cerr << stem.toStdString() << " different=" << different
                                  << " complete=" << complete << '\n';
                    records.append(QJsonObject{{"case", stem},
                                               {"text", label->text()},
                                               {"font", QFontInfo(label->font()).family()},
                                               {"stretch", label->font().stretch()},
                                               {"lines", lines},
                                               {"complete", complete},
                                               {"different", int(different)}});
                    Check(!dialog.isVisible() && !label->isVisible(),
                          "static observer never shows windows");
                }
            }
#ifdef _WIN32
            if (reference)
                DestroyWindow(reference);
            DWORD process = 0;
            const auto foreground = GetForegroundWindow();
            if (foreground)
                GetWindowThreadProcessId(foreground, &process);
            Check(GetFocus() == focusAtEntry &&
                      (process != GetCurrentProcessId() || foreground == foregroundAtEntry),
                  "static observer never takes focus or foreground");
#endif
        }
    }
    if (!native) {
        QWidget parent;
        QFont font("Noto Sans CJK JP");
        font.setPixelSize(12);
        parent.setFont(qt::MatchEditorFontMetrics(font));
        const auto text = QString::fromUtf8("選択されたフレームポイントのうち、物理影響ボーンの");
        qt::DialogStaticLabel label(text, 0x50020000u, &parent);
        const auto render = [&] {
            QImage image(label.size(), QImage::Format_RGB32);
            label.render(&image);
        };
        label.resize(280, 18);
        render();
        Check(label.size() == QSize(280, 18) && label.font().stretch() < 100,
              "portable long caption fits without changing control geometry");
        label.resize(600, 18);
        render();
        Check(label.font().stretch() == 100, "widening restores original font width");
        label.resize(280, 18);
        render();
        label.setText(QString::fromUtf8("幅"));
        render();
        Check(label.font().stretch() == 100, "dynamic physics caption restores normal width");
        font.setFamily("Noto Sans CJK SC");
        parent.setFont(qt::MatchEditorFontMetrics(font));
        render();
        Check(QFontInfo(label.font()).family() == "Noto Sans CJK SC",
              "fitted caption continues inheriting the parent's language font");
    }
    QFile report(output + "/report.json");
    Check(report.open(QIODevice::WriteOnly), "static label report");
    report.write(QJsonDocument(QJsonObject{{"native", native},
                                           {"windowsShown", false},
                                           {"cases", records.size()},
                                           {"failures", int(failures)},
                                           {"records", records}})
                     .toJson());
    std::cout << records.size() << " static label cases; " << failures
              << " failures; never shown\n";
    return failures ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
