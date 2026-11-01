#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "timeline.hpp"
#include "interpolation.hpp"
#include "interpolation_raster_reference.hpp"
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
    void Line(COLORREF color, int x, int y, int ex, int ey, int width = 1) {
        auto pen = CreatePen(PS_SOLID, width, color);
        auto op = SelectObject(dc, pen);
        MoveToEx(dc, x, y, nullptr);
        LineTo(dc, ex, ey);
        SelectObject(dc, op);
        DeleteObject(pen);
    }
};
bool Curves() {
    // Independent translation of sub_7FF7CB483530 / sub_7FF7CB483790.
    std::vector<editor::InterpolationCurve> curves(kCurveInputs.begin(), kCurveInputs.end());
    std::uint32_t seed = 3187;
    for (unsigned i = 0; i < 128; ++i) {
        editor::InterpolationCurve curve;
        for (auto& c : curve) {
            seed = seed * 1664525u + 1013904223u;
            c = int(seed >> 24) - (i < 64 ? 128 : 0);
            if (i >= 64)
                c &= 127;
        }
        curves.push_back(curve);
    }
    unsigned caseIndex = 0;
    for (const auto& curve : curves)
        for (bool highlight : {false, true}) {
            editor::InterpolationSelection selection;
            selection.curves = {curve};
            selection.uniform = highlight;
            const auto actual = qt::InterpolationImage(selection);
            Raster expected(128, 128);
            expected.Rect(0xffffff, 0, 0, 128, 128);
            int oldX = 0, oldY = 126;
            const int x1 = curve[0], y1 = curve[1], x2 = curve[2], y2 = curve[3];
            for (int step = 1; step < 100; ++step) {
                const float t = float(step) / 100.f;
                const float ax = float(x1) * t, ay = float(y1) * t;
                const float bx = float(x1) + float(x2 - x1) * t;
                const float by = float(y1) + float(y2 - y1) * t;
                const float cx = float(x2) + float(128 - x2) * t;
                const float cy = float(y2) + float(128 - y2) * t;
                const float dx = ax + (bx - ax) * t, dy = ay + (by - ay) * t;
                const float ex = bx + (cx - bx) * t, ey = by + (cy - by) * t;
                const int x = int(dx + (ex - dx) * t), y = 126 - int(dy + (ey - dy) * t);
                expected.Line(0xff0000, oldX, oldY, x, y);
                oldX = x;
                oldY = y;
            }
            if (highlight) {
                expected.Line(0xff, x1 - 3, 124 - y1, x1 + 3, 130 - y1, 2);
                expected.Line(0xff, x1 + 3, 124 - y1, x1 - 3, 130 - y1, 2);
                expected.Line(0xff, x2 - 3, 124 - y2, x2 + 3, 130 - y2, 2);
                expected.Line(0xff, x2 + 3, 124 - y2, x2 - 3, 130 - y2, 2);
                expected.Line(0, 0, 127, x1, 127 - y1);
                expected.Line(0, 127, 0, x2, 127 - y2);
            }
            GdiFlush();
            const auto* pixels = static_cast<const unsigned*>(expected.bits);
            unsigned differences = 0;
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 128; ++x)
                    if ((actual.pixel(x, y) & 0xffffffu) != (pixels[y * 128 + x] & 0xffffffu)) {
                        if (differences++ < 4)
                            std::cerr << "curve pixel x=" << x << " y=" << y << " Qt=" << std::hex
                                      << actual.pixel(x, y) << " GDI=" << pixels[y * 128 + x]
                                      << std::dec << '\n';
                    }
            if (differences) {
                std::cerr << "curve " << x1 << ',' << y1 << ',' << x2 << ',' << y2
                          << " highlight=" << highlight << " differences=" << differences << '\n';
                return false;
            }
            if (caseIndex++ < 10) {
                std::uint64_t hash = 14695981039346656037ull;
                for (unsigned i = 0; i < 128 * 128; ++i)
                    for (int shift : {16, 8, 0})
                        hash = (hash ^ ((pixels[i] >> shift) & 255)) * 1099511628211ull;
                if (hash != kCurveRasterHashes[caseIndex - 1]) {
                    std::cerr << "Independent GDI curve baseline changed\n";
                    return false;
                }
            }
        }
    return true;
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 2)
        return 2;
    if (!Curves())
        return 1;
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
        << "GDI oracle: exact timeline pixels (18 cases) and interpolation pixels (266 cases)\n";
}
