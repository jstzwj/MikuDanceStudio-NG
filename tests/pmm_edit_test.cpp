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
int main() {
    std::string reason;
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
    CHECK(SceneSession::Create(source, session, reason));
    CHECK(session.Seek(15));
    CHECK(session.Pose().camera.position[0] == 20);
    CHECK(session.Seek(30));
    CHECK(session.Pose().camera.position[0] == 30);
    CHECK(DeletePmmKey(source.cameraKeys, 0, 15));
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
    const auto old = rebuilt.envelope.remainingBytes;
    source.cameraState[0] = std::numeric_limits<float>::infinity();
    CHECK(!RebuildPmmV2(source, rebuilt, reason));
    CHECK(old == rebuilt.envelope.remainingBytes);
    CHECK(!SceneSession::Create(source, session, reason));
    CHECK(std::abs(session.Pose().camera.position[0] - 15) < .001f);
    std::puts(
        "editable PMM: topology, evaluation, regenerated offsets and atomic rejection passed");
}
