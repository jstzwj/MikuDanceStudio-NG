#include "mikudancestudio/editor_session.hpp"
#include "editor_morph_purge_fixture.hpp"
#include <fstream>
#include <climits>
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
    for (const auto* file : {"morph-panels.pmd", "morph-panels.pmx"})
        for (bool reverse : {false, true})
            for (std::int32_t shift : {-21, -20, -15, -10, -1, 0, 3, INT_MAX - 15}) {
                EditorSession expressions;
                CHECK(MorphPurgeFixture(expressions, root, file, reverse, 2, reason));
                CHECK(expressions.SetBone(1, 0, {7, 8, 9}, {0, 0, 0, 1}, reason));
                CHECK(expressions.SetBone(0, 0, {2, 3, 4}, {0, 0, 0, 1}, reason));
                CHECK(expressions.Undo(reason) && expressions.CanRedo());
                const auto before = expressions.Document().models[0];
                const auto watermark = expressions.Document().timeline.lastRegisteredFrame;
                const auto* runtime = &expressions.Runtime();
                const auto frames = runtime->Counters().frames;
                CHECK(expressions.ShiftMouthKeys(shift, reason));
                CHECK(expressions.Dirty() && expressions.CanRedo() &&
                      &expressions.Runtime() == runtime && runtime->Counters().frames == frames);
                CHECK(expressions.Document().models[1].bonePose[0].components[0] == 7 &&
                      expressions.Document().models[0].selection.maxFrame ==
                          before.selection.maxFrame &&
                      expressions.Document().timeline.lastRegisteredFrame == watermark);
                const auto& model = expressions.Document().models[0];
                const auto& track = model.morphKeys;
                const auto roots = unsigned(track.dense.size());
                std::vector<std::vector<PmmMorphKey>> chains;
                CHECK(PmmTrackChains(track, chains));
                std::vector<unsigned> mouths;
                for (unsigned i = 0; i < roots; ++i) {
                    int mapped;
                    CHECK(runtime->DocumentMorphIndex(0, int(i), mapped, reason));
                    const bool mouth =
                        runtime->Resource(0)->Document().morphs[mapped].header.panel == 3;
                    CHECK(track.dense[i].frame == 0 &&
                          track.dense[i].value == before.morphKeys.dense[i].value);
                    if (!mouth) {
                        CHECK(chains[i].size() == 3 && chains[i][1].frame == 10 &&
                              chains[i][2].frame == 20);
                        continue;
                    }
                    mouths.push_back(i);
                    const auto first = roots + 2 * i;
                    if (shift == -15 || shift == -10) {
                        CHECK(track.dense[i].next == first + 1 && chains[i].size() == 2 &&
                              chains[i][1].previous == first &&
                              chains[i][1].frame == unsigned(20 + shift));
                        CHECK(track.unused.at(first).value == 0 && !track.unused.at(first).next);
                    } else if (shift < -19 || shift == INT_MAX - 15)
                        CHECK(!track.dense[i].next);
                    else
                        CHECK(chains[i].size() == 3 && chains[i][1].frame == unsigned(10 + shift) &&
                              chains[i][2].frame == unsigned(20 + shift));
                }
                CHECK(!mouths.empty());
                PmmScenePlaybackState shiftedPlayback;
                CHECK(StartPmmScenePlayback(expressions.Document(), shiftedPlayback));
                CHECK(AdvancePmmScenePlayback(expressions.Document(), .1f, shiftedPlayback,
                                              {true, true, PmmPlaybackArithmetic::NativeFloat}));
                if (shift == -15 || shift == -10)
                    for (auto channel : mouths)
                        CHECK(shiftedPlayback.pose.models[0].morphValues[channel] ==
                              InterpolatePmmMorphValue(0, -.125f, 0, unsigned(20 + shift),
                                                       shiftedPlayback.frame));
                const auto path = root / "shifted-expressions.pmm";
                CHECK(expressions.Save(path, writer, reason) && expressions.Open(path, reason));
                CHECK(expressions.SelectModel(0, reason) && expressions.Seek(2, reason));
                if (shift == -15 || shift == -10)
                    for (auto channel : mouths)
                        CHECK(expressions.Document().models[0].morphPose[channel] ==
                              (2.f / float(20 + shift)) * -.125f);
                CHECK(expressions.Seek(30, reason) &&
                      expressions.SetMorph(0, mouths[0], .375f, reason));
                CHECK(expressions.RegisterMorph(0, mouths[0], reason));
                CHECK(expressions.Seek(2, reason));
                // Independent x64 oracle: reusing the freed predecessor makes
                // seek wrap both frame differences, while playback subtracts
                // float frame time and wraps only the denominator.
                if (shift == -15 || shift == -10)
                    CHECK(expressions.Document().models[0].morphPose[mouths[0]] == -.125f);
                PmmScenePlaybackState reusedPlayback;
                CHECK(StartPmmScenePlayback(expressions.Document(), reusedPlayback));
                CHECK(AdvancePmmScenePlayback(expressions.Document(), .1f, reusedPlayback,
                                              {true, true, PmmPlaybackArithmetic::NativeFloat}));
                if (shift == -15 || shift == -10)
                    CHECK(reusedPlayback.pose.models[0].morphValues[mouths[0]] == .375f);
                CHECK(expressions.Play(reason));
                const auto revision = expressions.Revision();
                CHECK(!expressions.ShiftMouthKeys(-15, reason) &&
                      expressions.Revision() == revision);
                expressions.Pause();
                CHECK(expressions.SelectModel(255, reason) &&
                      !expressions.ShiftMouthKeys(1, reason));
            }
    std::cout
        << "32 PMD/PMX mouth shifts: persistence, reused slots, playback and history passed\n";
}
