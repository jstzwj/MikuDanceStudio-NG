#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/clipboard_layout.hpp"
#include "bone_clipboard_fixture.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
static void Check(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
using namespace format;
template <class Out, class In> Out BoneKey(const In& key) {
    Out result{};
#define FIELD(name) std::memcpy(&result.name, &key.name, sizeof(result.name))
    FIELD(frame);
    FIELD(previous);
    FIELD(next);
    FIELD(interpolation);
    FIELD(position);
    FIELD(rotation);
    FIELD(allocated);
    FIELD(physicsDisabled);
#undef FIELD
    return result;
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 2, "provide original x64 executable");
    OriginalPhysicsModule original(argv[1]);
    unsigned count = 0, fixedCount = 0, selectedCount = 0;
    float fixedError = 0;
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned order = 0; order < 3; ++order)
        for (unsigned channel : {0, 1})
            for (unsigned kind : {0, 1, 2, 3})
                for (bool mirror : {false, true})
                    for (bool selectedTarget : {false, true})
                        for (unsigned frame : {0, 1, 4, 15, 29, 30, 40}) {
                            if (mirror && selectedTarget)
                                continue;
                            const bool writePosition = kind == 1;
                            auto track = bone_clipboard_fixture::Make(order);
                            auto model = std::make_unique<mdl::ModelRecord>();
                            std::array<mdl::BoneRecord, 3> bones{};
                            std::vector<mdl::BoneKey> records(mdl::kBoneKeyCapacity);
                            std::array<mdl::BoneKeyUndoEntry, 3> saved{};
                            model->boneCount = 3;
                            model->boneTable = bones.data();
                            model->selectedBone = int(channel);
                            model->boneKeys = records.data();
                            model->searchCursor = 3;
                            model->maxFrame = 9;
                            model->undoRings[0].slots[0].keySnapshots = saved.data();
                            for (const auto& [index, key] : frame_line_fixture::Records(track))
                                records[index] = BoneKey<mdl::BoneKey>(key);
                            for (const auto& [index, key] : track.unused)
                                records[index] = BoneKey<mdl::BoneKey>(key);
                            std::strcpy(bones[channel].name, "target");
                            bones[channel].type =
                                writePosition ? mdl::BoneType::RotateMove : mdl::BoneType::UnderIk;
                            float axis[3]{.25f, .5f, -1};
                            if (kind >= 2) {
                                bones[channel].type =
                                    kind == 2 ? mdl::BoneType::FixedAxis : mdl::BoneType::UnderIk;
                                bones[channel].flags = cpu_bone::kBoneFlagFixedAxis;
                                std::copy_n(axis, 3, bones[channel].axis);
                                model->physicsMode = 2;
                                if (order == 1) {
                                    model->physicsMode = 0;
                                    bones[channel].tailBone = 2;
                                    std::copy_n(axis, 3, bones[2].position);
                                    const float length = std::sqrt(1.3125f);
                                    for (auto& component : axis)
                                        component /= length;
                                }
                            }
                            const auto offset = std::min(frame, 3u);
                            const auto copied = bone_clipboard_fixture::Copied(frame - offset);
                            mdl::BoneClipboardRecord input{};
                            std::strcpy(input.name,
                                        selectedTarget ? "absent-source-name" : "target");
                            input.frame = copied.frame;
                            std::memcpy(input.position, copied.position.data(), 12);
                            std::memcpy(input.rotation, copied.rotation.data(), 16);
                            std::memcpy(input.interpolation, copied.interpolation.data(), 16);
                            input.physicsDisabled = copied.physicsDisabled;
                            std::map<std::uint32_t, PmmBoneKey> captured;
                            unsigned maximum = 9;
                            auto corrected = copied;
                            if (kind >= 2)
                                Check(
                                    ConstrainPmmClipboardRotation(corrected.rotation, axis, mirror),
                                    "portable fixed axis");
                            Check(RegisterPmmBoneClipboard(track, channel, corrected, offset,
                                                           writePosition, mirror, captured,
                                                           maximum),
                                  "portable clipboard registration");
                            const int ok =
                                mirror ? original.Function<int (*)(mdl::ModelRecord*,
                                                                   mdl::BoneClipboardRecord*, int)>(
                                             0xc9db0)(model.get(), &input, offset)
                                       : original.Function<int (*)(mdl::ModelRecord*,
                                                                   mdl::BoneClipboardRecord*, int,
                                                                   unsigned char)>(0xc9390)(
                                             model.get(), &input, offset, selectedTarget ? 1 : 0);
                            Check(ok && model->maxFrame == maximum, "native clipboard maximum");
                            auto expected = frame_line_fixture::Records(track);
                            expected.insert(track.unused.begin(), track.unused.end());
                            for (const auto& [index, key] : expected) {
                                auto actual = BoneKey<PmmBoneKey>(records[index]);
                                if (kind >= 2) {
                                    for (unsigned component = 0; component < 4; ++component) {
                                        const auto error = std::abs(actual.rotation[component] -
                                                                    key.rotation[component]);
                                        Check(std::isfinite(error) && error <= 2e-6f,
                                              "native fixed-axis rotation");
                                        fixedError = std::max(fixedError, error);
                                    }
                                    actual.rotation = key.rotation;
                                }
                                Check(frame_line_fixture::Bytes(key) ==
                                          frame_line_fixture::Bytes(actual),
                                      "native clipboard slot payload");
                            }
                            const auto& undo = model->undoRings[0].slots[0];
                            Check(undo.recordCount == captured.size(),
                                  "native clipboard capture count");
                            for (unsigned i = 0; i < undo.recordCount; ++i)
                                Check(frame_line_fixture::Bytes(captured.at(saved[i].index)) ==
                                          frame_line_fixture::Bytes(
                                              BoneKey<PmmBoneKey>(saved[i].key)),
                                      "native clipboard capture payload");
                            if (selectedTarget)
                                ++selectedCount;
                            else if (kind < 2) {
                                bone_clipboard_fixture::Hash(hash, track, captured, maximum);
                                ++count;
                            } else
                                ++fixedCount;
                        }
    std::printf("%u native clipboard tables/captures, hash %016llx; no window\n", count,
                static_cast<unsigned long long>(hash));
    std::printf("%u fixed-axis cases, rotation max error %.9g (limit 2e-6); other fields exact\n",
                fixedCount, fixedError);
    for (const auto& input : bone_clipboard_fixture::Names()) {
        auto model = std::make_unique<mdl::ModelRecord>();
        std::vector<mdl::BoneRecord> bones(input.names.size());
        std::vector<mdl::BoneKey> keys(input.names.size());
        mdl::BoneKeyUndoEntry saved{};
        model->boneCount = unsigned(bones.size());
        model->boneTable = bones.data();
        model->boneKeys = keys.data();
        model->undoRings[0].slots[0].keySnapshots = &saved;
        for (unsigned i = 0; i < bones.size(); ++i)
            std::strcpy(bones[i].name, input.names[i].c_str());
        mdl::BoneClipboardRecord record{};
        std::strcpy(record.name, input.copied.c_str());
        record.rotation[3] = 1;
        Check(original.Function<int (*)(mdl::ModelRecord*, mdl::BoneClipboardRecord*, int)>(
                  0xc9db0)(model.get(), &record, 0),
              "native mirror name");
        int selected = -1;
        for (unsigned i = 0; i < keys.size(); ++i)
            if (keys[i].allocated)
                selected = int(i);
        Check(selected == input.target &&
                  selected == FindPmmClipboardBone(
                                  input.copied, input.names.size(),
                                  [&](std::size_t i) { return input.names[i]; }, true),
              "native mirrored-name precedence");
    }
    std::printf("7 exact native mirrored-name cases\n");
    std::printf("%u selected-target clipboard cases (source name ignored)\n", selectedCount);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
