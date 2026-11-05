#pragma once
#include "numeric_dialog.hpp"
#include "multiline_hit.hpp"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

using namespace mikudancestudio;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
INT_PTR CALLBACK Reference(HWND, UINT, WPARAM, LPARAM) {
    return FALSE;
}
class Dialog final : public qt::TemplateDialog {
public:
    explicit Dialog(const qt::Language& language)
        : TemplateDialog(language.Code() == "en" ? ui::kNumericEnglishNameEn
                                                 : ui::kNumericEnglishNameJa,
                         language, nullptr) {}
    QPlainTextEdit* Field() { return Multiline(668); }
};
QImage Client(HWND window) {
    RECT bounds{}, parentBounds{};
    Check(GetClientRect(window, &bounds), "native multiline client bounds");
    Check(GetWindowRect(GetParent(window), &parentBounds), "native dialog bounds");
    POINT origin{};
    Check(ClientToScreen(window, &origin), "native client origin");
    const int width = parentBounds.right - parentBounds.left;
    const int height = parentBounds.bottom - parentBounds.top;
    auto dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    Check(dc && bitmap && bits, "native multiline memory canvas");
    auto previous = SelectObject(dc, bitmap);
    const RECT canvas{0, 0, width, height};
    FillRect(dc, &canvas, GetSysColorBrush(COLOR_BTNFACE));
    // WM_PRINTCLIENT excludes the real caret. Capture our own shown dialog
    // hierarchy instead, as the existing single-line qualification does.
    Check(PrintWindow(GetParent(window), dc, 0), "actual shown multiline dialog capture");
    GdiFlush();
    auto image = QImage(static_cast<uchar*>(bits), width, height, width * 4, QImage::Format_RGB32)
                     .copy(origin.x - parentBounds.left, origin.y - parentBounds.top, bounds.right,
                           bounds.bottom);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return image;
}
void Key(WORD key, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    Check(SendInput(1, &input, sizeof input) == 1, "isolated keyboard event");
    QTest::qWait(10);
}
void Button(bool down) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    Check(SendInput(1, &input, sizeof input) == 1, "isolated mouse event");
    QTest::qWait(20);
}
class MouseDelivery final {
public:
    MouseDelivery() {
        hook_ = SetWindowsHookExW(WH_GETMESSAGE, Observe, nullptr, GetCurrentThreadId());
        Check(hook_ != nullptr, "observe this isolated test thread's OS mouse delivery");
    }
    ~MouseDelivery() { UnhookWindowsHookEx(hook_); }
    static void Move(HWND root, QPoint point) {
        POINT previous{};
        Check(GetCursorPos(&previous), "isolated current pointer position");
        if (QPoint(previous.x, previous.y) == point) {
            QTest::qWait(5);
            return;
        }
        delivered_ = false;
        expected_ = point;
        root_ = root;
        Check(SetCursorPos(point.x(), point.y()), "isolated pointer movement");
        QElapsedTimer wait;
        wait.start();
        do {
            QTest::qWait(5);
        } while (!delivered_ && wait.elapsed() < 1000);
        Check(delivered_, "actual OS mouse move delivered to the expected test window");
    }

private:
    HHOOK hook_ = nullptr;
    static inline bool delivered_ = false;
    static inline QPoint expected_;
    static inline HWND root_ = nullptr;
    static LRESULT CALLBACK Observe(int code, WPARAM wParam, LPARAM lParam) {
        if (code >= 0 && wParam == PM_REMOVE) {
            const auto* message = reinterpret_cast<const MSG*>(lParam);
            if (message->message == WM_MOUSEMOVE && GetAncestor(message->hwnd, GA_ROOT) == root_ &&
                QPoint(message->pt.x, message->pt.y) == expected_)
                delivered_ = true;
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
};
void NativeFocus(HWND root, HWND control) {
    // Native references are not QWidgets. Clear the previous Qt active-window
    // state before pumping activation, so it cannot restore the previous edit.
    QApplication::setActiveWindow(nullptr);
    SetForegroundWindow(root);
    SetActiveWindow(root);
    SetFocus(control);
    QTest::qWait(30);
    Check(GetFocus() == control && GetForegroundWindow() == root,
          "native reference retains actual focus after pending Qt events");
}
// Uses actual OS mouse capture, not a directly addressed QMouseEvent replay.
void Gesture(HWND root, QPoint start, QPoint end, bool doubleClick, bool shift) {
    // Each case is independent. Avoid the preceding click being interpreted
    // as the first half of a double/triple click by either GUI framework.
    QTest::qWait(int(GetDoubleClickTime()) + 30);
    Check(GetAncestor(GetForegroundWindow(), GA_ROOT) == root,
          "isolated input foreground belongs to the reference under test");
    MouseDelivery::Move(root, start);
    const POINT point{start.x(), start.y()};
    Check(GetAncestor(WindowFromPoint(point), GA_ROOT) == root,
          "isolated input point belongs to the reference under test");
    if (shift)
        Key(VK_SHIFT, true);
    Button(true);
    Check(GetCapture() != nullptr, "actual mouse capture after press");
    if (!doubleClick)
        for (int step = 1; step <= 4; ++step) {
            const auto point = start + (end - start) * step / 4;
            MouseDelivery::Move(root, point);
        }
    Button(false);
    if (doubleClick) {
        Button(true);
        Button(false);
    }
    if (shift)
        Key(VK_SHIFT, false);
    Check(!GetCapture(), "release actual mouse capture");
}
QJsonArray Selection(HWND edit, const QString& text) {
    DWORD first = 0, last = 0;
    SendMessageW(edit, EM_GETSEL, WPARAM(&first), LPARAM(&last));
    const auto native = QString(text).replace("\n", "\r\n");
    return {int(first) - native.left(first).count('\r'), int(last) - native.left(last).count('\r')};
}
}
