#pragma once
#include "mmd_editor_layout.hpp"
namespace mikudancestudio::ui {
inline constexpr std::array<std::uint32_t, 35> kDefaultThemeColors = {
    8421504,  0x545454, 0x464646, 1315860, 3289700,  1315880, 0x464646, 1315860, 3289700,
    1315880,  6566450,  1971220,  3302450, 1318420,  3302500, 1321000,  6566450, 2626580,
    0x464646, 1315860,  0x464646, 1315860, 1250067,  3946295, 868768,   1250132, 3946345,
    5401478,  4418967,  100,      9895830, 16750230, 4934475, 0,        6579455,
};
// Horizontal-collapse state shared by the bottom-panel background and its
// GDI labels. The original advances the offset only for collapsed panels.
struct BottomPanelLayout {
    int leading;
    int afterLightOrFace;
    int afterSelfShadow;
    int accessory;
    int final;
};

inline BottomPanelLayout ComputeBottomPanelLayout(const EditorLayoutOptions& options) {
    const bool camera = options.cameraMode != 0;
    const bool cameraExpanded = options.cameraPanelExpanded != 0;
    const bool lightExpanded = options.lightPanelExpanded != 0;
    const bool accessoryExpanded = options.accessoryPanelExpanded != 0;
    const bool boneExpanded = options.bonePanelExpanded != 0;
    const bool facialExpanded = options.morphPanelExpanded != 0;
    const bool shadowExpanded = options.selfShadowPanelExpanded != 0;

    BottomPanelLayout result{};
    if (camera)
        result.leading = cameraExpanded ? -18 : 117;
    else
        result.leading = boneExpanded ? 35 : 214;

    result.afterLightOrFace = result.leading;
    if (camera) {
        if (!lightExpanded)
            result.afterLightOrFace += 162;
    } else if (!facialExpanded) {
        result.afterLightOrFace += 251;
    }

    result.afterSelfShadow = result.afterLightOrFace;
    if (camera && !shadowExpanded)
        result.afterSelfShadow += 159;

    result.accessory = camera ? result.afterSelfShadow - 180 : result.afterSelfShadow;
    result.final = result.accessory;
    if (camera && !accessoryExpanded)
        result.final += 178;
    if (!camera)
        result.final += 13;
    return result;
}

inline std::uint32_t ColorLerp(std::uint32_t a, std::uint32_t b, float t) {
    // Original computes G and B through one 16-bit register pair; the
    // observable result is per-channel truncation, preserved here.
    const unsigned char rA = static_cast<unsigned char>(a & 0xFF);
    const unsigned char gA = static_cast<unsigned char>((a >> 8) & 0xFF);
    const unsigned char bA = static_cast<unsigned char>((a >> 16) & 0xFF);
    const unsigned char rB = static_cast<unsigned char>(b & 0xFF);
    const unsigned char gB = static_cast<unsigned char>((b >> 8) & 0xFF);
    const unsigned char bB = static_cast<unsigned char>((b >> 16) & 0xFF);

    const int r = static_cast<int>(static_cast<double>(rA) - static_cast<double>(rA - rB) * t);
    const int g = static_cast<int>(static_cast<double>(gA) - static_cast<double>(gA - gB) * t);
    const int bl = static_cast<int>(static_cast<double>(bA) - static_cast<double>(bA - bB) * t);

    return static_cast<std::uint32_t>((r & 0xFF) | ((g & 0xFF) << 8) | ((bl & 0xFF) << 16));
}

struct PanelFill {
    int left, top, right, bottom;
    std::uint32_t color, color2;
    bool vertical;
};
inline std::vector<PanelFill>
MmdPanelFills(int side, int bottom, const EditorLayoutOptions& options,
              const std::array<std::uint32_t, 35>& colors = kDefaultThemeColors) {
    std::vector<PanelFill> result;
    const auto fill = [&](int l, int t, int r, int b, std::uint32_t a, std::uint32_t z, int v) {
        result.push_back({l, t, r, b, a, z, v != 0});
    };
    const std::uint32_t colA = colors[2];
    const std::uint32_t colB = colors[3];

    // Row 1 (0x42CEF0..0x42CF1F): t1 = -45.0/((50.0-bottom)-161.0), all
    // double math with a float truncation at the call boundary (x87
    // fstp dword; the fld/fstp pair is register shuffling only).
    float t = static_cast<float>(-45.0 / ((50.0 - static_cast<double>(bottom)) - 161.0));
    std::uint32_t lerp = ColorLerp(colA, colB, t); // 0x42CEFD (0x40AD00)
    fill(3, 50, side - 3, 95, colA, lerp, 1);      // 0x42CF1F (0x40DF10)

    // Row 2 (0x42CF24..0x42CF78): t2 = ((50.0-bottom)-228.0)/((50.0-bottom)-161.0).
    t = static_cast<float>(((50.0 - static_cast<double>(bottom)) - 228.0) /
                           ((50.0 - static_cast<double>(bottom)) - 161.0));
    const std::uint32_t lerp2 = ColorLerp(colA, colB, t); // 0x42CF5E
    fill(3, 95, 6, bottom - 228, lerp, lerp2, 1);         // 0x42CF78

    // Row 3 (0x42CF7D..0x42CFF6): the span (50.0-bottom)-161.0 (original
    // decompiler name v30) is kept as a
    // double (fst var_18 keeps it on the x87 stack); both quotients are
    // double, each truncated to float for its ColorLerp.
    const double row3Span = (50.0 - static_cast<double>(bottom)) - 161.0;
    const std::uint32_t lerpNeg111 =
        ColorLerp(colA, colB, static_cast<float>(-111.0 / row3Span)); // 0x42CFB5
    const std::uint32_t lerpNeg94 =
        ColorLerp(colA, colB, static_cast<float>(-94.0 / row3Span)); // 0x42CFD5
    fill(side - 19, 144, side - 3, 161, lerpNeg94, lerpNeg111, 1);   // 0x42CFF6

    // Row 4 (0x42CFFB..0x42D05C): t4 = ((50.0-bottom)-248.0)/((50.0-bottom)-161.0).
    t = static_cast<float>(((50.0 - static_cast<double>(bottom)) - 248.0) /
                           ((50.0 - static_cast<double>(bottom)) - 161.0));
    lerp = ColorLerp(colA, colB, t);                              // 0x42D036
    fill(3, bottom - 248, side - 3, bottom - 161, lerp, colB, 1); // 0x42D05C

    // Fixed rows from the colour-table pairs (colour.txt).
    fill(3, bottom - 157, 215, bottom - 3, colors[6], colors[7],
         1); // 0x42D089
    fill(3, 3, side - 3, 47, colors[4], colors[5],
         1); // 0x42D0B1
    fill(218, bottom - 157, 358, bottom - 3, colors[8], colors[9],
         1); // 0x42D0E1

    // Block A (0x42D0E6..0x42D179): IK/FK-style width switch; left 0x169,
    // top bottom-157, bottom bottom-3, colours 0xA0600/0xA0604.
    const std::uint32_t colC = colors[10];
    const std::uint32_t colD = colors[11];
    const BottomPanelLayout panelLayout = ComputeBottomPanelLayout(options);
    std::int32_t xOff = panelLayout.leading;
    if (options.cameraMode != 0) {                                       // 0x2F8
        if (options.cameraPanelExpanded != 0) {                          // 0x2F9
            fill(0x169, bottom - 157, 0x202, bottom - 3, colC, colD, 1); // 0x42D123
        } else {
            fill(0x169, bottom - 157, 0x17B, bottom - 3, colC, colD, 1); // 0x42D13B
        }
    } else if (options.bonePanelExpanded != 0) {                     // 0x2FC
        fill(0x169, bottom - 157, 0x22E, bottom - 3, colC, colD, 1); // 0x42D15C
    } else {
        fill(0x169, bottom - 157, 0x17B, bottom - 3, colC, colD, 1); // 0x42D174
    }

    // Block B (0x42D17E..0x42D218): colours 0xA0608/0xA060C; the offset is
    // subtracted from both edges and optionally grown.
    const std::uint32_t colE = colors[12];
    const std::uint32_t colF = colors[13];
    if (options.cameraMode != 0) {             // 0x2F8
        if (options.lightPanelExpanded != 0) { // 0x2FA
            fill(0x1F3 - xOff, bottom - 157, 0x2A7 - xOff, bottom - 3, colE, colF,
                 1); // 0x42D1C1
        } else {
            fill(0x1F3 - xOff, bottom - 157, 0x205 - xOff, bottom - 3, colE, colF,
                 1); // 0x42D1D5
        }
    } else if (options.morphPanelExpanded != 0) { // 0x2FD
        fill(0x254 - xOff, bottom - 157, 0x361 - xOff, bottom - 3, colE, colF,
             1); // 0x42D1FF
    } else {
        fill(0x254 - xOff, bottom - 157, 0x266 - xOff, bottom - 3, colE, colF,
             1); // 0x42D213
    }
    xOff = panelLayout.afterLightOrFace;

    // Block C (0x42D21E..0x42D27E): only when 0x2F8 set; note the swapped
    // colour roles - colour 0xA0628, edge 0xA0604 (push ho=0xA0604 first).
    if (options.cameraMode != 0) { // 0x2F8
        const std::uint32_t colG = colors[20];
        if (options.selfShadowPanelExpanded != 0) { // 0x2FE
            fill(0x2AA - xOff, bottom - 157, 0x35B - xOff, bottom - 3, colG, colD,
                 1); // 0x42D265
        } else {
            fill(0x2AA - xOff, bottom - 157, 0x2BC - xOff, bottom - 3, colG, colD,
                 1); // 0x42D279
        }
    }
    xOff = panelLayout.afterSelfShadow;

    // Block D (0x42D284..0x42D2E0): only when 0x2F8 set; colours 0xA0610/4.
    if (options.cameraMode != 0) { // 0x2F8
        const std::uint32_t colH = colors[14];
        const std::uint32_t colI = colors[15];
        if (options.accessoryPanelExpanded != 0) { // 0x2FB
            fill(0x35E - xOff, bottom - 157, 0x422 - xOff, bottom - 3, colH, colI,
                 1); // 0x42D2C7
        } else {
            fill(0x35E - xOff, bottom - 157, 0x370 - xOff, bottom - 3, colH, colI,
                 1); // 0x42D2DB
        }
    }

    // Final offset (0x42D2E6..0x42D2FA): 0x2F8 set -> -180, else +13.
    const std::int32_t xFinal = panelLayout.final;

    // Three closing rows (0x42D2FA..0x42D38E).
    fill(0x371 - xFinal, bottom - 157, 0x412 - xFinal, bottom - 0x52, colors[16], colors[17],
         1); // 0x42D330
    fill(0x371 - xFinal, bottom - 0x4F, 0x3F3 - xFinal, bottom - 3, colors[18], colors[19],
         1); // 0x42D361
    fill(0x3F6 - xFinal, bottom - 0x4F, 0x412 - xFinal, bottom - 3, colors[18], colors[19],
         1); // 0x42D38E
    return result;
}
}
