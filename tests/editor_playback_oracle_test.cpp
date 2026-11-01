#include <Windows.h>
#include "mikudancestudio/app_layout.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/pmm_edit.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
static void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
struct DeviceRecorder {
    void** vtable;
    unsigned projections = 0, lights = 0;
    static HRESULT WINAPI Transform(DeviceRecorder* self, D3DTRANSFORMSTATETYPE type,
                                    const D3DMATRIX*) {
        Check(type == D3DTS_PROJECTION, "unexpected native transform call");
        ++self->projections;
        return S_OK;
    }
    static HRESULT WINAPI Light(DeviceRecorder* self, DWORD index, const D3DLIGHT9*) {
        Check(index == 0, "unexpected native light call");
        ++self->lights;
        return S_OK;
    }
};
// No D3D object or device is created: the two observed COM calls are recorded
// in memory. The original entrypoint, windows and message loop are never used.
struct NativePlayback {
    std::unique_ptr<MMDAppState> app = std::make_unique<MMDAppState>();
    std::unique_ptr<D3DRenderer> wrapper = std::make_unique<D3DRenderer>();
    std::array<void*, 120> table{};
    DeviceRecorder device{table.data()};
    std::array<mdl::CameraKey, 2> camera{};
    std::array<mdl::LightKey, 2> light{};
    std::array<mdl::SelfShadowKey, 2> shadow{};
    std::array<mdl::GravityKey, 2> gravity{};
    std::array<mdl::AccessoryKey, 2> accessory{};
    mdl::AccessoryRecord object{};
    explicit NativePlayback(unsigned terminal) {
        table[44] = reinterpret_cast<void*>(&DeviceRecorder::Transform);
        table[51] = reinterpret_cast<void*>(&DeviceRecorder::Light);
        wrapper->device = reinterpret_cast<IDirect3DDevice9*>(&device);
        wrapper->aspectRatio = 4.f / 3;
        app->renderer = wrapper.get();
        app->cameraKeyTrack = camera.data();
        app->lightKeyTrack = light.data();
        app->selfShadowKeyTrack = shadow.data();
        app->gravityKeyTrack = gravity.data();
        app->accKeyTracks[0] = accessory.data();
        app->objectSlots[0] = &object;
        app->cameraTrackActive = app->lightTrackActive = app->shadowTrackActive =
            app->gravityTrackActive = 1;
        app->reservedAccessoryTrackActive[0] = 1;
        for (unsigned k = 0; k < 2; ++k) {
            camera[k].frame = light[k].frame = shadow[k].frame = gravity[k].frame =
                accessory[k].frame = k * terminal;
            camera[k].next = light[k].next = shadow[k].next = gravity[k].next = accessory[k].next =
                k ? 0 : 1;
            camera[k].parentModel = -1;
            camera[k].distance = -45.f + k * 13;
            camera[k].fov = 30 + k * 40;
            camera[k].eye[0] = k * 30.f;
            camera[k].eye[1] = 5.f + k * 2;
            camera[k].target[0] = k * .25f;
            for (unsigned c = 0; c < 6; ++c) {
                camera[k].interpolation[0][c] = camera[k].interpolation[1][c] = 20;
                camera[k].interpolation[2][c] = camera[k].interpolation[3][c] = 107;
            }
            for (unsigned c = 0; c < 3; ++c) {
                light[k].color[c] = .2f * c + k * .5f;
                light[k].direction[c] = -1.f + .25f * c + k;
                gravity[k].direction[c] = -1.f + .5f * c + k;
                accessory[k].position[c] = k * (10.f + c);
            }
            shadow[k].mode = std::uint8_t(1 + k);
            shadow[k].distance = .01f + k * .02f;
            gravity[k].acceleration = 2.f + k * 8;
            gravity[k].noise = 3 + k * 8;
            gravity[k].noiseEnabled = std::uint8_t(k);
            accessory[k].parentModel = -1;
            accessory[k].scale = 1.f + k * 2;
            accessory[k].opacity = .25f + k * .5f;
            accessory[k].visible = 1;
        }
    }
    format::PmmSceneDocument Scene() const {
        auto scene = format::NewPmmScene();
        for (unsigned k = 0; k < 2; ++k) {
            format::PmmCameraKey c;
            c.frame = camera[k].frame;
            c.next = camera[k].next;
            c.parentModel = -1;
            c.distance = camera[k].distance;
            c.fov = camera[k].fov;
            std::copy_n(camera[k].eye, 3, c.eye.begin());
            std::copy_n(camera[k].target, 3, c.target.begin());
            std::copy_n(&camera[k].interpolation[0][0], 24, &c.interpolation[0][0]);
            format::PmmLightKey l;
            l.frame = c.frame;
            l.next = c.next;
            std::copy_n(light[k].color, 3, l.color.begin());
            std::copy_n(light[k].direction, 3, l.direction.begin());
            format::PmmSelfShadowKey s;
            s.frame = c.frame;
            s.next = c.next;
            s.mode = shadow[k].mode;
            s.distance = shadow[k].distance;
            format::PmmGravityKey g;
            g.frame = c.frame;
            g.next = c.next;
            g.acceleration = gravity[k].acceleration;
            g.noise = gravity[k].noise;
            g.noiseEnabled = gravity[k].noiseEnabled;
            std::copy_n(gravity[k].direction, 3, g.direction.begin());
            format::PmmAccessoryKey a;
            a.frame = c.frame;
            a.next = c.next;
            a.scale = accessory[k].scale;
            a.opacity = accessory[k].opacity;
            a.parentModel = -1;
            a.visible = 1;
            std::copy_n(accessory[k].position, 3, a.position.begin());
            if (!k) {
                scene.cameraKeys.dense = {c};
                scene.lightKeys.dense = {l};
                scene.selfShadowKeys.dense = {s};
                scene.gravityKeys.dense = {g};
                scene.accessories.resize(1);
                scene.accessories[0].keyRecords.dense = {a};
            } else {
                scene.cameraKeys.sparse = {{1, 0, c}};
                scene.lightKeys.sparse = {{1, 0, l}};
                scene.selfShadowKeys.sparse = {{1, 0, s}};
                scene.gravityKeys.sparse = {{1, 0, g}};
                scene.accessories[0].keyRecords.sparse = {{1, 0, a}};
            }
        }
        return scene;
    }
    void Current(const format::PmmSceneEvaluation& pose) {
        std::copy(pose.camera.position.begin(), pose.camera.position.end(), app->cameraPosition);
        app->cameraPitch = pose.camera.rotation[0];
        app->cameraYaw = pose.camera.rotation[1];
        app->cameraRoll = pose.camera.rotation[2];
        app->cameraDistance = pose.camera.distance;
        app->cameraFov = pose.camera.fov;
        app->cameraParentModel = -1;
        std::copy_n(pose.lightState.begin(), 3, app->reservedLightColor);
        std::copy_n(pose.lightState.begin() + 3, 3, app->reservedLightDirection);
        app->selfShadowMode = pose.selfShadowMode;
        app->selfShadowDistance = pose.selfShadowDistance;
        app->gravityMagnitude = pose.gravity.acceleration;
        app->gravityX = pose.gravity.direction[0];
        app->gravityY = pose.gravity.direction[1];
        app->gravityZ = pose.gravity.direction[2];
        app->gravityNoise = pose.gravity.noise;
        app->gravityNoiseEnabled = pose.gravity.noiseEnabled;
        const auto& a = pose.accessories[0];
        std::copy(a.position.begin(), a.position.end(), object.position);
        object.scale = a.scale;
        object.opacity = a.opacity;
    }
};
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 2)
        return 2;
    OriginalPhysicsModule original(argv[1]);
    unsigned checkpoints = 0;
    float maximum = 0;
    const auto equal = [&](float a, float b) {
        const float error = std::abs(a - b);
        maximum = std::max(maximum, error);
        if (!std::isfinite(error) || error > 2e-5f) {
            std::fprintf(stderr, "checkpoint=%u actual=%.9g original=%.9g\n", checkpoints, a, b);
            Check(false, "original playback value differs");
        }
        ++checkpoints;
    };
    std::vector<float> times{0.f,      .1f,
                             .125f,    .183353333f,
                             .33335f,  .5f,
                             .667123f, std::nextafter(1.f, 0.f),
                             1.f,      std::nextafter(1.f, 2.f),
                             1.125f,   1.25f};
    for (unsigned tick = 1; tick < 997; ++tick)
        times.push_back(float(tick) / 997.f);
    std::sort(times.begin(), times.end());
    for (bool cameraMode : {false, true})
        for (bool follow : {false, true})
            for (unsigned terminal : {3u, 30u}) {
                NativePlayback native(terminal);
                const auto scene = native.Scene();
                format::PmmScenePlaybackState playback;
                Check(format::StartPmmScenePlayback(scene, playback), "portable start");
                native.Current(playback.pose);
                native.app->optflag[0] = cameraMode;
                native.app->followCameraEnabled = follow;
                unsigned projectionCalls = 0, lightCalls = 0;
                for (float seconds : times) {
                    for (bool substep : {true, false}) {
                        const bool globals = !substep && (cameraMode || follow);
                        projectionCalls += globals && native.app->cameraTrackActive;
                        lightCalls += globals && native.app->lightTrackActive;
                        native.app->playbackCursorSeconds = seconds;
                        original.Function<void (*)(void*, int)>(0x69A20)(native.app.get(),
                                                                         substep ? 1 : 0);
                        Check(format::AdvancePmmScenePlayback(
                                  scene, seconds, playback,
                                  {globals, !substep, format::PmmPlaybackArithmetic::NativeFloat}),
                              "portable advance");
                        const auto& p = playback.pose;
                        for (unsigned i = 0; i < 3; ++i) {
                            equal(p.camera.position[i], native.app->cameraPosition[i]);
                            equal(p.lightState[i], native.app->reservedLightColor[i]);
                            equal(p.lightState[i + 3], native.app->reservedLightDirection[i]);
                            equal(p.accessories[0].position[i], native.object.position[i]);
                        }
                        equal(p.camera.rotation[0], native.app->cameraPitch);
                        equal(p.camera.distance, native.app->cameraDistance);
                        equal(p.camera.fov, native.app->cameraFov);
                        equal(float(p.selfShadowMode), float(native.app->selfShadowMode));
                        equal(p.selfShadowDistance, native.app->selfShadowDistance);
                        equal(p.gravity.acceleration, native.app->gravityMagnitude);
                        equal(float(p.gravity.noise), float(native.app->gravityNoise));
                        equal(float(p.gravity.noiseEnabled),
                              float(native.app->gravityNoiseEnabled));
                        equal(p.gravity.direction[0], native.app->gravityX);
                        equal(p.gravity.direction[1], native.app->gravityY);
                        equal(p.gravity.direction[2], native.app->gravityZ);
                        equal(p.accessories[0].scale, native.object.scale);
                        equal(p.accessories[0].opacity, native.object.opacity);
                        Check(playback.camera.index == native.app->cameraTrackCursor &&
                                  playback.camera.active == bool(native.app->cameraTrackActive),
                              "camera cursor differs");
                        Check(playback.light.index == native.app->lightTrackCursor &&
                                  playback.light.active == bool(native.app->lightTrackActive),
                              "light cursor differs");
                        Check(playback.selfShadow.index == native.app->shadowTrackCursor &&
                                  playback.selfShadow.active == bool(native.app->shadowTrackActive),
                              "shadow cursor differs");
                        Check(playback.gravity.index == native.app->gravityTrackCursor &&
                                  playback.gravity.active == bool(native.app->gravityTrackActive),
                              "gravity cursor differs");
                        Check(playback.accessories[0].index ==
                                      native.app->accessoryTrackCursor[0] &&
                                  playback.accessories[0].active ==
                                      bool(native.app->reservedAccessoryTrackActive[0]),
                              "accessory cursor differs");
                        if (seconds == 1.125f && !substep) {
                            playback.pose.camera.position[0] = native.app->cameraPosition[0] = 123;
                            playback.pose.lightState[0] = native.app->reservedLightColor[0] = .875f;
                            playback.pose.gravity.acceleration = native.app->gravityMagnitude = 42;
                        }
                    }
                }
                Check(native.device.projections == projectionCalls,
                      "native projection gate differs");
                Check(native.device.lights == lightCalls, "native light gate differs");
            }
    std::printf(
        "Original x64 playback application: %u value checkpoints, maximum absolute error %.9g, recorded COM only, no windows\n",
        checkpoints, maximum);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
