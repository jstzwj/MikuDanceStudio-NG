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
int main() {
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
