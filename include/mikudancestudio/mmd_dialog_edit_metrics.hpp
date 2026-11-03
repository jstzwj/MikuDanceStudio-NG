#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace mikudancestudio::ui {
// Original 96-DPI dialog fonts, measured in a memory DC. These advances govern
// the native ES_AUTOHSCROLL=0 insertion limit, independently of host font fallback.
// The optional original numeric oracle verifies both arrays and actual hidden EDITs.
inline constexpr std::array<int, 95> kDialogArialAdvances{
    3, 3, 4, 7, 7, 11, 8, 2,  4,  4, 5, 7, 3, 4, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 3, 3, 7, 7,  7, 7,  12, 7, 8, 9, 9, 8, 7, 9, 9, 3, 6, 8, 7, 9, 9, 9,
    8, 9, 9, 8, 7, 9,  7, 11, 7,  7, 7, 3, 3, 3, 5, 7, 4, 7, 7, 6, 7, 7, 3, 7,
    7, 3, 3, 6, 3, 11, 7, 7,  7,  7, 4, 7, 3, 7, 5, 9, 5, 5, 5, 4, 3, 4, 7};
inline constexpr std::array<int, 95> kDialogGothicAdvances{
    4, 3, 6, 6, 6, 6, 7, 2, 4, 4, 6, 6, 2, 6, 2, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 2, 2, 6, 6, 6, 5,
    8, 8, 8, 8, 8, 7, 7, 8, 8, 3, 7, 7, 6, 9, 8, 8, 7, 8, 8, 7, 7, 8, 8, 9, 7, 7, 7, 4, 6, 4, 5, 4,
    5, 6, 6, 6, 6, 6, 4, 6, 6, 3, 3, 6, 3, 9, 6, 6, 6, 6, 4, 6, 4, 6, 6, 8, 6, 6, 5, 3, 3, 3, 5};
inline std::size_t NumericPrefillLength(std::string_view text, bool english, int windowWidth) {
    const auto& advances = english ? kDialogArialAdvances : kDialogGothicAdvances;
    // Four border pixels plus the original font-dependent EDIT margins/inset.
    const int available = windowWidth - (english ? 12 : 8);
    int width = 0;
    std::size_t count = 0;
    for (const unsigned char c : text) {
        if (c < 32 || c > 126 || width + advances[c - 32] > available)
            break;
        width += advances[c - 32];
        ++count;
    }
    return count;
}
// ASCII insertion into a native no-auto-scroll EDIT. Existing text outside the
// selection is retained; only the inserted prefix is limited by the client width.
inline std::size_t NumericInsertedLength(std::string_view before, std::size_t first,
                                         std::size_t last, std::string_view inserted, bool english,
                                         int windowWidth) {
    if (first > last || last > before.size())
        return 0;
    const auto& advances = english ? kDialogArialAdvances : kDialogGothicAdvances;
    int width = 0;
    for (std::size_t n = 0; n < before.size(); ++n)
        if (n < first || n >= last) {
            const unsigned char c = before[n];
            if (c < 32 || c > 126)
                return 0;
            width += advances[c - 32];
        }
    std::size_t count = 0;
    for (const unsigned char c : inserted) {
        if (c < 32 || c > 126 || width + advances[c - 32] > windowWidth - (english ? 12 : 8))
            break;
        width += advances[c - 32];
        ++count;
    }
    return count;
}
}
