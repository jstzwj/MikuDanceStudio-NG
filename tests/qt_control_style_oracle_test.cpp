#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

static void Check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
#ifndef MDS_SYSTEM_CONTROL_REFERENCE
#include "original_physics_module.hpp"
#endif
#include "numeric_dialog.hpp"
#include "editor_style.hpp"
#include <QApplication>
#include <QImage>
#include <QPushButton>
#include <QStyle>
#include <QProxyStyle>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
namespace {
class ResourceDialog final : public mikudancestudio::qt::TemplateDialog {
public:
    ResourceDialog(const mikudancestudio::ui::NumericTemplate& description,
                   const mikudancestudio::qt::Language& language)
        : TemplateDialog(description, language, nullptr) {}
};
INT_PTR CALLBACK HiddenDialog(HWND, UINT, WPARAM, LPARAM) {
    return FALSE;
}
QImage NativeImage(HWND control, bool visible) {
    RECT rectangle{};
    Check(GetWindowRect(control, &rectangle), "native control window rectangle");
    const int width = rectangle.right - rectangle.left, height = rectangle.bottom - rectangle.top;
    rectangle = {0, 0, width, height};
    auto dc = CreateCompatibleDC(nullptr);
    Check(dc != nullptr, "memory control DC");
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    Check(bitmap && bits, "memory control bitmap");
    const auto previous = SelectObject(dc, bitmap);
    FillRect(dc, &rectangle, GetSysColorBrush(COLOR_BTNFACE));
    if (visible)
        Check(PrintWindow(control, dc, 0), "capture our control in the isolated runner");
    else
        SendMessageW(control, WM_PRINT, reinterpret_cast<WPARAM>(dc),
                     PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
    GdiFlush();
    QImage image(static_cast<unsigned char*>(bits), width, height, width * 4, QImage::Format_RGB32);
    auto copy = image.copy();
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return copy;
}
unsigned Difference(const QImage& a, const QImage& b) {
    Check(a.size() == b.size(), "control raster dimensions");
    unsigned count = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            count += (a.pixel(x, y) & 0xffffff) != (b.pixel(x, y) & 0xffffff);
    return count;
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    QApplication app(argc, argv);
#ifdef MDS_SYSTEM_CONTROL_REFERENCE
    const bool visible = argc == 3 && QString::fromLocal8Bit(argv[1]) == "--isolated-nonclient";
    Check(argc == 2 || visible, "usage: [--isolated-nonclient] output-directory");
    Check(!visible || (qEnvironmentVariable("GITHUB_ACTIONS") == "true" &&
                       qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1"),
          "visible control qualification requires an isolated GitHub runner");
#else
    constexpr bool visible = false;
    Check(argc == 3, "usage: original.exe output-directory");
#endif
    Check(QGuiApplication::platformName() == "windows", "real Windows QPA required");
#ifdef MDS_SYSTEM_CONTROL_REFERENCE
    const auto instance = GetModuleHandleW(nullptr);
#else
    OriginalPhysicsModule original(std::filesystem::u8path(argv[1]));
    const auto instance = reinterpret_cast<HINSTANCE>(original.Function<void*>(0));
#endif
    const auto output = std::filesystem::absolute(std::filesystem::u8path(argv[argc - 1]));
    std::filesystem::create_directories(output);
    mikudancestudio::qt::InstallEditorStyle(app);
    INITCOMMONCONTROLSEX common{sizeof common, ICC_BAR_CLASSES};
    Check(InitCommonControlsEx(&common), "initialize original trackbar class");
    using namespace mikudancestudio::ui;
    const NumericTemplate* descriptions[] = {
        &kNumericBoneEn,
        &kNumericBoneJa,
        &kNumericCameraEn,
        &kNumericCameraJa,
        &kNumericScreenEn,
        &kNumericScreenJa,
        &kNumericEdgeEn,
        &kNumericEdgeJa,
        &kNumericGravityEn,
        &kNumericGravityJa,
        &kNumericMorphScaleEn,
        &kNumericMorphScaleJa,
        &kNumericCameraScaleEn,
        &kNumericCameraScaleJa,
        &kNumericBoneScaleEn,
        &kNumericBoneScaleJa,
        &kNumericPhysicsKeysEn,
        &kNumericPhysicsKeysJa,
        &kNumericDrawOrderEn,
        &kNumericDrawOrderJa,
        &kNumericCalculationOrderEn,
        &kNumericCalculationOrderJa,
        &kNumericGroundShadowEn,
        &kNumericGroundShadowJa,
        &kNumericCenterBiasEn,
        &kNumericCenterBiasJa,
        &kNumericAccessoryOrderEn,
        &kNumericAccessoryOrderJa,
        &kNumericFrameShiftEn,
        &kNumericFrameShiftJa,
        &kNumericBlinkEn,
        &kNumericBlinkJa,
        &kNumericEnglishNameEn,
        &kNumericEnglishNameJa,
        &kNumericToonEn,
        &kNumericToonJa,
        &kNumericModelPhysicsEn,
        &kNumericModelPhysicsJa,
    };
    unsigned mismatches = 0, cases = 0, templates = 0;
    for (std::size_t templateIndex = 0; templateIndex < sizeof descriptions / sizeof *descriptions;
         ++templateIndex) {
        const auto* description = descriptions[templateIndex];
        if (visible && description != &kNumericCameraEn && description != &kNumericCameraJa)
            continue;
        ++templates;
        const char* code = templateIndex % 2 ? "ja" : "en";
        mikudancestudio::qt::Language language;
        Check(language.Set(code), "load UI language");
        ResourceDialog dialog(*description, language);
        dialog.winId(); // Create only our own never-shown native parent.
#ifdef MDS_SYSTEM_CONTROL_REFERENCE
        const auto native = CreateWindowExW(0, L"#32770", L"", WS_POPUP, 0, 0, dialog.width(),
                                            dialog.height(), nullptr, nullptr, instance, nullptr);
        const auto font = CreateFontW(
            -description->pointSize * 96 / 72, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH, QString::fromUtf8(description->fontFamily).toStdWString().c_str());
        Check(font != nullptr, "native reference control font");
#else
        const auto resource = FindResourceW(instance, MAKEINTRESOURCEW(description->id), RT_DIALOG);
        Check(resource != nullptr, "original dialog resource");
        const auto* bytes =
            static_cast<const unsigned char*>(LockResource(LoadResource(instance, resource)));
        Check(bytes != nullptr, "original dialog bytes");
        DWORD flags{};
        std::memcpy(&flags, bytes + (bytes[2] == 255 && bytes[3] == 255 ? 12 : 0), sizeof flags);
        Check(!(flags & WS_VISIBLE), "original resource must never show a window");
        const auto native = CreateDialogParamW(instance, MAKEINTRESOURCEW(dialog.Template().id),
                                               nullptr, HiddenDialog, 0);
#endif
        Check(native && !IsWindowVisible(native), "create hidden original resource dialog");
        // Compare states without keyboard-focus cues. DefDlgProc can retain
        // its initial button focus internally even while never shown; Qt's
        // hidden controls have no active focus. Focused text/glyphs are separate.
        SendMessageW(native, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS | UISF_HIDEACCEL),
                     0);
        if (visible) {
            // Explicit runner-only path; never enabled by a local CTest label.
            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
            dialog.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
            for (auto* child : dialog.findChildren<QWidget*>())
                child->setFocusPolicy(Qt::NoFocus);
            dialog.show();
            ShowWindow(native, SW_SHOWNOACTIVATE);
            QCoreApplication::processEvents();
        }
        for (std::size_t index = 0; index < description->count; ++index) {
            const auto& entry = description->controls[index];
            // WM_PRINT does not capture hidden EDIT nonclient theme borders.
            // Do not count an empty memory-DC margin as an independent oracle.
            if (visible ? entry.kind != NumericControlKind::Edit
                        : (entry.kind != NumericControlKind::Button &&
                           entry.kind != NumericControlKind::Checkbox &&
                           entry.kind != NumericControlKind::Radio))
                continue;
            const int id = entry.id;
            if (!entry.width || !entry.height)
                continue;
            auto* widget = dialog.findChild<QWidget*>(QString("numeric.control.%1").arg(index));
            Check(widget != nullptr, "matching Qt control");
            // Qualify steady states, independently of the host's animation
            // accessibility preference and the time between memory captures.
            widget->setProperty("_q_no_animation", true);
#ifdef MDS_SYSTEM_CONTROL_REFERENCE
            // Public regression uses the frozen original resource styles and
            // independently painted Win32 controls. Full resource creation is
            // separately checked by the optional original-executable oracle.
            const auto control = CreateWindowExW(
                0, visible ? L"EDIT" : L"BUTTON", L"", entry.style, widget->x(), widget->y(),
                widget->width(), widget->height(), native,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            if (control)
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
#else
            const auto control = GetDlgItem(native, id);
#endif
            Check(control && (visible || !IsWindowVisible(control)), "native reference control");
            SetWindowTextW(control, L""); // Isolate theme paint from font qualification.
            auto* button = qobject_cast<QAbstractButton*>(widget);
            if (button)
                button->setText({});
            if (auto* edit = qobject_cast<QLineEdit*>(widget))
                edit->clear();
            for (unsigned state = 0; state < (!button                 ? 2u
                                              : button->isCheckable() ? 6u
                                                                      : 3u);
                 ++state) {
                const bool enabled = state % 3 != 1, pressed = state % 3 == 2, checked = state >= 3;
                EnableWindow(control, enabled);
                widget->setEnabled(enabled);
                if (button) {
                    SendMessageW(control, BM_SETSTATE, pressed, 0);
                    SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
                    button->setDown(pressed);
                    button->setChecked(checked);
                }
                if (visible) {
                    UpdateWindow(control);
                    QCoreApplication::processEvents();
                }
                const auto expected = NativeImage(control, visible);
                widget->resize(expected.size());
                QImage actual(expected.size(), QImage::Format_RGB32);
                actual.fill(app.palette().window().color());
                widget->render(&actual);
                const auto difference = Difference(actual, expected);
                const auto prefix =
                    QString("%1-%2-%3-%4").arg(description->id).arg(code).arg(id).arg(state);
                Check(expected.save(QString::fromStdWString(
                          (output / (prefix.toStdWString() + L"-native.png")).wstring())),
                      "save original control raster");
                Check(actual.save(QString::fromStdWString(
                          (output / (prefix.toStdWString() + L"-qt.png")).wstring())),
                      "save Qt control raster");
                std::printf("%d %s control=%d state=%u size=%dx%d different=%u\n", description->id,
                            code, id, state, actual.width(), actual.height(), difference);
                mismatches += difference != 0;
                ++cases;
            }
        }
        Check(visible || (!IsWindowVisible(native) && !dialog.isVisible()),
              "control qualification exposed a window");
        DestroyWindow(native);
#ifdef MDS_SYSTEM_CONTROL_REFERENCE
        DeleteObject(font);
#endif
    }
    std::printf("%u native control-theme cases; %u mismatches; no desktop input\n", cases,
                mismatches);
    QJsonObject report{{"qt", qVersion()},
                       {"platform", QGuiApplication::platformName()},
                       {"os", QSysInfo::prettyProductName()},
                       {"style", static_cast<QProxyStyle*>(app.style())->baseStyle()->objectName()},
                       {"templates", int(templates)},
                       {"cases", int(cases)},
                       {"mismatches", int(mismatches)},
                       {"comparison", visible ? "exact RGB, EDIT normal/disabled including nonclient"
                                               : "exact RGB, normal/disabled/pressed, checked/unchecked"},
                       {"textAndKeyboardFocusCues", false},
                       {"windowsShown", visible}};
#ifdef MDS_SYSTEM_CONTROL_REFERENCE
    report.insert("reference", "Win32 controls using frozen original resource styles");
#else
    report.insert("reference", "actual hash-verified original dialog resources");
#endif
    QFile file(QString::fromStdWString((output / "report.json").wstring()));
    Check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(report).toJson()) > 0,
          "write native control qualification");
    return mismatches ? 1 : 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
