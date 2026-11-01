#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/pmm_edit.hpp"
#include "frame_line_deletion_fixture.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <type_traits>
static void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
using namespace format;
namespace {
HWND WINAPI Child(HWND, int id) {
    return reinterpret_cast<HWND>(std::uintptr_t(id));
}
BOOL WINAPI Enable(HWND, BOOL) {
    return TRUE;
}
int automaticInterpolation = 0;
LRESULT WINAPI CheckState(HWND, UINT, WPARAM, LPARAM) {
    return automaticInterpolation;
}
template <class Out, class In> Out ConvertKey(const In& key) {
    Out out{};
#define FIELD(name)                                                                                \
    static_assert(sizeof(out.name) == sizeof(key.name));                                           \
    std::memcpy(&out.name, &key.name, sizeof(out.name))
    FIELD(frame);
    FIELD(previous);
    FIELD(next);
    if constexpr (std::is_same_v<Out, PmmBoneKey> || std::is_same_v<In, PmmBoneKey>) {
        FIELD(interpolation);
        FIELD(position);
        FIELD(rotation);
        FIELD(allocated);
        FIELD(physicsDisabled);
    } else if constexpr (std::is_same_v<Out, PmmMorphKey> || std::is_same_v<In, PmmMorphKey>) {
        FIELD(value);
        FIELD(allocated);
    } else if constexpr (std::is_same_v<Out, PmmCameraKey> || std::is_same_v<In, PmmCameraKey>) {
        FIELD(distance);
        FIELD(eye);
        FIELD(target);
        FIELD(parentModel);
        FIELD(parentBone);
        FIELD(interpolation);
        FIELD(perspective);
        FIELD(selected);
        FIELD(fov);
    } else {
        FIELD(color);
        FIELD(direction);
        FIELD(selected);
    }
#undef FIELD
    return out;
}
class Patch {
    unsigned char* address_;
    std::vector<unsigned char> saved_;
    void Write(const void* data) {
        DWORD old;
        Check(VirtualProtect(address_, saved_.size(), PAGE_EXECUTE_READWRITE, &old) != 0,
              "protect patch");
        std::memcpy(address_, data, saved_.size());
        FlushInstructionCache(GetCurrentProcess(), address_, saved_.size());
        VirtualProtect(address_, saved_.size(), old, &old);
    }

public:
    Patch(const OriginalPhysicsModule& module, unsigned rva, const char* name, void* replacement)
        : address_(module.Function<unsigned char*>(rva)), saved_(sizeof(void*)) {
        std::memcpy(saved_.data(), address_, saved_.size());
        Check(*reinterpret_cast<void**>(address_) ==
                  reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), name)),
              "import identity");
        Write(&replacement);
    }
    Patch(const OriginalPhysicsModule& module, unsigned rva, unsigned target)
        : address_(module.Function<unsigned char*>(rva)), saved_(5) {
        std::memcpy(saved_.data(), address_, 5);
        std::int32_t relative;
        std::memcpy(&relative, address_ + 1, 4);
        Check(address_[0] == 0xe8 && std::intptr_t(rva) + 5 + relative == target,
              "excluded call identity");
        const std::array<unsigned char, 5> nop{0x90, 0x90, 0x90, 0x90, 0x90};
        Write(nop.data());
    }
    ~Patch() { Write(saved_.data()); }
};
template <class T, class Native>
unsigned Exercise(OriginalPhysicsModule& original, bool modelTrack, bool primary,
                  unsigned capacity) {
    unsigned count = 0;
    for (unsigned cursor : {0, 1, 2, 15, 16, 30, 31})
        for (unsigned previousMaximum : {0, 99}) {
            auto model = std::make_unique<mdl::ModelRecord>();
            std::array<mdl::BoneRecord, 3> bones{};
            std::array<unsigned char, 3> edited{};
            model->boneCount = model->morphCount = 3;
            model->boneTable = bones.data();
            model->bonePhysicsState = edited.data();
            model->UndoCursor() = model->LatestUndoCursor() = 29;
            model->maxFrame = previousMaximum;
            PmmKeyTrack<T> track;
            track.dense.resize(modelTrack ? 3 : 1);
            // Independent sparse ordering and a hole before every used index.
            T first{}, middle{}, last{};
            first.frame = 1;
            first.next = 19;
            middle.frame = 15;
            middle.previous = 13;
            middle.next = 7;
            last.frame = 30;
            last.previous = 19;
            track.dense[0].next = 13;
            track.sparse = {{7, 0, last}, {19, 0, middle}, {13, 0, first}};
            std::vector<Native> records(capacity);
            const auto assign = [&](unsigned i, const T& key) {
                auto& target = records[i];
                std::memset(&target, 0x5a, sizeof target);
                target.frame = key.frame;
                target.previous = key.previous;
                target.next = key.next;
            };
            for (unsigned i = 0; i < track.dense.size(); ++i)
                assign(i, track.dense[i]);
            for (const auto& key : track.sparse)
                assign(unsigned(key.index), key.value);
            std::vector<unsigned char> app(0xa6000);
            const auto store = [&](unsigned offset, const auto& value) {
                std::memcpy(app.data() + offset, &value, sizeof value);
            };
            store(0x328, std::uint8_t(!modelTrack));
            store(0xbe8, model.get());
            store(0x1450, cursor);
            store(0x9f038, previousMaximum);
            if (modelTrack) {
                if (primary)
                    model->boneKeys = reinterpret_cast<mdl::BoneKey*>(records.data());
                else
                    model->morphKeys = reinterpret_cast<mdl::MorphKey*>(records.data());
            } else
                store(primary ? 0x3d0 : 0x3d8, records.data());
            auto expected = records;
            unsigned affected = 0;
            Check(InsertPmmFrameLine(track, cursor, affected), "portable insertion");
            unsigned maximum = previousMaximum;
            for (const auto& key : track.sparse) {
                expected[key.index].frame = key.value.frame;
                if (key.value.frame > cursor)
                    maximum = std::max(maximum, key.value.frame);
            }
            original.Function<void (*)(void*)>(primary ? 0x89a90 : 0x8b230)(app.data());
            Check(std::memcmp(records.data(), expected.data(), records.size() * sizeof(Native)) ==
                      0,
                  "original complete key table differs");
            unsigned nativeMaximum;
            std::memcpy(&nativeMaximum, app.data() + 0x9f038, 4);
            Check((modelTrack ? model->maxFrame : nativeMaximum) == maximum,
                  "native maximum differs");
            Check(bool(app[0xa1b31]) == (!modelTrack || !primary || affected != 0),
                  "native dirty/no-op differs");
            if (modelTrack && primary && affected) {
                const auto& undo = model->undoRings[0].slots[model->UndoCursor()];
                Check(undo.recordCount == affected && undo.frame == cursor,
                      "native insertion history");
            }
            if constexpr (std::is_same_v<Native, mdl::BoneKey>) {
                if (affected) {
                    for (auto& key : records)
                        key.allocated = 0xa5;
                    auto redoKeys = records;
                    auto undoKeys = records;
                    for (const auto& key : track.sparse)
                        if (key.value.frame > cursor) {
                            --undoKeys[key.index].frame;
                            undoKeys[key.index].allocated = 0x5a;
                        }
                    int restoredFrame = int(cursor);
                    original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcef90)(model.get(),
                                                                                  &restoredFrame);
                    Check(std::memcmp(records.data(), undoKeys.data(),
                                      records.size() * sizeof(Native)) == 0,
                          "native insertion undo restores only touched key selection");
                    original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcfb80)(model.get(),
                                                                                  &restoredFrame);
                    Check(std::memcmp(records.data(), redoKeys.data(),
                                      records.size() * sizeof(Native)) == 0,
                          "native insertion redo restores displaced key selection");
                }
            }
            for (auto& ring : model->undoRings)
                for (auto& slot : ring.slots) {
                    auto free = *original.Function<void (**)(void*)>(0x128450);
                    if (slot.bonePose)
                        free(slot.bonePose);
                    if (slot.keySnapshots)
                        free(slot.keySnapshots);
                }
            ++count;
        }
    return count;
}

