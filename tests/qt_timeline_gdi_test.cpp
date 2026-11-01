#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "timeline.hpp"
#include <QApplication>
#include <QPainter>
#include <iostream>
using namespace mikudancestudio;
// Independent GDI raster oracle: original ui_init / PanelPaint rectangle,
// grid and icon blit commands. No HWND, desktop DC, screenshot or input.
struct Raster {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    void* bits = nullptr;
    Raster(int w, int h) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        old = SelectObject(dc, bitmap);
    }
    ~Raster() {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
    void Rect(COLORREF color, int l, int t, int r, int b) {
        auto pen = CreatePen(PS_SOLID, 1, color);
        auto brush = CreateSolidBrush(color);
        auto op = SelectObject(dc, pen), ob = SelectObject(dc, brush);
        Rectangle(dc, l, t, r, b);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);
        DeleteObject(brush);
    }
    void Line(COLORREF color, int x, int y, int ex, int ey) {
        auto pen = CreatePen(PS_SOLID, 1, color);
        auto op = SelectObject(dc, pen);
        MoveToEx(dc, x, y, nullptr);
        LineTo(dc, ex, ey);
        SelectObject(dc, op);
        DeleteObject(pen);
    }
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 2)
        return 2;
    const auto assets = std::filesystem::absolute(argv[1]);
    auto sheet =
        static_cast<HBITMAP>(LoadImageW(nullptr, (assets / "sidebar_icon_sheet.bmp").c_str(),
                                        IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
    if (!sheet)
        return 1;
    auto icons = CreateCompatibleDC(nullptr);
    auto previous = SelectObject(icons, sheet);
    editor::EditorSession session;
    qt::Language language;
    qt::Timeline timeline(session, language, assets, nullptr);
    for (int width : {137, 251, 399})
        for (int scroll : {0, 3, 11})
            for (bool selected : {false, true}) {
                constexpr int height = 181;
                timeline.resize(width, height);
                timeline.Refresh();
                timeline.SetFrameScroll(scroll);
                if (selected)
                    timeline.SelectAll();
                else
                    session.Selected().keys.clear();
                QImage actual(width, height, QImage::Format_ARGB32);
                actual.fill(Qt::black);
                QPainter painter(&actual);
                timeline.render(&painter);
                painter.end();
                Raster expected(width, height);
                expected.Rect(1250067, 0, 0, width, height);
                expected.Rect(5401478, 90, 1, width, 15);
                for (int y = 15; y < height; y += 28)
                    expected.Rect(3946295, 91, y, width, y + 15);
                for (int y = 15; y < height; y += 14)
                    expected.Line(868768, 91, y, width, y);
                for (int x = 100, i = 0; x < width; x += 13, ++i) {
                    const auto f = scroll + i;
                    expected.Line(f == 0 ? 9895830 : f % 5 ? 4934475 : 16750230, x, 16, x, height);
                }
                if (!scroll)
                    for (int y : {17, 31, 45, 59}) {
                        BitBlt(expected.dc, 95, y, 11, 11, icons, 44, 0, SRCAND);
                        BitBlt(expected.dc, 95, y, 11, 11, icons, selected ? 22 : 0, 0, SRCPAINT);
                    }
                GdiFlush();
                const auto* pixels = static_cast<const unsigned*>(expected.bits);
                for (int y = 16; y < height; ++y)
                    for (int x = 91; x < width; ++x)
                        if ((actual.pixel(x, y) & 0xffffffu) !=
                            (pixels[y * width + x] & 0xffffffu)) {
                            std::cerr << "timeline raster mismatch w=" << width
                                      << " scroll=" << scroll << " selected=" << selected
                                      << " x=" << x << " y=" << y << " Qt=" << std::hex
                                      << actual.pixel(x, y) << " GDI=" << pixels[y * width + x]
                                      << '\n';
                            return 1;
                        }
                if (timeline.isVisible())
                    return 1;
            }
    SelectObject(icons, previous);
    DeleteDC(icons);
    DeleteObject(sheet);
    std::cout
        << "Timeline GDI oracle: exact grid, palette and selected/unselected atlas pixels, 18 cases\n";
}
