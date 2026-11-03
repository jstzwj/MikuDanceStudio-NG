#include <Windows.h>
#include "mikudancestudio/app_layout.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/physics_debug_geometry.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>
#include <fstream>
#include <stdexcept>
static void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
#include "original_frame_slice.hpp"
using namespace mikudancestudio;
using cpu_math::Matrix;
using render::PhysicsGizmoMesh;
namespace {
class Jump {
    void* address_;
    std::array<unsigned char, 12> before_;
    void Write(const void* source) {
        DWORD old;
        Check(VirtualProtect(address_, 12, PAGE_EXECUTE_READWRITE, &old),
              "protect original helper");
        std::memcpy(address_, source, 12);
        FlushInstructionCache(GetCurrentProcess(), address_, 12);
        VirtualProtect(address_, 12, old, &old);
    }

public:
    template <class F> Jump(void* address, F function) : address_(address) {
        std::memcpy(before_.data(), address, 12);
        std::array<unsigned char, 12> code{0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
        std::memcpy(code.data() + 2, &function, 8);
        Write(code.data());
    }
    ~Jump() { Write(before_.data()); }
};
using Mul = Matrix*(WINAPI*)(Matrix*, const Matrix*, const Matrix*);
using Scale = Matrix*(WINAPI*)(Matrix*, float, float, float);
using Translate = Matrix*(WINAPI*)(Matrix*, float, float, float);
Mul nativeMultiply;
Scale nativeScale;
Translate nativeTranslate;
std::vector<render::PhysicsGizmoDraw> captured;
std::uint32_t color;
Matrix world;
Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix m;
    nativeMultiply(&m, &a, &b);
    return m;
}
Matrix Scaling(float x, float y, float z) {
    Matrix m;
    nativeScale(&m, x, y, z);
    return m;
}
Matrix Translation(float x, float y, float z) {
    Matrix m;
    nativeTranslate(&m, x, y, z);
    return m;
}
void SetColor(void*, unsigned char r, unsigned char g, unsigned char b) {
    color = 0xff000000u | (unsigned(r) << 16) | (unsigned(g) << 8) | b;
}
void Sphere(void*, float radius, const Matrix* transform) {
    captured.push_back(
        {PhysicsGizmoMesh::Sphere, Multiply(Scaling(radius, radius, radius), *transform), color});
}
void Box(void*, float x, float y, float z, const Matrix* transform) {
    captured.push_back(
        {PhysicsGizmoMesh::Box, Multiply(Scaling(x * 2, y * 2, z * 2), *transform), color});
}
void Capsule(void*, float r, float h, const Matrix* transform) {
    captured.push_back({PhysicsGizmoMesh::Hemisphere,
                        Multiply(Multiply(Scaling(-r, -r, -r), Translation(0, -h, 0)), *transform),
                        color});
    captured.push_back({PhysicsGizmoMesh::Hemisphere,
                        Multiply(Multiply(Scaling(r, r, r), Translation(0, h, 0)), *transform),
                        color});
    captured.push_back(
        {PhysicsGizmoMesh::Cylinder, Multiply(Scaling(r, h * 2, r), *transform), color});
}
void Joint(void*) {
    captured.push_back({PhysicsGizmoMesh::Joint, world, 0});
}
void Axis(void*, void*) {
    captured.push_back({PhysicsGizmoMesh::Axis, world, 0});
}
HRESULT GetTransform(void*, unsigned state, Matrix* output) {
    Check(state == 256, "native world query");
    *output = world;
    return S_OK;
}
HRESULT SetTransform(void*, unsigned state, const Matrix* value) {
    Check(state == 256, "native world write");
    world = *value;
    return S_OK;
}
HRESULT SetState(void*, unsigned, unsigned) {
    return S_OK;
}
HRESULT Clear(void*, unsigned, void*, unsigned flags, unsigned, float, unsigned) {
    Check(flags == 2, "selected joint clears depth only");
    return S_OK;
}
struct Buffer {
    void** vtable;
    std::vector<unsigned char> bytes;
    static HRESULT Lock(Buffer* self, unsigned offset, unsigned, void** data, unsigned) {
        *data = self->bytes.data() + offset;
        return S_OK;
    }
    static HRESULT Unlock(Buffer*) { return S_OK; }
    static ULONG Release(Buffer*) { return 0; }
    static std::array<void*, 14>& Methods() {
        static std::array<void*, 14> methods{};
        methods[2] = reinterpret_cast<void*>(&Release);
        methods[11] = reinterpret_cast<void*>(&Lock);
        methods[12] = reinterpret_cast<void*>(&Unlock);
        return methods;
    }
    explicit Buffer(unsigned count) : vtable(Methods().data()), bytes(count) {}
};
std::vector<std::unique_ptr<Buffer>> buffers;
HRESULT CreateBuffer(void*, unsigned size, unsigned, unsigned, unsigned, void** output, void*) {
    buffers.push_back(std::make_unique<Buffer>(size));
    *output = buffers.back().get();
    return S_OK;
}
void Geometry(OriginalPhysicsModule& original, std::vector<unsigned char>& renderer) {
    PhysicsScene scene{};
    Check(original.Function<bool (*)(PhysicsScene*, void*)>(0x27b0)(&scene, renderer.data()),
          "native primitive creation");
    rhi::ModelReplayResource vertices, indices;
    render::PhysicsGizmoRanges ranges;
    render::PhysicsGizmoGeometry(vertices, indices, ranges);
    const void* nativeVertices[]{scene.gizmoSphereVB, scene.gizmoCubeVB, scene.gizmoSphere33VB,
                                 scene.gizmoArrowVB, scene.gizmoBoxSelVB};
    const void* nativeIndices[]{scene.gizmoSphereIB, scene.gizmoCubeIB, scene.gizmoSphere33IB,
                                scene.gizmoIdentityIB, scene.gizmoBoxSelIB};
    for (unsigned mesh = 0; mesh < 5; ++mesh) {
        const auto& vb = static_cast<const Buffer*>(nativeVertices[mesh])->bytes;
        const auto& ib = static_cast<const Buffer*>(nativeIndices[mesh])->bytes;
        const auto range = ranges[mesh == 4 ? 12 : mesh * 3];
        Check(ib.size() == range.count * 2, "native primitive index count");
        for (unsigned i = 0; i < range.count; ++i) {
            std::uint16_t a, b;
            std::memcpy(&a, ib.data() + i * 2, 2);
            std::memcpy(&b, indices.bytes.data() + (range.first + i) * 2, 2);
            Check(!std::memcmp(vb.data() + a * 16, vertices.bytes.data() + b * 16, 12),
                  "native primitive position bits / index order");
            if (mesh == 4) {
                std::uint32_t argb, rgba;
                std::memcpy(&argb, vb.data() + a * 16 + 12, 4);
                std::memcpy(&rgba, vertices.bytes.data() + b * 16 + 12, 4);
                Check(rgba == ((argb & 0xff00ff00u) | ((argb & 255) << 16) | ((argb >> 16) & 255)),
                      "native joint marker colors");
            }
        }
    }
    // Original native creation accepts equal body references, as used by the
    // model-physics dialog's freshly added joint. Exercise native allocation
    // and destruction, without sharing a modern Bullet object with VC9.
    Check(scene.groundBody && scene.constraintId == 0, "native ground body");
    using CreateJoint =
        int (*)(PhysicsScene*, void*, void*, const float*, const float*, const float*, const float*,
                const float*, const float*, const float*, const float*, const float*, const float*);
    const float zero[3]{}, identity[4]{0, 0, 0, 1};
    Check(original.Function<CreateJoint>(0x5b80)(&scene, scene.groundBody, scene.groundBody, zero,
                                                 identity, zero, identity, zero, zero, zero, zero,
                                                 zero, zero) == 0 &&
              scene.constraintId == 1,
          "native equal-body joint creation");
    original.Function<void (*)(PhysicsScene*)>(0x2580)(&scene);
}
class ImportSwap {
    void** address_;
    void* saved_;
    void Write(void* value) {
        DWORD old;
        Check(VirtualProtect(address_, sizeof value, PAGE_READWRITE, &old), "import protection");
        *address_ = value;
        VirtualProtect(address_, sizeof value, old, &old);
    }

public:
    template <class F>
    ImportSwap(void** address, F function) : address_(address), saved_(*address) {
        Write(reinterpret_cast<void*>(function));
    }
    ~ImportSwap() { Write(saved_); }
};
int echoControl;
std::string echoText;
HWND WINAPI Child(HWND, int id) {
    return reinterpret_cast<HWND>(std::uintptr_t(id));
}
BOOL WINAPI Echo(HWND child, const char* text) {
    echoControl = int(reinterpret_cast<std::uintptr_t>(child));
    echoText = text;
    return TRUE;
}
void Drags(OriginalPhysicsModule& original) {
    ImportSwap child(original.Function<void**>(0x128658), &Child);
    ImportSwap text(original.Function<void**>(0x128598), &Echo);
    OriginalFrameSlice block(original, 0x32f41, 0x35eeb,
                             {0x41, 0x83, 0xbc, 0x24, 0x84, 0x03, 0, 0, 0x0d},
                             {{13, 0.f}, {15, .005f}, {12, 5.f}});
    auto app = std::make_unique<MMDAppState>();
    auto model = std::make_unique<mdl::ModelRecord>();
    app->modelSlots[0] = model.get();
    model->selectedBone = -1;
    app->reservedFrameCopyDialog = reinterpret_cast<HWND>(10000);
    mdl::RigidRecord body{};
    mdl::JointRecord joint{};
    app->reservedRigidScratchArray = &body;
    app->reservedJointScratchArray = &joint;
    app->selectedRigidIndex = app->selectedJointIndex = 0;
    editor::ModelPhysicsEdit seed;
    seed.AddBody();
    seed.AddJoint();
    unsigned cases = 0;
    for (bool jointPage : {false, true})
        for (bool shift : {false, true})
            for (unsigned mode : {4u, 5u, 6u, 10u, 11u, 12u})
                for (int dy : {-121, -1, 0, 23, 1001})
                    for (float initial : {.2f, .1f, -2.5f, 100.123f, 100000.f}) {
                        auto edit = seed;
                        std::fill_n(body.size, 3, initial);
                        std::fill_n(body.position, 3, initial);
                        std::fill_n(body.rotation, 3, initial);
                        std::fill_n(joint.position, 3, initial);
                        std::fill_n(joint.rotation, 3, initial);
                        std::fill_n(edit.bodies[0].size, 3, initial);
                        std::fill_n(edit.bodies[0].position, 3, initial);
                        std::fill_n(edit.bodies[0].rotation, 3, initial);
                        std::fill_n(edit.joints[0].position, 3, initial);
                        std::fill_n(edit.joints[0].rotation, 3, initial);
                        app->physicsEditorJointPage = jointPage;
                        // These historical input members have different x64
                        // frame-driver offsets from the compatibility record.
                        const int modifier = shift ? 3 : 0, y = dy + 7, previous = 7;
                        auto* raw = reinterpret_cast<unsigned char*>(app.get());
                        std::memcpy(raw + 0x28, &modifier, 4);
                        std::memcpy(raw + 0xc, &y, 4);
                        std::memcpy(raw + 0x14, &previous, 4);
                        app->interactionDragMode = mode;
                        echoControl = 0;
                        echoText.clear();
                        block.Run(app.get());
                        const auto actual = edit.Drag(jointPage, 0, mode, dy, shift);
                        char expected[256];
                        std::snprintf(expected, sizeof expected, actual.format,
                                      double(actual.value));
                        if (actual.control != echoControl || echoText != expected) {
                            std::fprintf(
                                stderr,
                                "page=%d shift=%d mode=%u dy=%d initial=%.9g expected=%d/%s native=%d/%s\n",
                                jointPage, shift, mode, dy, initial, actual.control, expected,
                                echoControl, echoText.c_str());
                            Check(false, "native drag field / text");
                        }
                        Check(!std::memcmp(body.size, edit.bodies[0].size, 12) &&
                                  !std::memcmp(body.position, edit.bodies[0].position, 12) &&
                                  !std::memcmp(body.rotation, edit.bodies[0].rotation, 12) &&
                                  !std::memcmp(joint.position, edit.joints[0].position, 12) &&
                                  !std::memcmp(joint.rotation, edit.joints[0].rotation, 12),
                              "native drag float bits");
                        ++cases;
                    }
    std::printf("%u independent original physics drag float/text cases passed; no window or GPU\n",
                cases);
}
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 3, "physics_debug_oracle_test original.exe fixture");
    OriginalPhysicsModule original(argv[1]);
    Drags(original);
    nativeMultiply = *original.Function<Mul*>(0x1288f8);
    nativeScale = *original.Function<Scale*>(0x128900);
    nativeTranslate = *original.Function<Translate*>(0x128910);
    Jump sphere(original.Function<void*>(0x6ea0), &Sphere);
    Jump box(original.Function<void*>(0x7020), &Box);
    Jump capsule(original.Function<void*>(0x7190), &Capsule);
    Jump setColor(original.Function<void*>(0x7500), &SetColor);
    Jump joint(original.Function<void*>(0x7690), &Joint);
    Jump axis(original.Function<void*>(0xddc30), &Axis);
    std::array<void*, 120> methods{};
    methods[43] = reinterpret_cast<void*>(&Clear);
    methods[44] = reinterpret_cast<void*>(&SetTransform);
    methods[45] = reinterpret_cast<void*>(&GetTransform);
    methods[57] = reinterpret_cast<void*>(&SetState);
    methods[26] = methods[27] = reinterpret_cast<void*>(&CreateBuffer);
    void** table = methods.data();
    void* device = &table;
    std::vector<unsigned char> renderer(0x3ab00);
    std::memcpy(renderer.data() + 0x3a9c0, &device, sizeof device);
    Geometry(original, renderer);
    auto app = std::make_unique<MMDAppState>();
    app->renderer = renderer.data();
    auto model = std::make_unique<mdl::ModelRecord>();
    app->modelSlots[0] = model.get();
    editor::EditorSession session;
    std::string reason;
    Check(session.ImportModel(std::filesystem::path(argv[2]) / "articulated.pmd", reason),
          reason.c_str());
    const auto resource = session.Runtime().Resource(0);
    const auto* snapshot = session.Runtime().Snapshot(0);
    std::vector<mdl::BoneRecord> bones(snapshot->bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) {
        std::copy_n(resource->Document().bones[i].fields.position, 3, bones[i].position);
        std::copy_n(snapshot->bones[i].skin.begin(), 16, bones[i].matInit);
    }
    model->boneTable = bones.data();
    model->boneCount = unsigned(bones.size());
    std::vector<mdl::RigidRecord> rigid(100000);
    std::vector<mdl::JointRecord> joints(100000);
    for (auto& b : rigid)
        b.bodyId = -1;
    for (auto& j : joints)
        j.constraint = -1;
    app->reservedRigidScratchArray = rigid.data();
    app->reservedJointScratchArray = joints.data();
    editor::ModelPhysicsEdit edit;
    for (unsigned i = 0; i < 3; ++i) {
        const int b = edit.AddBody();
        edit.bodies[b].shape = std::uint8_t(i);
        std::copy_n(std::array<float, 3>{.5f, 2.f, 1.25f}.data(), 3, edit.bodies[b].size);
        std::copy_n(std::array<float, 3>{.1f, -.2f, .3f}.data(), 3, edit.bodies[b].rotation);
        std::copy_n(std::array<float, 3>{float(i), 2.f, 3.f}.data(), 3, edit.bodies[b].position);
    }
    for (unsigned i = 0; i < 2; ++i) {
        const int j = edit.AddJoint();
        edit.joints[j].rigidA = i;
        edit.joints[j].rigidB = i + 1;
        std::copy_n(std::array<float, 3>{.3f, -.1f, .2f}.data(), 3, edit.joints[j].rotation);
        std::copy_n(std::array<float, 3>{1.f, float(i), 2.f}.data(), 3, edit.joints[j].position);
    }
    unsigned cases = 0;
    for (int bone : {-1, 0, 1})
        for (int body : {-1, 0, 2})
            for (int selectedJoint : {-1, 0, 1})
                for (bool jointPage : {false, true})
                    for (auto initial : {0xffff0000u, 0xff9bff00u}) {
                        for (unsigned i = 0; i < 3; ++i) {
                            auto& b = edit.bodies[i];
                            b.boneIndex = bone;
                            rigid[i].bodyId = int(i);
                            rigid[i].boneIndex = bone;
                            rigid[i].shape = b.shape;
                            std::copy_n(b.size, 3, rigid[i].size);
                            std::copy_n(b.position, 3, rigid[i].position);
                            std::copy_n(b.rotation, 3, rigid[i].rotation);
                        }
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& j = edit.joints[i];
                            joints[i].constraint = int(i);
                            joints[i].rigidA = j.rigidA;
                            joints[i].rigidB = j.rigidB;
                            std::copy_n(j.position, 3, joints[i].position);
                            std::copy_n(j.rotation, 3, joints[i].rotation);
                        }
                        app->selectedRigidIndex = body;
                        app->selectedJointIndex = selectedJoint;
                        app->physicsEditorJointPage = jointPage;
                        color = initial;
                        world = cpu_math::Identity();
                        captured.clear();
                        original.Function<void (*)(MMDAppState*)>(0x911c0)(app.get());
                        std::vector<render::PhysicsGizmoDraw> actual;
                        std::uint32_t next;
                        Check(render::PhysicsGizmoDraws(session.Runtime(),
                                                        {&edit, 0, body, selectedJoint, jointPage},
                                                        initial, actual, next, reason),
                              reason.c_str());
                        Check(actual.size() == captured.size() && next == color,
                              "native draw count / persistent color");
                        for (std::size_t i = 0; i < actual.size(); ++i) {
                            Check(actual[i].mesh == captured[i].mesh &&
                                      actual[i].argb == captured[i].argb,
                                  "native primitive order / color");
                            for (unsigned k = 0; k < 16; ++k)
                                Check(std::isfinite(actual[i].world[k]) &&
                                          std::abs(actual[i].world[k] - captured[i].world[k]) <=
                                              2e-5f,
                                      "native body / joint transform");
                        }
                        ++cases;
                    }
    std::printf(
        "%u independent original physics draw-order/color/transform cases passed; no window or GPU\n",
        cases);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
