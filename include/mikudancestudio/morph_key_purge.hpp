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
}
