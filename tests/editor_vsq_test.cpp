#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/vsq_format.hpp"
#include "editor_vsq_fixture.hpp"
#include "editor_key_capacity_fixture.hpp"
#include "frame_line_deletion_fixture.hpp"
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
    const auto path = root / "lip-sync.vsq";
    const auto bytes = VsqFixture();
    CHECK(WriteVsqFixture(path, bytes));
    VsqDocument parsed;
    CHECK(ReadVsq(bytes, parsed, reason) && parsed.events.size() == 5);
    CHECK(parsed.events[0].start == 1.f && parsed.events[0].end == 1.375f);
    CHECK(parsed.events[2].vowel == 'u' && parsed.report.find("\r\n") != std::string::npos);
    // Every proper prefix must terminate with a bounded error and retain output.
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto before = parsed.report;
        CHECK(!ReadVsq({bytes.begin(), bytes.begin() + i}, parsed, reason));
        CHECK(parsed.events.size() == 5 && parsed.report == before);
    }
    for (auto corrupt : {0u, 4u, 8u, 10u, 12u, 14u, 18u, 22u}) {
        auto invalid = bytes;
        invalid[corrupt] = 255;
        CHECK(!ReadVsq(invalid, parsed, reason));
    }
    std::uint64_t hash = 14695981039346656037ull;
    for (const char* file : {"lips.pmd", "lips.pmx"})
        for (unsigned profile = 0; profile < VsqProfiles().size() + 2; ++profile) {
            EditorSession session;
            CHECK(!session.ImportVsq(path, reason));
            CHECK(session.ImportModel(root / file, reason));
            CHECK(session.ImportModel(root / "articulated.pmx", reason));
            CHECK(session.SelectModel(0, reason));
            CHECK(session.SetBone(1, 0, {7, 8, 9}, {0, 0, 0, 1}, reason));
            CHECK(session.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
            CHECK(session.Undo(reason));
            auto source = VsqProfiles()[profile < VsqProfiles().size() ? profile : 0];
            if (profile >= VsqProfiles().size())
                CHECK(session.Edit(
                    "vsq-capacity",
                    [&](auto& document, auto&) {
                        FillKeyCapacity(document.models[0].morphKeys,
                                        (profile - unsigned(VsqProfiles().size())) * 2);
                        return true;
                    },
                    reason));
            CHECK(WriteVsqFixture(path, source));
            const auto undo = session.CanUndo(), redo = session.CanRedo();
            const auto frames = session.Runtime().Counters().frames;
            const auto resource = session.Runtime().Resource(0);
            unsigned failed = 99;
            CHECK(session.ImportVsq(path, reason, &failed));
            CHECK((failed == 0) == (profile < VsqProfiles().size()));
            CHECK(session.CanUndo() == undo && session.CanRedo() == redo);
            CHECK(session.Runtime().Counters().frames == frames &&
                  session.Runtime().Resource(0) == resource && session.Dirty());
            CHECK(session.Document().models[1].bonePose[0].components[0] == 7);
            const auto& model = session.Document().models[0];
            for (const auto& key : model.boneKeys.dense)
                CHECK(!key.allocated);
            CHECK(!model.displayKeys.dense[0].allocated);
            if (profile == 0) {
                const unsigned first = std::string(file) == "lips.pmd" ? 1 : 0;
                std::vector<std::vector<PmmMorphKey>> chains;
                CHECK(PmmTrackChains(model.morphKeys, chains));
                const unsigned frames[]{28, 30, 39, 41};
                const float weights[]{0, .7f, .7f, 0};
                CHECK(chains[first].size() == 9); // a and e affect this mouth.
                for (unsigned i = 0; i < 4; ++i)
                    CHECK(chains[first][i + 1].frame == frames[i] &&
                          chains[first][i + 1].value == weights[i]);
            }
            frame_line_fixture::Hash(hash, model.morphKeys);
            const auto saved = root / "lips.pmm";
            CHECK(session.Save(
                saved,
                [](const auto& p, const auto& data, auto&) {
                    std::ofstream out(p, std::ios::binary);
                    return bool(out.write(reinterpret_cast<const char*>(data.data()),
                                          std::streamsize(data.size())));
                },
                reason));
            EditorSession reopened;
            CHECK(reopened.Open(saved, reason));
            CHECK(reopened.Document().timeline.currentFrame ==
                  session.Document().timeline.currentFrame);
            CHECK(reopened.Document().models[0].morphKeys.sparse.size() ==
                  model.morphKeys.sparse.size());
            CHECK(session.Play(reason) && !session.ImportVsq(path, reason));
            session.Pause();
        }
    std::cout << "VSQ frozen model hash: " << std::hex << hash << '\n';
    CHECK(hash == 0x6ba3f777ceba5cd4ull);

    EditorSession missing;
    CHECK(missing.ImportModel(root / "triangle.pmx", reason));
    const auto revision = missing.Revision();
    CHECK(!missing.ImportVsq(path, reason) && reason == "vsq-missing-a");
    CHECK(missing.Revision() != revision && missing.Dirty() &&
          std::filesystem::exists(root / "lip-sync.txt"));
    // Reject invalid input atomically, including unrepresentable lead-in frames.
    CHECK(missing.ImportModel(root / "lips.pmx", reason));
    auto invalid = VsqFixture({"a"}, 32767, {{0, 1}});
    CHECK(WriteVsqFixture(path, invalid));
    const auto before = missing.Revision();
    CHECK(!missing.ImportVsq(path, reason) && missing.Revision() == before);
    CHECK(reason == "vsq-frame-not-representable");
    std::puts(
        "VSQ SMF, vowel envelopes, tempo changes, bounded failures, state and PMM roundtrip passed");
}
