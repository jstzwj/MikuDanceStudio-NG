#pragma once
#include "editor_model_clipboard_chain_fixture.hpp"

namespace {
unsigned ModelClipboardChains(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    // Only presentation and pose evaluation are isolated. The dispatcher,
    // copying, complete allocation loops and all three registrars execute.
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch controls(original.Function<void*>(0x62bb0), &ret, 1);
    Patch seek(original.Function<void*>(0xcbd90), &ret, 1);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    auto release = *original.Function<void (**)(void*)>(0x128450);
    unsigned checkpoints = 0;
    std::uint64_t hash = 14695981039346656037ull;
    for (const char* file : {"facial.pmd", "tint-morph.pmx", "articulated.pmd", "articulated.pmx"})
        for (bool reversed : {false, true}) {
            EditorSession portable;
            std::string reason;
            Check(model_clipboard_chain_fixture::Initialize(portable, root, file, reversed, reason),
                  reason.c_str());
            const auto source = portable.Document().models[0];
            NativeAuxiliary native(source,
                                   portable.Runtime().Resource(0)->SourceKind() == ModelKind::Pmx);
            auto* model = native.model.get();
            std::vector<mdl::BoneRecord> poses(source.bonePose.size());
            std::vector<unsigned char> edited(poses.size());
            std::vector<mdl::BoneKey> bones(mdl::kBoneKeyCapacity);
            std::array<mdl::BoneReference, mdl::kDisplayKeyCapacity> parents{};
            model->boneCount = unsigned(poses.size());
            model->boneTable = poses.data();
            model->boneKeys = bones.data();
            model->bonePhysicsState = edited.data();
            std::array<mdl::BoneOrderEntry, 1> order{};
            model->boneOrderTable = order.data();
            model->boneOrderCount = 1; // Native loader's implicit root selector.
            model->physicsFlags = 1;
            model->maxFrame = source.selection.maxFrame;
            for (unsigned i = 0; i < poses.size(); ++i) {
                Check(source.boneNames[i].size() <= sizeof poses[i].name, "bone name capacity");
                std::copy(source.boneNames[i].begin(), source.boneNames[i].end(), poses[i].name);
                int mapped;
                Check(portable.Runtime().DocumentBoneIndex(0, int(i), mapped, reason),
                      reason.c_str());
                const auto& info = portable.Runtime().Resource(0)->EditingBones()[mapped];
                poses[i].type = static_cast<mdl::BoneType>(info.type);
                poses[i].flags = info.flags;
            }
            for (unsigned i = 0; i < native.poses.size(); ++i) {
                Check(source.morphNames[i].size() <= sizeof native.poses[i].name,
                      "Morph name capacity");
                std::copy(source.morphNames[i].begin(), source.morphNames[i].end(),
                          native.poses[i].name);
            }
            for (unsigned i = 0; i < native.chains.size(); ++i)
                native.chains[i].boneIndex = source.ikSources[i];
            const auto loadBones = [&](auto& pool, const auto& track) {
                const auto put = [&](unsigned index, const auto& key) {
                    auto& out = pool.at(index);
                    out = {};
                    out.frame = key.frame;
                    out.previous = key.previous;
                    out.next = key.next;
                    std::memcpy(out.position, key.position.data(), sizeof out.position);
                    std::memcpy(out.rotation, key.rotation.data(), sizeof out.rotation);
                    std::memcpy(out.interpolation, key.interpolation.data(),
                                sizeof out.interpolation);
                    out.allocated = key.allocated;
                    out.physicsDisabled = key.physicsDisabled;
                };
                for (const auto& [index, key] : frame_line_fixture::Records(track))
                    put(index, key);
                for (const auto& [index, key] : track.unused)
                    put(index, key);
            };
            loadBones(bones, source.boneKeys);
            for (const auto& [index, key] : frame_line_fixture::Records(source.displayKeys)) {
                auto& out = native.displays[index];
                out.frame = key.frame;
                out.previous = key.previous;
                out.next = key.next;
                out.visible = key.visible;
                out.allocated = key.allocated;
                native.ik[index] = key.ikEnabled;
                out.ikStates = native.ik[index].data();
            }
            for (unsigned i = 0; i < parents.size(); ++i) {
                parents[i].modelIndex = -1;
                native.displays[i].selectorStates = &parents[i];
            }
            std::memset(app.get(), 0, sizeof(*app));
            app->modelSlots[0] = model;
            app->lastRegisteredFrame = portable.Document().timeline.lastRegisteredFrame;
            auto* raw = reinterpret_cast<unsigned char*>(app.get());
            raw[0xa1b90] = 1;
            struct Owned {
                MMDAppState& app;
                mdl::ModelRecord& model;
                void (*release)(void*);
                ~Owned() {
                    for (auto& ring : model.undoRings)
                        for (auto& entry : ring.slots) {
                            release(entry.bonePose);
                            release(entry.keySnapshots);
                        }
                    const auto slot = [&](unsigned offset) {
                        void* value;
                        std::memcpy(&value, reinterpret_cast<unsigned char*>(&app) + offset,
                                    sizeof value);
                        return value;
                    };
                    auto* displays = static_cast<mdl::DisplayClipboardRecord*>(slot(0x3a0));
                    for (unsigned i = 0; i < app.reservedClipboardCounts.displays; ++i) {
                        release(displays[i].ikStates);
                        release(displays[i].selectorStates);
                    }
                    release(slot(0x390));
                    release(slot(0x398));
                    release(slot(0x3a0));
                }
            } owned{*app, *model, release};
            const auto clipboard = [&] {
                std::vector<std::uint8_t> bytes;
                const auto append = [&](const void* data, unsigned count, unsigned size) {
                    if (!count)
                        return;
                    Check(data != nullptr, "native clipboard payload lifetime");
                    const auto* begin = static_cast<const std::uint8_t*>(data);
                    bytes.insert(bytes.end(), begin, begin + std::size_t(count) * size);
                };
                const auto read = [&](unsigned offset, unsigned count, unsigned size) {
                    void* value;
                    std::memcpy(&value, raw + offset, sizeof value);
                    frame_line_fixture::Bytes(bytes, count);
                    frame_line_fixture::Bytes(bytes, std::uintptr_t(value));
                    append(value, count, size);
                    return value;
                };
                const auto& counts = app->reservedClipboardCounts;
                read(0x390, counts.bones, sizeof(mdl::BoneClipboardRecord));
                read(0x398, counts.morphs, sizeof(mdl::MorphClipboardRecord));
                const auto* displays = static_cast<mdl::DisplayClipboardRecord*>(
                    read(0x3a0, counts.displays, sizeof(mdl::DisplayClipboardRecord)));
                for (unsigned i = 0; i < counts.displays; ++i) {
                    append(displays[i].ikStates, displays[i].ikCount,
                           sizeof(mdl::IkClipboardState));
                    append(displays[i].selectorStates, displays[i].selectorCount,
                           sizeof(mdl::SelectorClipboardState));
                }
                return bytes;
            };
            for (const auto& command : clipboard_chain_fixture::Commands()) {
                if (command.copy) {
                    const auto selection =
                        model_clipboard_chain_fixture::Selection(portable, command);
                    const auto mark = [&](auto& pool, Track kind, unsigned roots) {
                        for (unsigned i = 0; i < pool.size(); ++i) {
                            if (i >= roots && !pool[i].frame) {
                                pool[i].allocated = 0;
                                continue;
                            }
                            auto owner = i;
                            while (owner >= roots)
                                owner = pool[owner].previous;
                            pool[i].allocated = std::uint8_t(selection.count(
                                {kind, 0, kind == Track::Display ? 0 : owner, pool[i].frame}));
                        }
                    };
                    mark(bones, Track::Bone, unsigned(poses.size()));
                    mark(native.morphs, Track::Morph, unsigned(source.morphPose.size()));
                    mark(native.displays, Track::Display, 1);
                } else
                    app->currentFrame = int(command.destination);
                std::printf("model chain file=%s reversed=%u checkpoint=%u copy=%u\n", file,
                            reversed, checkpoints, command.copy);
                const auto beforeClipboard = clipboard();
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(
                    nullptr, command.copy ? panel::kCopyFramesButton : panel::kPasteButton);
                if (!command.copy)
                    Check(beforeClipboard == clipboard(),
                          "native model paste mutated valid clipboard");
                else {
                    const auto selection =
                        model_clipboard_chain_fixture::Selection(portable, command);
                    const auto count = [&](Track kind) {
                        return std::count_if(selection.begin(), selection.end(),
                                             [&](const auto& key) { return key.track == kind; });
                    };
                    const auto& counts = app->reservedClipboardCounts;
                    Check(counts.bones == count(Track::Bone) &&
                              counts.morphs == count(Track::Morph) &&
                              counts.displays == count(Track::Display),
                          "native model clipboard copied counts differ");
                }
                const bool executed =
                    model_clipboard_chain_fixture::Execute(portable, command, reason);
                if (!executed)
                    std::fprintf(stderr, "portable chain command failed: %s\n", reason.c_str());
                Check(executed, reason.c_str());
                const auto& expected = portable.Document().models[0];
                std::vector<mdl::BoneKey> expectedBones(bones.size());
                loadBones(expectedBones, expected.boneKeys);
                Check(!std::memcmp(bones.data(), expectedBones.data(),
                                   bones.size() * sizeof(bones[0])),
                      "native complete chained bone pool differs");
                std::vector<mdl::MorphKey> expectedMorphs(native.morphs.size());
                const auto putMorph = [&](unsigned index, const auto& key) {
                    auto& out = expectedMorphs.at(index);
                    out.frame = key.frame;
                    out.previous = key.previous;
                    out.next = key.next;
                    out.value = key.value;
                    out.allocated = key.allocated;
                };
                for (const auto& [index, key] : frame_line_fixture::Records(expected.morphKeys))
                    putMorph(index, key);
                for (const auto& [index, key] : expected.morphKeys.unused)
                    putMorph(index, key);
                Check(!std::memcmp(native.morphs.data(), expectedMorphs.data(),
                                   expectedMorphs.size() * sizeof(expectedMorphs[0])),
                      "native complete chained Morph pool differs");
                std::vector<PmmDisplayKey> expectedDisplays(native.displays.size());
                for (const auto& [index, key] : frame_line_fixture::Records(expected.displayKeys))
                    expectedDisplays.at(index) = key;
                for (const auto& [index, key] : expected.displayKeys.unused)
                    expectedDisplays.at(index) = key;
                for (unsigned i = 0; i < native.displays.size(); ++i) {
                    const auto& a = native.displays[i];
                    const auto& b = expectedDisplays[i];
                    Check(a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
                              a.visible == b.visible && a.allocated == b.allocated,
                          "native complete chained display pool differs");
                    for (unsigned chain = 0; chain < native.chains.size(); ++chain)
                        Check(native.ik[i][chain] ==
                                  (chain < b.ikEnabled.size() ? b.ikEnabled[chain] : 0),
                              "native chained display IK payload differs");
                    Check(a.selectorStates == &parents[i] && parents[i].modelIndex == -1 &&
                              parents[i].boneIndex == 0,
                          "native implicit root selector lifetime");
                }
                Check(model->maxFrame == expected.selection.maxFrame,
                      "native chained model maximum differs");
                model_clipboard_chain_fixture::Hash(hash, expected);
                ++checkpoints;
            }
        }
    std::printf("Original model clipboard chain frozen hash: %016llx\n",
                static_cast<unsigned long long>(hash));
    Check(hash == 0x88478b1f491ed0bfull, "native model clipboard chain frozen result differs");
    return checkpoints;
}
} // namespace
