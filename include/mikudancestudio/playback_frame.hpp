#pragma once
#include <cmath>
#include <limits>

namespace mikudancestudio {
// x64 playback stores both products and the rounded quotient as binary32.
// The original x86 quotient stays on the x87 stack until the final float store.
// Keep these boundaries shared by live models and the editor's global tracks.
inline bool NativePlaybackFrame(float seconds, float& output) {
    if (!std::isfinite(seconds) || seconds < 0)
        return false;
    float frame = seconds * 30.f;
    const float scaled = frame * 1000.f;
    if (scaled >= static_cast<float>(std::numeric_limits<int>::max()))
        return false;
    const int whole = static_cast<int>(scaled);
    if (scaled - static_cast<float>(whole) >= .5f) {
#if defined(_M_IX86)
        frame = static_cast<float>(static_cast<double>(whole + 1) / 1000.);
#else
        frame = static_cast<float>(whole + 1) / 1000.f;
#endif
    }
    output = frame;
    return true;
}
}
