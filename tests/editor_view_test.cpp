#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include <cmath>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
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
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= 2e-5f;
}
int main(int argc, char** argv) {
    CHECK(argc == 2 || argc == 3);
    editor::EditorSession session;
    CHECK(session.ImportModel(std::filesystem::absolute(argv[1]) / "pose.pmd", reason));
    CHECK(session.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
    CHECK(session.SetBone(0, 3, {4, 5, 6}, {0, 0, .6f, .8f}, reason));
    CHECK(session.SelectBones({3}, 3, reason));
    auto camera = session.Runtime().Session()->Pose().camera;
    camera.position = {3, 7, -19};
    camera.rotation = {};
    camera.distance = -45;
    camera.fov = 30;
    camera.parentModel = -1;
    CHECK(session.SetCamera(camera, reason));
    render::SceneRenderSettings settings;
    settings.width = 641;
    settings.height = 479;
    settings.editorModel = 0;
    render::ModelFrameInputs frame;
    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
    CHECK(frame.world == cpu_math::Identity()); // editing pivot is independent of camera target
    CHECK(frame.view.editorNavigation);
    CHECK(frame.view.editorEye[2] == -45);
    CHECK(session.DragView(true, 20, -10, false, false, reason));
    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
    CHECK(frame.view.editorEye[0] == session.Document().cameraState[3]);
    CHECK(frame.view.editorEye[1] == session.Document().cameraState[4]);
    CHECK(session.Edit(
        "model-center",
        [](auto& d, auto&) {
            d.timeline.cameraReferenceMode = 1;
            return true;
        },
        reason));
    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
    const auto& root = session.Runtime().Snapshot(0)->bones[0].skin;
    CHECK(frame.world[12] == -root[12] && frame.world[13] == -root[13] &&
          frame.world[14] == -root[14]);
    CHECK(session.Edit(
        "active-bone",
        [](auto& d, auto&) {
            d.timeline.cameraReferenceMode = 2;
            return true;
        },
        reason));
    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
    const auto active = session.Runtime().Snapshot(0)->bones[3].worldPosition;
    CHECK(frame.world[12] == -active[0] && frame.world[13] == -active[1] &&
          frame.world[14] == -active[2]);
    CHECK(session.SelectBones({}, -1, reason));
    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
    CHECK(frame.world == cpu_math::Identity());
    settings.editorModel = 255;
    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
    CHECK(frame.world[12] == -3 && frame.world[13] == -7 && frame.world[14] == 19);
    camera = session.Runtime().Session()->Pose().camera;
    camera.parentModel = 0;
    camera.parentBone = 3;
    CHECK(session.SetCamera(camera, reason));
    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
    CHECK(frame.view.editorEye == camera.position);
    const auto& skin = session.Runtime().Snapshot(0)->bones[3].skin;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c)
            CHECK(frame.lightRotation[4 * r + c] == skin[4 * c + r]);
    const auto focused = cpu_math::TransformPoint(active, frame.world);
    CHECK(Near(focused[0], 0) && Near(focused[1], 0) && Near(focused[2], 0));
    CHECK(session.Edit(
        "suppress",
        [](auto& d, auto&) {
            d.appearance.suppressCameraTransform = 1;
            return true;
        },
        reason));
    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
    CHECK(frame.lightRotation == cpu_math::Identity());
    CHECK(frame.world[12] == -active[0] && frame.world[13] == -active[1]);
    camera = session.Runtime().Session()->Pose().camera;
    camera.perspective = 1;
    CHECK(session.SetCamera(camera, reason));
    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
    rhi::ModelPipeline pipeline;
    CHECK(render::BuildModelInputs(frame, {}, {}, pipeline));
    CHECK(pipeline.shader.projection[0] == -2.f / camera.position[2]);
    CHECK(pipeline.shader.projection[5] == (float(641) / 479) * pipeline.shader.projection[0]);
    CHECK(Near(pipeline.shader.view[12], -3) && Near(pipeline.shader.view[13], -7) &&
          Near(pipeline.shader.view[14], 19));

#ifdef _WIN32
    if (argc == 3) {
        // Explicit independent D3DX math oracle. No device, HWND or desktop.
        CHECK(std::string(argv[2]) == "--d3dx");
        HMODULE module = LoadLibraryW(L"d3dx9_43.dll");
        CHECK(module);
        using Matrix = cpu_math::Matrix;
        using Rotate = Matrix*(WINAPI*)(Matrix*, float);
        using Multiply = Matrix*(WINAPI*)(Matrix*, const Matrix*, const Matrix*);
        const auto rotX = reinterpret_cast<Rotate>(GetProcAddress(module, "D3DXMatrixRotationX"));
        const auto rotY = reinterpret_cast<Rotate>(GetProcAddress(module, "D3DXMatrixRotationY"));
        const auto rotZ = reinterpret_cast<Rotate>(GetProcAddress(module, "D3DXMatrixRotationZ"));
        const auto multiply =
            reinterpret_cast<Multiply>(GetProcAddress(module, "D3DXMatrixMultiply"));
        CHECK(rotX && rotY && rotZ && multiply);
        unsigned cases = 0;
        for (float pitch : {-.9f, 0.f, .3f, 1.5708f})
            for (float yaw : {-3.141592f, -.7f, 0.f, 2.f})
                for (float roll : {-.4f, 0.f, 1.f}) {
                    camera = session.Runtime().Session()->Pose().camera;
                    camera.rotation = {pitch, yaw, roll};
                    CHECK(session.SetCamera(camera, reason));
                    CHECK(render::SceneViewInputs(session.Runtime(), settings, frame, reason));
                    Matrix x, y, z, expected;
                    rotX(&x, pitch);
                    rotY(&y, yaw);
                    rotZ(&z, roll);
                    multiply(&expected, &y, &x);
                    multiply(&expected, &expected, &z);
                    for (unsigned i = 0; i < 16; ++i)
                        CHECK(Near(frame.lightRotation[i], expected[i]));
                    // Independently D3DX-created free-camera pan matrix.
                    camera.parentModel = -1;
                    camera.position = {0, 0, 0};
                    CHECK(session.SetCamera(camera, reason));
                    rotZ(&z, -roll);
                    multiply(&expected, &x, &y);
                    multiply(&expected, &z, &expected);
                    CHECK(session.SelectModel(255, reason));
                    CHECK(session.DragView(true, 13, -17, false, false, reason));
                    const auto actual = session.Runtime().Session()->Pose().camera.position;
                    CHECK(Near(actual[0], -13.f * expected[0] * .05f - 17.f * expected[4] * .05f));
                    CHECK(Near(actual[1], -13.f * expected[1] * .05f - 17.f * expected[5] * .05f));
                    CHECK(Near(actual[2], 13.f * expected[2] * .05f + 17.f * expected[6] * .05f));
                    camera.parentModel = 0;
                    CHECK(session.SetCamera(camera, reason));
                    ++cases;
                }
        FreeLibrary(module);
        std::cout << "D3DX view rotation and camera pan: " << cases << " cases passed\n";
    }
#endif
    std::cout
        << "Editor view: model/active-bone pivots, offsets, parent transpose and orthographic inputs passed\n";
}
