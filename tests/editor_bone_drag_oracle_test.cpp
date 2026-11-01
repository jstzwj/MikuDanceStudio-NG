#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
static void Check(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
// Execute only the original x64 frame driver's bone-drag block. The surrounding
// frame driver contains drawing and UI calls and is deliberately never entered.
// The one outgoing continuation is replaced by our ABI-restoring epilogue.
// No original instruction inside the tested block is replaced. Inputs supply
// published pose matrices; matrix production and cursor/UI handling are separate gates.
class OriginalDragBlock {
    unsigned char* memory_ = nullptr;
    unsigned char* continuation_ = nullptr;
    std::array<unsigned char, 14> saved_{};
    std::vector<unsigned char> bytes_;
    void Emit(std::initializer_list<unsigned char> b) { bytes_.insert(bytes_.end(), b); }
    template <class T> void Value(T value) {
        const auto* p = reinterpret_cast<const unsigned char*>(&value);
        bytes_.insert(bytes_.end(), p, p + sizeof value);
    }
    void Xmm(unsigned index, bool load) {
        Emit({0xf3});
        if (index >= 8)
            Emit({0x44});
        Emit({0x0f, static_cast<unsigned char>(load ? 0x6f : 0x7f),
              static_cast<unsigned char>(0x84 | ((index & 7) << 3)), 0x24});
        Value(std::uint32_t(0x2000 + (index - 6) * 16));
    }

public:
    explicit OriginalDragBlock(const OriginalPhysicsModule& original) {
        memory_ = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Check(memory_ != nullptr, "allocate original drag trampoline");
        Emit({0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
        Emit({0x48, 0x81, 0xec});
        Value(std::uint32_t(0x2208));
        for (unsigned n = 6; n <= 15; ++n)
            Xmm(n, false);
        Emit({0x48, 0x8d, 0xac, 0x24});
        Value(std::uint32_t(0x80));                                   // frame scratch base
        Emit({0x49, 0x89, 0xcc, 0x45, 0x31, 0xff, 0x45, 0x31, 0xed}); // r12=app,r15=0
        Emit({0x66, 0x45, 0x0f, 0xef, 0xed});                         // xmm13=0
        Emit({0xb8});
        Value(std::uint32_t(0x3ba3d70a));     // .005f
        Emit({0x66, 0x44, 0x0f, 0x6e, 0xf8}); // xmm15
        Emit({0xb8});
        Value(std::uint32_t(0x40a00000));     // 5.f
        Emit({0x66, 0x44, 0x0f, 0x6e, 0xe0}); // xmm12
        Emit({0x48, 0xb8});
        Value(original.Function<std::uintptr_t>(0x32f41));
        Emit({0xff, 0xe0});
        const auto epilogue = reinterpret_cast<std::uintptr_t>(memory_ + bytes_.size());
        for (unsigned n = 6; n <= 15; ++n)
            Xmm(n, true);
        Emit({0x48, 0x81, 0xc4});
        Value(std::uint32_t(0x2208));
        Emit({0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5f, 0x5e, 0x5d, 0x5b, 0xc3});
        std::memcpy(memory_, bytes_.data(), bytes_.size());
        DWORD old;
        Check(VirtualProtect(memory_, 4096, PAGE_EXECUTE_READ, &old) != 0,
              "protect drag trampoline");
        FlushInstructionCache(GetCurrentProcess(), memory_, bytes_.size());
        continuation_ = original.Function<unsigned char*>(0x35eeb);
        const unsigned char expected[]{0x41, 0x83, 0xbc, 0x24, 0x84, 0x03, 0, 0, 0x0d};
        Check(std::memcmp(continuation_, expected, sizeof expected) == 0,
              "original drag continuation signature");
        std::copy_n(continuation_, saved_.size(), saved_.begin());
        Check(VirtualProtect(continuation_, saved_.size(), PAGE_EXECUTE_READWRITE, &old) != 0,
              "patch drag continuation");
        const unsigned char jump[]{0xff, 0x25, 0, 0, 0, 0};
        std::memcpy(continuation_, jump, sizeof jump);
        std::memcpy(continuation_ + 6, &epilogue, 8);
        VirtualProtect(continuation_, saved_.size(), old, &old);
        FlushInstructionCache(GetCurrentProcess(), continuation_, saved_.size());
    }
    ~OriginalDragBlock() {
        if (continuation_) {
            DWORD old;
            VirtualProtect(continuation_, saved_.size(), PAGE_EXECUTE_READWRITE, &old);
            std::copy(saved_.begin(), saved_.end(), continuation_);
            VirtualProtect(continuation_, saved_.size(), old, &old);
        }
        if (memory_)
            VirtualFree(memory_, 0, MEM_RELEASE);
    }
    void Run(void* app) const { reinterpret_cast<void (*)(void*)>(memory_)(app); }
};
struct NativeDrag {
    std::vector<unsigned char> app = std::vector<unsigned char>(0xA6000, 0);
    std::vector<unsigned char> renderer = std::vector<unsigned char>(0x3AA00, 0);
    template <class T> void Store(std::size_t at, T value) {
        std::memcpy(app.data() + at, &value, sizeof value);
    }
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::vector<mdl::BoneRecord> bones;
    std::vector<unsigned char> selection, edits;
    NativeDrag(const editor::EditorSession& session, const editor::BoneDragInput& input) {
        const auto resource = session.Runtime().Resource(0);
        const auto snapshot = session.Runtime().Snapshot(0);
        bones.resize(snapshot->bones.size());
        selection.resize(bones.size());
        edits.resize(bones.size());
        model->boneTable = bones.data();
        model->boneCount = unsigned(bones.size());
        model->boneSelection = selection.data();
        model->bonePhysicsState = edits.data();
        model->selectedBone = session.ActiveBone();
        model->physicsMode = resource->SourceKind() == format::ModelKind::Pmx ? 2 : 1;
        for (unsigned i = 0; i < bones.size(); ++i) {
            auto& bone = bones[i];
            const auto& f = resource->Document().bones[i].fields;
            const auto& pose = session.Document().models[0].bonePose[i];
            std::copy_n(f.position, 3, bone.position);
            std::copy_n(f.axis, 3, bone.axis);
            std::copy_n(f.localAxes, 6, bone.localAxes);
            std::copy_n(snapshot->bones[i].skin.begin(), 16, bone.matInit);
            std::copy_n(snapshot->bones[i].world.begin(), 16, bone.matWorld);
            std::copy_n(pose.components.begin(), 3, bone.trans);
            std::copy_n(pose.components.begin() + 3, 4, bone.rotQuat);
            bone.parent = f.parent;
            bone.tailBone = std::max(0, resource->EditingBones()[i].tailBone);
            bone.type = resource->EditingBones()[i].type;
            bone.flags = resource->EditingBones()[i].flags;
            std::string name;
            if (text::EncodeCp932(resource->BoneNames()[i], name))
                std::copy_n(name.data(), std::min(name.size(), sizeof bone.name - 1), bone.name);
            edits[i] = pose.flags[1];
        }
        for (auto i : session.Selected().bones)
            selection[i] = 1;
        Store(0xBE8, model.get());
        Store(0x13E0, std::uint8_t(0));
        Store(0xA16E0, renderer.data());
        Store(0xA18F8, session.Document().envelope.settings.renderWidth);
        Store(0xA18FC, session.Document().envelope.settings.renderHeight);
        std::memcpy(renderer.data() + 0x3A9C8, &input.viewWidth, 4);
        std::memcpy(renderer.data() + 0x3A9CC, &input.viewHeight, 4);
        std::memcpy(renderer.data() + 0x3A9D4, &input.viewScale, 4);
        Store(0x8, input.x);
        Store(0xC, input.y);
        Store(0x10, input.previousX);
        Store(0x14, input.previousY);
        Store(0x1400, input.originX);
        Store(0x1404, input.originY);
        Store(0x13F8, input.clipW);
        Store(0x9FCA4, int(session.CoordinateSystem()));
        Store(0x384, input.mode);
        Store(0x28, input.shift ? 3 : 0);
        Store(0xC4, input.control ? 3 : 0);
        const auto camera = session.Runtime().Session()->Pose().camera;
        Store(0x348, camera.rotation[0]);
        Store(0x34C, camera.rotation[1]);
        Store(0x350, camera.rotation[2]);
        Store(0xA1900, camera.distance);
        Store(0x354, camera.perspective);
    }
};
int wmain(int argc, wchar_t** argv) try {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3)
        return 2;
    OriginalPhysicsModule original(argv[1]);
    OriginalDragBlock block(original);
    std::string reason;
    const auto root = std::filesystem::absolute(argv[2]);
    unsigned cases = 0;
    float maximumTranslation = 0, maximumRotation = 0;
    for (const auto* file : {"drag.pmd", "drag-roots.pmd", "drag-axes.pmd", "triangle.pmx",
                             "articulated.pmd", "articulated.pmx"})
        for (unsigned poseCase = 0; poseCase < 3; ++poseCase) {
            editor::EditorSession session;
            Check(session.ImportModel(root / file, reason), reason);
            const auto count = session.Document().models[0].bonePose.size();
            if (poseCase) {
                Check(session.SetBone(0, 0, {.2f, -.3f, .4f}, {0, 0, .6f, .8f}, reason), reason);
                if (count > 1)
                    Check(session.SetBone(0, 1, {-.7f, .5f, .3f}, {.5f, .5f, .5f, .5f}, reason),
                          reason);
            }
            std::vector<std::uint32_t> selected;
            for (unsigned i = 0; i < count; ++i)
                selected.push_back(i);
            Check(session.SelectBones(selected, poseCase == 2 && count > 1 ? 1 : 0, reason),
                  reason);
            auto camera = session.Runtime().Session()->Pose().camera;
            camera.rotation = {-.3f * poseCase, .7f * poseCase, .2f};
            camera.perspective = poseCase == 1 ? 1 : 0;
            camera.distance = -37;
            Check(session.SetCamera(camera, reason), reason);
            for (unsigned coordinate = 0; coordinate < 2; ++coordinate) {
                for (unsigned precision = 0; precision < 3; ++precision)
                    for (int mode = 1; mode <= 12; ++mode) {
                        editor::BoneDragInput input;
                        input.mode = mode;
                        input.x = 23;
                        input.y = -17;
                        input.previousX = 5;
                        input.previousY = 7;
                        input.originX = 119;
                        input.originY = 211;
                        input.viewWidth = 640;
                        input.viewHeight = 480;
                        input.viewScale = .6f;
                        input.clipW = .7f;
                        input.shift = precision == 1;
                        input.control = precision == 2;
                        NativeDrag native(session, input);
                        block.Run(native.app.data());
                        Check(session.BeginBoneDrag(unsigned(mode), reason), reason);
                        Check(session.DragBones(input, reason), reason);
                        session.EndBoneDrag();
                        for (unsigned i = 0; i < count; ++i) {
                            const auto& pose = session.Document().models[0].bonePose[i];
                            for (unsigned c = 0; c < 7; ++c) {
                                const float expected = c < 3 ? native.bones[i].trans[c]
                                                             : native.bones[i].rotQuat[c - 3];
                                const float error = std::abs(expected - pose.components[c]);
                                auto& maximum = c < 3 ? maximumTranslation : maximumRotation;
                                maximum = std::max(maximum, error);
                                if (!std::isfinite(error) || error > (c < 3 ? 2e-5f : 2e-6f)) {
                                    std::fprintf(
                                        stderr,
                                        "%s pose=%u coord=%u precision=%u mode=%d bone=%u c=%u original=%.9g portable=%.9g error=%.9g\n",
                                        file, poseCase, coordinate, precision, mode, i, c, expected,
                                        pose.components[c], error);
                                    Check(false, "original bone drag differs");
                                }
                            }
                            Check(pose.flags[1] == native.edits[i],
                                  "original pending edit marker differs");
                        }
                        Check(session.Undo(reason), reason);
                        ++cases;
                    }
                session.CycleCoordinateSystem();
            }
            std::printf("Original drag %s pose=%u cases=%u\n", file, poseCase, cases);
        }
    std::printf(
        "Original x64 bone drag: %u cases, max translation %.9g, rotation %.9g; no windows/devices\n",
        cases, maximumTranslation, maximumRotation);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
