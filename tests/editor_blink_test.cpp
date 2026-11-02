#include "mikudancestudio/editor_session.hpp"
#include "editor_blink_fixture.hpp"
#include <fstream>
#include <iostream>
using namespace mikudancestudio;
using namespace editor;
using namespace format;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << " " << reason << '\n';                          \
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
    for (const char* file : {"blink.pmd", "blink.pmx"}) {
        EditorSession editor;
        unsigned count = 77, failed = 77;
        CHECK(!editor.HasBlinkMorph() && !editor.RegisterBlinks(0, 1, count, reason));
        CHECK(editor.ImportModel(root / file, reason));
        CHECK(editor.ImportModel(root / "articulated.pmd", reason));
        CHECK(editor.SelectModel(0, reason) && editor.HasBlinkMorph());
        const unsigned channel = editor.Document().models[0].morphPose.size() == 2 ? 1 : 0;
        CHECK(editor.SetBone(1, 0, {7, 8, 9}, {0, 0, 0, 1}, reason));
        CHECK(editor.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason) && editor.Undo(reason));
        CHECK(editor.SetMorph(0, channel, .75f, reason) && editor.CanRedo());
        const auto* runtime = &editor.Runtime();
        const auto* resource = editor.Runtime().Resource(0).get();
        const auto undo = editor.CanUndo(), redo = editor.CanRedo();
        CHECK(editor.RegisterBlinks(0, 1, count, reason, [] { return 32767u; }, &failed));
        CHECK(count == 1 && !failed && editor.Dirty() && editor.CanUndo() == undo &&
              editor.CanRedo() == redo && &editor.Runtime() == runtime &&
              editor.Runtime().Resource(0).get() == resource);
        CHECK(editor.Document().models[0].morphPose[channel] == 0 &&
              editor.Document().models[1].bonePose[0].components[0] == 7);
        std::vector<std::vector<PmmMorphKey>> chains;
        CHECK(PmmTrackChains(editor.Document().models[0].morphKeys, chains));
        CHECK(chains[channel].size() == 5);
        const unsigned frames[]{230, 232, 233, 236};
        const float values[]{0, 1, 1, 0};
        for (unsigned i = 0; i < 4; ++i)
            CHECK(chains[channel][i + 1].frame == frames[i] &&
                  chains[channel][i + 1].value == values[i] &&
                  chains[channel][i + 1].allocated == 1);
        CHECK(editor.Document().timeline.lastRegisteredFrame == 236);
        const auto path = root / "blinks.pmm";
        CHECK(editor.Save(path, writer, reason));
        EditorSession reopened;
        CHECK(reopened.Open(path, reason) && reopened.SelectModel(0, reason));
        CHECK(reopened.HasBlinkMorph() &&
              PmmTrackChains(reopened.Document().models[0].morphKeys, chains));
        CHECK(chains[channel].back().frame == 236 && chains[channel][2].value == 1);
        const auto revision = editor.Revision();
        unsigned calls = 0;
        count = 77;
        CHECK(!editor.RegisterBlinks(0, 500, count, reason,
                                     [&] { return calls++ ? 32768u : 32767u; }));
        CHECK(editor.Revision() == revision && count == 77 && editor.CanRedo() == redo);
        CHECK(editor.Play(reason));
        CHECK(!editor.RegisterBlinks(0, 1, count, reason) && editor.Playing());
        editor.Pause();
        CHECK(editor.SelectModel(255, reason) && !editor.HasBlinkMorph());
        CHECK(!editor.RegisterBlinks(0, 1, count, reason));
        CHECK(editor.SelectModel(1, reason) && !editor.HasBlinkMorph());
        CHECK(!editor.RegisterBlinks(0, 1, count, reason));
        for (unsigned free : {0u, 2u}) {
            EditorSession full;
            CHECK(full.ImportModel(root / file, reason));
            CHECK(full.Edit(
                "blink-capacity",
                [&](auto& d, auto&) {
                    FillBlinkCapacity(d, free);
                    return true;
                },
                reason));
            const auto maximum = full.Document().models[0].selection.maxFrame;
            CHECK(full.RegisterBlinks(0, 1, count, reason, [] { return 32767u; }, &failed));
            CHECK(count == 1 && failed == 4 - free && full.Dirty());
            CHECK(full.Document().models[0].morphKeys.sparse.size() +
                      full.Document().models[0].morphKeys.dense.size() ==
                  20000);
            CHECK(full.Document().models[0].selection.maxFrame == maximum &&
                  full.Document().timeline.lastRegisteredFrame == int(maximum));
            CHECK(PmmTrackChains(full.Document().models[0].morphKeys, chains));
            if (free)
                CHECK(chains[channel][1].frame == 230 && chains[channel][2].frame == 232);
        }
        EditorSession random;
        CHECK(random.ImportModel(root / file, reason));
        CHECK(random.RegisterBlinks(0, 1, count, reason));
        CHECK(PmmTrackChains(random.Document().models[0].morphKeys, chains));
        CHECK(count == 1 && chains[channel][1].frame == 7 && chains[channel].back().frame == 13);
    }
    std::cout
        << "Blink registration: native RNG endpoints, partial capacity, history and roundtrip passed\n";
}
