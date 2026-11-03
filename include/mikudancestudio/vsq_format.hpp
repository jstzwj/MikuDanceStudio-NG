#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mikudancestudio::format {
struct VsqEvent {
    std::string token, consonant;
    char vowel = 0;
    float start = 0, end = 0;
};
struct VsqDocument {
    std::vector<VsqEvent> events;
    std::string report; // Native CP932 text, with CRLF even on Unix.
};
// Bounded SMF/VSQ reader. Output is unchanged on invalid/unsupported input.
// Timing deliberately follows MMD x64's float operations and tempo traversal.
bool ReadVsq(const std::vector<std::uint8_t>&, VsqDocument&, std::string& reason);
}
