#pragma once
#include "mikudancestudio/bone_key_layout.hpp"
#include <array>
#include <limits>
#include <cstdint>

namespace bone_curve_fixture {
using Keys = std::array<mikudancestudio::mdl::BoneKey, 6>;
template <class Visitor> void Visit(Visitor visit) {
    const auto maximum = std::numeric_limits<float>::max();
    const std::array<float, 4> positions[]{{0, 1, 2, 3},
                                           {3, 2, 1, 0},
                                           {0, 2, 1, 3},
                                           {0, 1, 1, 2},
                                           {0, 0, 0, 0},
                                           {-1, 0, 1, 1},
                                           {0, 1e-30f, 2e-30f, 3e-30f},
                                           {-maximum, maximum, maximum, -maximum},
                                           {maximum, -maximum, maximum, -maximum},
                                           {1, 1.00000011920928955078125f, 1, 2}};
    for (const auto& values : positions)
        for (unsigned index : {0, 1, 2, 3, 4})
            for (unsigned lane = 0; lane < 4; ++lane) {
                Keys keys{};
                for (unsigned i = 0; i < keys.size(); ++i) {
                    for (unsigned c = 0; c < 16; ++c)
                        keys[i].interpolation[c] = std::uint8_t(i * 16 + c);
                    keys[i].allocated = i % 2;
                    keys[i].physicsDisabled = (i + 1) % 2;
                    keys[i].reserved[0] = 0xa5;
                    keys[i].reserved[1] = 0x5a;
                    keys[i].rotation[3] = 1;
                }
                keys[0].next = 2;
                for (unsigned i = 2; i < 5; ++i) {
                    keys[i].frame = i * 10;
                    keys[i].previous = i == 2 ? 0 : i - 1;
                    keys[i].next = i == 4 ? 0 : i + 1;
                }
                for (unsigned i = 0; i < 4; ++i) {
                    auto& key = keys[i ? i + 1 : 0];
                    key.position[0] = key.position[2] = values[i];
                    key.position[1] = -values[i];
                }
                visit(keys, index, lane);
            }
}
inline void Hash(std::uint64_t& hash, const Keys& keys) {
    // Only output curve bytes are frozen; native comparison also checks every
    // untouched payload byte. No struct padding enters the portable checksum.
    for (const auto& key : keys)
        for (auto value : key.interpolation)
            hash = (hash ^ value) * 1099511628211ull;
}
}
