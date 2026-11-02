#pragma once
#include <cstdint>

namespace mikudancestudio::keyframe_edit {
inline constexpr unsigned MorphKeyCapacity = 20000;
// Original RegisterMorphKeyCurrent. Overwrite keeps links, free-slot payloads
// survive append, and capacity failure leaves the complete table unchanged.
template <class At>
bool RegisterCurrentMorph(unsigned roots, unsigned root, std::uint32_t frame, float value,
                          std::uint32_t& maximum, At&& at) {
    auto current = root;
    while (at(current).frame < frame && at(current).next)
        current = at(current).next;
    if (at(current).frame == frame) {
        at(current).value = value;
        at(current).allocated = 1;
        return true;
    }
    unsigned fresh = roots;
    while (fresh < MorphKeyCapacity && at(fresh).frame)
        ++fresh;
    if (fresh == MorphKeyCapacity)
        return false;
    auto& key = at(fresh);
    if (at(current).frame < frame) {
        at(current).next = fresh;
        key.previous = current;
    } else {
        key.previous = at(current).previous;
        at(key.previous).next = fresh;
        key.next = current;
        at(current).previous = fresh;
    }
    key.frame = frame;
    key.value = value;
    key.allocated = 1;
    if (frame > maximum)
        maximum = frame;
    return true;
}
}
