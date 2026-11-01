#pragma once
#include "mikudancestudio/pmm_frame_line.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include <cstring>

namespace frame_line_fixture {
using namespace mikudancestudio::format;
inline PmmSceneDocument Scene(const PmmKeyTrack<PmmBoneKey>& track) {
    auto doc = NewPmmScene();
    PmmSceneModel model;
    model.layout.boneCount = unsigned(track.dense.size());
    model.boneKeys = track;
    model.displayKeys.dense.resize(1);
    model.displayKeys.dense[0].visible = 1;
    model.bonePose.resize(track.dense.size());
    doc.models.push_back(model);
    return doc;
}
template <class T> PmmKeyTrack<T> Make(unsigned order) {
    PmmKeyTrack<T> track;
    constexpr bool model = std::is_same_v<T, PmmBoneKey> || std::is_same_v<T, PmmMorphKey>;
    track.dense.resize(model ? 3 : 1);
    const unsigned indices[3][3] = {{13, 19, 7}, {7, 13, 19}, {19, 13, 7}};
    for (unsigned i = 0; i < 3; ++i) {
        T key{};
        key.frame = i == 0 ? 1 : i == 1 ? 15 : 30;
        key.previous = i ? indices[order][i - 1] : 0;
        key.next = i < 2 ? indices[order][i + 1] : 0;
        track.sparse.push_back({int(indices[order][i]), 0, key});
    }
    track.dense[0].next = indices[order][0];
    if constexpr (model) {
        T a{}, b{};
        a.frame = 1;
        a.previous = 1;
        a.next = 17;
        b.frame = 15;
        b.previous = 5;
        track.dense[1].next = 5;
        track.sparse.push_back({5, 0, a});
        track.sparse.push_back({17, 0, b});
    }
    const auto fill = [](unsigned i, T& key) {
        if constexpr (std::is_same_v<T, PmmBoneKey>) {
            key.position = {float(i), -float(i), float(i % 5)};
            key.rotation = {0, 0, 0, 1};
            for (unsigned c = 0; c < 16; ++c)
                key.interpolation[c] = std::uint8_t((i * 7 + c * 3) % 128);
            key.allocated = i % 2;
            key.physicsDisabled = (i + 1) % 2;
        } else if constexpr (std::is_same_v<T, PmmMorphKey>) {
            key.value = float(i) / 32;
            key.allocated = i % 2;
        } else if constexpr (std::is_same_v<T, PmmCameraKey>) {
            key.distance = -30 - float(i);
            key.eye = {float(i), float(i + 1), float(i + 2)};
            key.target = {-float(i), float(i) / 8, 0};
            key.parentModel = int(i) - 1;
            key.parentBone = int(i) + 4;
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned c = 0; c < 6; ++c)
                    key.interpolation[row][c] = std::uint8_t((i + row * 17 + c * 3) % 128);
            key.perspective = i % 2;
            key.selected = (i + 1) % 2;
            key.fov = 30 + i;
        } else {
            key.color = {float(i) / 32, .3f, .6f};
            key.direction = {float(i), -1, 2};
            key.selected = i % 2;
        }
    };
    for (unsigned i = 0; i < track.dense.size(); ++i)
        fill(i, track.dense[i]);
    for (auto& key : track.sparse)
        fill(unsigned(key.index), key.value);
    return track;
}
template <class T> void Bytes(std::vector<std::uint8_t>& out, T value) {
    static_assert(std::is_arithmetic_v<T>);
    if constexpr (std::is_floating_point_v<T>) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        Bytes(out, bits);
    } else {
        for (unsigned i = 0; i < sizeof(T); ++i)
            out.push_back(std::uint8_t(std::uint64_t(value) >> (8 * i)));
    }
}
template <class T, std::size_t N>
void Bytes(std::vector<std::uint8_t>& out, const std::array<T, N>& values) {
    for (const auto& value : values)
        Bytes(out, value);
}
template <class T> std::vector<std::uint8_t> Bytes(const T& key) {
    std::vector<std::uint8_t> out;
    Bytes(out, key.frame);
    Bytes(out, key.previous);
    Bytes(out, key.next);
    if constexpr (std::is_same_v<T, PmmBoneKey>) {
        Bytes(out, key.interpolation);
        Bytes(out, key.position);
        Bytes(out, key.rotation);
        Bytes(out, key.allocated);
        Bytes(out, key.physicsDisabled);
    } else if constexpr (std::is_same_v<T, PmmMorphKey>) {
        Bytes(out, key.value);
        Bytes(out, key.allocated);
    } else if constexpr (std::is_same_v<T, PmmCameraKey>) {
        Bytes(out, key.distance);
        Bytes(out, key.eye);
        Bytes(out, key.target);
        Bytes(out, key.parentModel);
        Bytes(out, key.parentBone);
        Bytes(out, key.interpolation);
        Bytes(out, key.perspective);
        Bytes(out, key.selected);
        Bytes(out, key.fov);
    } else {
        Bytes(out, key.color);
        Bytes(out, key.direction);
        Bytes(out, key.selected);
    }
    return out;
}
template <class T> std::map<std::uint32_t, T> Records(const PmmKeyTrack<T>& track) {
    std::map<std::uint32_t, T> records;
    for (unsigned i = 0; i < track.dense.size(); ++i)
        records.emplace(i, track.dense[i]);
    for (const auto& key : track.sparse)
        records.emplace(unsigned(key.index), key.value);
    return records;
}
template <class T> void Hash(std::uint64_t& hash, const PmmKeyTrack<T>& track) {
    std::vector<std::uint8_t> bytes;
    const auto records = Records(track);
    Bytes(bytes, std::uint32_t(records.size()));
    for (const auto& [index, key] : records) {
        Bytes(bytes, index);
        const auto payload = Bytes(key);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    for (auto byte : bytes)
        hash = (hash ^ byte) * 1099511628211ull;
}
}