template <class T, class Native>
unsigned ExerciseDeletion(OriginalPhysicsModule& original, unsigned capacity, std::uint64_t& hash) {
    constexpr bool bone = std::is_same_v<T, PmmBoneKey>;
    constexpr bool modelTrack = bone || std::is_same_v<T, PmmMorphKey>;
    constexpr bool primary = bone || std::is_same_v<T, PmmCameraKey>;
    unsigned count = 0;
    for (unsigned order = 0; order < 3; ++order)
        for (unsigned cursor : {0, 1, 2, 15, 16, 30, 31})
            for (int automatic : {0, 1}) {
                automaticInterpolation = automatic;
                auto track = frame_line_fixture::Make<T>(order);
                auto model = std::make_unique<mdl::ModelRecord>();
                std::array<mdl::BoneRecord, 3> bones{};
                std::array<unsigned char, 3> edited{};
                model->boneCount = model->morphCount = 3;
                model->boneTable = bones.data();
                model->bonePhysicsState = edited.data();
                model->UndoCursor() = model->LatestUndoCursor() = 29;
                model->maxFrame = 99;
                std::vector<Native> records(capacity);
                for (const auto& [index, key] : frame_line_fixture::Records(track))
                    records[index] = ConvertKey<Native>(key);
                std::vector<unsigned char> app(0xa6000);
                const auto store = [&](unsigned offset, const auto& value) {
                    std::memcpy(app.data() + offset, &value, sizeof value);
                };
                store(0x328, std::uint8_t(!modelTrack));
                store(0xbe8, model.get());
                store(0x1450, cursor);
                store(0x9f038, unsigned(99));
                if constexpr (bone)
                    model->boneKeys = records.data();
                else if constexpr (modelTrack)
                    model->morphKeys = records.data();
                else
                    store(primary ? 0x3d0 : 0x3d8, records.data());
                PmmFrameLineDeletion<T> deletion;
                Check(DeletePmmFrameLine(track, cursor, automatic != 0, deletion),
                      "portable deletion");
                original.Function<void (*)(void*)>(primary ? 0x8a110 : 0x8b3a0)(app.data());
                const auto compare = [&](const char* stage) {
                    const auto expected = frame_line_fixture::Records(track);
                    for (unsigned index = 0; index < records.size(); ++index) {
                        const bool alive = index < track.dense.size() || records[index].frame;
                        Check(alive == bool(expected.count(index)), "native record presence");
                        if (alive && frame_line_fixture::Bytes(ConvertKey<T>(records[index])) !=
                                         frame_line_fixture::Bytes(expected.at(index))) {
                            std::fprintf(stderr, "order=%u cursor=%u auto=%d index=%u stage=%s\n",
                                         order, cursor, automatic, index, stage);
                            Check(false, "native persisted record differs");
                        }
                    }
                    frame_line_fixture::Hash(hash, track);
                    if constexpr (std::is_same_v<T, PmmBoneKey>) {
                        mdl::DisplayKey display{};
                        display.visible = 1;
                        model->displayKeys = &display;
                        model->morphCount = 0;
                        for (auto& bone : bones)
                            bone.slotIndex = -1;
                        const auto doc = frame_line_fixture::Scene(track);
                        for (unsigned sample : {0, 1, 2, 7, 14, 15, 29, 30}) {
                            PmmSceneEvaluation pose;
                            Check(EvaluatePmmSceneFrame(doc, sample, pose),
                                  "portable deletion seek");
                            original.Function<int (*)(mdl::ModelRecord*, int, int)>(0xcbd90)(
                                model.get(), int(sample), 0);
                            for (unsigned i = 0; i < bones.size(); ++i) {
                                const auto& value = pose.models[0].bones[i];
                                if (std::memcmp(bones[i].trans, value.translation.data(), 12) ||
                                    std::memcmp(bones[i].rotQuat, value.rotation.data(), 16)) {
                                    std::fprintf(
                                        stderr,
                                        "seek: order=%u cursor=%u auto=%d sample=%u bone=%u stage=%s native=%g portable=%g\n",
                                        order, cursor, automatic, sample, i, stage,
                                        bones[i].trans[0], value.translation[0]);
                                    Check(false, "native deletion seek pose differs");
                                }
                            }
                        }
                        model->displayKeys = nullptr;
                    }
                    ++count;
                };
                compare("delete");
                Check(model->maxFrame == 99 && app[0x9f038] == 99, "deletion keeps maximum");
                Check(bool(app[0xa1b31]) == (!bone || deletion.affected), "deletion dirty/no-op");
                if constexpr (bone) {
                    if (deletion.affected) {
                        const auto& undo = model->undoRings[0].slots[model->UndoCursor()];
                        Check(undo.recordCount == deletion.captured.size(), "capture count");
                        for (unsigned i = 0; i < undo.recordCount; ++i) {
                            const auto& key = undo.keySnapshots[i];
                            Check(frame_line_fixture::Bytes(ConvertKey<T>(key.key)) ==
                                      frame_line_fixture::Bytes(deletion.captured.at(key.index)),
                                  "capture at native mutation point");
                        }
                        for (unsigned cycle = 0; cycle < 3; ++cycle) {
                            std::map<std::uint32_t, T> redo, unused;
                            if (!RestorePmmKeyRecords(track, deletion.captured, redo)) {
                                std::fprintf(stderr, "partial undo: order=%u cursor=%u auto=%d\n",
                                             order, cursor, automatic);
                                Check(false, "portable partial undo");
                            }
                            int frame = int(cursor);
                            original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcef90)(
                                model.get(), &frame);
                            compare("undo");
                            Check(RestorePmmKeyRecords(track, redo, unused),
                                  "portable partial redo");
                            original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcfb80)(
                                model.get(), &frame);
                            compare("redo");
                        }
                    }
                }
                for (auto& ring : model->undoRings)
                    for (auto& slot : ring.slots) {
                        auto free = *original.Function<void (**)(void*)>(0x128450);
                        if (slot.bonePose)
                            free(slot.bonePose);
                        if (slot.keySnapshots)
                            free(slot.keySnapshots);
                    }
            }
    return count;
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 2, "provide original x64 executable");
    OriginalPhysicsModule original(argv[1]);
    Patch child(original, 0x128658, "GetDlgItem", reinterpret_cast<void*>(&Child));
    Patch enable(original, 0x1285d0, "EnableWindow", reinterpret_cast<void*>(&Enable));
    Patch check(original, 0x1287b8, "SendMessageA", reinterpret_cast<void*>(&CheckState));
    // All original key loops, history capture and dirty/max-frame stores run.
    // Only pose evaluation and UI refresh are omitted, tested separately by
    // the portable session tests. No original entrypoint/window/device runs.
    Patch a(original, 0x89b03, 0x59c30), b(original, 0x89b0b, 0x60ea0),
        c(original, 0x8a0e1, 0xcbd90), d(original, 0x8a0e9, 0x60ea0), e(original, 0x8a0f1, 0x62bb0),
        f(original, 0x8b2a3, 0x5ba90), g(original, 0x8b2ab, 0x60ea0), h(original, 0x8b375, 0xcbd90),
        i(original, 0x8b37d, 0x60ea0), j(original, 0x8b385, 0x62bb0);
    Patch k(original, 0x8a4e9, 0x59c30), l(original, 0x8a4f1, 0x60ea0),
        m(original, 0x8b1e9, 0xcbd90), n(original, 0x8b1f1, 0x60ea0), o(original, 0x8b1f9, 0x62bb0),
        p(original, 0x8b505, 0x5ba90), q(original, 0x8b50d, 0x60ea0), r(original, 0x8b759, 0xcbd90),
        s(original, 0x8b761, 0x60ea0), t(original, 0x8b769, 0x62bb0);
    unsigned cases =
        Exercise<PmmBoneKey, mdl::BoneKey>(original, true, true, mdl::kBoneKeyCapacity);
    cases += Exercise<PmmMorphKey, mdl::MorphKey>(original, true, false, mdl::kMorphKeyCapacity);
    cases +=
        Exercise<PmmCameraKey, mdl::CameraKey>(original, false, true, mdl::kTimelineKeyCapacity);
    cases +=
        Exercise<PmmLightKey, mdl::LightKey>(original, false, false, mdl::kTimelineKeyCapacity);
    std::printf("%u exact original frame-line insertion tables/flags; no UI or GPU\n", cases);
    std::uint64_t hash = 14695981039346656037ull;
    cases = ExerciseDeletion<PmmBoneKey, mdl::BoneKey>(original, mdl::kBoneKeyCapacity, hash);
    cases += ExerciseDeletion<PmmMorphKey, mdl::MorphKey>(original, mdl::kMorphKeyCapacity, hash);
    cases +=
        ExerciseDeletion<PmmCameraKey, mdl::CameraKey>(original, mdl::kTimelineKeyCapacity, hash);
    cases +=
        ExerciseDeletion<PmmLightKey, mdl::LightKey>(original, mdl::kTimelineKeyCapacity, hash);
    std::printf(
        "%u deletion/history persisted tables, hash %016llx; 6192 exact native seek poses; no UI or GPU\n",
        cases, static_cast<unsigned long long>(hash));
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
