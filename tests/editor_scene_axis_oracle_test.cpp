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
unsigned echoes = 0;
HWND WINAPI Child(HWND, int) {
    return nullptr;
}
BOOL WINAPI Echo(HWND, LPCSTR) {
    ++echoes;
    return TRUE;
}
// Isolate the reference's edit-control echo. No window procedure or desktop
// API is invoked; arithmetic and sprintf_s remain the original instructions.
class Import {
    void** slot_;
    void* saved_;

public:
    Import(const OriginalPhysicsModule& module, unsigned rva, const char* name, void* replacement)
        : slot_(module.Function<void**>(rva)), saved_(*slot_) {
        Check(saved_ ==
                  reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), name)),
              "original UI import identity");
        DWORD old;
        Check(VirtualProtect(slot_, sizeof(void*), PAGE_READWRITE, &old) != 0, "protect UI import");
        *slot_ = replacement;
        VirtualProtect(slot_, sizeof(void*), old, &old);
    }
    ~Import() {
        DWORD old;
        VirtualProtect(slot_, sizeof(void*), PAGE_READWRITE, &old);
        *slot_ = saved_;
        VirtualProtect(slot_, sizeof(void*), old, &old);
    }
};
struct Native {
    std::vector<unsigned char> app = std::vector<unsigned char>(0xA6000);
    std::array<unsigned char, 0x4b8> accessory{};
    template <class T> void Store(std::size_t offset, const T& value) {
        std::memcpy(app.data() + offset, &value, sizeof value);
    }
    float Read(std::size_t offset) const {
        float value;
        std::memcpy(&value, app.data() + offset, 4);
        return value;
    }
    float Accessory(unsigned offset) const {
        float value;
        std::memcpy(&value, accessory.data() + offset, 4);
        return value;
    }
};
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 3, "expected original executable and fixture directory");
    OriginalPhysicsModule original(argv[1]);
    Import child(original, 0x128658, "GetDlgItem", reinterpret_cast<void*>(&Child));
    Import echo(original, 0x128598, "SetWindowTextA", reinterpret_cast<void*>(&Echo));
    OriginalFrameSlice block(
        original, 0x35eeb, 0x36b66,
        {0x41, 0x8b, 0x8c, 0x24, 0x84, 0x03, 0, 0, 0x41, 0x8b, 0x54, 0x24, 0x0c, 0x41},
        {{6, .05f}, {7, .005f}, {11, .5f}, {12, 0.f}, {13, -0.f}});
    editor::EditorSession session;
    std::string reason;
    Check(session.ImportModel(std::filesystem::path(argv[2]) / "pose.pmd", reason), reason.c_str());
    Check(session.ImportAccessory(std::filesystem::path(argv[2]) / "bound.x", reason),
          reason.c_str());
    Check(session.SelectModel(255, reason), reason.c_str());
    Native native;
    unsigned cases = 0;
    float maximum = 0;
    for (int coordinate : {0, 1, 2}) {
        while (session.CoordinateSystem() != unsigned(coordinate))
            session.CycleCoordinateSystem();
        for (int parent : {-1, 0})
            for (int variant = 0; variant < 3; ++variant)
                for (int precision = 0; precision < 4; ++precision)
                    for (int dy : {-31, -1, 0, 3, 47})
                        for (unsigned mode = 13; mode <= 18; ++mode) {
                            const bool shift = precision == 1 || precision == 3;
                            const bool control = precision >= 2;
                            auto camera = session.Runtime().Session()->Pose().camera;
                            camera.position = {.1f, -1.7f, 2.3f};
                            camera.rotation = {.17f * variant, -.31f * variant, .23f * variant};
                            camera.parentModel = parent;
                            camera.parentBone = 0;
                            Check(session.SetCamera(camera, reason), reason.c_str());
                            auto accessory = session.Document().accessories[0].state;
                            accessory.position = {.1f, 7.3f, -9.7f};
                            accessory.rotation = {.1f, -.2f, .3f};
                            Check(session.SetAccessory(0, accessory, reason), reason.c_str());
                            native.Store(0x328, std::uint8_t(1));
                            native.Store(0x384, int(mode));
                            native.Store(0x9FCA4, coordinate);
                            native.Store(0xA1404, parent);
                            native.Store(0xC, dy);
                            native.Store(0x14, 0);
                            native.Store(0x28, shift ? 3 : 0);
                            native.Store(0xC4, control ? 3 : 0);
                            native.Store(0x36C, camera.position);
                            native.Store(0x348, camera.rotation);
                            native.Store(0x9E840, native.accessory.data());
                            std::memcpy(native.accessory.data() + 0x224, accessory.position.data(),
                                        12);
                            std::memcpy(native.accessory.data() + 0x230, accessory.rotation.data(),
                                        12);
                            echoes = 0;
                            block.Run(native.app.data());
                            Check(echoes == unsigned(coordinate == 2), "original edit echo count");
                            Check(session.DragSceneAxis(mode, dy, shift, control, reason),
                                  reason.c_str());
                            const auto actual = session.Runtime().Session()->Pose().camera;
                            const auto actualAccessory = session.Document().accessories[0].state;
                            for (unsigned field = 0; field < 4; ++field)
                                for (unsigned c = 0; c < 3; ++c) {
                                    const auto value = field == 0   ? actual.position[c]
                                                       : field == 1 ? actual.rotation[c]
                                                       : field == 2 ? actualAccessory.position[c]
                                                                    : actualAccessory.rotation[c];
                                    const auto reference = field == 0   ? native.Read(0x36c + 4 * c)
                                                           : field == 1 ? native.Read(0x348 + 4 * c)
                                                           : field == 2
                                                               ? native.Accessory(0x224 + 4 * c)
                                                               : native.Accessory(0x230 + 4 * c);
                                    const auto error = std::abs(value - reference);
                                    maximum = std::max(maximum, error);
                                    if (!std::isfinite(value) || error > 2e-6f) {
                                        std::fprintf(
                                            stderr,
                                            "coord=%d parent=%d variant=%d precision=%d dy=%d mode=%u field=%u component=%u actual=%.9g original=%.9g\n",
                                            coordinate, parent, variant, precision, dy, mode, field,
                                            c, value, reference);
                                        throw std::runtime_error(
                                            "scene axis differs from original");
                                    }
                                }
                            ++cases;
                        }
    }
    std::printf(
        "Original x64 camera/accessory axes: %u cases, maximum absolute error %.9g, no windows or GPU\n",
        cases, maximum);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
