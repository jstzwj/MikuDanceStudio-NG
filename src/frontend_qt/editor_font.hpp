#pragma once
#include <QFont>
#include <QString>
#include <QStringList>

namespace mikudancestudio::qt {
// Registers the packaged OFL font collection, without replacing installed
// original Windows fonts. False means the runtime package is incomplete.
bool InstallEditorFonts();
inline QFont EditorFont(const QString& language) {
    QFont font;
    if (language == "en") {
        font.setFamilies({"Tahoma", "Liberation Sans", "Helvetica", "DejaVu Sans"});
        // Native CreateFont has positive cell height 13, with two pixels of
        // internal leading. Qt's pixel size describes the 11-pixel em height.
        font.setPixelSize(11);
    } else {
        font.setFamilies(language == "ja"
                             ? QStringList{"MS PGothic", "MS P\u30b4\u30b7\u30c3\u30af",
                                           "Noto Sans CJK JP", "Hiragino Sans", "Noto Sans CJK SC",
                                           "DejaVu Sans"}
                             : QStringList{"Microsoft YaHei", "Noto Sans CJK SC", "PingFang SC",
                                           "MS PGothic", "Hiragino Sans", "DejaVu Sans"});
        // Native Japanese cell/em height is 12, with no internal leading.
        font.setPixelSize(12);
    }
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setKerning(false);
    return font;
}
inline QFont FrameNavigationFont() {
    // The frame edit and its six buttons retain Win32's class SYSTEM_FONT,
    // instead of receiving the language-dependent WM_SETFONT.
    QFont font;
    font.setFamilies({"System", "DejaVu Sans", "Helvetica"});
    font.setPixelSize(16);
    font.setBold(true);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setKerning(false);
    return font;
}
QFont GlyphFont(const QString& language, int pixels, bool bold = false);
}
