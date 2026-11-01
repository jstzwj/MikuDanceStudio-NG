#pragma once
#include <cstdint>

namespace mikudancestudio::cpu_bone {
// The caller clears all three model key pools and supplies validated chains.
// Original x64 52760..52832 selects the physical root and its forward chain,
// including the terminal record. The next-link sentinel comparison is signed.
template <class Physical, class KeyAt>
void MarkPhysicsOnKeys(unsigned count, Physical physical, KeyAt keyAt) {
    for (unsigned bone = 0; bone < count; ++bone) {
        if (!physical(bone))
            continue;
        auto index = bone;
        for (;;) {
            auto& key = keyAt(index);
            if (key.physicsDisabled == 0)
                key.allocated = 1;
            if (static_cast<std::int32_t>(key.next) <= 0)
                break;
            index = key.next;
        }
    }
}
}
