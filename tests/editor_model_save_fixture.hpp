#pragma once
#include "mikudancestudio/model_reader.hpp"
#include <cstring>

inline constexpr std::uint64_t EnhancedNativeSaveHashes[]{
    0x355e96c5890e21aaull, 0x612e73fbdf2dae67ull, 0x911300e0acc5fca2ull, 0x5efddf4560884ffaull};

inline mikudancestudio::format::PmdDocument EnhancedSaveFixture(unsigned profile) {
    using namespace mikudancestudio::format;
    PmdDocument d;
    std::memcpy(d.header.name.data(), "original model", 14);
    std::memcpy(d.header.comment.data(), "original model\ncomment", 22);
    d.vertices.resize(3);
    d.indices = {2, 0, 1};
    for (unsigned i = 0; i < 3; ++i) {
        auto& v = d.vertices[i];
        v.position[0] = float(i) + .125f;
        v.position[1] = -.25f;
        v.position[2] = .5f;
        v.normal[1] = 1;
        v.uv[0] = .25f * float(i);
        v.uv[1] = .75f;
        v.bone[0] = 0;
        v.bone[1] = 1;
        v.weightPercent = std::uint8_t(100 - i * 25);
        v.edgeDisabled = std::uint8_t(i * 23);
    }
    d.materials.resize(3);
    for (unsigned i = 0; i < 3; ++i) {
        auto& m = d.materials[i];
        for (unsigned c = 0; c < 4; ++c)
            m.diffuse[c] = float(i + c) * .125f;
        m.specular[0] = .25f;
        m.ambient[2] = -.125f;
        m.specularPower = 16;
        m.toonReference = std::uint8_t(i == 2 ? 255 : i);
        m.flags = std::uint8_t(i * 19);
        m.faceVertexCount = i == 0 ? 3 : 0;
    }
    std::memcpy(d.materials[0].pmdTexture, "tex.bmp", 7);
    d.materials[0].pmdTexture[8] = '\x7f'; // Disk padding does not belong to the filename.
    std::memcpy(d.materials[1].pmdTexture, "tex.bmp*sph.spa", 15);
    std::memcpy(d.materials[2].pmdTexture, "*only.sph", 9);
    d.bones.resize(3);
    for (unsigned i = 0; i < 3; ++i) {
        auto& b = d.bones[i];
        std::memcpy(b.pmdName,
                    i == 0   ? "root"
                    : i == 1 ? "IK"
                             : "child",
                    i == 2   ? 5
                    : i == 1 ? 2
                             : 4);
        b.parent = i ? 0 : -1;
        b.tailBone = i == 0 ? 1 : 0;
        b.type = std::uint8_t(i * 2);
        b.tailIdx = i == 2 ? 1 : 0;
        b.position[1] = float(i);
    }
    d.ikChains.push_back({1, 2, 40, .125f, {0}});
    d.morphs.resize(3);
    for (unsigned i = 0; i < 3; ++i) {
        auto& m = d.morphs[i];
        std::memcpy(m.header.name, i == 0 ? "base" : "morph", i == 0 ? 4 : 5);
        m.header.panel = std::uint8_t(i);
        m.offsets = {{i == 0 ? 2u : 0u, {.25f, .5f, .75f}},
                     {i == 0 ? 0u : 1u, {-.25f, .125f, .375f}}};
        m.header.offsetCount = int(m.offsets.size());
    }
    if (profile != 1)
        d.facialTargets = {2, 1};
    d.groupNames.resize(2);
    std::memcpy(d.groupNames[0].data(), "group A", 7);
    std::memcpy(d.groupNames[1].data(), "group B", 7);
    d.boneFrames = {{2, 2}, {0, 1}};
    d.englishBoneNames.resize(3);
    d.englishMorphNames.resize(2);
    d.englishGroupNames.resize(2);
    std::memcpy(d.englishModel.name, "English model", 13);
    std::memcpy(d.englishModel.comment, "English model\ncomment", 21);
    for (unsigned i = 0; i < 3; ++i)
        std::memcpy(d.englishBoneNames[i].data(), "English bone", 12);
    for (unsigned i = 0; i < 2; ++i) {
        std::memcpy(d.englishMorphNames[i].data(), "English morph", 13);
        std::memcpy(d.englishGroupNames[i].data(), "English group", 13);
    }
    for (unsigned i = 0; i < 10; ++i) {
        std::memcpy(d.toonNames[i].data(), "toon01.bmp", 10);
        d.toonNames[i][5] = char('1' + i % 9);
    }
    d.rigids.resize(2);
    for (unsigned i = 0; i < 2; ++i) {
        auto& r = d.rigids[i];
        std::memcpy(r.pmdName, "rigid", 5);
        r.boneIndex = i ? -1 : 2;
        r.group = 3;
        r.noCollapse = 0xa53c;
        r.shape = std::uint8_t(i);
        r.size[0] = .125f;
        r.size[1] = .5f;
        r.size[2] = 1;
        r.position[1] = 2.25f;
        r.rotation[2] = -.5f;
        r.mass = 3.5f;
        r.linearDamping = .125f;
        r.angularDamping = .25f;
        r.restitution = .375f;
        r.friction = .5f;
        r.mode = std::uint8_t(i + 1);
    }
    d.joints.resize(1);
    auto& j = d.joints[0];
    std::memcpy(j.pmdName, "joint", 5);
    j.rigidA = 0;
    j.rigidB = 1;
    j.position[0] = -.25f;
    j.rotation[1] = .375f;
    for (unsigned i = 0; i < 12; ++i)
        j.limits[i] = float(int(i) - 6) * .125f;
    for (unsigned i = 0; i < 6; ++i)
        j.springs[i] = float(i) * .25f;
    if (profile == 2) {
        // Distinct base entries can name the same vertex. Native save remaps
        // later morphs to the first matching base entry, without changing offsets.
        d.morphs[0].offsets[1].vertexIndex = d.morphs[0].offsets[0].vertexIndex;
        std::memset(d.englishModel.name, 'a', 18);
        d.englishModel.name[18] = '\x82'; // Retain a truncated CP932 byte.
    }
    if (profile == 3) {
        d.morphs.clear();
        d.englishMorphNames.clear();
        d.facialTargets.clear();
        d.ikChains.clear();
        d.rigids.clear();
        d.joints.clear();
    }
    return d;
}
inline std::uint64_t EnhancedSaveHash(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t h = 14695981039346656037ull;
    for (auto b : bytes)
        h = (h ^ b) * 1099511628211ull;
    return h;
}
