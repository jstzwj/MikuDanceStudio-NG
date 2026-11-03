#include "editor_font.hpp"
#include "editor_style.hpp"
#include "editor_window.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include <QApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QMenuBar>
#include <QStyle>
#include <iostream>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
using namespace mikudancestudio;
#ifdef _WIN32
namespace {
bool NativeMetrics(const QFont& font, HFONT native) {
    const auto dc = CreateCompatibleDC(nullptr);
    if (!dc || !native) {
        if (dc)
            DeleteDC(dc);
        return false;
    }
    const auto old = SelectObject(dc, native);
    wchar_t face[128]{};
    GetTextFaceW(dc, 128, face);
    std::wcout << L"GDI resolved=" << face << L'\n';
    const QFontMetrics metrics(font);
    TEXTMETRICW tm{};
    bool equal = GetTextMetricsW(dc, &tm) && metrics.height() == tm.tmHeight &&
                 metrics.ascent() == tm.tmAscent && metrics.descent() == tm.tmDescent;
    if (!equal)
        std::cerr << "font metrics " << QFontInfo(font).family().toStdString() << ' '
                  << font.pixelSize() << ": Qt=" << metrics.height() << ',' << metrics.ascent()
                  << ',' << metrics.descent() << " GDI=" << tm.tmHeight << ',' << tm.tmAscent << ','
                  << tm.tmDescent << '\n';
    for (wchar_t c = 32; c <= 126; ++c) {
        int advance = 0;
        const bool same =
            GetCharWidth32W(dc, c, c, &advance) && metrics.horizontalAdvance(QChar(c)) == advance;
        if (!same)
            std::cerr << "advance " << char(c) << ": " << advance << '/'
                      << metrics.horizontalAdvance(QChar(c)) << '\n';
        equal &= same;
    }
    SelectObject(dc, old);
    DeleteDC(dc);
    return equal;
}
}
#endif
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " #x "\n";                                                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 2);
#ifdef _WIN32
    // The offscreen font database has no Windows system fonts. Load the
    // installed files only; proprietary fonts are never copied or distributed.
    for (const char* file : {"tahoma.ttf", "arial.ttf", "msgothic.ttc", "msyh.ttc"})
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/" + file);
#endif
    qt::EditorWindow window(std::filesystem::absolute(argv[1]), true);
    const auto style = app.style();
    CHECK(style->objectName() == "mmd.editorStyle");
    qt::InstallEditorStyle(app);
    CHECK(app.style() == style);
    const auto menuFont = window.menuBar()->font();
    const auto revision = window.Session().Revision();
    const auto frames = window.Session().Runtime().Counters().frames;
    for (const char* language : {"en", "ja", "zh_CN", "en"}) {
        CHECK(window.SetLanguage(language));
        const auto expected = qt::EditorFont(language), actual = window.font();
        CHECK(actual == expected);
        CHECK(window.Control(panel::kLoadModelButton)->font() == expected);
        for (const int id :
             {panel::kCurrentFrameEdit, panel::kPreviousFrameButton, panel::kNextFrameButton,
              panel::kNextKeyframeButton, panel::kPreviousKeyframeButton, panel::kFirstFrameButton,
              panel::kLastFrameButton})
            CHECK(window.Control(id)->font() == qt::FrameNavigationFont());
        CHECK(window.menuBar()->font() == menuFont);
        const QFontMetrics metrics(actual);
        CHECK(metrics.inFont(QChar('A')) && metrics.inFont(QChar('0')));
        std::cout << language << " resolved=" << QFontInfo(actual).family().toStdString()
                  << " cell=" << metrics.height() << " ascent=" << metrics.ascent()
                  << " descent=" << metrics.descent() << '\n';
#ifdef _WIN32
        // Match the original CreateFont arguments using only a memory DC.
        // FreeType's offscreen rasterizer is deliberately not a GDI oracle.
        if (QGuiApplication::platformName() == "windows" &&
            qAbs(app.devicePixelRatio() - 1.) < .001 && std::string(language) != "zh_CN") {
            const bool english = std::string(language) == "en";
            const auto native =
                CreateFontW(english ? 13 : 12, 0, 0, 0, 400, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                            FIXED_PITCH | FF_MODERN, english ? L"Tahoma" : L"MS PGothic");
            const bool equal = NativeMetrics(actual, native);
            DeleteObject(native);
            CHECK(equal);
            CHECK(NativeMetrics(qt::FrameNavigationFont(),
                                static_cast<HFONT>(GetStockObject(SYSTEM_FONT))));
            for (const int pixels : {11, 12}) {
                const auto glyph = qt::GlyphFont(language, pixels);
                const auto nativeGlyph =
                    CreateFontW(-pixels, 0, 0, 0, 400, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH, english ? L"Arial" : L"MS PGothic");
                const bool same = NativeMetrics(glyph, nativeGlyph);
                DeleteObject(nativeGlyph);
                CHECK(same);
            }
        }
#endif
    }
    CHECK(window.Session().Revision() == revision &&
          window.Session().Runtime().Counters().frames == frames);
    CHECK(!window.isVisible() && window.RenderWindow()->Submissions() == 0);
    std::cout << "Language fonts, class-default frame controls and unchanged document passed\n";
}
