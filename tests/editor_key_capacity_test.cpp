#include "mikudancestudio/editor_session.hpp"
#include "editor_key_capacity_fixture.hpp"
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
    std::cout << cases
              << " full/near-full registration, overwrite, marks and PMM reload cases passed\n";
}
