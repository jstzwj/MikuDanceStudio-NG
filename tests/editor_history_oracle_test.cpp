#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/editor_session.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
static void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
struct NativeHistory {
    OriginalPhysicsModule& original;
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 8> bones{};
    std::array<mdl::BoneKey, 8> keys{};
    std::array<unsigned char, 8> edits{};
    int frame = 0;
    explicit NativeHistory(OriginalPhysicsModule& o) : original(o) {
        model->boneCount = unsigned(bones.size());
        model->boneTable = bones.data();
        model->bonePhysicsState = edits.data();
        model->boneKeys = keys.data();
        model->UndoCursor() = model->LatestUndoCursor() = 29;
        for (unsigned i = 0; i < bones.size(); ++i) {
            bones[i].rotQuat[3] = keys[i].rotation[3] = 1;
            keys[i].allocated = 1;
            for (unsigned c = 0; c < 16; ++c)
                keys[i].interpolation[c] = c < 8 ? 20 : 107;
        }
    }
    void Free(void* p) {
        if (p)
            (*original.Function<void (**)(void*)>(0x128450))(p);
    }
    ~NativeHistory() {
        for (auto& ring : model->undoRings)
            for (auto& slot : ring.slots) {
                Free(slot.bonePose);
                Free(slot.keySnapshots);
            }
    }
    void Set(unsigned bone, float x) {
        const auto at = (model->UndoCursor() + 1) % 30;
        model->UndoCursor() = model->LatestUndoCursor() = at;
        auto& record = model->undoRings[0].slots[at];
        Free(record.bonePose);
        record.operation = mdl::UndoOperation::BonePose;
        record.recordCount = 1;
        record.bonePose = static_cast<mdl::BonePoseSnapshot*>(
            original.Function<void* (*)(std::size_t)>(0xdea6c)(sizeof(mdl::BonePoseSnapshot)));
        auto& pose = *record.bonePose;
        pose = {};
        pose.boneIndex = int(bone);
        std::copy_n(bones[bone].trans, 3, pose.position);
        std::copy_n(bones[bone].rotQuat, 4, pose.rotation);
        pose.poseEdited = edits[bone];
        bones[bone].trans[0] = x;
        edits[bone] = 1;
    }
    void Undo() {
        original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcef90)(model.get(), &frame);
    }
    void Redo() {
        original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcfb80)(model.get(), &frame);
    }
    void Register(unsigned bone) {
        const auto at = (model->UndoCursor() + 1) % 30;
        model->UndoCursor() = model->LatestUndoCursor() = at;
        auto& record = model->undoRings[0].slots[at];
        Free(record.bonePose);
        Free(record.keySnapshots);
        record.operation = mdl::UndoOperation::BoneKeys;
        record.frame = unsigned(frame);
        record.recordCount = 1;
        record.bonePose =
            static_cast<mdl::BonePoseSnapshot*>(original.Function<void* (*)(std::size_t)>(0xdea6c)(
                sizeof(mdl::BonePoseSnapshot) * bones.size()));
        for (unsigned i = 0; i < bones.size(); ++i) {
            auto& pose = record.bonePose[i];
            pose = {};
            pose.boneIndex = int(i);
            std::copy_n(bones[i].trans, 3, pose.position);
            std::copy_n(bones[i].rotQuat, 4, pose.rotation);
            pose.poseEdited = edits[i];
        }
        record.keySnapshots = static_cast<mdl::BoneKeyUndoEntry*>(
            original.Function<void* (*)(std::size_t)>(0xdea6c)(sizeof(mdl::BoneKeyUndoEntry)));
        record.keySnapshots->index = int(bone);
        record.keySnapshots->key = keys[bone];
        std::copy_n(bones[bone].trans, 3, keys[bone].position);
        std::copy_n(bones[bone].rotQuat, 4, keys[bone].rotation);
        keys[bone].physicsDisabled = bones[bone].physicsDisabled;
        edits[bone] = 0;
    }
    void RedoKeys() {
        Redo();
        // The UI calls SeekModelFrame after a key redo. This fixture has only
        // frame-zero keys, so its independent evaluation is a direct copy.
        for (unsigned i = 0; i < bones.size(); ++i) {
            std::copy_n(keys[i].position, 3, bones[i].trans);
            std::copy_n(keys[i].rotation, 4, bones[i].rotQuat);
        }
    }
    void Seek(int target) {
        original.Function<void (*)(mdl::ModelRecord*, int)>(0xcbb40)(model.get(), frame);
        for (auto& b : bones) {
            std::fill_n(b.trans, 3, 0.f);
            std::fill_n(b.rotQuat, 3, 0.f);
            b.rotQuat[3] = 1;
        }
        edits.fill(0);
        frame = target;
    }
    void Compare(const editor::EditorSession& s) const {
        Check(s.Document().timeline.currentFrame == frame, "original frame differs");
        const auto& poses = s.Document().models[0].bonePose;
        Check(poses.size() == bones.size(), "fixture topology");
        for (unsigned i = 0; i < poses.size(); ++i) {
            Check(!std::memcmp(poses[i].components.data(), bones[i].trans, 12),
                  "original position differs");
            Check(!std::memcmp(poses[i].components.data() + 3, bones[i].rotQuat, 16),
                  "original quaternion differs");
            Check(poses[i].flags[1] == edits[i], "original edit marker differs");
            const auto& key = s.Document().models[0].boneKeys.dense[i];
            Check(key.frame == keys[i].frame && key.previous == keys[i].previous &&
                      key.next == keys[i].next && key.allocated == keys[i].allocated &&
                      key.physicsDisabled == keys[i].physicsDisabled &&
                      !std::memcmp(key.interpolation.data(), keys[i].interpolation, 16) &&
                      !std::memcmp(key.position.data(), keys[i].position, 12) &&
                      !std::memcmp(key.rotation.data(), keys[i].rotation, 16),
                  "original root key differs");
        }
    }
};
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3)
        return 2;
    OriginalPhysicsModule original(argv[1]);
    NativeHistory native(original);
    editor::EditorSession portable;
    std::string reason;
    Check(portable.ImportModel(std::filesystem::path(argv[2]) / "pose.pmd", reason),
          reason.c_str());
    unsigned count = 0;
    const auto compare = [&] {
        native.Compare(portable);
        ++count;
    };
    for (int i = 1; i <= 35; ++i) {
        native.Set(1, float(i));
        Check(portable.SetBone(0, 1, {float(i), 0, 0}, {0, 0, 0, 1}, reason), reason.c_str());
        compare();
    }
    for (unsigned i = 0; i < 30; ++i) {
        native.Undo();
        Check(portable.Undo(reason), reason.c_str());
        compare();
    }
    Check(!portable.CanUndo(), "history depth");
    for (unsigned i = 0; i < 30; ++i) {
        native.Redo();
        Check(portable.Redo(reason), reason.c_str());
        compare();
    }
    Check(!portable.CanRedo(), "redo depth");
    native.Seek(12);
    Check(portable.Seek(12, reason), reason.c_str());
    compare();
    for (unsigned i = 0; i < 3; ++i) {
        native.Undo();
        Check(portable.Undo(reason), reason.c_str());
        compare();
        native.Redo();
        Check(portable.Redo(reason), reason.c_str());
        compare();
    }
    {
        NativeHistory registered(original);
        editor::EditorSession session;
        Check(session.ImportModel(std::filesystem::path(argv[2]) / "pose.pmd", reason),
              reason.c_str());
        for (unsigned bone : {1u, 2u, 3u}) {
            registered.Set(bone, float(bone + 3));
            Check(session.SetBone(0, bone, {float(bone + 3), 0, 0}, {0, 0, 0, 1}, reason),
                  reason.c_str());
            registered.Register(bone);
            Check(session.Register({{editor::Track::Bone, 0, bone, 0}}, reason), reason.c_str());
            registered.Compare(session);
            ++count;
            for (unsigned cycle = 0; cycle < 3; ++cycle) {
                registered.Undo();
                Check(session.Undo(reason), reason.c_str());
                registered.Compare(session);
                ++count;
                registered.RedoKeys();
                Check(session.Redo(reason), reason.c_str());
                registered.Compare(session);
                ++count;
            }
        }
    }
    std::printf("Original x64 undo/redo: %u exact pose/frame checkpoints, no windows\n", count);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
