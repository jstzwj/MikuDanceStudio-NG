#pragma once
#include "mikudancestudio/pmm_bone_clipboard.hpp"
#include "frame_line_deletion_fixture.hpp"

namespace bone_clipboard_fixture {
using namespace mikudancestudio::format;
struct NameCase {
    std::string copied;
    std::vector<std::string> names;
    int target;
};
inline std::vector<NameCase> Names() {
    const std::string left = "\x8d\xb6", right = "\x89\x45";
    return {{"prefix" + left + "tail", {"A" + right + "tail", "B" + right + "tail", "other"}, 0},
            {right + "tail", {"xx" + left + "tail", "other"}, 0},
            {"missing", {"other", "target"}, -1},
            {"target", {"target", "other"}, 0},
            {left + "x" + right + "tail",
             {left + "x" + right + "tail", right + "x" + right + "tail", left + "tail"},
             1},
            {left + "x" + right + "tail", {"prefix" + left + "tail", "other"}, 0},
            {left + "tail", {"other", left + "tail"}, 1}};
}
inline PmmBoneKey Copied(unsigned relativeFrame) {
    PmmBoneKey key;
    key.frame = relativeFrame;
    key.position = {7.25f, -2.5f, 3};
    key.rotation = {.25f, -.5f, .125f, .75f};
    key.physicsDisabled = 1;
    for (unsigned i = 0; i < 16; ++i)
        key.interpolation[i] = std::uint8_t(11 + 3 * i);
    return key;
}
inline PmmKeyTrack<PmmBoneKey> Make(unsigned order) {
    auto track = frame_line_fixture::Make<PmmBoneKey>(order);
    // The first free slot contains retired payload, including a position
    // which ordinary rotation-only paste must retain.
    auto& unused = track.unused[3];
    unused.position = {-4, 5, 6};
    unused.rotation = {0, 0, 0, 1};
    return track;
}
inline void Hash(std::uint64_t& hash, const PmmKeyTrack<PmmBoneKey>& track,
                 const std::map<std::uint32_t, PmmBoneKey>& captured, unsigned maximum) {
    auto all = track;
    for (const auto& [index, key] : track.unused)
        all.sparse.push_back({int(index), 0, key});
    frame_line_fixture::Hash(hash, all);
    std::vector<std::uint8_t> bytes;
    frame_line_fixture::Bytes(bytes, maximum);
    for (const auto& [index, key] : captured) {
        frame_line_fixture::Bytes(bytes, index);
        const auto payload = frame_line_fixture::Bytes(key);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    for (auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
}
}
