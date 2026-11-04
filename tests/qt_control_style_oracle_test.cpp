#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>
#include <stdexcept>
#include <memory>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>

static void Check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
#ifndef MDS_SYSTEM_CONTROL_REFERENCE
#include "original_physics_module.hpp"
#endif
#include "numeric_dialog.hpp"
#include "editor_style.hpp"
#include "editor_style_windows.hpp"
#include <QElapsedTimer>
#include <QThread>
#include "mikudancestudio/mmd_trackbar_geometry.hpp"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QStyleOptionSlider>
#include <QStyleOptionButton>
#include <QProxyStyle>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStyleOptionComboBox>
#include <QSysInfo>
#include <QKeyEvent>
namespace {
class ResourceDialog final : public mikudancestudio::qt::TemplateDialog {
public:
    ResourceDialog(const mikudancestudio::ui::NumericTemplate& description,
                   const mikudancestudio::qt::Language& language)
        : TemplateDialog(description, language, nullptr) {}
};
struct DialogCommands {
    unsigned last = 0;
};
INT_PTR CALLBACK HiddenDialog(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG)
        SetWindowLongPtrW(window, DWLP_USER, lParam);
    // Native multiline Return posts a default-button command via
    // WM_NEXTDLGCTL. Preserve its command/text processing while preventing
    // any hidden-window focus/activation, including indirect SetFocus calls.
    if (message == WM_NEXTDLGCTL && !IsWindowVisible(window))
        return TRUE;
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED)
        if (auto* commands =
                reinterpret_cast<DialogCommands*>(GetWindowLongPtrW(window, DWLP_USER))) {
            commands->last = LOWORD(wParam);
            return TRUE;
        }
    return FALSE;
}
class ThreadKeyboardState {
public:
    explicit ThreadKeyboardState(bool control) {
        Check(GetKeyboardState(previous_), "read this test thread's keyboard table");
        SetControl(control);
    }
    static void SetControl(bool control) {
        BYTE state[256]{};
        if (control)
            state[VK_CONTROL] = 0x80;
        Check(SetKeyboardState(state), "set only this test thread's keyboard table");
    }
    ~ThreadKeyboardState() { SetKeyboardState(previous_); }

private:
    BYTE previous_[256]{};
};
unsigned CheckHiddenInput(HWND foregroundBefore, HWND focusBefore) {
    const auto foreground = GetForegroundWindow();
    DWORD process = 0;
    if (foreground)
        GetWindowThreadProcessId(foreground, &process);
    // The user may switch between unrelated applications while this hidden
    // observer runs. Only this thread's focus and activation of our own process
    // are under test; never restore or otherwise alter desktop foreground.
    Check(process != GetCurrentProcessId(), "hidden observer activated a test-owned window");
    Check(GetFocus() == focusBefore, "hidden observer changed its thread's native focus");
    return foreground != foregroundBefore;
}
QImage NativeImage(HWND control, bool visible, bool clientOnly = false) {
    RECT rectangle{}, child{};
    Check(GetWindowRect(control, &child), "native control window rectangle");
    // EDIT does not implement PrintWindow on every Windows version. Ask the
    // dialog to paint its complete child hierarchy, then crop our own control.
    // This never reads the screen or another process's window.
    const auto capture = visible ? GetParent(control) : control;
    Check(GetWindowRect(capture, &rectangle), "native capture window rectangle");
    const QRect crop(child.left - rectangle.left, child.top - rectangle.top,
                     child.right - child.left, child.bottom - child.top);
    // Original resources include overlapping body/joint controls whose real
    // dialog procedure normally switches visibility. A whole-dialog capture
    // must not compare those siblings over this independently rendered widget.
    std::vector<HWND> overlaps;
    if (visible)
        for (auto sibling = GetTopWindow(capture); sibling;
             sibling = GetWindow(sibling, GW_HWNDNEXT)) {
            RECT bounds{}, intersection{};
            if (sibling != control && IsWindowVisible(sibling) && GetWindowRect(sibling, &bounds) &&
                IntersectRect(&intersection, &child, &bounds)) {
                overlaps.push_back(sibling);
                ShowWindow(sibling, SW_HIDE);
            }
        }
    if (clientOnly)
        Check(GetClientRect(control, &rectangle), "native control client rectangle");
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
    if (clientOnly)
        SendMessageW(control, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc),
                     PRF_CLIENT | PRF_ERASEBKGND);
    else if (visible)
        Check(PrintWindow(capture, dc, 0), "capture our dialog in the isolated runner");
    else
        SendMessageW(control, WM_PRINT, reinterpret_cast<WPARAM>(dc),
                     PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
    GdiFlush();
    QImage image(static_cast<unsigned char*>(bits), width, height, width * 4, QImage::Format_RGB32);
    Check(clientOnly || image.rect().contains(crop), "control is inside native dialog capture");
    auto copy = clientOnly ? image.copy() : image.copy(crop);
    for (const auto sibling : overlaps)
        ShowWindow(sibling, SW_SHOWNOACTIVATE);
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
unsigned TrackbarGeometryCorpus() {
    const auto parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 600, 80, nullptr,
                                        nullptr, nullptr, nullptr);
    Check(parent != nullptr, "hidden geometry parent");
    unsigned cases = 0;
    for (const bool vertical : {false, true})
        for (const int width : {64, 170, 193, 513})
            for (const int height : {11, 12, 15, 19, 20, 23, 24, 26, 32})
                for (const bool ticks : {false, true}) {
                    const auto control =
                        CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                        WS_CHILD | WS_VISIBLE | (ticks ? 0 : TBS_NOTICKS) |
                                            (vertical ? TBS_VERT : 0),
                                        0, 0, vertical ? height : width, vertical ? width : height,
                                        parent, nullptr, nullptr, nullptr);
                    Check(control && !IsWindowVisible(control), "never-shown geometry trackbar");
                    QSlider slider(vertical ? Qt::Vertical : Qt::Horizontal);
                    slider.resize(vertical ? height : width, vertical ? width : height);
                    slider.setProperty("mmdNativeTrackbar", true);
                    for (const auto range :
                         {std::pair{0, 100}, std::pair{0, 200}, std::pair{-100, 100}}) {
                        SendMessageW(control, TBM_SETRANGE, FALSE,
                                     MAKELONG(range.first, range.second));
                        for (const int percent : {0, 37, 50, 100}) {
                            const int value =
                                range.first + (range.second - range.first) * percent / 100;
                            SendMessageW(control, TBM_SETPOS, TRUE, value);
                            const auto geometry = mikudancestudio::ui::DialogTrackbarGeometry(
                                width, height, ticks, range.first, range.second, value);
                            for (const bool thumb : {false, true}) {
                                RECT r{};
                                SendMessageW(control, thumb ? TBM_GETTHUMBRECT : TBM_GETCHANNELRECT,
                                             0, reinterpret_cast<LPARAM>(&r));
                                // TBM_GETCHANNELRECT documents horizontal coordinates even
                                // for TBS_VERT; GETTHUMBRECT already returns client coordinates.
                                if (vertical && !thumb) {
                                    std::swap(r.left, r.top);
                                    std::swap(r.right, r.bottom);
                                }
                                auto expected = thumb ? geometry.thumb : geometry.channel;
                                if (vertical)
                                    expected = {expected.y, expected.x, expected.height,
                                                expected.width};
                                if (r.left != expected.x || r.top != expected.y ||
                                    r.right - r.left != expected.width ||
                                    r.bottom - r.top != expected.height) {
                                    std::fprintf(
                                        stderr,
                                        "trackbar geometry %dx%d vertical=%d ticks=%d range=%d..%d value=%d thumb=%d native=%ld,%ld,%ld,%ld expected=%d,%d,%d,%d\n",
                                        width, height, vertical, ticks, range.first, range.second,
                                        value, thumb, r.left, r.top, r.right - r.left,
                                        r.bottom - r.top, expected.x, expected.y, expected.width,
                                        expected.height);
                                    Check(false, "independent native trackbar geometry corpus");
                                }
                                QStyleOptionSlider option;
                                option.initFrom(&slider);
                                option.orientation = slider.orientation();
                                option.minimum = range.first;
                                option.maximum = range.second;
                                option.sliderPosition = value;
                                option.tickPosition =
                                    ticks ? QSlider::TicksBelow : QSlider::NoTicks;
                                option.upsideDown = false;
                                Check(slider.style()->subControlRect(
                                          QStyle::CC_Slider, &option,
                                          thumb ? QStyle::SC_SliderHandle : QStyle::SC_SliderGroove,
                                          &slider) ==
                                          QRect(r.left, r.top, r.right - r.left, r.bottom - r.top),
                                      "Qt trackbar geometry matches native orientation");
                            }
                            ++cases;
                        }
                    }
                    DestroyWindow(control);
                }
    DestroyWindow(parent);
    return cases;
}
unsigned ButtonTextGeometryCorpus(const std::filesystem::path& output) {
    const auto parent = CreateWindowExW(0, L"#32770", L"", WS_POPUP, 0, 0, 200, 100, nullptr,
                                        nullptr, nullptr, nullptr);
    Check(parent && !IsWindowVisible(parent), "never-shown button text corpus");
    unsigned cases = 0, mismatches = 0;
    for (const auto family : {L"Arial", L"Tahoma", L"MS PGothic", L"MS UI Gothic"}) {
        const auto font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      0, 0, DEFAULT_QUALITY, 0, family);
        Check(font != nullptr, "button text corpus font");
        for (int height = 12; height <= 24; ++height)
            for (const int kind : {0, 1, 2, 3})
                for (const auto caption : {L"ABC", L"\u30b8\u30e7\u30a4\u30f3\u30c8"}) {
                    const bool radio = kind == 1, pushLike = kind == 3, push = kind >= 2;
                    const auto control =
                        CreateWindowExW(0, L"BUTTON", caption,
                                        WS_CHILD | WS_VISIBLE |
                                            (pushLike ? BS_AUTOCHECKBOX | BS_PUSHLIKE
                                             : push   ? BS_PUSHBUTTON
                                             : radio  ? BS_AUTORADIOBUTTON
                                                      : BS_AUTOCHECKBOX),
                                        0, 0, 100, height, parent, nullptr, nullptr, nullptr);
                    Check(control && !IsWindowVisible(control), "never-shown text corpus control");
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
                    SendMessageW(control, WM_CHANGEUISTATE,
                                 MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
                    std::unique_ptr<QAbstractButton> button(
                        push    ? static_cast<QAbstractButton*>(new QPushButton)
                        : radio ? static_cast<QAbstractButton*>(new QRadioButton)
                                : static_cast<QAbstractButton*>(new QCheckBox));
                    button->setText(QString::fromWCharArray(caption));
                    button->setCheckable(pushLike);
                    button->setProperty("mmdControlId", 1);
                    QFont qfont(QString::fromWCharArray(family));
                    qfont.setPixelSize(12);
                    button->setFont(qfont);
                    button->resize(100, height);
                    for (int state = 0; state < (pushLike ? 6 : 3); ++state) {
                        const bool enabled = state % 3 != 1, focused = state % 3 == 2;
                        button->setChecked(state >= 3);
                        SendMessageW(control, BM_SETCHECK, state >= 3 ? BST_CHECKED : BST_UNCHECKED,
                                     0);
                        button->setEnabled(enabled);
                        EnableWindow(control, enabled);
                        SendMessageW(control, focused ? WM_SETFOCUS : WM_KILLFOCUS, 0, 0);
                        SendMessageW(control, WM_CHANGEUISTATE,
                                     MAKEWPARAM(focused ? UIS_CLEAR : UIS_SET, UISF_HIDEFOCUS), 0);
                        const auto expected = NativeImage(control, false);
                        QImage actual(button->size(), QImage::Format_RGB32);
                        actual.fill(qApp->palette().window().color());
                        if (focused) {
                            QStyleOptionButton option;
                            option.initFrom(button.get());
                            option.text = button->text();
                            option.state |=
                                QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
                            if (button->isChecked())
                                option.state |= QStyle::State_On;
                            QPainter painter(&actual);
                            button->style()->drawControl(push    ? QStyle::CE_PushButton
                                                         : radio ? QStyle::CE_RadioButton
                                                                 : QStyle::CE_CheckBox,
                                                         &option, &painter, button.get());
                        } else
                            button->render(&actual);
                        const auto different = Difference(actual, expected);
                        if (different) {
                            const auto prefix = QString::fromStdWString(output.wstring()) +
                                                QString("/text-geometry-%1").arg(cases);
                            actual.save(prefix + "-qt.png");
                            expected.save(prefix + "-native.png");
                            std::fprintf(stderr,
                                         "text geometry case=%u height=%d radio=%d diff=%u\n",
                                         cases, height, radio, different);
                        }
                        mismatches += different != 0;
                        ++cases;
                    }
                    DestroyWindow(control);
                }
        DeleteObject(font);
    }
    DestroyWindow(parent);
    Check(!mismatches, "independent native button text geometry corpus");
    return cases;
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    QApplication app(argc, argv);
#ifdef MDS_SYSTEM_CONTROL_REFERENCE
    const bool replayFocus = argc == 4 && QString::fromLocal8Bit(argv[1]) == "--replay-edit-focus";
    const bool editHover = argc == 3 && QString::fromLocal8Bit(argv[1]) == "--isolated-edit-hover";
    const bool editFocus =
        replayFocus || (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--isolated-edit-focus");
    const bool editText =
        replayFocus || editHover ||
        (argc == 3 && (QString::fromLocal8Bit(argv[1]) == "--edit-text" ||
                       QString::fromLocal8Bit(argv[1]) == "--isolated-edit-text" || editFocus));
    const bool comboFocus =
        argc == 3 && QString::fromLocal8Bit(argv[1]) == "--isolated-content-focus";
    const bool focused =
        !replayFocus && (editFocus || comboFocus ||
                         (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--isolated-list-focus"));
    const bool visible = argc == 3 && (QString::fromLocal8Bit(argv[1]) == "--isolated-edit-text" ||
                                       QString::fromLocal8Bit(argv[1]) == "--isolated-nonclient" ||
                                       QString::fromLocal8Bit(argv[1]) == "--isolated-content" ||
                                       editHover || focused);
    const bool text = argc == 3 && QString::fromLocal8Bit(argv[1]) == "--text";
    const bool content = argc == 3 && (QString::fromLocal8Bit(argv[1]) == "--content" ||
                                       QString::fromLocal8Bit(argv[1]) == "--isolated-content" ||
                                       (focused && !editFocus));
    Check(argc == 2 || visible || text || content || editText,
          "usage: [--isolated-nonclient|--text|--content] output-directory");
    Check(!visible || (qEnvironmentVariable("GITHUB_ACTIONS") == "true" &&
                       qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") == "1"),
          "visible control qualification requires an isolated GitHub runner");
#else
    constexpr bool replayFocus = false;
    constexpr bool editHover = false;
    constexpr bool editFocus = false;
    constexpr bool visible = false;
    constexpr bool focused = false;
    constexpr bool comboFocus = false;
    const bool editText = argc == 4 && QString::fromLocal8Bit(argv[1]) == "--edit-text";
    const bool text = argc == 4 && QString::fromLocal8Bit(argv[1]) == "--text";
    const bool content = argc == 4 && QString::fromLocal8Bit(argv[1]) == "--content";
    Check(argc == 3 || text || content || editText,
          "usage: [--text|--content|--edit-text] original.exe output-directory");
#endif
    Check(QGuiApplication::platformName() == "windows", "real Windows QPA required");
    if (visible) {
        // Only the guarded disposable runner may move its cursor. Keep the
        // normal/focus profiles out of native hover state; a cursor left over
        // a resource control otherwise makes the expected state accidental.
        Check(SetCursorPos(0, 0), "park isolated runner cursor outside both dialogs");
        app.setCursorFlashTime(0);
    }
#ifdef MDS_SYSTEM_CONTROL_REFERENCE
    const auto instance = GetModuleHandleW(nullptr);
#else
    OriginalPhysicsModule original(
        std::filesystem::u8path(argv[text || content || editText ? 2 : 1]));
    const auto instance = reinterpret_cast<HINSTANCE>(original.Function<void*>(0));
#endif
    const auto output = std::filesystem::absolute(std::filesystem::u8path(argv[argc - 1]));
    std::filesystem::create_directories(output);
    mikudancestudio::qt::InstallEditorStyle(app);
    INITCOMMONCONTROLSEX common{sizeof common, ICC_BAR_CLASSES};
    Check(InitCommonControlsEx(&common), "initialize original trackbar class");
    const auto geometryCases = visible ? 0 : TrackbarGeometryCorpus();
    const auto textGeometryCases = text ? ButtonTextGeometryCorpus(output) : 0;
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
    unsigned mismatches = 0, cases = 0, templates = 0, clientCases = 0, clientMismatches = 0;
    QJsonArray comboLayouts;
    QJsonArray editLayouts;
    QJsonArray editCursorLayouts;
    unsigned editCursorMismatches = 0;
    QJsonArray editReference;
    QJsonArray multilineReturn;
    unsigned editDeleteCases = 0;
    unsigned externalForegroundChanges = 0;
    if (editText) {
        QFile file(QString::fromUtf8(MDS_EDIT_LAYOUT_REFERENCE));
        Check(file.open(QIODevice::ReadOnly), "open independent original EDIT layouts");
        editReference = QJsonDocument::fromJson(file.readAll()).object()["controls"].toArray();
        Check(editReference.size() == 230, "complete independent original EDIT layouts");
    }
    for (std::size_t templateIndex = 0; templateIndex < sizeof descriptions / sizeof *descriptions;
         ++templateIndex) {
        const auto* description = descriptions[templateIndex];
        ++templates;
        const char* code = templateIndex % 2 ? "ja" : "en";
        mikudancestudio::qt::Language language;
        Check(language.Set(code), "load UI language");
        ResourceDialog dialog(*description, language);
        dialog.winId(); // Create only our own never-shown native parent.
        // Both paths use actual dialog resources. The public executable links
        // the byte-verified RC source; the optional oracle maps the original
        // executable. Do not reconstruct CreateWindowEx styles here: the dialog
        // manager also changes client edges, default buttons and fonts.
        const auto resource = FindResourceW(instance, MAKEINTRESOURCEW(description->id), RT_DIALOG);
        Check(resource != nullptr, "original dialog resource");
        const auto* bytes =
            static_cast<const unsigned char*>(LockResource(LoadResource(instance, resource)));
        Check(bytes != nullptr, "original dialog bytes");
        DWORD flags{};
        std::memcpy(&flags, bytes + (bytes[2] == 255 && bytes[3] == 255 ? 12 : 0), sizeof flags);
        Check(!(flags & WS_VISIBLE), "original resource must never show a window");
        DialogCommands commands;
        const auto native =
            CreateDialogParamW(instance, MAKEINTRESOURCEW(dialog.Template().id), nullptr,
                               HiddenDialog, reinterpret_cast<LPARAM>(&commands));
        Check(native && !IsWindowVisible(native), "create hidden original resource dialog");
        if (!visible && !text && !content && !editText) {
            for (std::size_t index = 0; index < description->count; ++index) {
                const auto& entry = description->controls[index];
                if (entry.kind == NumericControlKind::Edit) {
                    const auto control = GetDlgItem(native, entry.id);
                    auto* field = qobject_cast<QLineEdit*>(
                        dialog.findChild<QWidget*>(QString("numeric.control.%1").arg(index)));
                    Check(control && field && !IsWindowVisible(control),
                          "hidden EDIT deletion controls");
                    const bool enabled = field->isEnabled();
                    EnableWindow(control, TRUE);
                    field->setEnabled(true);
                    const auto foreground = GetForegroundWindow(), focus = GetFocus();
                    ThreadKeyboardState keyboard(true);
                    // Read-only key rejection can invoke USER32 MessageBeep.
                    // Keep that check in the Qt offscreen regression instead.
                    SendMessageW(control, EM_SETREADONLY, FALSE, 0);
                    field->setReadOnly(false);
                    for (const auto range :
                         {std::pair{0, 0}, {1, 1}, {3, 3}, {0, 2}, {2, 0}, {1, 3}, {3, 1}}) {
                        // Fit the actual native no-autoscroll field, as
                        // MMD's numeric EM_REPLACESEL prefill does. Native
                        // overflowing WM_SETTEXT has distinct virtual rows.
                        constexpr auto before = L"1.5";
                        SetWindowTextW(control, before);
                        SendMessageW(control, EM_SETSEL, range.first, range.second);
                        field->setText(QString::fromWCharArray(before));
                        field->setSelection(range.first, range.second - range.first);
                        // Native/Qt setup can pump this thread's queue. Bind
                        // the intended modifier immediately to the observed
                        // native command, independently of physical user keys.
                        ThreadKeyboardState::SetControl(true);
                        const auto controlBefore = GetKeyState(VK_CONTROL);
                        SendMessageW(control, WM_KEYDOWN, VK_DELETE, 1);
                        const auto controlAfter = GetKeyState(VK_CONTROL);
                        SendMessageW(control, WM_KEYUP, VK_DELETE, LPARAM(0xc0000001u));
                        QKeyEvent key(QEvent::KeyPress, Qt::Key_Delete, Qt::ControlModifier);
                        QCoreApplication::sendEvent(field, &key);
                        wchar_t value[32]{};
                        GetWindowTextW(control, value, 32);
                        DWORD first = 0, last = 0;
                        SendMessageW(control, EM_GETSEL, reinterpret_cast<WPARAM>(&first),
                                     reinterpret_cast<LPARAM>(&last));
                        const auto qtFirst = field->hasSelectedText() ? field->selectionStart()
                                                                      : field->cursorPosition();
                        if (field->text() != QString::fromWCharArray(value) ||
                            qtFirst != int(first) ||
                            qtFirst + field->selectedText().size() != int(last))
                            std::fprintf(
                                stderr,
                                "EDIT delete template=%d control=%d range=%d,%d native=%lu,%lu:'%ls' Qt=%d,%d:'%s' ctrl=%x,%x\n",
                                description->id, entry.id, range.first, range.second, first, last,
                                value, qtFirst, int(qtFirst + field->selectedText().size()),
                                field->text().toUtf8().constData(), unsigned(controlBefore),
                                unsigned(controlAfter));
                        Check(field->text() == QString::fromWCharArray(value) &&
                                  qtFirst == int(first) &&
                                  qtFirst + field->selectedText().size() == int(last),
                              "native EDIT Ctrl+Delete text and selection");
                        if (std::wstring(value) != before) {
                            Check(SendMessageW(control, EM_UNDO, 0, 0), "native delete undo");
                            field->undo();
                            GetWindowTextW(control, value, 32);
                            Check(field->text() == QString::fromWCharArray(value) &&
                                      std::wstring(value) == before,
                                  "native EDIT delete undo payload");
                            Check(SendMessageW(control, EM_UNDO, 0, 0), "native delete redo");
                            field->redo();
                            GetWindowTextW(control, value, 32);
                            Check(field->text() == QString::fromWCharArray(value),
                                  "native EDIT delete redo payload");
                        }
                        ++editDeleteCases;
                    }
                    externalForegroundChanges += CheckHiddenInput(foreground, focus);
                    SendMessageW(control, EM_SETREADONLY, (entry.style & ES_READONLY) != 0, 0);
                    field->setReadOnly((entry.style & ES_READONLY) != 0);
                    EnableWindow(control, enabled);
                    field->setEnabled(enabled);
                }
                if (entry.kind != NumericControlKind::Multiline)
                    continue;
                const auto control = GetDlgItem(native, entry.id);
                Check(control && !IsWindowVisible(control), "hidden original multiline edit");
                for (const bool ctrl : {false, true}) {
                    // This changes only the caller thread's input table. No
                    // AttachThreadInput, hardware input, focus or activation.
                    ThreadKeyboardState state(ctrl);
                    SetWindowTextW(control, L"first");
                    SendMessageW(control, EM_SETSEL, 5, 5);
                    MSG message{};
                    message.hwnd = control;
                    message.message = WM_KEYDOWN;
                    message.wParam = VK_RETURN;
                    message.lParam = 1;
                    const auto code = SendMessageW(control, WM_GETDLGCODE, message.wParam,
                                                   reinterpret_cast<LPARAM>(&message));
                    commands.last = 0;
                    const auto foreground = GetForegroundWindow();
                    const auto focus = GetFocus();
                    const bool consumed = IsDialogMessageW(native, &message);
                    unsigned pendingCount = 0;
                    MSG pending{};
                    while (PeekMessageW(&pending, native, 0, 0, PM_REMOVE)) {
                        Check(++pendingCount < 128, "bounded hidden dialog command queue");
                        if (!IsDialogMessageW(native, &pending)) {
                            TranslateMessage(&pending);
                            DispatchMessageW(&pending);
                        }
                    }
                    externalForegroundChanges += CheckHiddenInput(foreground, focus);
                    const bool inserts = ctrl || (entry.style & ES_WANTRETURN);
                    wchar_t value[32]{};
                    GetWindowTextW(control, value, 32);
                    multilineReturn.append(QJsonObject{{"template", description->id},
                                                       {"control", entry.id},
                                                       {"ctrl", ctrl},
                                                       {"dialogCode", int(code)},
                                                       {"consumedByDialog", consumed},
                                                       {"command", int(commands.last)},
                                                       {"text", QString::fromWCharArray(value)}});
                    Check(bool(code & DLGC_WANTALLKEYS) && consumed &&
                              commands.last == (inserts ? 0u : IDOK) &&
                              std::wstring(value) == (inserts ? L"first\r\n" : L"first"),
                          "original multiline Enter/Ctrl+Enter default command");
                }
            }
        }
        // Compare states without keyboard-focus cues. DefDlgProc can retain
        // its initial button focus internally even while never shown; Qt's
        // hidden controls have no active focus. Focused text/glyphs are separate.
        SendMessageW(native, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS | UISF_HIDEACCEL),
                     0);
        if (visible) {
            // Explicit runner-only path; never enabled by a local CTest label.
            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
            if (!focused) {
                dialog.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
                for (auto* child : dialog.findChildren<QWidget*>())
                    child->setFocusPolicy(Qt::NoFocus);
            }
            dialog.show();
            ShowWindow(native, SW_SHOWNOACTIVATE);
            dialog.move(32, 32);
            SetWindowPos(native, nullptr, 32, 32, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
            QCoreApplication::processEvents();
        }
        bool savedEditExample = false;
        for (std::size_t index = 0; index < description->count; ++index) {
            const auto& entry = description->controls[index];
            if (editText && entry.kind != NumericControlKind::Edit)
                continue;
            const bool editExample = editText && !savedEditExample;
            savedEditExample |= editText;
            if (focused && !editFocus && entry.kind != NumericControlKind::Listbox &&
                !(comboFocus && entry.kind == NumericControlKind::Combobox))
                continue;
            // WM_PRINTCLIENT captures actual EDIT text/background without
            // inventing a replacement for its unavailable hidden nonclient
            // paint. Complete borders require isolated visible qualification.
            const bool editClientOnly =
                !visible && !replayFocus && entry.kind == NumericControlKind::Edit;
            const bool other = entry.kind == NumericControlKind::Combobox ||
                               entry.kind == NumericControlKind::Listbox ||
                               entry.kind == NumericControlKind::Slider;
            if (content && entry.kind != NumericControlKind::Listbox &&
                !(visible && entry.kind == NumericControlKind::Combobox))
                continue;
            if (text && entry.kind != NumericControlKind::Button &&
                entry.kind != NumericControlKind::Checkbox &&
                entry.kind != NumericControlKind::Radio)
                continue;
            if (visible ? (!content && entry.kind != NumericControlKind::Edit)
                        : (entry.kind != NumericControlKind::Edit &&
                           entry.kind != NumericControlKind::Button &&
                           entry.kind != NumericControlKind::Checkbox &&
                           entry.kind != NumericControlKind::Radio && !other))
                continue;
            const int id = entry.id;
            if (!entry.width || !entry.height)
                continue;
            auto* widget = dialog.findChild<QWidget*>(QString("numeric.control.%1").arg(index));
            Check(widget != nullptr, "matching Qt control");
            // Qualify steady states, independently of the host's animation
            // accessibility preference and the time between memory captures.
            widget->setProperty("_q_no_animation", true);
            const auto control = GetDlgItem(native, id);
            Check(control && (visible || !IsWindowVisible(control)), "native reference control");
            if (!text)
                SetWindowTextW(control, L""); // Isolate theme paint from font qualification.
            auto* button = qobject_cast<QAbstractButton*>(widget);
            if (button) {
                if (text) {
                    std::wstring caption(GetWindowTextLengthW(control) + 1, L'\0');
                    caption.resize(GetWindowTextW(control, caption.data(), int(caption.size())));
                    if (QString::fromStdWString(caption) != button->text())
                        std::fprintf(stderr, "caption mismatch template=%d control=%d\n",
                                     description->id, id);
                    Check(QString::fromStdWString(caption) == button->text(),
                          "actual original caption");
                    SetWindowTextW(control, button->text().toStdWString().c_str());
                    SendMessageW(control, WM_CHANGEUISTATE,
                                 MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
                } else
                    button->setText({});
            }
            if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
                if (editText) {
                    // Qualify inactive unscrolled contents here. Narrow
                    // ES_AUTOHSCROLL fields use a fitting value; native scroll
                    // offsets/selection remain a separate focus qualification.
                    const auto caption = QString(entry.style & 0x80u ? "1.5" : "-12.5");
                    edit->setText(caption);
                    edit->setCursorPosition(0);
                    SetWindowTextW(control, caption.toStdWString().c_str());
                    SendMessageW(control, EM_SETSEL, 0, 0);
                    RECT formatting{}, client{}, outer{};
                    SendMessageW(control, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&formatting));
                    GetClientRect(control, &client);
                    GetWindowRect(control, &outer);
                    const auto margins = SendMessageW(control, EM_GETMARGINS, 0, 0);
                    POINT origin{0, 0};
                    MapWindowPoints(control, nullptr, &origin, 1);
                    const QJsonObject layout{
                        {"template", description->id},
                        {"control", id},
                        {"style", double(DWORD(GetWindowLongPtrW(control, GWL_STYLE)))},
                        {"extendedStyle", double(DWORD(GetWindowLongPtrW(control, GWL_EXSTYLE)))},
                        {"window",
                         QJsonArray{int(outer.right - outer.left), int(outer.bottom - outer.top)}},
                        {"client", QJsonArray{int(client.right), int(client.bottom)}},
                        {"clientOffset",
                         QJsonArray{int(origin.x - outer.left), int(origin.y - outer.top)}},
                        {"format", QJsonArray{int(formatting.left), int(formatting.top),
                                              int(formatting.right), int(formatting.bottom)}},
                        {"margins", QJsonArray{LOWORD(margins), HIWORD(margins)}}};
                    Check(widget->size() ==
                              QSize(outer.right - outer.left, outer.bottom - outer.top),
                          "original EDIT outer geometry");
                    QJsonObject reference;
                    for (const auto& item : editReference) {
                        const auto value = item.toObject();
                        if (value["template"] == description->id && value["control"] == id)
                            reference = value;
                    }
                    if (layout != reference) {
                        std::fprintf(
                            stderr, "EDIT layout actual=%s expected=%s\n",
                            QJsonDocument(layout).toJson(QJsonDocument::Compact).constData(),
                            QJsonDocument(reference).toJson(QJsonDocument::Compact).constData());
                    }
                    Check(layout == reference, "independent original EDIT styles/client/margins");
                    editLayouts.append(layout);
                    QImage layoutPaint(edit->size(), QImage::Format_RGB32);
                    edit->render(&layoutPaint);
                    const auto nativeCursor = SendMessageW(control, EM_POSFROMCHAR, 0, 0);
                    Check(nativeCursor != -1, "native EDIT character position");
                    const QPoint expectedCursor(
                        int(origin.x - outer.left) + short(LOWORD(nativeCursor)),
                        int(origin.y - outer.top) + short(HIWORD(nativeCursor)));
                    const auto cursor = edit->inputMethodQuery(Qt::ImCursorRectangle).toRect();
                    // Shown USER32 captures place the actual glyph/selection/
                    // caret one pixel below EM_POSFROMCHAR. The frontend now
                    // returns that caret, without Qt's five-pixel repaint halo.
                    editCursorMismatches +=
                        cursor.topLeft() != expectedCursor + QPoint(0, 1) || cursor.width() != 1;
                    editCursorLayouts.append(QJsonObject{
                        {"template", description->id},
                        {"control", id},
                        {"nativeCharacter", QJsonArray{expectedCursor.x(), expectedCursor.y()}},
                        {"nativeCaret", QJsonArray{expectedCursor.x(), expectedCursor.y() + 1}},
                        {"qt",
                         QJsonArray{cursor.x(), cursor.y(), cursor.width(), cursor.height()}}});
                } else
                    edit->clear();
            }
            if (auto* combo = qobject_cast<QComboBox*>(widget)) {
                COMBOBOXINFO geometry{sizeof(COMBOBOXINFO)};
                Check(GetComboBoxInfo(control, &geometry), "native combo geometry");
                QStyleOptionComboBox option;
                option.initFrom(combo);
                option.editable = combo->isEditable();
                const auto edit = combo->style()->subControlRect(
                    QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo);
                const auto& item = geometry.rcItem;
                Check(edit == QRect(item.left, item.top, item.right - item.left,
                                    item.bottom - item.top),
                      "native combo item rectangle");
                comboLayouts.append(QJsonObject{
                    {"template", description->id},
                    {"control", id},
                    {"native",
                     QJsonArray{int(item.left), int(item.top), int(item.right), int(item.bottom)}},
                    {"qt",
                     QJsonArray{edit.left(), edit.top(), edit.right() + 1, edit.bottom() + 1}}});
                combo->clear();
                SendMessageW(control, CB_RESETCONTENT, 0, 0);
                if (content) {
                    const auto caption = templateIndex % 2
                                             ? QStringLiteral("\u65e5\u672c\u8a9e ABC")
                                             : QString("ABC 012345");
                    combo->addItem(caption);
                    SendMessageW(control, CB_ADDSTRING, 0,
                                 reinterpret_cast<LPARAM>(caption.toStdWString().c_str()));
                    SendMessageW(control, CB_SETCURSEL, 0, 0);
                }
            }
            if (auto* list = qobject_cast<QListWidget*>(widget)) {
                list->clear();
                SendMessageW(control, LB_RESETCONTENT, 0, 0);
                if (content) {
                    for (const auto caption :
                         {QString("ABC 012345"), QStringLiteral("\u65e5\u672c\u8a9e ABC")}) {
                        list->addItem(caption);
                        SendMessageW(control, LB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(caption.toStdWString().c_str()));
                    }
                    list->setCurrentRow(-1);
                }
            }
            auto* slider = qobject_cast<QSlider*>(widget);
            if (slider) {
                slider->setRange(0, 100);
                slider->setTickInterval(1000);
                SendMessageW(control, TBM_SETRANGE, FALSE, MAKELONG(0, 100));
                SendMessageW(control, TBM_SETTICFREQ, 1000, 0);
            }
            for (unsigned variant = 0; variant < (editText && !editHover ? 5u : 1u); ++variant) {
                if (editText) {
                    const QString captions[]{
                        entry.style & ES_AUTOHSCROLL ? "1.5" : "-12.5", QStringLiteral("\u65e5"),
                        QStringLiteral("\u4e2d"), QStringLiteral("A\u65e5"),
                        entry.style & ES_AUTOHSCROLL && !editFocus ? "1.5"
                                                                   : "12345678901234567890"};
                    auto* edit = qobject_cast<QLineEdit*>(widget);
                    edit->setText(captions[variant]);
                    edit->setCursorPosition(0);
                    SetWindowTextW(control, captions[variant].toStdWString().c_str());
                    SendMessageW(control, EM_SETSEL, 0, 0);
                }
                for (unsigned state = 0; state < (editHover                                   ? 1u
                                                  : editFocus                                 ? 3u
                                                  : editText                                  ? 4u
                                                  : focused                                   ? 1u
                                                  : slider                                    ? 8u
                                                  : entry.kind == NumericControlKind::Listbox ? 4u
                                                  : !button                                   ? 2u
                                                  : button->isCheckable()                     ? 6u
                                                                                              : 3u);
                     ++state) {
                    const bool enabled =
                        editHover || editFocus || (editText ? state % 2 == 0 : state % 3 != 1);
                    const bool pressed = state % 3 == 2, checked = state >= 3;
                    EnableWindow(control, enabled);
                    widget->setEnabled(enabled);
                    if (editText) {
                        auto* edit = qobject_cast<QLineEdit*>(widget);
                        if (editFocus && state == 1)
                            edit->setSelection(0, std::min(1, int(edit->text().size())));
                        else if (state >= 2)
                            edit->selectAll();
                        else
                            edit->setCursorPosition(0);
                        SendMessageW(control, EM_SETSEL, 0,
                                     editFocus && state == 1 ? 1
                                     : state >= 2            ? -1
                                                             : 0);
                    }
                    if (slider) {
                        constexpr int positions[]{0, 37, 50, 100};
                        slider->setEnabled(state % 2 == 0);
                        EnableWindow(control, state % 2 == 0);
                        slider->setValue(positions[state / 2]);
                        SendMessageW(control, TBM_SETPOS, TRUE, positions[state / 2]);
                        QStyleOptionSlider option;
                        option.initFrom(slider);
                        option.orientation = Qt::Horizontal;
                        option.minimum = 0;
                        option.maximum = 100;
                        option.sliderPosition = slider->value();
                        option.tickPosition = slider->tickPosition();
                        for (const auto part : {QStyle::SC_SliderGroove, QStyle::SC_SliderHandle}) {
                            RECT r{};
                            SendMessageW(control,
                                         part == QStyle::SC_SliderGroove ? TBM_GETCHANNELRECT
                                                                         : TBM_GETTHUMBRECT,
                                         0, reinterpret_cast<LPARAM>(&r));
                            Check(slider->style()->subControlRect(QStyle::CC_Slider, &option, part,
                                                                  slider) ==
                                      QRect(r.left, r.top, r.right - r.left, r.bottom - r.top),
                                  "native trackbar channel/thumb geometry");
                        }
                    }
                    if (auto* list = qobject_cast<QListWidget*>(widget)) {
                        if (state == 2) {
                            if (!content)
                                list->addItem(QString());
                            list->setCurrentRow(0);
                            if (!content)
                                SendMessageW(control, LB_ADDSTRING, 0,
                                             reinterpret_cast<LPARAM>(L""));
                            SendMessageW(control, LB_SETCURSEL, 0, 0);
                        }
                        // List states 2/3 retain the same selection while toggling enabled.
                        const bool listEnabled = state % 2 == 0;
                        list->setEnabled(listEnabled);
                        EnableWindow(control, listEnabled);
                    }
                    if (button) {
                        SendMessageW(control, BM_SETSTATE, pressed, 0);
                        SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED,
                                     0);
                        button->setDown(pressed);
                        button->setChecked(checked);
                    }
                    if (visible) {
                        if (editHover) {
                            Check(SetWindowPos(native, HWND_TOP, 0, 0, 0, 0,
                                               SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE),
                                  "raise isolated native hover reference");
                            Check(SetWindowPos(control, HWND_TOP, 0, 0, 0, 0,
                                               SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE),
                                  "isolate overlapping original hover control");
                            RECT bounds{};
                            Check(GetWindowRect(control, &bounds), "native hover bounds");
                            const POINT point{(bounds.left + bounds.right) / 2,
                                              (bounds.top + bounds.bottom) / 2};
                            Check(SetCursorPos(point.x, point.y) &&
                                      WindowFromPoint(point) == control,
                                  "native hover cursor belongs to reference control");
                            QElapsedTimer dwell;
                            dwell.start();
                            while (dwell.elapsed() < 200) {
                                QCoreApplication::processEvents();
                                QThread::msleep(1);
                            }
                        }
                        if (focused) {
                            auto* list = qobject_cast<QListWidget*>(widget);
                            if (list) {
                                list->setCurrentRow(0);
                                SendMessageW(control, LB_SETCURSEL, 0, 0);
                            } else if (!editFocus)
                                Check(qobject_cast<QComboBox*>(widget) != nullptr,
                                      "focus qualification combo");
                            SetActiveWindow(native);
                            SetFocus(control);
                            Check(GetFocus() == control,
                                  "native control owns actual keyboard focus");
                            SendMessageW(control, WM_UPDATEUISTATE,
                                         MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
                        } else
                            // DefDlgProc restores its first EDIT's focus on
                            // show, even with SW_SHOWNOACTIVATE. This profile
                            // compares inactive controls, including no caret.
                            SetFocus(nullptr);
                        RedrawWindow(native, nullptr, nullptr,
                                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN |
                                         RDW_UPDATENOW);
                        UpdateWindow(control);
                        QCoreApplication::processEvents();
                        if (focused)
                            Check(GetFocus() == control,
                                  "native control retains focus before capture");
                        else
                            Check(GetFocus() == nullptr,
                                  "inactive native control has no keyboard focus");
                    }
                    const auto expected = NativeImage(control, visible, editClientOnly);
                    if (editHover) {
                        widget->raise();
                        const auto window = reinterpret_cast<HWND>(dialog.winId());
                        Check(SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                                           SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE),
                              "raise isolated Qt hover reference");
                        const auto point = widget->mapToGlobal(widget->rect().center());
                        Check(SetCursorPos(point.x(), point.y()), "move isolated Qt hover cursor");
                        QElapsedTimer dwell;
                        dwell.start();
                        while (dwell.elapsed() < 200) {
                            QCoreApplication::processEvents();
                            QThread::msleep(1);
                        }
                        Check(widget->underMouse(), "Qt field owns actual hover state");
                    }
                    if (focused) {
                        dialog.activateWindow();
                        SetActiveWindow(reinterpret_cast<HWND>(dialog.winId()));
                        QApplication::setActiveWindow(&dialog);
                        // Win32 SetFocus does not run the dialog manager's Tab
                        // selection policy. QLineEdit's TabFocusReason selects
                        // all text, invalidating the requested state-0 cursor.
                        widget->setFocus(editFocus ? Qt::OtherFocusReason : Qt::TabFocusReason);
                        QCoreApplication::processEvents();
                        Check(widget->hasFocus(), "Qt control owns actual keyboard focus");
                    }
                    if (!editClientOnly)
                        widget->resize(expected.size());
                    QImage actual(widget->size(), QImage::Format_RGB32);
                    actual.fill(app.palette().window().color());
                    widget->render(&actual);
                    if (replayFocus) {
                        auto* edit = qobject_cast<QLineEdit*>(widget);
                        QPainter painter(&actual);
                        const mikudancestudio::qt::WindowsEditLayout layout{
                            edit->cursorPosition(),
                            edit->inputMethodQuery(Qt::ImCursorRectangle).toRect().x(),
                            edit->selectionStart(),
                            int(edit->selectedText().size()),
                            true,
                            true};
                        Check(mikudancestudio::qt::DrawWindowsEditText(
                                  painter, edit->rect(), edit->font(), edit->text(),
                                  code == std::string("en"), edit->hasFrame(), true,
                                  edit->isReadOnly(), entry.style & ES_AUTOHSCROLL, &layout),
                              "replay native EDIT client paint without focus/exposure");
                    }
                    if (editClientOnly) {
                        RECT client{};
                        Check(GetClientRect(control, &client), "edit client geometry");
                        POINT origin{0, 0};
                        MapWindowPoints(control, nullptr, &origin, 1);
                        RECT outer{};
                        Check(GetWindowRect(control, &outer), "edit outer geometry");
                        actual = actual.copy(origin.x - outer.left, origin.y - outer.top,
                                             client.right, client.bottom);
                    }
                    auto prefix =
                        QString("%1-%2-%3-%4").arg(description->id).arg(code).arg(id).arg(state);
                    if (editText)
                        prefix += QString("-text%1").arg(variant);
                    auto reference = expected;
                    if (replayFocus) {
                        reference =
                            QImage(QString::fromLocal8Bit(argv[2]) + "/" + prefix + "-native.png");
                        Check(!reference.isNull() && reference.size() == actual.size(),
                              "captured native EDIT replay input");
                        // Different OS theme images are separate from this
                        // replay of client glyph/selection/caret coordinates.
                        reference = reference.copy(reference.rect().adjusted(2, 2, -2, -2));
                        actual = actual.copy(actual.rect().adjusted(2, 2, -2, -2));
                    }
                    const auto difference = Difference(actual, reference);
                    if (!editText || editExample || difference) {
                        Check(reference.save(QString::fromStdWString(
                                  (output / (prefix.toStdWString() + L"-native.png")).wstring())),
                              "save original control raster");
                        Check(actual.save(QString::fromStdWString(
                                  (output / (prefix.toStdWString() + L"-qt.png")).wstring())),
                              "save Qt control raster");
                    }
                    std::printf("%d %s control=%d state=%u size=%dx%d different=%u\n",
                                description->id, code, id, state, actual.width(), actual.height(),
                                difference);
                    if (editClientOnly && !editText) {
                        clientMismatches += difference != 0;
                        ++clientCases;
                    } else {
                        mismatches += difference != 0;
                        ++cases;
                    }
                }
            }
        }
        Check(visible || (!IsWindowVisible(native) && !dialog.isVisible()),
              "control qualification exposed a window");
        DestroyWindow(native);
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
        {"editClientCases", int(clientCases)},
        {"editClientMismatches", int(clientMismatches)},
        {"trackbarGeometryCases", int(geometryCases)},
        {"textGeometryCases", int(textGeometryCases)},
        {"textGeometryKeyboardFocus", text},
        {"comparison", visible && !content
                           ? "exact RGB, EDIT normal/disabled including nonclient"
                           : "exact RGB, normal/disabled/pressed, checked/unchecked"},
        {"keyboardFocusCues", focused},
        {"textGlyphs", text || content || editText},
        {"editClientText", editText},
        {"editTextVariants", editHover  ? 1
                             : editText ? 5
                                        : 0},
        {"inactiveEditSelection", editText && !editFocus},
        {"activeEditSelectionAndScroll", editFocus},
        {"actualEditHover", editHover},
        {"clientReplayWithoutExposure", replayFocus},
        {"savedEditImages", "all mismatches; one passing control per template/content/state"},
        {"listContent", content},
        {"comboLayouts", comboLayouts},
        {"editLayouts", editLayouts},
        {"editCursorLayouts", editCursorLayouts},
        {"editCursorMismatches", int(editCursorMismatches)},
        {"multilineReturn", multilineReturn},
        {"editDeleteCases", int(editDeleteCases)},
        {"externalForegroundChanges", int(externalForegroundChanges)},
        {"windowsShown", visible}};
    report.insert("capture",
                  "own dialog PrintWindow; overlapping siblings excluded; explicit focus state");
#ifdef MDS_SYSTEM_CONTROL_REFERENCE
    report.insert("reference", "compiled byte-verified original RT_DIALOG resources");
#else
    report.insert("reference", "actual hash-verified original dialog resources");
#endif
    QFile file(QString::fromStdWString((output / "report.json").wstring()));
    Check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(report).toJson()) > 0,
          "write native control qualification");
    return mismatches || clientMismatches || editCursorMismatches ? 1 : 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
