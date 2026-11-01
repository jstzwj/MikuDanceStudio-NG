#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/editor_session.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
static void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;

// The reference function at x64 RVA 72B70 is camera-reference reanchoring.
// It calls only matrix routines and the stack-cookie check: no HWND, D3D device,
// message pump or executable entrypoint is used. Inputs below describe the same
// published skin matrices; this oracle checks the reference-change operation,
// not the independent production of those matrices.
struct NativeCamera {
    std::vector<unsigned char> app = std::vector<unsigned char>(0xA6000, 0);
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 8> bones{};
    template <class T> void Store(std::size_t at, const T& value) {
        std::memcpy(app.data() + at, &value, sizeof value);
    }
    float Float(std::size_t at) const {
        float value;
        std::memcpy(&value, app.data() + at, sizeof value);
        return value;
    }
    explicit NativeCamera(const editor::EditorSession& session) {
        model->boneTable = bones.data();
        model->boneCount = unsigned(bones.size());
        model->centerBone = 0;
        Store(0xBE8, model.get());
        Store(0x13E0, std::uint8_t(0));
        const auto* snapshot = session.Runtime().Snapshot(0);
        Check(snapshot && snapshot->bones.size() == bones.size(), "camera fixture topology");
        for (unsigned i = 0; i < bones.size(); ++i) {
            std::copy_n(snapshot->bones[i].skin.begin(), 16, bones[i].matInit);
            // Independently generated PMD rest positions are (0, index, 0).
            bones[i].position[1] = float(i);
        }
    }
    void Capture(const editor::EditorSession& session, unsigned newMode) {
        const auto& d = session.Document();
        Store(0x340, d.cameraState[3]);
        Store(0x344, d.cameraState[4]);
        Store(0x348, d.cameraState[6]);
        Store(0x34C, d.cameraState[7]);
        Store(0x350, d.cameraState[8]);
        Store(0xA1900, d.cameraState[5]);
        Store(0xA1404, d.appearance.cameraParentModel);
        Store(0xA140C, d.appearance.cameraAttachmentBasis);
        Store(0x378, std::uint8_t(newMode));
        model->selectedBone = session.ActiveBone();
    }
};
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3)
        return 2;
    OriginalPhysicsModule original(argv[1]);
    editor::EditorSession session;
    std::string reason;
    Check(session.ImportModel(std::filesystem::path(argv[2]) / "pose.pmd", reason), reason.c_str());
    Check(session.SetBone(0, 0, {1, 2, 3}, {0, 0, .6f, .8f}, reason), reason.c_str());
    Check(session.SetBone(0, 3, {4, 5, 6}, {0, .6f, 0, .8f}, reason), reason.c_str());
    Check(session.Edit(
              "saved-camera-basis",
              [](auto& d, auto&) {
                  d.appearance.cameraAttachmentBasis = cpu_math::RotationY(.7f);
                  return true;
              },
              reason),
          reason.c_str());
    NativeCamera native(session);
    const auto* snapshot = session.Runtime().Snapshot(0);
    const auto updates = session.Runtime().Counters().frames;
    unsigned cases = 0;
    float maximum = 0;
    for (float pitch : {-.8f, 0.f, .9f})
        for (float yaw : {-1.1f, 0.f, 2.2f})
            for (float roll : {-.3f, .5f})
                for (int parent : {-1, 0})
                    for (int active : {-1, 0, 3})
                        for (unsigned oldMode = 0; oldMode < 3; ++oldMode)
                            for (unsigned newMode = 0; newMode < 3; ++newMode) {
                                Check(session.SelectBones({0, 3}, active, reason), reason.c_str());
                                auto camera = session.Runtime().Session()->Pose().camera;
                                camera.position = {3, 7, -19};
                                camera.rotation = {pitch, yaw, roll};
                                camera.distance = -45;
                                camera.parentModel = parent;
                                camera.parentBone = 3;
                                Check(session.SetCamera(camera, reason), reason.c_str());
                                Check(session.SetCameraReference(oldMode, reason), reason.c_str());
                                native.Capture(session, newMode);
                                original.Function<void (*)(void*, int)>(0x72B70)(native.app.data(),
                                                                                 int(oldMode));
                                Check(session.SetCameraReference(newMode, reason), reason.c_str());
                                const auto& state = session.Document().cameraState;
                                const std::array<float, 3> expected{native.Float(0x340),
                                                                    native.Float(0x344),
                                                                    native.Float(0xA1900)};
                                for (unsigned i = 0; i < 3; ++i) {
                                    const float error = std::abs(state[3 + i] - expected[i]);
                                    maximum = std::max(maximum, error);
                                    if (!std::isfinite(error) || error > 2e-5f) {
                                        std::fprintf(
                                            stderr,
                                            "case=%u mode=%u->%u parent=%d active=%d axis=%u actual=%.9g original=%.9g\n",
                                            cases, oldMode, newMode, parent, active, i,
                                            state[3 + i], expected[i]);
                                        Check(false, "original camera reanchor differs");
                                    }
                                }
                                Check(session.Document().timeline.cameraReferenceMode == newMode,
                                      "reference mode differs");
                                ++cases;
                            }
    Check(session.Runtime().Snapshot(0) == snapshot &&
              session.Runtime().Counters().frames == updates,
          "camera-reference switching updated the scene");
    std::printf(
        "Original x64 camera reference: %u cases, maximum absolute error %.9g, no windows\n", cases,
        maximum);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
