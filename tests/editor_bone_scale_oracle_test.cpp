#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "editor_bone_scale_fixture.hpp"
#include <cstdio>
#include <fstream>
#include <cstring>
#include <memory>
#include <stdexcept>
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
class Patch {
    void* address_;
    std::vector<unsigned char> before_;
    void Write(const void* bytes) {
        DWORD old;
        Check(VirtualProtect(address_, before_.size(), PAGE_EXECUTE_READWRITE, &old),
              "patch protection");
        std::memcpy(address_, bytes, before_.size());
        FlushInstructionCache(GetCurrentProcess(), address_, before_.size());
        VirtualProtect(address_, before_.size(), old, &old);
    }

public:
    Patch(void* address, const void* bytes, std::size_t count) : address_(address), before_(count) {
        std::memcpy(before_.data(), address, count);
        Write(bytes);
    }
    ~Patch() { Write(before_.data()); }
};
std::array<std::string, 12> fields;
HWND WINAPI Child(HWND, int id) {
    return id >= 686 && id < 698 ? reinterpret_cast<HWND>(std::uintptr_t(id)) : nullptr;
}
int WINAPI Text(HWND window, LPSTR text, int capacity) {
    const auto& value = fields.at(unsigned(reinterpret_cast<std::uintptr_t>(window)) - 686);
    const auto size = std::min(value.size(), std::size_t(capacity - 1));
    std::memcpy(text, value.data(), size);
    text[size] = 0;
    return int(size);
}
void Put(mdl::BoneKey& out, const format::PmmBoneKey& key) {
    out.frame = key.frame;
    out.previous = key.previous;
    out.next = key.next;
    out.allocated = key.allocated;
    out.physicsDisabled = key.physicsDisabled;
    std::copy(key.position.begin(), key.position.end(), out.position);
    std::copy(key.rotation.begin(), key.rotation.end(), out.rotation);
    std::copy(key.interpolation.begin(), key.interpolation.end(), out.interpolation);
}
struct Native {
    OriginalPhysicsModule& original;
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::vector<mdl::BoneRecord> bones;
    std::vector<mdl::BoneKey> keys{mdl::kBoneKeyCapacity};
    std::vector<unsigned char> edits;
    Native(OriginalPhysicsModule& o, const editor::EditorSession& session) : original(o) {
        const auto& source = session.Document().models[0];
        bones.resize(source.bonePose.size());
        edits.resize(bones.size());
        model->boneTable = bones.data();
        model->boneCount = unsigned(bones.size());
        model->boneKeys = keys.data();
        model->bonePhysicsState = edits.data();
        model->UndoCursor() = model->LatestUndoCursor() = 29;
        for (unsigned i = 0; i < bones.size(); ++i) {
            format::PmmEvaluatedBone live;
            bool physics;
            std::string reason;
            Check(session.Runtime().BoneRegistrationPose(0, i, 0, live, physics, reason),
                  reason.c_str());
            std::copy(live.translation.begin(), live.translation.end(), bones[i].trans);
            std::copy(live.rotation.begin(), live.rotation.end(), bones[i].rotQuat);
            edits[i] = source.bonePose[i].flags[1];
        }
        Copy(source.boneKeys, keys);
    }
    static void Copy(const format::PmmKeyTrack<format::PmmBoneKey>& track,
                     std::vector<mdl::BoneKey>& out) {
        for (unsigned i = 0; i < track.dense.size(); ++i)
            Put(out[i], track.dense[i]);
        for (const auto& key : track.sparse)
            Put(out.at(unsigned(key.index)), key.value);
        for (const auto& [i, key] : track.unused)
            Put(out.at(i), key);
    }
    ~Native() {
        const auto free = *original.Function<void (**)(void*)>(0x128450);
        for (auto& ring : model->undoRings)
            for (auto& slot : ring.slots) {
                if (slot.bonePose)
                    free(slot.bonePose);
                if (slot.keySnapshots)
                    free(slot.keySnapshots);
            }
    }
    void Compare(const editor::EditorSession& session, float& maximum,
                 bool rotationEdit = false) const {
        std::vector<mdl::BoneKey> expected(mdl::kBoneKeyCapacity);
        Copy(session.Document().models[0].boneKeys, expected);
        for (unsigned i = 0; i < keys.size(); ++i) {
            for (unsigned c = 0; c < 4; ++c) {
                const float error = std::abs(expected[i].rotation[c] - keys[i].rotation[c]);
                maximum = std::max(maximum, error);
                if (!rotationEdit || !keys[i].allocated)
                    Check(!std::memcmp(&expected[i].rotation[c], &keys[i].rotation[c], 4),
                          "native unchanged quaternion differs");
                Check(std::isfinite(error) && error <= 2e-5f, "native scaled quaternion differs");
                // Only rotations have the existing CPU math budget. Position,
                // marks, curves, topology, flags and all unoccupied bytes are exact.
                expected[i].rotation[c] = keys[i].rotation[c];
            }
        }
        Check(!std::memcmp(keys.data(), expected.data(), keys.size() * sizeof(mdl::BoneKey)),
              "native bone scale or history pool differs");
    }
};
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3)
        return 2;
    OriginalPhysicsModule original(argv[1]);
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1),
        curve(original.Function<void*>(0x62bb0), &ret, 1),
        seek(original.Function<void*>(0xcbd90), &ret, 1),
        refresh(original.Function<void*>(0x20dd0), &ret, 1);
    auto child = &Child;
    auto text = &Text;
    Patch getChild(original.Function<void*>(0x128658), &child, sizeof child),
        getText(original.Function<void*>(0x128690), &text, sizeof text);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    float maximum = 0;
    for (const char* file : {"facial.pmd", "articulated.pmx"})
        for (const auto& q : {cpu_math::Quaternion{0, 0, 0, 1},
                              {.2f, .3f, .4f, .5f},
                              {.5f, .5f, -.5f, .5f},
                              {-.5f, .5f, .5f, .5f}})
            for (unsigned variant = 0; variant < 8; ++variant) {
                editor::EditorSession portable;
                std::string reason;
                Check(BoneScaleFixture(portable, argv[2], file, q, reason), reason.c_str());
                if (variant == 7)
                    Check(portable.SetSelectedKeys({}, reason, {{editor::Track::Bone, 0}}),
                          reason.c_str());
                Native native(original, portable);
                auto* model = native.model.get();
                std::fill(app.begin(), app.end(), 0);
                std::memcpy(app.data() + 0xbe8, &model, sizeof model);
                int frame = portable.Document().timeline.currentFrame;
                std::memcpy(app.data() + 0x1450, &frame, 4);
                auto values = BoneScaleIdentity;
                if (variant >= 1 && variant <= 6) {
                    values[2 * (variant - 1)] = -1.25f;
                    values[2 * (variant - 1) + 1] = 35.7f;
                }
                for (unsigned i = 0; i < values.size(); ++i) {
                    char field[24];
                    std::snprintf(field, sizeof field, "%.9g", double(values[i]));
                    fields[i] = field;
                }
                original.Function<void (*)(HWND)>(0x9c9c0)(nullptr);
                Check(portable.ScaleBoneKeys(values, reason), reason.c_str());
                native.Compare(portable, maximum, variant >= 4 && variant <= 6);
                Check(app[0xa1b31], "native bone scale dirty flag");
                Check(model->UndoCursor() == (variant == 7 ? 29 : 0),
                      "native bone scale history gate");
                if (variant != 7) {
                    const auto& undo = model->undoRings[0].slots[0];
                    Check(undo.frame == 15 && undo.recordCount == 3,
                          "native selected-slot capture");
                    if (variant == 1) {
                        Check(portable.SetSelectedKeys({{editor::Track::Bone, 0, 0, 20}}, reason,
                                                       {{editor::Track::Bone, 0}}),
                              reason.c_str());
                        for (auto& key : native.keys)
                            key.allocated = 0;
                        native.keys[native.bones.size() + 1].allocated = 1;
                    }
                    for (unsigned cycle = 0; cycle < 2; ++cycle) {
                        original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcef90)(model,
                                                                                      &frame);
                        Check(portable.Undo(reason), reason.c_str());
                        native.Compare(portable, maximum);
                        original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcfb80)(model,
                                                                                      &frame);
                        Check(portable.Redo(reason), reason.c_str());
                        native.Compare(portable, maximum, variant >= 4 && variant <= 6);
                    }
                }
                ++cases;
            }
    std::printf(
        "Original x64 bone scale: %u full-pool/undo/redo cases, quaternion max error %.9g, no windows\n",
        cases, maximum);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
