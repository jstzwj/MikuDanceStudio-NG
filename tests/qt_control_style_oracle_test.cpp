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
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionFrame>
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
QImage NativeImage(HWND control, bool visible, bool borderOnly = false) {
    RECT rectangle{}, child{};
    Check(GetWindowRect(control, &child), "native control window rectangle");
    // EDIT does not implement PrintWindow on every Windows version. Ask the
    // dialog to paint its complete child hierarchy, then crop our own control.
    // This never reads the screen or another process's window.
    const auto capture = visible ? GetParent(control) : control;
    Check(GetWindowRect(capture, &rectangle), "native capture window rectangle");
    const QRect crop(child.left - rectangle.left, child.top - rectangle.top,
                     child.right - child.left, child.bottom - child.top);
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
    if (borderOnly)
        FrameRect(dc, &rectangle, GetSysColorBrush(COLOR_WINDOWFRAME));
    else if (visible)
        Check(PrintWindow(capture, dc, 0), "capture our dialog in the isolated runner");
    else
        SendMessageW(control, WM_PRINT, reinterpret_cast<WPARAM>(dc),
                     PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
    GdiFlush();
    QImage image(static_cast<unsigned char*>(bits), width, height, width * 4, QImage::Format_RGB32);
    Check(image.rect().contains(crop), "control is inside native dialog capture");
    auto copy = image.copy(crop);
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
    unsigned mismatches = 0, cases = 0, templates = 0, borderCases = 0, borderMismatches = 0;
    for (std::size_t templateIndex = 0; templateIndex < sizeof descriptions / sizeof *descriptions;
         ++templateIndex) {
        const auto* description = descriptions[templateIndex];
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
            // WM_PRINT omits hidden EDIT nonclient paint. The hidden border
            // regression therefore compares only the GDI WS_BORDER primitive;
            // complete EDIT painting is qualified separately in isolated CI.
            const bool borderOnly = !visible && entry.kind == NumericControlKind::Edit;
            const bool other = entry.kind == NumericControlKind::Combobox ||
                               entry.kind == NumericControlKind::Listbox;
            if (visible ? entry.kind != NumericControlKind::Edit
                        : (entry.kind != NumericControlKind::Edit &&
                           entry.kind != NumericControlKind::Button &&
                           entry.kind != NumericControlKind::Checkbox &&
                           entry.kind != NumericControlKind::Radio && !other))
                continue;
            if (borderOnly && !(entry.style & 0x00800000u))
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
                // DS_3DLOOK dialog creation converts a listbox's WS_BORDER to
                // WS_EX_CLIENTEDGE. CreateWindowEx alone does not do that.
                entry.kind == NumericControlKind::Listbox ? WS_EX_CLIENTEDGE : 0,
                entry.kind == NumericControlKind::Edit       ? L"EDIT"
                : entry.kind == NumericControlKind::Combobox ? L"COMBOBOX"
                : entry.kind == NumericControlKind::Listbox  ? L"LISTBOX"
                : entry.kind == NumericControlKind::Slider   ? TRACKBAR_CLASSW
                                                             : L"BUTTON",
                L"",
                entry.kind == NumericControlKind::Listbox ? entry.style & ~WS_BORDER : entry.style,
                widget->x(), widget->y(), widget->width(), widget->height(), native,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            if (control)
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            if (control && entry.kind == NumericControlKind::Listbox)
                SetWindowPos(control, nullptr, 0, 0, widget->width(), widget->height(),
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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
            if (auto* combo = qobject_cast<QComboBox*>(widget)) {
                combo->clear();
                SendMessageW(control, CB_RESETCONTENT, 0, 0);
            }
            if (auto* list = qobject_cast<QListWidget*>(widget)) {
                list->clear();
                SendMessageW(control, LB_RESETCONTENT, 0, 0);
            }
            for (unsigned state = 0; state < (entry.kind == NumericControlKind::Listbox ? 4u
                                              : !button                                 ? 2u
                                              : button->isCheckable()                   ? 6u
                                                                                        : 3u);
                 ++state) {
                const bool enabled = state % 3 != 1, pressed = state % 3 == 2, checked = state >= 3;
                EnableWindow(control, enabled);
                widget->setEnabled(enabled);
                if (auto* list = qobject_cast<QListWidget*>(widget)) {
                    if (state == 2) {
                        list->addItem(QString());
                        list->setCurrentRow(0);
                        SendMessageW(control, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L""));
                        SendMessageW(control, LB_SETCURSEL, 0, 0);
                    }
                    // List states 2/3 check the same selected, textless row.
                    const bool listEnabled = state % 2 == 0;
                    list->setEnabled(listEnabled);
                    EnableWindow(control, listEnabled);
                }
                if (button) {
                    SendMessageW(control, BM_SETSTATE, pressed, 0);
                    SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
                    button->setDown(pressed);
                    button->setChecked(checked);
                }
                if (visible) {
                    RedrawWindow(native, nullptr, nullptr,
                                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN |
                                     RDW_UPDATENOW);
                    UpdateWindow(control);
                    QCoreApplication::processEvents();
                }
                const auto expected = NativeImage(control, visible, borderOnly);
                widget->resize(expected.size());
                QImage actual(expected.size(), QImage::Format_RGB32);
                actual.fill(app.palette().window().color());
                if (borderOnly) {
                    QStyleOptionFrame option;
                    option.initFrom(widget);
                    QPainter painter(&actual);
                    widget->style()->drawPrimitive(QStyle::PE_FrameLineEdit, &option, &painter,
                                                   widget);
                } else
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
                if (borderOnly) {
                    borderMismatches += difference != 0;
                    ++borderCases;
                } else {
                    mismatches += difference != 0;
                    ++cases;
                }
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
    QJsonObject report{
        {"qt", qVersion()},
        {"platform", QGuiApplication::platformName()},
        {"os", QSysInfo::prettyProductName()},
        {"style", static_cast<QProxyStyle*>(app.style())->baseStyle()->objectName()},
        {"templates", int(templates)},
        {"cases", int(cases)},
        {"mismatches", int(mismatches)},
        {"gdiBorderCases", int(borderCases)},
        {"gdiBorderMismatches", int(borderMismatches)},
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
    return mismatches || borderMismatches ? 1 : 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
