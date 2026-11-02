#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmm_edit.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <limits>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ": " << reason << '\n';                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
bool Near(float a, float b) {
    return std::abs(a - b) <= 2e-5f;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    // Frozen instruction-stage results from the original x64 model kernel;
    // the globals and model paths must preserve the same binary32 boundaries.
    for (const auto& sample : {std::pair<std::uint32_t, std::uint32_t>{0x3DCCCCCD, 0x40400000},
                               {0x3D088889, 0x3F800000},
                               {0x3EAAACDA, 0x41200419},
                               {0x3F9E0419, 0x421423D7}}) {
        float seconds;
        std::memcpy(&seconds, &sample.first, sizeof seconds);
        double frame;
        CHECK(format::PmmPlaybackFrame(seconds, frame, format::PmmPlaybackArithmetic::NativeFloat));
        const float stored = float(frame);
        std::uint32_t bits;
        std::memcpy(&bits, &stored, sizeof bits);
        CHECK(bits == sample.second && frame == double(stored));
    }
    double unchanged = 123;
    for (float invalid : {-1.f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN(), 1e30f}) {
        CHECK(!format::PmmPlaybackFrame(invalid, unchanged,
                                        format::PmmPlaybackArithmetic::NativeFloat));
        CHECK(unchanged == 123);
    }
    editor::EditorSession boundary;
    CHECK(boundary.Edit(
        "terminal-frame-three",
        [](auto& d, auto&) {
            auto start = d.cameraKeys.dense[0];
            auto end = start;
            end.frame = 3;
            end.eye[0] = 7;
            d.timeline.playStopFrame = 100;
            return format::BuildPmmTrack<format::PmmCameraKey>({{start, end}}, d.cameraKeys);
        },
        reason));
    CHECK(boundary.Play(reason));
    CHECK(boundary.Advance(.1, reason));
    CHECK(boundary.Runtime().Session()->Playback().frame == 3);
    CHECK(boundary.Runtime().Session()->Playback().camera.active);
    CHECK(boundary.Document().cameraState[0] == 7);
    CHECK(boundary.Advance(.0001, reason));
    CHECK(!boundary.Runtime().Session()->Playback().camera.active);
    boundary.Pause();
    editor::EditorSession session;
    const auto root = std::filesystem::absolute(argv[1]);
    for (const char* file : {"floor.pmd", "floor.pmx"})
        for (unsigned mode = 0; mode < 4; ++mode)
            for (bool force : {false, true}) {
                editor::EditorSession policy;
                CHECK(policy.ImportModel(root / file, reason));
                CHECK(policy.Edit(
                    "playback physics policy",
                    [=](auto& d, auto&) {
                        d.physics.playbackMode = std::uint8_t(mode);
                        d.appearance.floorVisible = 0;
                        d.timeline.startAtCurrent = 1;
                        return true;
                    },
                    reason));
                const auto* runtime = &policy.Runtime();
                const auto revision = policy.Revision(), frames = runtime->Counters().frames;
                const auto initial = runtime->Bodies(0);
                const bool undo = policy.CanUndo(), redo = policy.CanRedo();
                policy.SetPlaybackPhysicsOnOff(force);
                CHECK(runtime->Counters().frames == frames && runtime->Bodies(0) == initial);
                CHECK(policy.Revision() == revision && policy.CanUndo() == undo &&
                      policy.CanRedo() == redo);
                CHECK(policy.Play(reason));
                const auto activeMode = force ? 2u : mode;
                CHECK(runtime->Simulation().physicsMode == activeMode);
                CHECK(policy.Document().physics.playbackMode == mode);
                policy.SetPlaybackPhysicsOnOff(!force);
                CHECK(runtime->Simulation().physicsMode == activeMode);
                for (unsigned i = 0; i < 30; ++i)
                    CHECK(policy.Advance(1. / 60, reason));
                const auto body = runtime->Bodies(0);
                std::cout << "policy " << file << " mode=" << mode << " force=" << force
                          << " active=" << activeMode << " y=" << initial[0][13] << " -> "
                          << body[0][13] << " physics="
                          << unsigned(policy.Document().models[0].boneKeys.dense[0].physicsDisabled)
                          << '\n';
                CHECK(activeMode ? body[0][13] < initial[0][13] - 1 : body == initial);
                policy.Pause();
                CHECK(!policy.Playing() && runtime == &policy.Runtime());
                CHECK(runtime->Simulation().physicsMode == mode);
                CHECK(policy.Revision() == revision);
                CHECK(policy.Play(reason));
                CHECK(runtime->Simulation().physicsMode == (force ? mode : 2u));
                CHECK(policy.Seek(0, reason));
                CHECK(!policy.Playing() && runtime->Simulation().physicsMode == mode);
                CHECK(policy.New(reason) && policy.PlaybackPhysicsOnOff() == !force);
            }
    for (bool loop : {false, true}) {
        editor::EditorSession policy;
        CHECK(policy.Edit(
            "short forced run",
            [=](auto& d, auto&) {
                d.physics.playbackMode = 0;
                d.timeline.playStopFrame = 3;
                d.timeline.playbackLoop = loop;
                return true;
            },
            reason));
        policy.SetPlaybackPhysicsOnOff(true);
        CHECK(policy.Play(reason) && policy.Runtime().Simulation().physicsMode == 2);
        CHECK(policy.Advance(.2, reason));
        CHECK(policy.Playing() == loop);
        CHECK(policy.Runtime().Simulation().physicsMode == (loop ? 2 : 0));
        policy.Pause();
        CHECK(policy.Runtime().Simulation().physicsMode == 0);
    }
    for (const char* file : {"floor.pmd", "floor.pmx"})
        for (unsigned mode = 0; mode < 4; ++mode) {
            editor::EditorSession idle;
            CHECK(idle.ImportModel(root / file, reason));
            CHECK(idle.Edit(
                "idle physics mode",
                [mode](auto& d, auto&) {
                    d.physics.playbackMode = std::uint8_t(mode);
                    d.appearance.floorVisible = 0;
                    return true;
                },
                reason));
            const auto bodies = idle.Runtime().Bodies(0);
            const auto revision = idle.Revision(), epoch = idle.PlaybackEpoch();
            const bool dirty = idle.Dirty(), undo = idle.CanUndo(), redo = idle.CanRedo();
            const auto cursor = idle.CursorSeconds();
            const auto frames = idle.Runtime().Counters().frames;
            const auto prepared = idle.Runtime().Counters().preparedModels;
            const auto write = [](void* p, const void* bytes, std::size_t count) {
                auto& out = *static_cast<std::vector<std::uint8_t>*>(p);
                const auto* data = static_cast<const std::uint8_t*>(bytes);
                out.insert(out.end(), data, data + count);
                return true;
            };
            std::vector<std::uint8_t> before, after;
            CHECK(format::WritePmmSceneDocument(write, &before, idle.Document()));
            CHECK(idle.TickIdle(0, reason));
            for (double invalid : {-1., 1.01, std::numeric_limits<double>::infinity(),
                                   std::numeric_limits<double>::quiet_NaN()})
                CHECK(!idle.TickIdle(invalid, reason));
            CHECK(idle.Runtime().Counters().frames == frames && idle.Runtime().Bodies(0) == bodies);
            for (unsigned n = 0; n < 60; ++n)
                CHECK(idle.TickIdle(1. / 60, reason));
            CHECK(idle.Runtime().Counters().frames == frames + 60);
            CHECK(idle.Runtime().Counters().preparedModels == prepared + 60);
            const auto result = idle.Runtime().Bodies(0);
            CHECK(mode == 1 || mode == 2 ? result[0][13] < bodies[0][13] - 5 : result == bodies);
            CHECK(idle.CursorSeconds() == cursor && !idle.Playing());
            CHECK(idle.Runtime().Simulation().cursorSeconds == float(cursor));
            CHECK(idle.Revision() == revision && idle.PlaybackEpoch() == epoch &&
                  idle.Dirty() == dirty && idle.CanUndo() == undo && idle.CanRedo() == redo);
            CHECK(format::WritePmmSceneDocument(write, &after, idle.Document()) && before == after);
            CHECK(idle.Play(reason));
            const auto activeFrames = idle.Runtime().Counters().frames;
            CHECK(!idle.TickIdle(.1, reason));
            CHECK(idle.Runtime().Counters().frames == activeFrames);
            CHECK(idle.Advance(.1, reason));
            CHECK(idle.Runtime().Counters().frames == activeFrames + 1);
            idle.Pause();
            const auto pausedCursor = idle.CursorSeconds();
            CHECK(idle.TickIdle(1. / 60, reason));
            CHECK(idle.CursorSeconds() == pausedCursor);
            CHECK(idle.UnloadModel(0, reason));
            CHECK(idle.TickIdle(1. / 60, reason) && idle.Runtime().Bodies(0).empty());
        }
    {
        // Native v1 loading leaves the current floor setting alone when its
        // optional section is absent. A fresh editor starts with floor on.
        editor::EditorSession inherited;
        inherited.SetPhysicsFloor(false);
        CHECK(inherited.Open(root / "v1.pmm", reason));
        CHECK(!inherited.Document().appearance.floorVisible);
        CHECK(inherited.New(reason) && inherited.Open(root / "v1.pmm", reason));
        CHECK(inherited.Document().appearance.floorVisible);
    }
    for (const char* model : {"floor.pmd", "floor.pmx"})
        for (bool enabled : {false, true})
            for (bool reopen : {false, true}) {
                editor::EditorSession floor;
                CHECK(floor.Document().appearance.floorVisible == 1);
                CHECK(floor.ImportModel(root / model, reason));
                const auto* runtime = &floor.Runtime();
                const auto before = floor.Runtime().Bodies(0);
                const auto count = floor.Runtime().Counters().frames;
                const auto revision = floor.Revision();
                floor.SetPhysicsFloor(enabled);
                CHECK(&floor.Runtime() == runtime && floor.Runtime().Bodies(0) == before);
                CHECK(floor.Runtime().Counters().frames == count && floor.Revision() == revision);
                if (reopen) {
                    CHECK(floor.Save(
                        root / "floor.pmm",
                        [](const auto& path, const auto& bytes, auto&) {
                            std::ofstream file(path, std::ios::binary);
                            file.write(reinterpret_cast<const char*>(bytes.data()),
                                       std::streamsize(bytes.size()));
                            return bool(file);
                        },
                        reason));
                    CHECK(floor.Open(root / "floor.pmm", reason));
                }
                CHECK(bool(floor.Document().appearance.floorVisible) == enabled);
                CHECK(floor.Play(reason));
                for (unsigned n = 0; n < 60; ++n)
                    CHECK(floor.Advance(1. / 60., reason));
                const auto bodies = floor.Runtime().Bodies(0);
                CHECK(bodies.size() == 1);
                const float height = bodies[0][13];
                std::cout << "floor model=" << model << " enabled=" << enabled
                          << " reopen=" << reopen << " y=" << height << '\n';
                CHECK(enabled ? height >= .59f && height <= .7f : height < -20);
            }
    CHECK(session.ImportModel(root / "pose.pmd", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    CHECK(session.Edit(
        "animated-globals",
        [](auto& d, auto&) {
            auto camera = d.cameraKeys.dense[0];
            camera.eye = {};
            camera.target = {};
            camera.parentModel = -1;
            camera.distance = -45;
            for (unsigned c = 0; c < 6; ++c) {
                camera.interpolation[0][c] = camera.interpolation[1][c] = 20;
                camera.interpolation[2][c] = camera.interpolation[3][c] = 107;
            }
            auto cameraEnd = camera;
            cameraEnd.frame = 30;
            cameraEnd.eye[0] = 30;
            auto light = d.lightKeys.dense[0];
            light.color = {0, .5f, 1};
            auto lightEnd = light;
            lightEnd.frame = 30;
            lightEnd.color[0] = 1;
            auto shadow = d.selfShadowKeys.dense[0];
            shadow.mode = 0;
            shadow.distance = .01f;
            auto shadowEnd = shadow;
            shadowEnd.frame = 30;
            shadowEnd.mode = 2;
            auto gravity = d.gravityKeys.dense[0];
            gravity.acceleration = 2;
            auto gravityEnd = gravity;
            gravityEnd.frame = 30;
            gravityEnd.acceleration = 10;
            auto accessory = d.accessories[0].keyRecords.dense[0];
            accessory.position = {};
            accessory.scale = 1;
            auto accessoryEnd = accessory;
            accessoryEnd.frame = 30;
            accessoryEnd.position[0] = 60;
            accessoryEnd.scale = 3;
            auto bone = d.models[0].boneKeys.dense[0];
            bone.frame = 30;
            bone.position[0] = 30;
            d.cameraState[0] = 91;
            d.cameraState[3] = 13;
            d.cameraState[4] = 14;
            d.lightState[0] = .9f;
            d.selfShadowMode = 1;
            d.physics.gravityMagnitude = 17;
            d.accessories[0].state.position[0] = 88;
            d.timeline.playStopFrame = 300;
            d.timeline.startAtCurrent = 1;
            return format::BuildPmmTrack<format::PmmCameraKey>({{camera, cameraEnd}},
                                                               d.cameraKeys) &&
                   format::BuildPmmTrack<format::PmmLightKey>({{light, lightEnd}}, d.lightKeys) &&
                   format::BuildPmmTrack<format::PmmSelfShadowKey>({{shadow, shadowEnd}},
                                                                   d.selfShadowKeys) &&
                   format::BuildPmmTrack<format::PmmGravityKey>({{gravity, gravityEnd}},
                                                                d.gravityKeys) &&
                   format::BuildPmmTrack<format::PmmAccessoryKey>({{accessory, accessoryEnd}},
                                                                  d.accessories[0].keyRecords) &&
                   format::RegisterPmmKey(d.models[0].boneKeys, 0, bone);
        },
        reason));
    const auto& d = session.Document();
    CHECK(session.Seek(15, reason));
    CHECK(d.cameraState[0] == 91 && d.cameraState[3] == 13 && d.cameraState[4] == 14);
    CHECK(d.lightState[0] == .9f && d.selfShadowMode == 1 && d.physics.gravityMagnitude == 17);
    CHECK(d.accessories[0].state.position[0] == 88 &&
          Near(d.models[0].bonePose[0].components[0], 15));
    CHECK(session.Play(reason));
    CHECK(session.Document().cameraState[0] == 91 && session.Document().lightState[0] == .9f);
    CHECK(session.Document().accessories[0].state.position[0] == 0 &&
          Near(session.Document().accessories[0].state.scale, 2));
    CHECK(!session.Runtime().Session()->Playback().camera.active);
    const auto frames = session.Runtime().Counters().frames;
    CHECK(session.Advance(.1, reason));
    CHECK(session.Document().cameraState[0] == 91 && session.Document().lightState[0] == .9f);
    CHECK(session.Document().selfShadowMode == 1 &&
          session.Document().physics.gravityMagnitude == 17);
    CHECK(session.Document().accessories[0].state.position[0] == 0 &&
          Near(session.Document().accessories[0].state.scale, 2.2f));
    CHECK(session.Runtime().Counters().frames == frames + 1);
    CHECK(session.Runtime().Session()->Playback().frame == 18);
    CHECK(session.Runtime().Snapshot(0)->frame == 18);
    session.Pause();
    CHECK(session.SelectModel(255, reason));
    CHECK(session.Seek(15, reason));
    CHECK(Near(session.Document().cameraState[0], 15));
    CHECK(session.Document().cameraState[3] == 0 && session.Document().cameraState[4] == 0);
    CHECK(session.Document().lightState[0] == .5f && session.Document().selfShadowMode == 0);
    CHECK(session.Document().physics.gravityMagnitude == 6 &&
          session.Document().accessories[0].state.position[0] == 0 &&
          Near(session.Document().accessories[0].state.scale, 2));
    CHECK(session.Runtime().Simulation().gravityMagnitude == 6);
    CHECK(session.SelectModel(0, reason));
    CHECK(session.SetFollowCamera(true, reason));
    CHECK(session.Seek(12, reason));
    CHECK(Near(session.Document().cameraState[0], 12) &&
          Near(session.Document().cameraState[3], 12));
    CHECK(session.Document().accessories[0].state.position[0] == 0 &&
          Near(session.Document().accessories[0].state.scale, 1.8f));
    CHECK(!session.Document().appearance.suppressCameraTransform);
    CHECK(session.SetFollowCamera(false, reason));
    const auto held = session.Document().cameraState;
    CHECK(session.Seek(20, reason));
    CHECK(session.Document().cameraState == held &&
          Near(session.Document().accessories[0].state.scale, 1.8f));
    CHECK(session.SelectModel(255, reason));
    CHECK(session.Seek(0, reason));
    CHECK(session.Play(reason));
    CHECK(session.Advance(1, reason));
    CHECK(
        session.Runtime().Session()->Playback().camera.active); // exact terminal key remains active
    CHECK(session.Advance(.1, reason));
    const auto& playback = session.Runtime().Session()->Playback();
    CHECK(!playback.camera.active && !playback.light.active && !playback.selfShadow.active &&
          !playback.gravity.active);
    auto camera = session.Runtime().Session()->Pose().camera;
    camera.position = {123, 456, 789};
    camera.distance = -999;
    CHECK(session.SetCamera(camera, reason));
    const auto manual = session.Document().cameraState;
    CHECK(session.Advance(.1, reason));
    CHECK(session.Document().cameraState == manual);
    session.Pause();
    // Standalone/export hosts keep the default full-track seek behavior.
    format::SceneSession standalone;
    CHECK(format::SceneSession::Create(session.Document(), standalone, reason));
    CHECK(standalone.Seek(15));
    CHECK(Near(standalone.Pose().camera.position[0], 15) && standalone.Pose().lightState[0] == .5f);
    CHECK(standalone.Pose().accessories[0].position[0] == 0 &&
          Near(standalone.Pose().accessories[0].scale, 2));
    CHECK(standalone.StartPlayback());
    const auto initial = standalone.Pose();
    CHECK(standalone.Advance(.5f, {false, false})); // physics substep: models only
    CHECK(standalone.Pose().camera.position == initial.camera.position &&
          standalone.Pose().lightState == initial.lightState);
    CHECK(standalone.Pose().accessories[0].position == initial.accessories[0].position);
    CHECK(standalone.Playback().camera.index == 0 &&
          standalone.Playback().accessories[0].index == 0);
    CHECK(standalone.Advance(.5f));
    CHECK(Near(standalone.Pose().camera.position[0], 15) &&
          Near(standalone.Pose().accessories[0].scale, 2));
    std::cout
        << "Editor track application: model/follow/camera gates, accessories, terminal cursors and substeps passed\n";
}
