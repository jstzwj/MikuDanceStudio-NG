#pragma once
#include <cstdint>

namespace mikudancestudio::keyframe_edit {
// Original 9ED40 visits only the forward chains of physics bones. Detached
// records and selected unused slots do not participate in this command.
// The caller validates topology and maps document channels to physics bones.
template <class Physical, class At, class Visit>
void VisitSelectedPhysicsKeys(std::uint32_t roots, Physical&& physical, At&& at, Visit&& visit) {
    for (std::uint32_t root = 0; root < roots; ++root) {
        if (!physical(root))
            continue;
        auto index = root;
        do {
            auto& key = at(index);
            if (key.allocated)
                visit(index, key);
            index = key.next;
        } while (index);
    }
}
}
