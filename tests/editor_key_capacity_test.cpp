#include "mikudancestudio/editor_session.hpp"
#include "editor_key_capacity_fixture.hpp"
#include "editor_paste_capacity_fixture.hpp"
#include <fstream>
#include <iostream>
using namespace mikudancestudio;
using namespace editor;
using namespace format;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ' ' << reason << '\n';                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    const auto writer = [](const auto& path, const auto& bytes, auto&) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        return bool(file);
    };
    unsigned cases = 0;
    for (auto kind : {Track::Camera, Track::Light, Track::Shadow, Track::Accessory, Track::Display})
        for (unsigned free : {0u, 1u})
            for (unsigned frame : {0u, 2u, 3u, 21000u}) {
                EditorSession session;
                CHECK(session.ImportModel(root / "articulated.pmx", reason));
                if (kind == Track::Accessory)
                    CHECK(session.ImportAccessory(root / "bound.x", reason));
                CHECK(session.Edit(
                    "capacity",
                    [&](auto& d, auto&) {
                        d.timeline.currentFrame = int(frame);
                        if (kind == Track::Camera)
                            FillKeyCapacity(d.cameraKeys, free);
                        if (kind == Track::Light)
                            FillKeyCapacity(d.lightKeys, free);
                        if (kind == Track::Shadow)
                            FillKeyCapacity(d.selfShadowKeys, free);
                        if (kind == Track::Accessory)
                            FillKeyCapacity(d.accessories[0].keyRecords, free);
                        if (kind == Track::Display)
                            FillKeyCapacity(d.models[0].displayKeys, free);
                        d.cameraKeys.dense[0].selected = d.lightKeys.dense[0].selected =
                            d.selfShadowKeys.dense[0].selected = d.gravityKeys.dense[0].selected =
                                1;
                        return true;
                    },
                    reason));
                const auto before = session.Document();
                const auto resource = session.Runtime().Resource(0);
                const auto counters = session.Runtime().Counters();
                const bool undo = session.CanUndo(), redo = session.CanRedo();
                const KeyAddress key{
                    kind, std::uint8_t(PmmModelKey(kind) || kind == Track::Accessory ? 0 : 255), 0,
                    frame};
                unsigned failed = 99;
                CHECK(session.Register({key}, reason, &failed));
                CHECK(failed == unsigned(!free && (frame == 3 || frame == 21000)));
                CHECK(session.Dirty() && session.Runtime().Resource(0) == resource &&
                      session.Runtime().Counters().frames == counters.frames &&
                      session.CanUndo() == undo && session.CanRedo() == redo);
                CHECK(session.Document().timeline.lastRegisteredFrame ==
                      ((!failed && frame > unsigned(before.timeline.lastRegisteredFrame))
                           ? int(frame)
                           : before.timeline.lastRegisteredFrame));
                CHECK(session.Selected().keys.count(key) == unsigned(!failed));
                if (!PmmModelKey(kind)) {
                    CHECK(session.Document().cameraKeys.dense[0].selected ==
                          unsigned(kind == Track::Camera && frame == 0));
                    CHECK(session.Document().lightKeys.dense[0].selected ==
                          unsigned(kind == Track::Light && frame == 0));
                    CHECK(!session.Document().gravityKeys.dense[0].selected);
                }
                const auto path = root / "capacity.pmm";
                CHECK(session.Save(path, writer, reason));
                EditorSession reopened;
                CHECK(reopened.Open(path, reason));
                CHECK(reopened.Document().timeline.lastRegisteredFrame ==
                      session.Document().timeline.lastRegisteredFrame);
                CHECK(reopened.Seek(frame, reason));
                ++cases;
            }
    std::uint64_t pasteHash = 14695981039346656037ull;
    for (unsigned cameraFree : {0u, 1u, 2u})
        for (unsigned lightFree : {0u, 1u, 2u})
            for (unsigned frame : {0u, 3u, 21000u}) {
                EditorSession session;
                CHECK(GlobalPasteCapacityFixture(session, cameraFree, lightFree, frame, reason));
                const auto frames = session.Runtime().Counters().frames;
                std::vector<Track> rejected;
                CHECK(session.Paste(frame, false, reason, &rejected));
                const auto count = [&](unsigned free) {
                    return frame == 0   ? (!free ? 1u : 3u)
                           : frame == 3 ? (!free       ? 0u
                                           : free == 1 ? 2u
                                                       : 3u)
                                        : free;
                };
                const auto cameraCount = count(cameraFree), lightCount = count(lightFree);
                CHECK(rejected.size() == unsigned(cameraCount < 3) + unsigned(lightCount < 3));
                CHECK(session.Selected().keys.size() == cameraCount + lightCount);
                const auto check = [&](const auto& track, unsigned changed, auto value) {
                    using T = typename std::decay_t<decltype(track.dense)>::value_type;
                    std::vector<std::vector<T>> chains;
                    CHECK(PmmTrackChains(track, chains));
                    for (const auto& key : chains[0]) {
                        const bool pasted = key.frame >= frame && key.frame < frame + changed;
                        CHECK(bool(key.selected) == pasted);
                        if (pasted)
                            CHECK(value(key) == (key.frame - frame));
                    }
                    return 0;
                };
                CHECK(check(session.Document().cameraKeys, cameraCount,
                            [](const auto& key) { return unsigned(key.eye[0] - 31); }) == 0);
                CHECK(check(session.Document().lightKeys, lightCount,
                            [](const auto& key) { return unsigned(key.color[0] * 8 - 1); }) == 0);
                CHECK(session.Runtime().Counters().frames == frames &&
                      session.Runtime().Simulation().resetPending && session.Dirty());
                HashPasteCapacity(pasteHash, session.Document());
                const auto path = root / "partial-paste.pmm";
                CHECK(session.Save(path, writer, reason));
                EditorSession reopened;
                CHECK(reopened.Open(path, reason));
                CHECK(reopened.Document().cameraKeys.sparse.size() ==
                      session.Document().cameraKeys.sparse.size());
                CHECK(reopened.Document().lightKeys.sparse.size() ==
                      session.Document().lightKeys.sparse.size());
                ++cases;
            }
    CHECK(pasteHash == 0xbdf068b38686a6b4ull);
    std::uint64_t modelHash = 14695981039346656037ull;
    for (unsigned morphFree : {0u, 1u, 2u})
        for (unsigned displayFree : {0u, 1u, 2u}) {
            EditorSession session;
            CHECK(ModelPasteCapacityFixture(session, root, morphFree, displayFree, reason));
            const bool undo = session.CanUndo(), redo = session.CanRedo();
            const auto frames = session.Runtime().Counters().frames;
            std::vector<Track> rejected;
            CHECK(session.Paste(3, false, reason, &rejected));
            // Three Morph roots put the first sparse key after the destination
            // range. Display has one root, so frame four is an overwrite.
            CHECK(session.Document().models[0].morphKeys.dense.size() > 2);
            CHECK(rejected.size() == unsigned(morphFree < 3) + unsigned(displayFree < 2));
            const auto displayCount = !displayFree ? 0u : displayFree == 1 ? 2u : 3u;
            const auto selected = std::count_if(
                session.Selected().keys.begin(), session.Selected().keys.end(),
                [](const auto& key) { return PmmModelKey(key.track) && key.slot == 0; });
            CHECK(selected == morphFree + displayCount);
            CHECK(session.CanUndo() == undo && session.CanRedo() == redo &&
                  session.Runtime().Counters().frames == frames);
            if (morphFree && displayFree) {
                frame_line_fixture::Hash(modelHash, session.Document().models[0].morphKeys);
                frame_line_fixture::Hash(modelHash, session.Document().models[0].displayKeys);
            }
            ++cases;
        }
    CHECK(modelHash == 0xef5ef02e35193905ull);
    std::cout << cases
              << " full/near-full registration, overwrite, marks and PMM reload cases passed\n";
}
