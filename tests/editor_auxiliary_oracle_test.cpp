#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/menu_commands.h"
#include <cstdio>
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
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Check(argc == 3, "usage: editor_auxiliary_oracle_test original.exe generated-fixture");
    OriginalPhysicsModule original(argv[1]);
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
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
