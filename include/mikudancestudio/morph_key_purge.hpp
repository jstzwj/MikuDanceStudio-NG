#pragma once
#include <cstdint>
namespace mikudancestudio::keyframe_edit {
// Original x64 4D061..4D178: clear every linked record, including the tail.
// The loop clears the CURRENT successor, then advances; its final block clears
// the last record. Unlinked occupied records and unrelated free slots survive.
// The caller supplies a validated, acyclic active chain.
template <class At> void PurgeMorphChain(std::uint32_t root, At&& at) {
    auto index = root;
    for (;;) {
        auto& key = at(index);
        const auto next = key.next;
        key.frame = key.previous = key.next = 0;
        key.allocated = 0;
        key.value = 0.f;
        if (!next)
            break;
        index = next;
    }
}
// Original x64 4D1DE..4D47B: shift linked mouth keys, never frame zero.
// Deleting a key changes root.next and clears that slot; it deliberately leaves
// the successor's previous index intact. Addition wraps like the native ADD.
template <class At> void ShiftMorphChain(std::uint32_t root, std::int32_t shift, At&& at) {
    auto index = at(root).next;
    while (index) {
        auto& key = at(index);
        const auto next = key.next;
        key.frame += std::uint32_t(shift);
        if (!key.frame || key.frame > 0x7fffffffU) {
            at(root).next = next;
            key.frame = key.previous = key.next = 0;
            key.allocated = 0;
            key.value = 0.f;
        }
        index = next;
    }
}
}
