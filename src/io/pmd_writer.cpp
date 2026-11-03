#include "mikudancestudio/pmd_writer.hpp"
#include <algorithm>
#include <cstring>

namespace mikudancestudio::format {
namespace {
struct Writer {
    std::vector<std::uint8_t> bytes;
    void raw(const void* value, std::size_t size) {
        const auto* begin = static_cast<const std::uint8_t*>(value);
        bytes.insert(bytes.end(), begin, begin + size);
    }
    void integer(std::uint64_t value, unsigned size) {
        for (unsigned i = 0; i < size; ++i)
            bytes.push_back(std::uint8_t(value >> (i * 8)));
    }
    void number(float value) {
        std::uint32_t bits;
        static_assert(sizeof bits == sizeof value);
        std::memcpy(&bits, &value, sizeof bits);
        integer(bits, 4);
    }
    void numbers(const float* values, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i)
            number(values[i]);
    }
};
struct Cursor {
    const std::vector<std::uint8_t>& bytes;
    std::size_t offset = 0;
    static int Read(void* opaque, void* destination, std::size_t count) {
        auto& c = *static_cast<Cursor*>(opaque);
        if (count > c.bytes.size() - c.offset)
            return 0;
        std::memcpy(destination, c.bytes.data() + c.offset, count);
        c.offset += count;
        return int(count);
    }
};
}
bool WriteEnhancedPmd(const PmdDocument& source, std::vector<std::uint8_t>& output,
                      std::string& reason) {
    const auto fail = [&](const char* message) {
        reason = message;
        return false;
    };
    if (source.vertices.size() > 1'000'000 || source.indices.size() > 3'000'000 ||
        source.materials.size() > 100'000 || source.bones.size() > 65535 ||
        source.ikChains.size() > 65535 || source.morphs.size() > 65535 ||
        source.facialTargets.size() > 255 || source.groupNames.size() > 255 ||
        source.boneFrames.size() > 100'000 || source.rigids.size() > 100'000 ||
        source.joints.size() > 100'000)
        return fail("pmd-record-count");
    if (source.englishBoneNames.size() != source.bones.size() ||
        source.englishMorphNames.size() != (source.morphs.empty() ? 0 : source.morphs.size() - 1) ||
        source.englishGroupNames.size() != source.groupNames.size())
        return fail("pmd-english-count");
    std::size_t offsets = 0;
    for (const auto& morph : source.morphs) {
        if (morph.offsets.size() > 1'000'000 - offsets)
            return fail("pmd-morph-count");
        offsets += morph.offsets.size();
    }
    for (const auto& ik : source.ikChains)
        if (ik.links.size() > 255)
            return fail("pmd-ik-link-count");
    // Native save restores the base morph's absolute positions before writing.
    // Do that in private scratch so the running editor keeps its CPU snapshot.
    auto vertices = source.vertices;
    if (!source.morphs.empty())
        for (const auto& offset : source.morphs.front().offsets) {
            if (offset.vertexIndex >= vertices.size())
                return fail("pmd-base-morph-index");
            std::memcpy(vertices[offset.vertexIndex].position, offset.offset, sizeof offset.offset);
        }
    Writer w;
    w.raw("Pmd", 3);
    w.number(1.0f);
    w.raw(source.header.name.data(), 20);
    w.raw(source.header.comment.data(), 256);
    w.integer(vertices.size(), 4);
    for (const auto& v : vertices) {
        w.numbers(v.position, 3);
        w.numbers(v.normal, 3);
        w.numbers(v.uv, 2);
        w.integer(v.bone[0], 2);
        w.integer(v.bone[1], 2);
        w.integer(v.weightPercent, 1);
        w.integer(v.edgeDisabled != 0, 1);
    }
    w.integer(source.indices.size(), 4);
    for (auto index : source.indices)
        w.integer(index, 2);
    w.integer(source.materials.size(), 4);
    for (const auto& m : source.materials) {
        w.numbers(m.diffuse, 4);
        w.number(m.specularPower);
        w.numbers(m.specular, 3);
        w.numbers(m.ambient, 3);
        w.integer(m.toonReference, 1);
        w.integer(m.flags != 0, 1);
        w.integer(std::uint32_t(m.faceVertexCount), 4);
        std::array<char, 20> filename{};
        std::copy(m.pmdTexture, std::find(m.pmdTexture, m.pmdTexture + 20, '\0'), filename.begin());
        w.raw(filename.data(), filename.size());
    }
    w.integer(source.bones.size(), 2);
    for (const auto& b : source.bones) {
        w.raw(b.pmdName, 20);
        w.integer(std::uint16_t(b.parent), 2);
        w.integer(std::uint16_t(b.tailBone), 2);
        w.integer(b.type, 1);
        w.integer(std::uint16_t(b.tailIdx), 2);
        w.numbers(b.position, 3);
    }
    w.integer(source.ikChains.size(), 2);
    for (const auto& ik : source.ikChains) {
        w.integer(ik.boneIndex, 2);
        w.integer(ik.targetBone, 2);
        w.integer(ik.links.size(), 1);
        w.integer(ik.iterations, 2);
        w.number(ik.maxAngle);
        for (auto link : ik.links)
            w.integer(link, 2);
    }
    w.integer(source.morphs.size(), 2);
    for (unsigned i = 0; i < source.morphs.size(); ++i) {
        const auto& morph = source.morphs[i];
        w.raw(morph.header.name, 20);
        w.integer(morph.offsets.size(), 4);
        w.integer(morph.header.panel, 1);
        for (const auto& offset : morph.offsets) {
            auto index = offset.vertexIndex;
            if (i) {
                const auto& base = source.morphs.front().offsets;
                if (index >= base.size())
                    return fail("pmd-morph-index");
                const auto vertex = base[index].vertexIndex;
                index = 0;
                while (base[index].vertexIndex != vertex)
                    ++index;
            }
            w.integer(index, 4);
            w.numbers(offset.offset, 3);
        }
    }
    w.integer(source.facialTargets.size(), 1);
    for (auto target : source.facialTargets)
        w.integer(target, 2);
    w.integer(source.groupNames.size(), 1);
    for (const auto& name : source.groupNames)
        w.raw(name.data(), name.size());
    w.integer(source.boneFrames.size(), 4);
    for (const auto& row : source.boneFrames) {
        w.integer(row.bone, 2);
        w.integer(row.group, 1);
    }
    w.integer(1, 1);
    w.raw(source.englishModel.name, 20);
    w.raw(source.englishModel.comment, 256);
    for (const auto& name : source.englishBoneNames)
        w.raw(name.data(), name.size());
    for (const auto& name : source.englishMorphNames)
        w.raw(name.data(), name.size());
    for (const auto& name : source.englishGroupNames)
        w.raw(name.data(), name.size());
    for (const auto& name : source.toonNames)
        w.raw(name.data(), name.size());
    w.integer(source.rigids.size(), 4);
    for (const auto& r : source.rigids) {
        w.raw(r.pmdName, 20);
        w.integer(std::uint16_t(r.boneIndex), 2);
        w.integer(r.group, 1);
        w.integer(r.noCollapse, 2);
        w.integer(r.shape, 1);
        w.numbers(r.size, 3);
        w.numbers(r.position, 3);
        w.numbers(r.rotation, 3);
        w.number(r.mass);
        w.number(r.linearDamping);
        w.number(r.angularDamping);
        w.number(r.restitution);
        w.number(r.friction);
        w.integer(r.mode, 1);
    }
    w.integer(source.joints.size(), 4);
    for (const auto& j : source.joints) {
        w.raw(j.pmdName, 20);
        w.integer(std::uint32_t(j.rigidA), 4);
        w.integer(std::uint32_t(j.rigidB), 4);
        w.numbers(j.position, 3);
        w.numbers(j.rotation, 3);
        w.numbers(j.limits + 3, 3);
        w.numbers(j.limits, 3);
        w.numbers(j.limits + 9, 3);
        w.numbers(j.limits + 6, 3);
        w.numbers(j.springs, 6);
    }
    Cursor cursor{w.bytes};
    ModelDocument checked;
    if (!ReadModelDocument(Cursor::Read, &cursor, checked) || cursor.offset != w.bytes.size())
        return fail("pmd-output-invalid");
    output = std::move(w.bytes);
    reason.clear();
    return true;
}
}
