#pragma once
#include <cstdint>

namespace mikudancestudio::keyframe_edit {
// Menu 222 compares unmodified neighbours before any selected key is deleted.
// Numeric equality deliberately matches signed zeros, but never NaNs. Curves,
// frame distances and per-key physics flags are not part of this predicate.
template <class Key> bool SameBonePose(const Key& a, const Key& b) {
    for (unsigned i = 0; i < 3; ++i)
        if (a.position[i] != b.position[i])
            return false;
    for (unsigned i = 0; i < 4; ++i)
        if (a.rotation[i] != b.rotation[i])
            return false;
    return true;
}
template <class KeyAt, class Same, class Mark>
unsigned MarkRedundantInterior(std::uint32_t root, KeyAt&& at, Same&& same, Mark&& mark) {
    unsigned count = 0;
    for (auto current = at(root).next; current && at(current).next; current = at(current).next) {
        const auto& key = at(current);
        if (same(key, at(key.previous)) && same(key, at(key.next))) {
            mark(current);
            ++count;
        }
    }
    return count;
}
}
