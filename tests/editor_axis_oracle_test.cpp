#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/bone_local_axes.hpp"
#include "mikudancestudio/bone_operation_axis.hpp"
#include "mikudancestudio/text_encoding.hpp"
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
using namespace mikudancestudio;
namespace {
struct Capture {
    std::array<void*, 45> methods{};
    void** vtable = methods.data();
    cpu_math::Matrix world{};
    unsigned transforms = 0, clears = 0;
    static HRESULT Transform(Capture* self, unsigned type, const float* matrix) {
        Check(type == 0x100, "original axis transform type");
        std::copy_n(matrix, 16, self->world.begin());
        ++self->transforms;
        return S_OK;
    }
    static HRESULT Clear(Capture* self, unsigned count, const void* rects, unsigned flags,
                         unsigned color, float depth, unsigned stencil) {
        Check(count == 0 && !rects && flags == 2 && color == 0xffffff && depth == 1 && stencil == 0,
              "original axis depth clear");
        ++self->clears;
        return S_OK;
    }
    // The first field of a COM object must be its vtable, so use a tiny facade.
    struct Device {
        void** vtable;
        Capture* owner;
        static HRESULT Transform(Device* self, unsigned type, const float* matrix) {
            return Capture::Transform(self->owner, type, matrix);
        }
        static HRESULT Clear(Device* self, unsigned count, const void* rects, unsigned flags,
                             unsigned color, float depth, unsigned stencil) {
            return Capture::Clear(self->owner, count, rects, flags, color, depth, stencil);
        }
    } device{methods.data(), this};
    Capture() {
        methods[44] = reinterpret_cast<void*>(&Device::Transform);
        methods[43] = reinterpret_cast<void*>(&Device::Clear);
    }
};
struct Native {
    std::vector<unsigned char> app = std::vector<unsigned char>(0xA6000);
    std::vector<unsigned char> renderer = std::vector<unsigned char>(0x3AA00);
    std::array<unsigned char, 0x4b8> mesh{}; // zero subsets: no real GPU calls
    Capture capture;
    template <class T> void Store(std::size_t offset, const T& value) {
        std::memcpy(app.data() + offset, &value, sizeof value);
    }
    Native() {
        auto* device = &capture.device;
        std::memcpy(renderer.data() + 0x3A9C0, &device, sizeof device);
        Store(0xA16E0, renderer.data());
        Store(0x9FCA8, mesh.data());
    }
};
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 3, "expected original executable and fixture directory");
    OriginalPhysicsModule original(argv[1]);
    const auto reference = original.Function<void (*)(void*, const float*)>(0x37490);
    unsigned cases = 0;
    float maximum = 0;
    for (const char* filename :
         {"drag.pmd", "drag-axes.pmd", "triangle.pmx", "articulated.pmx", "overlay-types.pmd"}) {
        editor::EditorSession session;
        std::string reason;
        Check(session.ImportModel(std::filesystem::path(argv[2]) / filename, reason),
              reason.c_str());
        const auto resource = session.Runtime().Resource(0);
        auto model = std::make_unique<mdl::ModelRecord>();
        std::vector<mdl::BoneRecord> bones(resource->EditingBones().size());
        model->boneCount = unsigned(bones.size());
        model->boneTable = bones.data();
        model->physicsMode = resource->SourceKind() == format::ModelKind::Pmx ? 2 : 1;
        for (unsigned i = 0; i < bones.size(); ++i) {
            auto& b = bones[i];
            const auto& f = resource->Document().bones[i].fields;
            std::copy_n(f.position, 3, b.position);
            std::copy_n(f.localAxes, 6, b.localAxes);
            b.tailBone = std::max(0, resource->EditingBones()[i].tailBone);
            b.type = resource->EditingBones()[i].type;
            b.flags = resource->EditingBones()[i].flags;
            std::string name;
            if (text::EncodeCp932(resource->BoneNames()[i], name))
                std::copy_n(name.data(), std::min(name.size(), sizeof b.name - 1), b.name);
            const auto skin = cpu_math::Multiply(cpu_math::RotationY(.13f * i),
                                                 cpu_math::Translation({1.f * i, 2.3f, -4.7f}));
            std::copy_n(skin.data(), 16, b.matInit);
        }
        Native native;
        native.Store(0xBE8, model.get());
        for (int variant = 0; variant < 3; ++variant) {
            const auto frame =
                cpu_math::Multiply(cpu_math::Multiply(cpu_math::RotationX(.21f * variant),
                                                      cpu_math::RotationZ(-.37f * variant)),
                                   cpu_math::Translation({-.7f, 1.3f, .9f}));
            const std::array<float, 3> eye{.4f * variant, -.8f * variant, -45.f};
            const float fov = std::array{15.f, 30.f, 75.f}[variant];
            native.Store(0x340, eye[0]);
            native.Store(0x344, eye[1]);
            native.Store(0xA1900, eye[2]);
            native.Store(0x9F0B4, fov);
            for (unsigned active = 0; active < bones.size(); ++active) {
                model->selectedBone = int(active);
                for (int coordinate : {0, 1})
                    for (int operation : {0, 9, 14, 15})
                        for (int mode = 0; mode <= 18; ++mode) {
                            native.Store(0x9FCA4, coordinate);
                            native.Store(0x13FC, operation);
                            native.Store(0x384, mode);
                            native.capture.transforms = native.capture.clears = 0;
                            reference(native.app.data(), frame.data());
                            cpu_math::Matrix actual;
                            const bool built = cpu_bone::OperationAxis(
                                bones[active], operation, mode, coordinate, frame, eye, fov,
                                [&](float* out) {
                                    cpu_bone::LocalAxes(model.get(), out, [](float* v) {
                                        cpu_model::Math{}.normalize(v, v);
                                    });
                                },
                                [](const auto& a, const auto& b) {
                                    return cpu_math::Multiply(a, b);
                                },
                                actual);
                            Check(native.capture.transforms == unsigned(built) &&
                                      native.capture.clears == unsigned(built),
                                  "axis eligibility differs from original");
                            if (built)
                                for (unsigned c = 0; c < 16; ++c) {
                                    const auto error =
                                        std::abs(actual[c] - native.capture.world[c]);
                                    maximum = std::max(maximum, error);
                                    if (!std::isfinite(actual[c]) || error > 2e-5f) {
                                        std::fprintf(
                                            stderr,
                                            "%s active=%u variant=%d coord=%d operation=%d mode=%d component=%u actual=%.9g original=%.9g\n",
                                            filename, active, variant, coordinate, operation, mode,
                                            c, actual[c], native.capture.world[c]);
                                        throw std::runtime_error(
                                            "axis transform differs from original");
                                    }
                                }
                            ++cases;
                        }
            }
        }
    }
    std::printf(
        "Original x64 operation axis: %u cases, maximum absolute error %.9g, no windows or GPU\n",
        cases, maximum);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
