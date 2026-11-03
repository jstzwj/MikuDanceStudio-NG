#include "mikudancestudio/pmm_edit.hpp"
#include "mikudancestudio/scene_session.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
using namespace mikudancestudio::format;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
template <class T> void StableRecords() {
    PmmKeyTrack<T> track;
    track.dense.resize(2);
    T key{};
    key.frame = 30;
    CHECK(RegisterPmmKey(track, 1, key)); // index 2
    key.frame = 10;
    CHECK(RegisterPmmKey(track, 0, key)); // index 3
    key.frame = 20;
    CHECK(RegisterPmmKey(track, 1, key)); // index 4, before 2
    CHECK(track.dense[0].next == 3 && track.dense[1].next == 4);
    CHECK(track.sparse[0].index == 2 && track.sparse[0].value.previous == 4);
    CHECK(track.sparse[1].index == 3 && track.sparse[1].value.previous == 0);
    CHECK(track.sparse[2].index == 4 && track.sparse[2].value.next == 2);
    key.next = key.previous = 999; // input topology never replaces live links
    CHECK(RegisterPmmKey(track, 1, key));
    CHECK(track.sparse[2].value.previous == 1 && track.sparse[2].value.next == 2);
    CHECK(DeletePmmKey(track, 1, 30));
    CHECK(track.dense[0].previous == 4); // native end-of-chain sentinel store
    CHECK(track.sparse[0].index == 3 && track.sparse[1].index == 4);
    CHECK(track.sparse[1].value.next == 0);
    CHECK(DeletePmmKey(track, 1, 20));
    CHECK(track.dense[0].previous == 1 && track.dense[1].next == 0);
    key.frame = 5;
    CHECK(RegisterPmmKey(track, 0, key)); // first free slot 2 is reused
    CHECK(track.dense[0].next == 2 && track.dense[0].previous == 1);
    CHECK(track.sparse[0].index == 2 && track.sparse[0].value.next == 3);
    CHECK(track.sparse[1].index == 3 && track.sparse[1].value.previous == 2);
    key.frame = 0;
    CHECK(RegisterPmmKey(track, 0, key));
    CHECK(track.dense[0].next == 2 && track.dense[0].previous == 1);
    key.frame = 0x80000000U;
    CHECK(!RegisterPmmKey(track, 0, key));
    CHECK(track.sparse.size() == 2 && track.dense[0].next == 2);
    std::vector<std::vector<T>> chains;
    track.dense[0].previous = 999; // a freed sentinel predecessor is not an edge
    CHECK(PmmTrackChains(track, chains));
    track.dense[1].previous = 1; // other roots must still have no predecessor
    CHECK(!PmmTrackChains(track, chains));
    CHECK(!DeletePmmKey(track, 0, 5));
    CHECK(!RegisterPmmKey(track, 0, T{}));
    CHECK(track.sparse.size() == 2 && track.dense[0].next == 2);
}
template <class T> PmmKeyTrack<T> CapacityTrack(unsigned count) {
    PmmKeyTrack<T> track;
    track.dense.resize(1);
    track.dense[0].next = count > 1 ? 1 : 0;
    track.sparse.reserve(count - 1);
    for (unsigned i = 1; i < count; ++i) {
        T key{};
        key.frame = i * 2;
        key.previous = i - 1;
        key.next = i + 1 < count ? i + 1 : 0;
        track.sparse.push_back({int(i), 0, key});
    }
    return track;
}
template <class T> void CapacityBoundary() {
    constexpr auto capacity = PmmKeyCapacity<T>;
    PmmKeyTrack<T> track;
    track.dense.resize(1);
    track.dense[0].next = capacity - 1;
    T key{};
    key.frame = 10;
    track.sparse.push_back({int(capacity - 1), 0, key});
    std::vector<std::vector<T>> chains;
    CHECK(PmmTrackChains(track, chains));
    auto invalid = track;
    invalid.sparse[0].index = capacity;
    invalid.dense[0].next = capacity;
    CHECK(!PmmTrackChains(invalid, chains));
    invalid = track;
    invalid.unused[capacity] = {};
    CHECK(!PmmTrackChains(invalid, chains));
    std::map<std::uint32_t, T> captured{{capacity, {}}}, displaced;
    CHECK(!RestorePmmKeyRecords(track, captured, displaced) && displaced.empty());
    key.frame = 20;
    CHECK(RegisterPmmKey(track, 0, key));
    CHECK(track.sparse[0].index == 1 && track.sparse[1].index == int(capacity - 1));
    CHECK(track.sparse[1].value.next == 1 && track.sparse[0].value.previous == capacity - 1);
}
template <class T> void FullCapacity() {
    const auto capacity = PmmKeyCapacity<T>;
    auto track = CapacityTrack<T>(capacity);
    std::vector<std::vector<T>> chains;
    CHECK(PmmTrackChains(track, chains));
    T key{};
    key.frame = 2;
    CHECK(RegisterPmmKey(track, 0, key)); // Existing slots remain writable at capacity.
    key.frame = 3;
    std::map<std::uint32_t, T> captured;
    CHECK(!RegisterPmmKey(track, 0, key, &captured) && captured.empty());
    CHECK(track.dense[0].next == 1 && track.sparse.size() == capacity - 1 &&
          track.sparse[0].value.frame == 2 && track.sparse[0].value.next == 2);
    CHECK(DeletePmmKey(track, 0, 2));
    CHECK(RegisterPmmKey(track, 0, key));
    CHECK(track.sparse[0].index == 1 && track.sparse[0].value.frame == 3);
    CHECK(PmmTrackChains(track, chains));
    PmmKeyTrack<T> rebuilt;
    CHECK(BuildPmmTrack(chains, rebuilt) && rebuilt.sparse.size() == capacity - 1);
    key.frame = capacity * 2 + 1;
    chains[0].push_back(key);
    CHECK(!BuildPmmTrack(chains, rebuilt) && rebuilt.sparse.size() == capacity - 1);
}
void LargeBoneRoundtrip() {
    auto doc = NewPmmScene();
    PmmSceneModel model;
    model.layout.boneCount = 1;
    model.boneNames = {{'B'}};
    model.bonePose.resize(1);
    model.bonePose[0].components[6] = 1;
    // Exceeds the old arbitrary 100k sparse-record ceiling without requiring
    // a full 600k-table allocation in ordinary CI.
    model.boneKeys = CapacityTrack<PmmBoneKey>(100003);
    for (auto& record : model.boneKeys.sparse)
        record.value.rotation[3] = 1;
    model.boneKeys.dense[0].rotation[3] = 1;
    model.displayKeys.dense.resize(1);
    doc.models.push_back(std::move(model));
    PmmSceneDocument rebuilt;
    std::string reason;
    CHECK(RebuildPmmV2(doc, rebuilt, reason));
    CHECK(rebuilt.models[0].boneKeys.sparse.size() == 100002);
    const auto& last = rebuilt.models[0].boneKeys.sparse.back();
    CHECK(last.index == 100002 && last.value.frame == 200004 && !last.value.next);
    auto invalid = doc;
    invalid.models[0].boneKeys.unused[PmmKeyCapacity<PmmBoneKey>] = {};
    CHECK(!RebuildPmmV2(invalid, rebuilt, reason) &&
          rebuilt.models[0].boneKeys.sparse.size() == 100002);
}
int main() {
    CapacityBoundary<PmmBoneKey>();
    CapacityBoundary<PmmMorphKey>();
    CapacityBoundary<PmmDisplayKey>();
    CapacityBoundary<PmmCameraKey>();
    CapacityBoundary<PmmLightKey>();
    CapacityBoundary<PmmSelfShadowKey>();
    CapacityBoundary<PmmGravityKey>();
    CapacityBoundary<PmmAccessoryKey>();
    FullCapacity<PmmDisplayKey>();
    FullCapacity<PmmMorphKey>();
    FullCapacity<PmmCameraKey>();
    FullCapacity<PmmLightKey>();
    FullCapacity<PmmSelfShadowKey>();
    FullCapacity<PmmGravityKey>();
    FullCapacity<PmmAccessoryKey>();
    LargeBoneRoundtrip();
    StableRecords<PmmBoneKey>();
    StableRecords<PmmMorphKey>();
    StableRecords<PmmCameraKey>();
    StableRecords<PmmLightKey>();
    StableRecords<PmmDisplayKey>();
    StableRecords<PmmAccessoryKey>();
    StableRecords<PmmGravityKey>();
    StableRecords<PmmSelfShadowKey>();
    std::string reason;
    for (std::uint8_t enabled : {0, 1, 2})
        for (std::uint8_t mode : {0, 1, 2}) {
            auto input = NewPmmScene();
            input.selfShadowEnabled = enabled;
            input.selfShadowMode = mode;
            PmmSceneDocument live, disk;
            CHECK(RebuildPmmV2(input, live, reason));
            CHECK(live.selfShadowEnabled == enabled && live.selfShadowMode == mode);
            CHECK(ParsePmmSceneDocument(live.envelope, nullptr, nullptr, disk));
            CHECK(disk.selfShadowEnabled == enabled && disk.selfShadowMode == enabled);
        }
    auto source = NewPmmScene();
    PmmSceneDocument rebuilt;
    CHECK(RebuildPmmV2(source, rebuilt, reason));
    CHECK(rebuilt.cameraKeys.denseRecordOffsets.at(0) > 0);
    CHECK(rebuilt.envelope.settings.renderWidth == 640);
    SceneSession session;
    CHECK(SceneSession::Create(source, session, reason));
    auto camera = source.cameraKeys.dense[0];
    camera.frame = 30;
    camera.eye[0] = 30;
    CHECK(RegisterPmmKey(source.cameraKeys, 0, camera));
    camera.frame = 15;
    camera.eye[0] = 15;
    CHECK(RegisterPmmKey(source.cameraKeys, 0, camera));
    camera.eye[0] = 20;
    CHECK(RegisterPmmKey(source.cameraKeys, 0, camera));
    CHECK(source.cameraKeys.sparse.size() == 2);
    CHECK(source.cameraKeys.sparse[0].index == 1 && source.cameraKeys.sparse[0].value.frame == 30);
    CHECK(source.cameraKeys.sparse[1].index == 2 && source.cameraKeys.sparse[1].value.frame == 15);
    CHECK(SceneSession::Create(source, session, reason));
    CHECK(session.Seek(15));
    CHECK(session.Pose().camera.position[0] == 20);
    CHECK(session.Seek(30));
    CHECK(session.Pose().camera.position[0] == 30);
    CHECK(DeletePmmKey(source.cameraKeys, 0, 15));
    CHECK(source.cameraKeys.sparse[0].index == 1);
    CHECK(!DeletePmmKey(source.cameraKeys, 0, 0));
    CHECK(!DeletePmmKey(source.cameraKeys, 0, 15));
    CHECK(SceneSession::Create(source, session, reason));
    CHECK(session.Seek(15));
    CHECK(std::abs(session.Pose().camera.position[0] - 15) < .001f);
    // Several roots with interleaved insertions must keep independent links.
    PmmKeyTrack<PmmMorphKey> morphs;
    morphs.dense.resize(3);
    for (int root = 2; root >= 0; --root)
        for (unsigned frame : {40, 10, 25}) {
            PmmMorphKey k;
            k.frame = frame;
            k.value = float(root * 100 + frame);
            CHECK(RegisterPmmKey(morphs, root, k));
        }
    CHECK(DeletePmmKey(morphs, 1, 25));
    std::vector<std::vector<PmmMorphKey>> chains;
    CHECK(PmmTrackChains(morphs, chains));
    CHECK(chains[1].size() == 3);
    CHECK(chains[2][2].value == 225);
    CHECK(chains[0][3].frame == 40);
    auto broken = morphs;
    broken.sparse[0].value.next = broken.sparse[0].index;
    CHECK(!PmmTrackChains(broken, chains));
    broken = morphs;
    broken.sparse[0].value.previous = 99;
    CHECK(!PmmTrackChains(broken, chains));
    // Native mouth shifts may leave the first survivor referring to a freed
    // predecessor. Live nonzero free payloads are not silently treated as zero.
    PmmKeyTrack<PmmMorphKey> shifted;
    shifted.dense.resize(2);
    shifted.dense[0].next = 3;
    PmmMorphKey survivor;
    survivor.frame = 5;
    survivor.previous = 2;
    survivor.value = .5f;
    shifted.sparse.push_back({3, 0, survivor});
    CHECK(PmmTrackChains(shifted, chains));
    shifted.unused[2].value = .25f;
    CHECK(!PmmTrackChains(shifted, chains));
    shifted.unused[2].value = 0;
    CHECK(PmmTrackChains(shifted, chains));
    shifted.sparse[0].value.previous = 100002;
    CHECK(!PmmTrackChains(shifted, chains));
    shifted.sparse[0].value.previous = 3;
    CHECK(!PmmTrackChains(shifted, chains));
    // Invalid changes cannot publish a partially rebuilt document/session.
    CHECK(RebuildPmmV2(source, rebuilt, reason));
    CHECK(rebuilt.cameraKeys.sparse[0].index == 1);
    const auto old = rebuilt.envelope.remainingBytes;
    source.cameraState[0] = std::numeric_limits<float>::infinity();
    CHECK(!RebuildPmmV2(source, rebuilt, reason));
    CHECK(old == rebuilt.envelope.remainingBytes);
    CHECK(!SceneSession::Create(source, session, reason));
    CHECK(std::abs(session.Pose().camera.position[0] - 15) < .001f);
    std::puts(
        "editable PMM: topology, evaluation, regenerated offsets and atomic rejection passed");
}
