#include <Windows.h>
#include <commctrl.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/app_layout.hpp"
#include "editor_physics_selection_fixture.hpp"
#include "editor_reduction_fixture.hpp"
#include "editor_morph_scale_fixture.hpp"
#include "editor_camera_scale_fixture.hpp"
#include "gravity_edit_reference.hpp"
#include "mikudancestudio/gravity_edit.hpp"
#include "mikudancestudio/camera_key_math.hpp"
#include "mikudancestudio/cpu_model_state.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
using namespace format;
using namespace editor;
namespace {
class Patch {
    unsigned char* address_;
    std::vector<unsigned char> before_;
    void Write(const void* bytes) {
        DWORD old;
        Check(VirtualProtect(address_, before_.size(), PAGE_EXECUTE_READWRITE, &old),
              "patch protection");
        std::memcpy(address_, bytes, before_.size());
        FlushInstructionCache(GetCurrentProcess(), address_, before_.size());
        VirtualProtect(address_, before_.size(), old, &old);
    }

public:
    Patch(void* address, const void* bytes, std::size_t count)
        : address_(static_cast<unsigned char*>(address)), before_(count) {
        std::memcpy(before_.data(), address_, count);
        Write(bytes);
    }
    ~Patch() { Write(before_.data()); }
};
std::map<int, std::string> resetTexts;
std::set<int> resetSliders;
HWND WINAPI ResetChild(HWND, int id) {
    return reinterpret_cast<HWND>(std::uintptr_t(id));
}
BOOL WINAPI ResetText(HWND window, LPCSTR text) {
    resetTexts.emplace(int(reinterpret_cast<std::uintptr_t>(window)), text);
    return TRUE;
}
LRESULT WINAPI ResetSlider(HWND window, UINT message, WPARAM redraw, LPARAM position) {
    Check(message == 0x405 && redraw == 1 && position == 0, "native reset slider arguments");
    resetSliders.insert(int(reinterpret_cast<std::uintptr_t>(window)));
    return 0;
}
struct NativeAuxiliary {
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::vector<mdl::MorphRecord> poses;
    std::vector<mdl::MorphKey> morphs{mdl::kMorphKeyCapacity};
    std::vector<mdl::DisplayKey> displays{mdl::kDisplayKeyCapacity};
    std::vector<mdl::IkChain> chains;
    std::vector<std::vector<std::uint8_t>> ik{mdl::kDisplayKeyCapacity};
    explicit NativeAuxiliary(const PmmSceneModel& source, bool pmx)
        : poses(source.morphPose.size()), chains(source.ikPose.size()) {
        model->physicsMode = pmx ? 2 : 0;
        model->morphCount = unsigned(poses.size());
        model->morphs = poses.data();
        model->morphKeys = morphs.data();
        model->displayKeys = displays.data();
        model->ikChains = chains.data();
        model->ikChainCount = unsigned(chains.size());
        model->UndoCursor() = model->LatestUndoCursor() = 29;
        Check(source.displayKeys.dense[0].rigidStates.empty(), "fixture has no parent bindings");
        for (unsigned i = 0; i < displays.size(); ++i) {
            ik[i].resize(chains.size());
            displays[i].ikStates = ik[i].data();
        }
        for (unsigned i = 0; i < source.morphKeys.dense.size(); ++i) {
            morphs[i].value = source.morphKeys.dense[i].value;
            morphs[i].allocated = source.morphKeys.dense[i].allocated;
        }
        const auto& display = source.displayKeys.dense[0];
        displays[0].visible = display.visible;
        displays[0].allocated = display.allocated;
        std::copy(display.ikEnabled.begin(), display.ikEnabled.end(), ik[0].begin());
    }
    void Register(OriginalPhysicsModule& original, const PmmSceneModel& source, Track kind,
                  unsigned frame, unsigned channel = 0) {
        model->loadComplete = source.selection.loadComplete;
        model->maxFrame = source.selection.maxFrame;
        for (unsigned i = 0; i < poses.size(); ++i)
            poses[i].value = source.morphPose[i];
        for (unsigned i = 0; i < chains.size(); ++i)
            chains[i].enabled = source.ikPose[i];
        // The command dispatcher clears key marks before calling these kernels.
        // This harness isolates registration; it does not execute the UI tail.
        for (auto& key : morphs)
            key.allocated = 0;
        for (auto& key : displays)
            key.allocated = 0;
        if (kind == Track::Morph)
            original.Function<void (*)(mdl::ModelRecord*, int, int)>(0xca9c0)(
                model.get(), int(channel), int(frame));
        else
            original.Function<void (*)(mdl::ModelRecord*, int)>(0xcaf80)(model.get(), int(frame));
    }
    void Compare(const PmmSceneModel& source) const {
        Check(model->maxFrame == source.selection.maxFrame, "native maximum frame differs");
        Check(model->UndoCursor() == 29 && model->LatestUndoCursor() == 29,
              "auxiliary registration altered native bone history");
        std::map<unsigned, PmmMorphKey> expectedMorphs;
        for (unsigned i = 0; i < source.morphKeys.dense.size(); ++i)
            expectedMorphs[i] = source.morphKeys.dense[i];
        for (const auto& key : source.morphKeys.sparse)
            expectedMorphs[unsigned(key.index)] = key.value;
        for (unsigned i = 0; i < morphs.size(); ++i) {
            const auto& actual = morphs[i];
            const auto found = expectedMorphs.find(i);
            Check((i < poses.size() || actual.frame != 0) == (found != expectedMorphs.end()),
                  "native morph slot differs");
            if (found == expectedMorphs.end())
                continue;
            const auto& expected = found->second;
            Check(actual.frame == expected.frame && actual.previous == expected.previous &&
                      actual.next == expected.next && actual.allocated == expected.allocated &&
                      !std::memcmp(&actual.value, &expected.value, sizeof(float)),
                  "native morph registration differs");
        }
        std::map<unsigned, PmmDisplayKey> expectedDisplays{{0, source.displayKeys.dense[0]}};
        for (const auto& key : source.displayKeys.sparse)
            expectedDisplays[unsigned(key.index)] = key.value;
        for (unsigned i = 0; i < displays.size(); ++i) {
            const auto& actual = displays[i];
            const auto found = expectedDisplays.find(i);
            Check((i == 0 || actual.frame != 0) == (found != expectedDisplays.end()),
                  "native display slot differs");
            if (found == expectedDisplays.end())
                continue;
            const auto& expected = found->second;
            Check(actual.frame == expected.frame && actual.previous == expected.previous &&
                      actual.next == expected.next && actual.allocated == expected.allocated &&
                      actual.visible == expected.visible && ik[i] == expected.ikEnabled,
                  "native display registration differs");
        }
    }
};
unsigned ResetCommand(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    void* child = reinterpret_cast<void*>(&ResetChild);
    void* text = reinterpret_cast<void*>(&ResetText);
    void* slider = reinterpret_cast<void*>(&ResetSlider);
    for (const auto& item : {std::pair{0x128658u, "GetDlgItem"},
                             {0x128598u, "SetWindowTextA"},
                             {0x1287b8u, "SendMessageA"}})
        Check(*original.Function<void**>(item.first) ==
                  reinterpret_cast<void*>(
                      GetProcAddress(GetModuleHandleW(L"user32.dll"), item.second)),
              "reset command import identity");
    Patch a(original.Function<void*>(0x128658), &child, sizeof child);
    Patch b(original.Function<void*>(0x128598), &text, sizeof text);
    Patch c(original.Function<void*>(0x1287b8), &slider, sizeof slider);
    const unsigned char expected[]{0x48, 0x8b, 0x8d, 0xb0, 0x0b, 0, 0};
    Check(!std::memcmp(original.Function<void*>(0x53de6), expected, sizeof expected),
          "dispatcher epilogue identity");
    // The extracted branch calls Win32 with RSP aligned as in the dispatcher,
    // and changes only RBX/RDI/R12 among nonvolatile registers. Jump into it:
    // its outgoing shadow space must not contain a wrapper return address.
    std::vector<unsigned char> code{0x53, 0x57, 0x41, 0x54, 0x48, 0x83, 0xec, 0x20,
                                    0x48, 0x8b, 0xd9, 0x45, 0x33, 0xe4, 0x48, 0xb8};
    const auto entry = reinterpret_cast<std::uintptr_t>(original.Function<void*>(0x522f8));
    for (unsigned shift = 0; shift < 64; shift += 8)
        code.push_back(std::uint8_t(entry >> shift));
    code.push_back(0xff);
    code.push_back(0xe0);
    const auto returnOffset = code.size();
    const unsigned char done[]{0x48, 0x83, 0xc4, 0x20, 0x41, 0x5c, 0x5f, 0x5b, 0xc3};
    code.insert(code.end(), std::begin(done), std::end(done));
    void* memory = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(memory != nullptr, "reset wrapper allocation");
    std::memcpy(memory, code.data(), code.size());
    DWORD previous;
    Check(VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &previous),
          "reset wrapper executable");
    FlushInstructionCache(GetCurrentProcess(), memory, code.size());
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(memory) + returnOffset;
    std::vector<unsigned char> jump{0xff, 0x25, 0, 0, 0, 0};
    for (unsigned shift = 0; shift < 64; shift += 8)
        jump.push_back(std::uint8_t(returnAddress >> shift));
    Patch tail(original.Function<void*>(0x53de6), jump.data(), jump.size());
    const auto execute = reinterpret_cast<void (*)(void*)>(memory);
    unsigned count = 0;
    for (const char* file : {"facial.pmd", "tint-morph.pmx"})
        for (bool dirty : {false, true}) {
            EditorSession portable;
            std::string reason;
            Check(portable.ImportModel(root / file, reason), reason.c_str());
            const auto channels = portable.Document().models[0].morphPose.size();
            for (unsigned i = 0; i < channels; ++i)
                Check(portable.SetMorph(0, i, float(i + 1) / 8, reason), reason.c_str());
            if (!dirty)
                Check(portable.Save(
                          root / "reset.pmm",
                          [](const auto& path, const auto& bytes, auto&) {
                              std::ofstream out(path, std::ios::binary);
                              out.write(reinterpret_cast<const char*>(bytes.data()),
                                        std::streamsize(bytes.size()));
                              return bool(out);
                          },
                          reason),
                      reason.c_str());
            NativeAuxiliary native(portable.Document().models[0], true);
            for (unsigned i = 0; i < channels; ++i)
                native.poses[i].value = portable.Document().models[0].morphPose[i];
            std::vector<unsigned char> app(0xa3000, dirty ? 1 : 0);
            app[0x328] = 0;
            app[0x13e0] = 7;
            auto* model = native.model.get();
            std::memcpy(app.data() + 0xbe8 + 7 * 8, &model, sizeof model);
            const auto before = app;
            std::vector<unsigned char> modelBefore(sizeof *model);
            std::memcpy(modelBefore.data(), model, modelBefore.size());
            resetTexts.clear();
            resetSliders.clear();
            execute(app.data());
            Check(app == before && !std::memcmp(model, modelBefore.data(), modelBefore.size()),
                  "reset changed native document/history flags");
            Check(resetTexts ==
                      (std::map<int, std::string>{
                          {506, "0.00000"}, {511, "0.00000"}, {516, "0.00000"}, {521, "0.00000"}}),
                  "native reset edit texts");
            Check(resetSliders == (std::set<int>{505, 510, 515, 520}),
                  "native reset slider identities");
            Check(portable.ResetMorphs(0, reason), reason.c_str());
            Check(portable.Dirty() == dirty, "portable reset changed dirty state");
            for (unsigned i = 0; i < channels; ++i)
                Check(native.poses[i].value == 0 &&
                          portable.Document().models[0].morphPose[i] == native.poses[i].value,
                      "native reset weight differs");
            ++count;
        }
    VirtualFree(memory, 0, MEM_RELEASE);
    return count;
}
unsigned undoCalls = 0;
void CaptureUndo(void*) {
    ++undoCalls;
}
unsigned FrameRateCommands(OriginalPhysicsModule& original) {
    auto state = std::make_unique<MMDAppState>();
    auto* pointer = state.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const auto execute = original.Function<void (*)(HWND, WPARAM)>(0x3f550);
    unsigned count = 0;
    for (float previous : {30.f, 60.f, 1000.f})
        for (int command : {MDS_CMD_FPS_UNLIMITED, MDS_CMD_FPS_30, MDS_CMD_FPS_60}) {
            std::memset(state.get(), 0, sizeof(*state));
            state->fpsLimit = previous;
            state->sceneModified = 0x7d;
            state->physicsResetPending = 1;
            state->currentFrame = 123;
            std::vector<unsigned char> expected(sizeof(*state));
            std::memcpy(expected.data(), state.get(), expected.size());
            const float target = command == MDS_CMD_FPS_30   ? 30.f
                                 : command == MDS_CMD_FPS_60 ? 60.f
                                                             : 1000.f;
            std::memcpy(expected.data() + offsetof(MMDAppState, fpsLimit), &target, sizeof target);
            // No original entrypoint, window, device or simulation is involved.
            execute(nullptr, command);
            Check(!std::memcmp(state.get(), expected.data(), expected.size()),
                  "native FPS command changed fields other than the display rate");
            ++count;
        }
    return count;
}
unsigned SceneCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    // Execute the full original dispatcher with a private application record.
    // Stub only window refresh and history capture; the actual quaternion
    // conversion, selection loop and deferred-reset writes execute unchanged.
    const unsigned char ret = 0xc3;
    Patch refresh(original.Function<void*>(0x20dd0), &ret, 1);
    Patch readouts(original.Function<void*>(0x20ce0), &ret, 1);
    Patch timeline(original.Function<void*>(0x5f7f0), &ret, 1);
    std::vector<unsigned char> jump{0xff, 0x25, 0, 0, 0, 0};
    const auto capture = reinterpret_cast<std::uintptr_t>(&CaptureUndo);
    for (unsigned shift = 0; shift < 64; shift += 8)
        jump.push_back(std::uint8_t(capture >> shift));
    Patch undo(original.Function<void*>(0x26fd0), jump.data(), jump.size());
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const auto execute = original.Function<void (*)(HWND, WPARAM)>(0x3f550);
    unsigned count = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx", "triangle.pmd"})
        for (int choice : {-1, 0, 1}) {
            EditorSession portable;
            std::string reason;
            Check(portable.ImportModel(root / file, reason), reason.c_str());
            const auto resource = portable.Runtime().Resource(0);
            const auto bonesCount = unsigned(resource->BoneNames().size());
            const int active = choice == 1 ? int(bonesCount - 1) : choice;
            Check(portable.SelectBones({0}, active, reason), reason.c_str());
            if (active >= 0)
                Check(portable.SetBone(0, unsigned(active), {2, 3, 4}, {0, .6f, 0, .8f}, reason),
                      reason.c_str());
            NativeAuxiliary native(portable.Document().models[0],
                                   resource->SourceKind() == ModelKind::Pmx);
            auto* model = native.model.get();
            std::vector<mdl::BoneRecord> bones(bonesCount);
            std::vector<unsigned char> selected(bonesCount), edited(bonesCount);
            selected[0] = 1;
            model->boneTable = bones.data();
            model->boneCount = bonesCount;
            model->boneSelection = selected.data();
            model->bonePhysicsState = edited.data();
            model->selectedBone = active;
            for (unsigned i = 0; i < bonesCount; ++i) {
                const auto& pose = portable.Document().models[0].bonePose[i];
                std::copy_n(pose.components.begin(), 3, bones[i].trans);
                std::copy_n(pose.components.begin() + 3, 4, bones[i].rotQuat);
                edited[i] = pose.flags[1];
                bones[i].hasRigidBody = resource->EditingBones()[i].hasRigidBody;
            }
            std::fill(app.begin(), app.end(), 0);
            std::memcpy(app.data() + 0xbe8, &model, sizeof model);
            undoCalls = 0;
            const auto frame = portable.Runtime().Counters().frames;
            execute(nullptr, MDS_CMD_RESET_BONE_CAMERA_ROTATION);
            Check(portable.ResetRotation(reason), reason.c_str());
            Check(undoCalls == unsigned(active >= 0), "native rotation undo capture count");
            Check(app[0xa1b31] == unsigned(active >= 0), "native rotation dirty gate");
            for (unsigned i = 0; i < bonesCount; ++i) {
                const auto& pose = portable.Document().models[0].bonePose[i];
                Check(!std::memcmp(bones[i].trans, pose.components.data(), 12) &&
                          !std::memcmp(bones[i].rotQuat, pose.components.data() + 3, 16) &&
                          edited[i] == pose.flags[1],
                      "native rotation reset pose");
            }
            ++count;
            execute(nullptr, MDS_CMD_SELECT_PHYSICS_BONES);
            Check(portable.SelectPhysicsBones(reason), reason.c_str());
            for (unsigned i = 0; i < bonesCount; ++i)
                Check(bool(selected[i]) == (std::find(portable.Selected().bones.begin(),
                                                      portable.Selected().bones.end(),
                                                      i) != portable.Selected().bones.end()),
                      "native physics bone selection");
            Check(model->selectedBone == portable.ActiveBone(), "native selected physics bone");
            ++count;
            execute(nullptr, MDS_CMD_RESET_PHYSICS_BODIES);
            portable.ResetPhysicsBodies();
            Check(app[0x9fcc1] == portable.Runtime().Simulation().resetPending && app[0x9fcc1] == 1,
                  "native deferred physical reset");
            Check(frame == portable.Runtime().Counters().frames, "commands advanced physics");
            ++count;
            Check(portable.SelectModel(255, reason), reason.c_str());
            auto camera = portable.Runtime().Session()->Pose().camera;
            camera.position = {1, 2, 3};
            camera.rotation = {.2f, -.4f, .6f};
            camera.distance = -17;
            Check(portable.SetCamera(camera, reason), reason.c_str());
            app[0x328] = 1;
            app[0xa1b31] = 0;
            std::memcpy(app.data() + 0x348, camera.rotation.data(), 12);
            std::memcpy(app.data() + 0x36c, camera.position.data(), 12);
            std::memcpy(app.data() + 0xa1900, &camera.distance, 4);
            execute(nullptr, MDS_CMD_RESET_BONE_CAMERA_ROTATION);
            Check(portable.ResetRotation(reason), reason.c_str());
            const auto actual = portable.Runtime().Session()->Pose().camera;
            Check(!std::memcmp(app.data() + 0x348, actual.rotation.data(), 12) &&
                      !std::memcmp(app.data() + 0x36c, actual.position.data(), 12) &&
                      !std::memcmp(app.data() + 0xa1900, &actual.distance, 4) && app[0xa1b31] == 1,
                  "native camera rotation reset");
            ++count;
        }
    return count;
}
unsigned PhysicsKeyCommand(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const auto execute = original.Function<void (*)(HWND, WPARAM)>(0x3f550);
    unsigned cases = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx", "triangle.pmd"})
        for (bool reverse : {false, true})
            for (unsigned mode = 0; mode < 3; ++mode) {
                EditorSession portable;
                std::string reason;
                Check(PhysicsSelectionFixture(portable, root, file, reverse, mode, reason),
                      reason.c_str());
                const auto& source = portable.Document().models[0];
                const auto resource = portable.Runtime().Resource(0);
                NativeAuxiliary native(source, resource->SourceKind() == ModelKind::Pmx);
                auto* model = native.model.get();
                std::vector<mdl::BoneKey> keys(mdl::kBoneKeyCapacity);
                std::vector<mdl::BoneRecord> bones(source.bonePose.size());
                model->boneCount = unsigned(bones.size());
                model->boneTable = bones.data();
                model->boneKeys = keys.data();
                for (unsigned i = 0; i < bones.size(); ++i) {
                    int index;
                    Check(portable.Runtime().DocumentBoneIndex(0, int(i), index, reason),
                          reason.c_str());
                    bones[i].hasRigidBody =
                        resource->EditingBones()[std::size_t(index)].hasRigidBody;
                }
                const auto copy = [&](unsigned index, const auto& key) {
                    keys[index].frame = key.frame;
                    keys[index].previous = key.previous;
                    keys[index].next = key.next;
                    keys[index].physicsDisabled = key.physicsDisabled;
                    std::copy(key.position.begin(), key.position.end(), keys[index].position);
                    std::copy(key.rotation.begin(), key.rotation.end(), keys[index].rotation);
                };
                for (unsigned i = 0; i < source.boneKeys.dense.size(); ++i)
                    copy(i, source.boneKeys.dense[i]);
                for (const auto& key : source.boneKeys.sparse)
                    copy(unsigned(key.index), key.value);
                for (const auto& key : source.boneKeys.unused)
                    copy(key.first, key.second);
                for (auto& key : keys)
                    key.allocated = 1;
                for (auto& key : native.morphs)
                    key.allocated = 1;
                for (auto& key : native.displays)
                    key.allocated = 1;
                std::fill(app.begin(), app.end(), 0);
                std::memcpy(app.data() + 0xbe8, &model, sizeof model);
                execute(nullptr, MDS_CMD_SELECT_PHYSICS_ON_FRAMES);
                Check(portable.SelectPhysicsOnKeys(reason), reason.c_str());
                std::set<unsigned> marked;
                const auto& result = portable.Document().models[0].boneKeys;
                for (unsigned i = 0; i < result.dense.size(); ++i)
                    if (result.dense[i].allocated)
                        marked.insert(i);
                for (const auto& key : result.sparse)
                    if (key.value.allocated)
                        marked.insert(unsigned(key.index));
                for (unsigned i = 0; i < keys.size(); ++i) {
                    if (bool(keys[i].allocated) != (marked.count(i) != 0))
                        std::fprintf(stderr,
                                     "%s reverse=%u mode=%u record=%u native=%u portable=%u\n",
                                     file, unsigned(reverse), mode, i, unsigned(keys[i].allocated),
                                     unsigned(marked.count(i)));
                    Check(bool(keys[i].allocated) == (marked.count(i) != 0),
                          "native physics-key pool marks");
                }
                for (const auto& key : native.morphs)
                    Check(!key.allocated, "native physics-key morph sweep");
                for (const auto& key : native.displays)
                    Check(!key.allocated, "native physics-key display sweep");
                Check(!app[0xa1b31], "native physics-key selection does not dirty scene");
                ++cases;
            }
    return cases;
}
std::map<int, std::string> rangeFields;
int WINAPI RangeText(HWND window, LPSTR text, int capacity) {
    const auto& value = rangeFields.at(int(reinterpret_cast<std::uintptr_t>(window)));
    const auto count = std::min(value.size(), std::size_t(capacity - 1));
    std::memcpy(text, value.data(), count);
    text[count] = 0;
    return int(count);
}
unsigned RangeCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch controls(original.Function<void*>(0x62bb0), &ret, 1);
    auto child = &ResetChild;
    auto text = &RangeText;
    Patch getChild(original.Function<void*>(0x128658), &child, sizeof child);
    Patch getText(original.Function<void*>(0x128690), &text, sizeof text);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const auto execute = original.Function<void (*)(HWND, WPARAM)>(0x3f550);
    unsigned cases = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx"}) {
        EditorSession portable;
        std::string reason;
        Check(PhysicsSelectionFixture(portable, root, file, false, 1, reason), reason.c_str());
        const bool edited = portable.Edit(
            "detached-range-record",
            [](auto& document, auto&) {
                auto& track = document.models[0].boneKeys;
                auto key = track.dense[0];
                key.frame = 3;
                key.previous = key.next = 0;
                key.allocated = 0;
                track.sparse.push_back({30, 0, key});
                key.frame = 5; // overlaps the forward chain but owns its own mark
                track.sparse.push_back({31, 0, key});
                return true;
            },
            reason);
        Check(edited, reason.c_str());
        const auto& source = portable.Document().models[0];
        NativeAuxiliary native(source,
                               portable.Runtime().Resource(0)->SourceKind() == ModelKind::Pmx);
        auto* model = native.model.get();
        std::vector<mdl::BoneKey> bones(mdl::kBoneKeyCapacity);
        std::vector<mdl::BoneRecord> poses(source.bonePose.size());
        model->boneCount = unsigned(poses.size());
        model->boneTable = poses.data();
        model->boneKeys = bones.data();
        std::strcpy(poses[0].name, "selection-root");
        const auto copy = [](const auto& track, auto& pool) {
            const auto one = [&](unsigned index, const auto& key) {
                pool[index].frame = key.frame;
                pool[index].previous = key.previous;
                pool[index].next = key.next;
                pool[index].allocated = key.allocated;
            };
            for (unsigned i = 0; i < track.dense.size(); ++i)
                one(i, track.dense[i]);
            for (const auto& key : track.sparse)
                one(unsigned(key.index), key.value);
            for (const auto& key : track.unused)
                one(key.first, key.second);
        };
        copy(source.boneKeys, bones);
        copy(source.morphKeys, native.morphs);
        copy(source.displayKeys, native.displays);
        std::fill(app.begin(), app.end(), 0);
        std::memcpy(app.data() + 0xbe8, &model, sizeof model);
        struct Step {
            const char* name;
            unsigned first, last;
            KeyScope scope;
        };
        const Step steps[] = {
            {"selection-root", 5, 5, {KeyScopeKind::Track, {Track::Bone, 0, 0, 0}}},
            {"All frame", 2, 2, {}},
            {"All bone", 0, 0, {KeyScopeKind::Bones}},
            {"All facial", 5, 5, {KeyScopeKind::Morphs}},
            {"disp/IK/OP", 0, 0, {KeyScopeKind::Track, {Track::Display, 0, 0, 0}}},
            {"selection-root", 5, 5, {KeyScopeKind::Track, {Track::Bone, 0, 0, 0}}},
            {"All frame", 99, 99, {}},
            {"selection-root", 5, 5, {KeyScopeKind::Track, {Track::Bone, 0, 0, 0}}},
            {"selection-root", 3, 3, {KeyScopeKind::Track, {Track::Bone, 0, 0, 0}}},
            {"All frame", 3, 3, {}},
            {"All frame", 5, 2, {}}};
        const auto compare = [&](const auto& track, const auto& pool) {
            std::vector<unsigned char> expected(pool.size());
            for (unsigned i = 0; i < track.dense.size(); ++i)
                expected[i] = track.dense[i].allocated;
            for (const auto& key : track.sparse)
                expected[unsigned(key.index)] = key.value.allocated;
            for (const auto& key : track.unused)
                expected[key.first] = key.second.allocated;
            for (unsigned i = 0; i < pool.size(); ++i) {
                if (pool[i].allocated != expected[i])
                    std::fprintf(stderr, "%s step=%u record=%u original=%u portable=%u\n", file,
                                 cases, i, unsigned(pool[i].allocated), unsigned(expected[i]));
                Check(pool[i].allocated == expected[i], "native range pool marks");
            }
        };
        const auto revision = portable.Revision(), frames = portable.Runtime().Counters().frames;
        for (const auto& step : steps) {
            rangeFields = {{425, std::to_string(step.first)},
                           {426, std::to_string(step.last)},
                           {434, step.name}};
            execute(nullptr, 415);
            portable.SelectKeys(step.first, step.last, step.scope);
            const auto& result = portable.Document().models[0];
            compare(result.boneKeys, bones);
            compare(result.morphKeys, native.morphs);
            compare(result.displayKeys, native.displays);
            Check(!app[0xa1b31] && portable.Revision() == revision &&
                      portable.Runtime().Counters().frames == frames,
                  "range selection is metadata only");
            ++cases;
        }
    }
    return cases;
}
std::array<float, 3> nativeGravityVector;
unsigned nativeGravityWrites = 0;
void CaptureGravity(void*, const float* value) {
    std::copy_n(value, 3, nativeGravityVector.begin());
    ++nativeGravityWrites;
}
void WriteGravity(std::vector<unsigned char>& app, const PmmEvaluatedGravity& value) {
    std::memcpy(app.data() + 0x9fcc4, value.direction.data(), 12);
    std::memcpy(app.data() + 0x9fcd0, &value.acceleration, 4);
    std::memcpy(app.data() + 0x9fcd4, &value.noise, 4);
    app[0xa1d68] = value.noiseEnabled;
}
unsigned GravityChannels(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x5f7f0), &ret, 1);
    std::vector<unsigned char> app(0xa3000), physics(0x88);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    std::array<void*, 14> vtable{};
    vtable[13] = reinterpret_cast<void*>(&CaptureGravity);
    auto* table = vtable.data();
    auto* world = &table;
    auto* scene = physics.data();
    std::memcpy(physics.data() + 0x80, &world, sizeof world);
    std::memcpy(app.data() + 0x9fcb8, &scene, sizeof scene);
    const auto execute = original.Function<void (*)(int, float)>(0x95b40);
    EditorSession portable;
    std::string reason;
    Check(portable.ImportModel(root / "articulated.pmx", reason), "gravity fixture import");
    unsigned count = 0;
    const auto revision = portable.Revision(), frames = portable.Runtime().Counters().frames;
    const bool undo = portable.CanUndo();
    for (unsigned channel : {0, 1, 2, 3, 4, 2, 0})
        for (float value : {0.f, -.75f, -2.f, .57f, 2.f, 10.125f, 1000000.f}) {
            WriteGravity(app, portable.Runtime().Session()->Pose().gravity);
            app[0xa137b] = 0;
            nativeGravityWrites = 0;
            execute(int(channel), value);
            Check(portable.SetGravityChannel(channel, value, reason), "portable gravity edit");
            const auto& result = portable.Runtime().Session()->Pose().gravity;
            Check(!std::memcmp(app.data() + 0x9fcc4, result.direction.data(), 12) &&
                      !std::memcmp(app.data() + 0x9fcd0, &result.acceleration, 4) &&
                      !std::memcmp(app.data() + 0x9fcd4, &result.noise, 4),
                  "native gravity channel state");
            Check(nativeGravityWrites == unsigned(channel < 4),
                  "native immediate gravity update gate");
            if (channel < 4) {
                std::array<float, 3> vector;
                cpu_model::Math{}.normalize(vector.data(), result.direction.data());
                for (unsigned i = 0; i < 3; ++i) {
                    vector[i] = (vector[i] * result.acceleration) * 10.f;
                    Check(std::abs(vector[i] - nativeGravityVector[i]) <=
                              2e-5f * std::max(1.f, std::abs(nativeGravityVector[i])),
                          "native gravity vector within portable normalization budget");
                }
            }
            Check(app[0xa137b] == 1 && !app[0xa1b31] && portable.Revision() == revision &&
                      portable.Runtime().Counters().frames == frames &&
                      portable.CanUndo() == undo &&
                      portable.Selected().tracks.count({Track::Gravity, 255, 0, 0}),
                  "gravity preview preserves history/physics and selects row");
            ++count;
        }
    return count;
}
unsigned GravityRegistration(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    EditorSession portable;
    std::string reason;
    Check(portable.ImportModel(root / "articulated.pmx", reason), "gravity registration fixture");
    Check(portable.SelectModel(255, reason), "gravity camera context");
    std::vector<unsigned char> app(0xa3000);
    std::vector<mdl::GravityKey> native(10000);
    auto* pool = native.data();
    std::memcpy(app.data() + 0x3e8, &pool, sizeof pool);
    const auto execute = original.Function<void (*)(void*, unsigned)>(0x5da60);
    unsigned count = 0;
    const auto copy = [](const auto& key, auto& result) {
        result.frame = key.frame;
        result.previous = key.previous;
        result.next = key.next;
        result.acceleration = key.acceleration;
        std::copy(key.direction.begin(), key.direction.end(), result.direction);
        result.noise = key.noise;
        result.noiseEnabled = key.noiseEnabled;
        result.selected = key.selected;
    };
    for (unsigned cycle = 0; cycle < 3; ++cycle)
        for (unsigned frame : {0, 10, 4, 20, 10, 0, 60, 30}) {
            Check(portable.Seek(frame, reason), "gravity registration seek");
            Check(portable.SetGravityChannel(0, float(count) * .375f - 3.f, reason) &&
                      portable.SetGravityChannel(1, -.5f, reason) &&
                      portable.SetGravityChannel(2, float(cycle) * .25f, reason) &&
                      portable.SetGravityChannel(3, .75f, reason) &&
                      portable.SetGravityChannel(4, float(count) - 10.5f, reason) &&
                      portable.SetGravityNoise(count % 2 != 0, reason),
                  "gravity registration values");
            std::fill(native.begin(), native.end(), mdl::GravityKey{});
            const auto& source = portable.Document().gravityKeys;
            copy(source.dense[0], native[0]);
            for (const auto& key : source.sparse)
                copy(key.value, native[unsigned(key.index)]);
            for (const auto& key : source.unused)
                copy(key.second, native[key.first]);
            for (auto& key : native)
                key.selected = 0; // original dialog's caller sweep
            WriteGravity(app, portable.Runtime().Session()->Pose().gravity);
            auto maximum = portable.Document().timeline.lastRegisteredFrame;
            std::memcpy(app.data() + 0x9f038, &maximum, 4);
            execute(app.data(), frame);
            const auto updates = portable.Runtime().Counters().frames;
            const auto physics = portable.Runtime().Simulation().cursorSeconds;
            Check(portable.RegisterGravity(reason), "portable gravity registration");
            const auto& result = portable.Document().gravityKeys;
            std::vector<mdl::GravityKey> actual(10000);
            copy(result.dense[0], actual[0]);
            for (const auto& key : result.sparse)
                copy(key.value, actual[unsigned(key.index)]);
            for (const auto& key : result.unused)
                copy(key.second, actual[key.first]);
            Check(!std::memcmp(actual.data(), native.data(), actual.size() * sizeof(actual[0])),
                  "native gravity registration pool bytes");
            Check(!std::memcmp(app.data() + 0x9f038,
                               &portable.Document().timeline.lastRegisteredFrame, 4),
                  "native gravity registration watermark");
            Check(portable.Runtime().Counters().frames == updates &&
                      portable.Runtime().Simulation().cursorSeconds == physics,
                  "gravity registration never evaluates or steps physics");
            ++count;
        }
    return count;
}
void GravityHiddenControls(OriginalPhysicsModule& original, const wchar_t* path) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x5f7f0), &ret, 1);
    std::vector<unsigned char> app(0xa3000), physics(0x88);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    std::array<void*, 14> vtable{};
    vtable[13] = reinterpret_cast<void*>(&CaptureGravity);
    auto* table = vtable.data();
    auto* world = &table;
    auto* scene = physics.data();
    std::memcpy(physics.data() + 0x80, &world, sizeof world);
    std::memcpy(app.data() + 0x9fcb8, &scene, sizeof scene);
    HMODULE controls = LoadLibraryW(L"comctl32.dll");
    const auto init = reinterpret_cast<BOOL(WINAPI*)(const INITCOMMONCONTROLSEX*)>(
        GetProcAddress(controls, "InitCommonControlsEx"));
    INITCOMMONCONTROLSEX config{sizeof config, ICC_BAR_CLASSES};
    Check(init && init(&config), "hidden gravity common controls");
    HMODULE resources =
        LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    Check(resources != nullptr, "hidden gravity resource module");
    const auto resource = FindResourceW(resources, MAKEINTRESOURCEW(803), MAKEINTRESOURCEW(5));
    const auto* bytes =
        static_cast<const unsigned char*>(LockResource(LoadResource(resources, resource)));
    DWORD style;
    Check(bytes && SizeofResource(resources, resource) >= 26, "gravity dialog resource");
    std::memcpy(&style, bytes + 12, 4);
    Check(!(style & WS_VISIBLE), "gravity resource must never show automatically");
    WriteGravity(app, {0, 3, 9.8f, {0, -1, 0}});
    HWND dialog = CreateDialogParamA(resources, MAKEINTRESOURCEA(803), nullptr,
                                     original.Function<DLGPROC>(0x94e80), 0);
    Check(dialog && !IsWindowVisible(dialog), "original gravity dialog stays hidden");
    std::memcpy(app.data() + 0xa1d58, &dialog, sizeof dialog);
    const auto mirror = original.Function<void (*)(HWND)>(0x95f00);
    for (const auto& sample : kGravityMirrorReference) {
        WriteGravity(app, {0, 3, sample.input, {0, -1, 0}});
        mirror(dialog);
        char text[256];
        GetWindowTextA(GetDlgItem(dialog, 709), text, 256);
        float live;
        std::memcpy(&live, app.data() + 0x9fcd0, 4);
        Check(std::strcmp(text, sample.text) == 0 && live == sample.output,
              "original gravity formatting and EDIT feedback");
        Check(!IsWindowVisible(dialog), "original gravity mirror must stay hidden");
    }
    const auto slider = GetDlgItem(dialog, 637);
    Check(SendMessageW(slider, TBM_GETPAGESIZE, 0, 0) == 40 &&
              SendMessageW(slider, TBM_GETLINESIZE, 0, 0) == 1,
          "native dialog trackbar steps");
    for (const auto& key : std::vector<std::pair<int, int>>{{VK_LEFT, -1},
                                                            {VK_UP, -1},
                                                            {VK_RIGHT, 1},
                                                            {VK_DOWN, 1},
                                                            {VK_PRIOR, -40},
                                                            {VK_NEXT, 40},
                                                            {VK_HOME, -100},
                                                            {VK_END, 100}}) {
        SendMessageW(slider, TBM_SETPOS, 0, 0);
        SendMessageW(slider, WM_KEYDOWN, key.first, 0);
        Check(SendMessageW(slider, TBM_GETPOS, 0, 0) == key.second,
              "native dialog trackbar keyboard");
    }
    SendMessageW(slider, TBM_SETPOS, 0, 0);
    for (unsigned i = 0; i < kDialogWheelDeltas.size(); ++i) {
        const auto delta = kDialogWheelDeltas[i];
        SendMessageW(slider, WM_MOUSEWHEEL, WPARAM(WORD(delta)) << 16, 0);
        Check(SendMessageW(slider, TBM_GETPOS, 0, 0) == kDialogWheelPositions[i],
              "native dialog trackbar wheel accumulation");
    }
    DestroyWindow(dialog);
    FreeLibrary(resources);
    FreeLibrary(controls);
    std::printf(
        "8 original hidden gravity mirrors: real EDIT notifications and formatted values\n");
}
unsigned MorphScaleCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch curve(original.Function<void*>(0x62bb0), &ret, 1);
    Patch seek(original.Function<void*>(0xcbd90), &ret, 1);
    Patch controls(original.Function<void*>(0xcd750), &ret, 1);
    auto child = &ResetChild;
    auto text = &RangeText;
    Patch getChild(original.Function<void*>(0x128658), &child, sizeof child);
    Patch getText(original.Function<void*>(0x128690), &text, sizeof text);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    struct Input {
        const char* scaleText;
        const char* addText;
        float scale, add;
    };
    const Input inputs[]{{"1.0", "0.0", 1, 0},
                         {"0", "0", 0, 0},
                         {"-2suffix", ".5suffix", -2, .5f},
                         {"1.25", "-.125", 1.25f, -.125f},
                         {"nonnumeric", "1.5", 0, 1.5f},
                         {"1.00000000000000000e9", "                  9.5", 1, 9}};
    unsigned cases = 0;
    for (const char* file : {"facial.pmd", "articulated.pmx"})
        for (const auto& input : inputs) {
            EditorSession portable;
            std::string reason;
            Check(MorphScaleFixture(portable, root, file, reason), reason.c_str());
            const auto before = portable.Document().models[0];
            NativeAuxiliary native(before,
                                   portable.Runtime().Resource(0)->SourceKind() == ModelKind::Pmx);
            const auto put = [&](unsigned i, const auto& key) {
                auto& out = native.morphs.at(i);
                out.frame = key.frame;
                out.previous = key.previous;
                out.next = key.next;
                out.value = key.value;
                out.allocated = key.allocated;
            };
            for (unsigned i = 0; i < before.morphKeys.dense.size(); ++i)
                put(i, before.morphKeys.dense[i]);
            for (const auto& key : before.morphKeys.sparse)
                put(unsigned(key.index), key.value);
            for (const auto& [i, key] : before.morphKeys.unused)
                put(i, key);
            auto* model = native.model.get();
            std::fill(app.begin(), app.end(), 0);
            std::memcpy(app.data() + 0xbe8, &model, sizeof model);
            rangeFields = {{686, input.scaleText}, {687, input.addText}};
            original.Function<void (*)(HWND)>(0x9d120)(nullptr);
            Check(portable.ScaleMorphKeys(input.scale, input.add, reason), reason.c_str());
            const auto& track = portable.Document().models[0].morphKeys;
            std::map<unsigned, PmmMorphKey> expected(track.unused.begin(), track.unused.end());
            for (unsigned i = 0; i < track.dense.size(); ++i)
                expected[i] = track.dense[i];
            for (const auto& key : track.sparse)
                expected[unsigned(key.index)] = key.value;
            for (unsigned i = 0; i < native.morphs.size(); ++i) {
                const auto& a = native.morphs[i];
                const auto b = expected.count(i) ? expected.at(i) : PmmMorphKey{};
                Check(a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
                          a.allocated == b.allocated &&
                          !std::memcmp(&a.value, &b.value, sizeof(float)),
                      "native morph-scale full pool differs");
            }
            Check(app[0xa1b31] && model->UndoCursor() == 29 && model->LatestUndoCursor() == 29,
                  "native morph-scale dirty/history contract");
            ++cases;
        }
    return cases;
}
unsigned CameraScaleCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch seek(original.Function<void*>(0x59c30), &ret, 1);
    Patch refresh(original.Function<void*>(0x20dd0), &ret, 1);
    auto child = &ResetChild;
    auto text = &RangeText;
    Patch getChild(original.Function<void*>(0x128658), &child, sizeof child);
    Patch getText(original.Function<void*>(0x128690), &text, sizeof text);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    for (const char* file : {"facial.pmd", "articulated.pmx"})
        for (unsigned variant = 0; variant < 11; ++variant) {
            EditorSession portable;
            std::string reason;
            Check(CameraScaleFixture(portable, root, file, reason), reason.c_str());
            const auto before = portable.Document().cameraKeys;
            std::vector<mdl::CameraKey> native(10000);
            const auto put = [&](unsigned i, const auto& key) {
                auto& out = native.at(i);
                out.frame = key.frame;
                out.previous = key.previous;
                out.next = key.next;
                out.fov = keyframe_edit::SignedCameraFov(key.fov);
                out.distance = key.distance;
                out.selected = key.selected;
                out.perspective = key.perspective;
                out.parentModel = key.parentModel;
                out.parentBone = key.parentBone;
                std::copy(key.eye.begin(), key.eye.end(), out.eye);
                std::copy(key.target.begin(), key.target.end(), out.target);
                std::memcpy(out.interpolation, key.interpolation.data(), 24);
            };
            for (unsigned i = 0; i < before.dense.size(); ++i)
                put(i, before.dense[i]);
            for (const auto& key : before.sparse)
                put(unsigned(key.index), key.value);
            for (const auto& [i, key] : before.unused)
                put(i, key);
            const auto nativeBefore = native;
            std::fill(app.begin(), app.end(), 0);
            auto* keys = native.data();
            std::memcpy(app.data() + 0x3d0, &keys, sizeof keys);
            auto values = CameraScaleIdentity;
            if (variant >= 1 && variant <= 8) {
                values[2 * (variant - 1)] = -1.25f;
                values[2 * (variant - 1) + 1] = 35.7f;
            } else if (variant == 9) {
                values.fill(0);
            } else if (variant == 10) {
                values[14] = 1e30f;
                values[15] = -1e30f;
            }
            rangeFields.clear();
            for (unsigned i = 0; i < values.size(); ++i) {
                char field[24];
                std::snprintf(field, sizeof field, "%.9g", double(values[i]));
                rangeFields[686 + int(i)] = field;
            }
            original.Function<void (*)(HWND)>(0x9c330)(nullptr);
            Check(portable.ScaleCameraKeys(values, reason), reason.c_str());
            const auto& track = portable.Document().cameraKeys;
            std::map<unsigned, PmmCameraKey> expected(track.unused.begin(), track.unused.end());
            for (unsigned i = 0; i < track.dense.size(); ++i)
                expected[i] = track.dense[i];
            for (const auto& key : track.sparse)
                expected[unsigned(key.index)] = key.value;
            for (unsigned i = 0; i < native.size(); ++i) {
                const auto& a = native[i];
                if (!expected.count(i)) {
                    Check(!std::memcmp(&a, &nativeBefore[i], sizeof a),
                          "native free camera changed");
                    continue;
                }
                const auto& b = expected.at(i);
                Check(a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
                          a.selected == b.selected && std::uint32_t(a.fov) == b.fov &&
                          a.parentModel == b.parentModel && a.parentBone == b.parentBone &&
                          a.perspective == b.perspective &&
                          !std::memcmp(&a.distance, &b.distance, 4) &&
                          !std::memcmp(a.eye, b.eye.data(), 12) &&
                          !std::memcmp(a.target, b.target.data(), 12) &&
                          !std::memcmp(a.interpolation, b.interpolation.data(), 24),
                      "native camera-scale full pool differs");
            }
            Check(app[0xa1b31], "native camera-scale dirty flag");
            ++cases;
        }
    return cases;
}
unsigned reductionCount = 0, reductionMessages = 0;
int WINAPI ReductionMessage(HWND, LPCSTR text, LPCSTR title, UINT flags) {
    Check(std::strcmp(title, "delete unused frame") == 0 && flags == MB_TOPMOST,
          "native reduction report identity");
    reductionCount = unsigned(std::atoi(text));
    ++reductionMessages;
    return IDOK;
}
unsigned ReductionCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    // Execute the original marking and reporting command. Deletion's independent
    // slot/undo comparison lives in editor_frame_line_oracle_test.
    const unsigned char ret = 0xc3;
    Patch deletion(original.Function<void*>(0x64b30), &ret, 1);
    auto message = &ReductionMessage;
    Patch report(original.Function<void*>(0x128618), &message, sizeof message);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx", "facial.pmd"})
        for (bool reverse : {false, true})
            for (unsigned variant = 0; variant < 8; ++variant) {
                EditorSession portable;
                std::string reason;
                Check(ReductionFixture(portable, root, file, reverse, variant, reason),
                      reason.c_str());
                const auto before = portable.Document().models[0];
                const auto resource = portable.Runtime().Resource(0);
                NativeAuxiliary native(before, resource->SourceKind() == ModelKind::Pmx);
                auto* model = native.model.get();
                std::vector<mdl::BoneRecord> bones(before.bonePose.size());
                std::vector<mdl::BoneKey> keys(mdl::kBoneKeyCapacity);
                model->boneCount = unsigned(bones.size());
                model->boneTable = bones.data();
                model->boneKeys = keys.data();
                for (unsigned i = 0; i < bones.size(); ++i) {
                    int mapped;
                    Check(portable.Runtime().DocumentBoneIndex(0, int(i), mapped, reason),
                          reason.c_str());
                    bones[i].type =
                        static_cast<mdl::BoneType>(resource->EditingBones()[mapped].type);
                }
                const auto copy = [](const auto& track, auto&& put) {
                    for (unsigned i = 0; i < track.dense.size(); ++i)
                        put(i, track.dense[i]);
                    for (const auto& key : track.sparse)
                        put(unsigned(key.index), key.value);
                    for (const auto& [i, key] : track.unused)
                        put(i, key);
                };
                const auto links = [](auto& out, const auto& in) {
                    out.frame = in.frame;
                    out.previous = in.previous;
                    out.next = in.next;
                    out.allocated = in.allocated;
                };
                copy(before.boneKeys, [&](unsigned i, const auto& key) {
                    auto& out = keys[i];
                    links(out, key);
                    std::copy(key.position.begin(), key.position.end(), out.position);
                    std::copy(key.rotation.begin(), key.rotation.end(), out.rotation);
                    std::copy(key.interpolation.begin(), key.interpolation.end(),
                              out.interpolation);
                    out.physicsDisabled = key.physicsDisabled;
                });
                copy(before.morphKeys, [&](unsigned i, const auto& key) {
                    links(native.morphs[i], key);
                    native.morphs[i].value = key.value;
                });
                copy(before.displayKeys, [&](unsigned i, const auto& key) {
                    links(native.displays[i], key);
                    native.displays[i].visible = key.visible;
                    native.ik[i] = key.ikEnabled;
                    native.displays[i].ikStates = native.ik[i].data();
                });
                for (auto& key : keys)
                    key.allocated = 1;
                for (auto& key : native.morphs)
                    key.allocated = 1;
                for (auto& key : native.displays)
                    key.allocated = 1;
                std::fill(app.begin(), app.end(), 0);
                std::memcpy(app.data() + 0xbe8, &model, sizeof model);
                app[0xa1b90] = 1;
                reductionMessages = 0;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr,
                                                                   MDS_CMD_DELETE_UNUSED_FRAMES);
                unsigned count;
                Check(portable.DeleteUnusedFrames(count, reason), reason.c_str());
                Check(count == reductionCount && reductionMessages == 1 && app[0xa1b31] == 1,
                      "native reduction count/dirty/report");
                const auto& after = portable.Document().models[0];
                const auto compare = [](const auto& pool, const auto& old, const auto& changed) {
                    std::set<unsigned> removed;
                    for (const auto& key : old.sparse)
                        if (std::none_of(changed.sparse.begin(), changed.sparse.end(),
                                         [&](const auto& k) { return k.index == key.index; }))
                            removed.insert(unsigned(key.index));
                    for (unsigned i = 0; i < pool.size(); ++i)
                        Check(bool(pool[i].allocated) == (removed.count(i) != 0),
                              "native full-pool reduction marks");
                };
                compare(keys, before.boneKeys, after.boneKeys);
                compare(native.morphs, before.morphKeys, after.morphKeys);
                compare(native.displays, before.displayKeys, after.displayKeys);
                ++cases;
            }
    return cases;
}
unsigned gravityCapacityMessages = 0;
int WINAPI GravityCapacityMessage(HWND, LPCSTR text, LPCSTR, UINT) {
    Check(std::strstr(text, "10000") != nullptr, "native gravity capacity message");
    ++gravityCapacityMessages;
    return IDOK;
}
void GravityCapacity(OriginalPhysicsModule& original) {
    EditorSession portable;
    std::string reason;
    const bool edited = portable.Edit(
        "gravity-capacity",
        [](auto& d, auto&) {
            FillGravityCapacity(d);
            return true;
        },
        reason);
    Check(edited, reason.c_str());
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const unsigned char ret = 0xc3;
    Patch rebuild(original.Function<void*>(0x5f7f0), &ret, 1);
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    auto message = &GravityCapacityMessage;
    Patch report(original.Function<void*>(0x128618), &message, sizeof message);
    std::vector<mdl::CameraKey> camera(10000);
    std::vector<mdl::LightKey> light(10000);
    std::vector<mdl::SelfShadowKey> shadow(10000);
    std::vector<mdl::GravityKey> gravity(10000);
    std::vector<mdl::AccessoryKey> accessories(10000), lastAccessory(10000);
    const auto bind = [&](unsigned offset, auto& pool) {
        auto* data = pool.data();
        std::memcpy(app.data() + offset, &data, sizeof data);
        for (auto& key : pool)
            key.selected = 1;
    };
    bind(0x3d0, camera);
    bind(0x3d8, light);
    bind(0x3e0, shadow);
    bind(0x3e8, gravity);
    for (unsigned slot = 0; slot < 255; ++slot)
        bind(0x3f0 + slot * 8, slot == 254 ? lastAccessory : accessories);
    for (unsigned i = 0; i < gravity.size(); ++i) {
        gravity[i].frame = i;
        gravity[i].previous = i ? i - 1 : 0;
        gravity[i].next = i == 9999 ? 0 : i + 1;
        gravity[i].acceleration = 9.8f;
        gravity[i].direction[1] = -1;
    }
    WriteGravity(app, portable.Runtime().Session()->Pose().gravity);
    const unsigned frame = 10000, maximum = 9999;
    std::memcpy(app.data() + 0x1450, &frame, 4);
    std::memcpy(app.data() + 0x9f038, &maximum, 4);
    app[0xa1b90] = 1;
    original.Function<void (*)()>(0x95dd0)();
    Check(gravityCapacityMessages == 1 && app[0xa1b31] == 1,
          "native full-pool command reports and dirties");
    const auto cleared = [](const auto& pool) {
        return std::all_of(pool.begin(), pool.end(), [](const auto& key) { return !key.selected; });
    };
    Check(cleared(camera) && cleared(light) && cleared(shadow) && cleared(gravity) &&
              cleared(accessories) && cleared(lastAccessory),
          "native full-pool command clears all marks");
    const auto revision = portable.Revision(), frames = portable.Runtime().Counters().frames;
    Check(!portable.RegisterGravity(reason) && reason == "gravity-key-capacity",
          "portable capacity report");
    Check(portable.Revision() != revision && portable.Runtime().Counters().frames == frames &&
              portable.Document().gravityKeys.sparse.size() == 9999 &&
              portable.Document().timeline.lastRegisteredFrame == 9999 &&
              !portable.Document().gravityKeys.dense[0].selected,
          "portable native capacity side effects");
    for (const auto& key : portable.Document().gravityKeys.sparse)
        Check(!key.value.selected, "portable full-pool marks");
    std::puts(
        "Original full gravity dialog registration: capacity, all track sweeps and dirty flag passed");
}
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Check(argc == 3, "usage: editor_auxiliary_oracle_test original.exe generated-fixture");
    OriginalPhysicsModule original(argv[1]);
    std::printf("%u original FPS commands: exact rate and unchanged application state\n",
                FrameRateCommands(original));
    unsigned count = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx", "facial.pmd"}) {
        EditorSession session;
        std::string reason;
        Check(session.ImportModel(std::filesystem::path(argv[2]) / file, reason), reason.c_str());
        const bool pmx = session.Runtime().Resource(0)->SourceKind() == ModelKind::Pmx;
        NativeAuxiliary native(session.Document().models[0], pmx);
        const unsigned frames[]{0, 20, 5, 30, 20, 1, 0, 60};
        for (unsigned cycle = 0; cycle < 3; ++cycle)
            for (unsigned i = 0; i < std::size(frames); ++i)
                for (auto kind : {Track::Morph, Track::Display}) {
                    const unsigned channel = pmx ? 0 : 1;
                    if (kind == Track::Morph &&
                        channel >= session.Document().models[0].morphPose.size())
                        continue; // PMD fixture contains only the non-editable base morph.
                    Check(session.Seek(frames[i], reason), reason.c_str());
                    Check(session.Edit(
                              "auxiliary-input",
                              [&](auto& d, auto&) {
                                  auto& model = d.models[0];
                                  model.selection.loadComplete = (i + cycle) % 2;
                                  for (auto& weight : model.morphPose)
                                      weight = float((i * 3 + cycle) % 9) / 8;
                                  for (auto& enabled : model.ikPose)
                                      enabled = (i + cycle / 2) % 2;
                                  return true;
                              },
                              reason),
                          reason.c_str());
                    native.Register(original, session.Document().models[0], kind, frames[i],
                                    channel);
                    const auto* runtime = &session.Runtime();
                    const auto updates = runtime->Counters().frames;
                    const bool undo = session.CanUndo(), redo = session.CanRedo();
                    Check(kind == Track::Morph
                              ? session.RegisterMorph(0, channel, reason)
                              : session.Register({{kind, 0, 0, frames[i]}}, reason),
                          reason.c_str());
                    native.Compare(session.Document().models[0]);
                    Check(runtime == &session.Runtime() && updates == runtime->Counters().frames,
                          "registration advanced or replaced the CPU runtime");
                    Check(undo == session.CanUndo() && redo == session.CanRedo(),
                          "registration altered portable bone history");
                    ++count;
                }
    }
    std::printf("%u original morph/display registrations: exact slots, values, IK and visibility\n",
                count);
    std::printf(
        "%u original reset-command branches: weights, controls, document and history flags\n",
        ResetCommand(original, argv[2]));
    std::printf(
        "%u original full-dispatch checks: rotation reset, physics selection and deferred reset\n",
        SceneCommands(original, argv[2]));
    std::printf(
        "%u original physics-key commands: full pool sweeps, reversed bindings and empty chains\n",
        PhysicsKeyCommand(original, argv[2]));
    std::printf(
        "%u original chained range commands: full pools, additive rows and detached records\n",
        RangeCommands(original, argv[2]));
    std::printf(
        "%u original gravity channel edits: values, row selection and immediate world vectors\n",
        GravityChannels(original, argv[2]));
    std::printf("%u original gravity registrations: complete pool bytes, payloads and watermarks\n",
                GravityRegistration(original, argv[2]));
    GravityHiddenControls(original, argv[1]);
    GravityCapacity(original);
    std::printf(
        "%u original reduction commands: complete pool marks, counts and detached records\n",
        ReductionCommands(original, argv[2]));
    std::printf(
        "%u original morph-scale applies: full pool bytes, prefix parsing and unused slots\n",
        MorphScaleCommands(original, argv[2]));
    std::printf(
        "%u original camera-scale applies: full pool bytes, angle conversion and signed FOV\n",
        CameraScaleCommands(original, argv[2]));
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
