#pragma once
#include <cstdint>

namespace mikudancestudio::cpu_keyframe {
// Original x64 RVA 5DA60: overwrite retains links; append retains the free
// slot's successor payload. Only a newly allocated key raises the watermark.
// The caller validates topology, owns the 10000-record pool and supplies payload.
template <class FrameAt, class KeyAt, class Fill>
bool RegisterGravityCurrent(std::uint32_t frame, FrameAt&& frameAt, KeyAt&& keyAt, Fill&& fill,
                            std::uint32_t& maximum) {
    std::uint32_t current = 0;
    while (keyAt(current).frame < frame && keyAt(current).next)
        current = keyAt(current).next;
    if (keyAt(current).frame == frame) {
        fill(keyAt(current));
        return true;
    }
    std::uint32_t free = 1;
    while (free < 10000 && frameAt(free))
        ++free;
    if (free == 10000)
        return false;
    auto& added = keyAt(free);
    if (keyAt(current).frame >= frame) {
        const auto previous = keyAt(current).previous;
        keyAt(previous).next = free;
        added.previous = previous;
        keyAt(current).previous = free;
        added.next = current;
    } else {
        keyAt(current).next = free;
        added.previous = current;
    }
    added.frame = frame;
    fill(added);
    if (frame > maximum)
        maximum = frame;
    return true;
}
}
