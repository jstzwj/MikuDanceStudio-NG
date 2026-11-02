#pragma once
#include <cstdint>

namespace mikudancestudio::keyframe_edit {
// Original x64 dispatcher 0x4da34..0x4da92. In particular, the divisor is
// RAND_MAX (32767), not 32768, and the phase byte is tested as signed.
inline bool AdvanceBlink(std::uint32_t& frame, std::uint8_t& phase, unsigned random15) {
    const unsigned step = (random15 * 230u) / 32767u;
    const bool nonpositive = phase == 0 || phase >= 128;
    if (step < 90 && nonpositive) {
        frame += 7;
        phase = 3;
        return true;
    }
    if (step > 50) {
        frame += step;
        --phase;
        return true;
    }
    return false;
}
inline unsigned BlinkRandom15(std::uint32_t& state) {
    state = state * 214013u + 2531011u;
    return (state >> 16) & 32767u;
}
}
