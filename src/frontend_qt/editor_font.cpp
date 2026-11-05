#include "editor_font.hpp"
#include <QCoreApplication>
#include <QApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFileInfo>
#include <algorithm>
#include <map>
#include <tuple>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace mikudancestudio::qt {
QFont SystemMenuFont() {
    auto font = QApplication::font("QMenuBar");
    font.setResolveMask(QFont::AllPropertiesResolved);
    return font;
}
QFont MatchEditorFontMetrics(QFont font) {
    auto strategy = font.styleStrategy();
    const auto family = QFontInfo(font).family();
    if (family.startsWith("Noto Sans CJK "))
        strategy = QFont::StyleStrategy(strategy | QFont::PreferTypoLineMetrics);
#ifdef _WIN32
    if (family == "MS Gothic" || family == "MS PGothic" || family == "MS UI Gothic") {
        auto families = font.families();
        // Prefer the installed, metric-compatible Windows CJK fallback before
        // Noto. Mixing MS Gothic and Noto ascenders/descenders expands a native
        // 12-pixel row to 17 (or 13 even with typo metrics). The multiline
        // document layout separately excludes external leading, as EDIT does.
        // No system font is copied or redistributed.
        if (!families.contains("SimSun")) {
            const auto index = families.indexOf(family);
            families.insert(index < 0 ? std::min(1, int(families.size())) : index + 1, "SimSun");
            font.setFamilies(families);
        }
    }
#endif
    // Resolve even the default explicitly: a Latin control must not inherit
    // its parent's CJK metrics policy through QWidget::setFont/QPainter::setFont.
    font.setStyleStrategy(strategy);
    return font;
}
bool InstallEditorFonts() {
    static const bool loaded = [] {
        const auto directory = QCoreApplication::applicationDirPath();
        for (const auto& path : {directory + "/fonts/NotoSansCJK-Regular.ttc",
                                 directory + "/../Resources/fonts/NotoSansCJK-Regular.ttc"}) {
            if (!QFileInfo::exists(path))
                continue;
            const auto id = QFontDatabase::addApplicationFont(path);
            const auto families = QFontDatabase::applicationFontFamilies(id);
            return id >= 0 && families.contains("Noto Sans CJK JP") &&
                   families.contains("Noto Sans CJK SC");
        }
        return false;
    }();
    return loaded;
}
QFont GlyphFont(const QString& language, int pixels, bool bold) {
    // Font mapping is stable within a UI session. Never create GDI objects on
    // each label paint; the cache contains only Qt value objects.
    static std::map<std::tuple<QString, int, bool>, QFont> cache;
    const auto key = std::make_tuple(language, pixels, bold);
    if (const auto found = cache.find(key); found != cache.end())
        return found->second;
    auto font = EditorFont(language);
    if (language == "en")
        font.setFamilies({"MS PGothic", "Noto Sans CJK JP", "Arial", "Liberation Sans", "Helvetica",
                          "DejaVu Sans"});
    // DrawGlyph uses negative LOGFONT height: an em size, unlike the positive
    // cell height applied to child controls. Its SHIFTJIS_CHARSET matters:
    // on the qualified Windows profile Arial maps to MS PGothic.
    font.setPixelSize(pixels);
    font.setBold(bold);
#ifdef _WIN32
    if (language != "zh_CN") {
        const auto native =
            CreateFontW(-pixels, 0, 0, 0, bold ? 700 : 400, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
                        language == "en" ? L"Arial" : L"MS PGothic");
        const auto dc = CreateCompatibleDC(nullptr);
        if (native && dc) {
            const auto previous = SelectObject(dc, native);
            wchar_t family[LF_FACESIZE]{};
            if (GetTextFaceW(dc, LF_FACESIZE, family)) {
                auto families = font.families();
                families.prepend(QString::fromWCharArray(family));
                font.setFamilies(families);
            }
            SelectObject(dc, previous);
        }
        if (dc)
            DeleteDC(dc);
        if (native)
            DeleteObject(native);
    }
#endif
    font = MatchEditorFontMetrics(font);
    cache.emplace(key, font);
    return font;
}
}
