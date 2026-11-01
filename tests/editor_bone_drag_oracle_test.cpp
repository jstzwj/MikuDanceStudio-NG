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
#include "original_frame_slice.hpp"
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
    OriginalFrameSlice block(original, 0x32f41, 0x35eeb,
                             {0x41, 0x83, 0xbc, 0x24, 0x84, 0x03, 0, 0, 0x0d},
                             {{13, 0.f}, {15, .005f}, {12, 5.f}});
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
