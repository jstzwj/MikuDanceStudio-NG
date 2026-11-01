#pragma once
#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/editor_session.hpp"
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
            bones[i].hasRigidBody = physics;
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
