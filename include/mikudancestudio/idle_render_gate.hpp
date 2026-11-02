#pragma once
#include <cstdint>

namespace mikudancestudio {
// MMD v9.32 x64 0x2755e..0x275e4; an input message restarts at 1.
// Keep both settling frames: physics, drawing and present share this gate.
inline void AdvanceIdleRenderGate(std::uint32_t& state, bool playing, bool lowPower,
                                  bool englishNameDialog) {
    if (playing || !lowPower || englishNameDialog)
        state = 1;
    if (state == 1)
        state = 2;
    else if (state == 2)
        state = 3;
    else if (state == 3)
        state = 0;
}
}
