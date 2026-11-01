#include <Windows.h>
#include "mikudancestudio/editor_session.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
static void Check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
#include "original_physics_module.hpp"
#include "original_frame_slice.hpp"
using namespace mikudancestudio;
namespace {
struct Buffer {
    void** table;
    std::array<unsigned char, 168> bytes{};
    unsigned locks = 0;
};
HRESULT WINAPI Lock(Buffer* buffer, UINT offset, UINT size, void** data, DWORD) {
    if (offset || size != buffer->bytes.size())
        return E_INVALIDARG;
    ++buffer->locks;
    *data = buffer->bytes.data();
    return S_OK;
}
HRESULT WINAPI Unlock(Buffer*) {
    return S_OK;
}
struct Native {
    std::vector<unsigned char> app = std::vector<unsigned char>(0xA6000);
    std::vector<unsigned char> renderer = std::vector<unsigned char>(0x3AA00);
    std::array<void*, 13> table{};
    Buffer buffer{table.data()};
    template <class T> void Store(unsigned offset, const T& value) {
        std::memcpy(app.data() + offset, &value, sizeof value);
    }
    template <class T> T Read(unsigned offset) const {
        T value;
        std::memcpy(&value, app.data() + offset, sizeof value);
        return value;
    }
    Native() {
        table[11] = reinterpret_cast<void*>(&Lock);
        table[12] = reinterpret_cast<void*>(&Unlock);
        Store(0x9F320, &buffer);
        Store(0xA16E0, renderer.data());
        Store(0xA15DB, std::uint8_t(1)); // avoid native pointer warps; no desktop calls
        Store(0xA1DF4, 1000);
        Store(0xA1DF8, 800);
    }
    void ViewScale(float scale) { std::memcpy(renderer.data() + 0x3A9D4, &scale, 4); }
};
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 3, "expected original executable and fixture directory");
    OriginalPhysicsModule original(argv[1]);
    OriginalFrameSlice block(
        original, 0x32207, 0x32BEA,
        {0x49, 0x8b, 0xcc, 0xe8, 0xde, 0xe1, 0xfe, 0xff, 0x45, 0x89, 0xbc, 0x24, 0x78, 0},
        {{6, .05f},
         {7, .005f},
         {9, .3f},
         {10, .0002f},
         {11, .000002f},
         {13, .00002f},
         {14, .01f},
         {15, .005f}},
        {{8, 0x7fffffffffffffffull}});
    editor::EditorSession session;
    std::string reason;
    const auto fixture = std::filesystem::path(argv[2]);
    Check(session.ImportModel(fixture / "pose.pmd", reason), reason.c_str());
    Native native;
    unsigned cases = 0;
    float maximum = 0;
    const auto compare = [&](const auto& camera, unsigned operation, int dx, int dy, bool shift,
                             bool control, float viewScale, bool picture) {
        const auto initial = session.Document().cameraState;
        native.Store(0x328, std::uint8_t(session.Selected().model == 255));
        native.Store(0x380, int(operation));
        native.Store(0x28, shift ? 3 : 0);
        native.Store(0xC4, control ? 3 : 0);
        native.Store(0x8, 500 + dx);
        native.Store(0xC, 500 + dy);
        native.Store(0x10, 500);
        native.Store(0x14, 500);
        native.Store(0xA1404, camera.parentModel);
        native.Store(0x36C, camera.position);
        native.Store(0x348, camera.rotation);
        native.Store(0x340, std::array{initial[3], initial[4]});
        native.Store(0xA1900, camera.distance);
        native.Store(0x9F0B4, camera.fov);
        native.ViewScale(viewScale);
        native.buffer.locks = 0;
        const auto& background = session.Document().picture;
        native.Store(0x9F318, picture ? std::uintptr_t(1) : std::uintptr_t(0));
        native.Store(0x9F310, std::uint8_t(background.enabled));
        native.Store(0x9F328, background.offsetX);
        native.Store(0x9F32C, background.offsetY);
        native.Store(0x9F330, background.scale);
        if (picture) {
            native.Store(0x9F334, int(session.Assets().picture->width));
            native.Store(0x9F338, int(session.Assets().picture->height));
        }
        block.Run(native.app.data());
        Check(native.buffer.locks == unsigned(picture && control && background.enabled),
              "original picture refresh count");
        Check(session.DragViewTool(operation, dx, dy, shift, control, viewScale, reason),
              reason.c_str());
        const auto& actual = session.Document();
        const std::array<unsigned, 9> offsets{0x36C,   0x370, 0x374, 0x340, 0x344,
                                              0xA1900, 0x348, 0x34C, 0x350};
        for (unsigned field = 0; field < 10; ++field) {
            const float value = field < 9 ? actual.cameraState[field] : actual.picture.scale;
            const float reference = native.Read<float>(field < 9 ? offsets[field] : 0x9F330);
            const auto error = std::abs(value - reference);
            maximum = std::max(maximum, error);
            if (!std::isfinite(value) || error > 2e-5f) {
                std::fprintf(
                    stderr,
                    "case=%u op=%u dx=%d dy=%d shift=%d ctrl=%d picture=%d field=%u actual=%.9g original=%.9g\n",
                    cases, operation, dx, dy, shift, control, picture, field, value, reference);
                throw std::runtime_error("header drag differs from original");
            }
        }
        Check(actual.picture.offsetX == native.Read<std::int32_t>(0x9F328) &&
                  actual.picture.offsetY == native.Read<std::int32_t>(0x9F32C),
              "picture offset truncation");
        ++cases;
    };
    for (bool cameraMode : {false, true}) {
        Check(session.SelectModel(cameraMode ? 255 : 0, reason), reason.c_str());
        for (int parent : {-1, 0})
            for (float depth : {-.3f, .5f, -45.f})
                for (unsigned variant = 0; variant < 3; ++variant)
                    for (unsigned precision = 0; precision < 4; ++precision)
                        for (const auto motion :
                             {std::array{-31, 47}, std::array{-1, -3}, std::array{0, 0},
                              std::array{3, 1}, std::array{47, -31}})
                            for (unsigned operation : {1, 2}) {
                                auto camera = session.Runtime().Session()->Pose().camera;
                                camera.position = {.1f, -1.7f, depth};
                                camera.rotation = {.17f * variant, -.31f * variant, .23f * variant};
                                camera.parentModel = parent;
                                camera.parentBone = 0;
                                camera.fov = 30.f + 15.f * variant;
                                camera.distance = -37.3f;
                                Check(session.SetCamera(camera, reason), reason.c_str());
                                compare(camera, operation, motion[0], motion[1], precision & 1,
                                        precision & 2, 1.f, false);
                            }
    }
    Check(session.ImportPicture(fixture / "navigation-picture.tga", 640, 480, reason),
          reason.c_str());
    for (bool visible : {false, true}) {
        Check(session.ShowPicture(visible, reason), reason.c_str());
        for (bool cameraMode : {false, true}) {
            Check(session.SelectModel(cameraMode ? 255 : 0, reason), reason.c_str());
            for (float scale : {.125f, .4790625f, 1.f, 2.75f})
                for (bool shift : {false, true})
                    for (const auto motion :
                         {std::array{-31, 47}, std::array{-1, -3}, std::array{0, 0},
                          std::array{3, 1}, std::array{47, -31}})
                        for (unsigned operation : {1, 2})
                            compare(session.Runtime().Session()->Pose().camera, operation,
                                    motion[0], motion[1], shift, true, scale, true);
        }
    }
    std::printf(
        "Original x64 header gestures: %u cases, maximum absolute error %.9g; no windows or GPU\n",
        cases, maximum);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
