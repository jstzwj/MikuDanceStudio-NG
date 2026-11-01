#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/editor_session.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
using namespace format;
using namespace editor;
namespace {
struct NativeAuxiliary {
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::vector<mdl::MorphRecord> poses;
    std::vector<mdl::MorphKey> morphs{mdl::kMorphKeyCapacity};
    std::vector<mdl::DisplayKey> displays{mdl::kDisplayKeyCapacity};
    std::vector<mdl::IkChain> chains;
    std::vector<std::vector<std::uint8_t>> ik{mdl::kDisplayKeyCapacity};
    explicit NativeAuxiliary(const PmmSceneModel& source, bool pmx)
        : poses(source.morphPose.size()), chains(source.ikPose.size()) {
        model->physicsMode = pmx ? 2 : 0;
        model->morphCount = unsigned(poses.size());
        model->morphs = poses.data();
        model->morphKeys = morphs.data();
        model->displayKeys = displays.data();
        model->ikChains = chains.data();
        model->ikChainCount = unsigned(chains.size());
        model->UndoCursor() = model->LatestUndoCursor() = 29;
        Check(source.displayKeys.dense[0].rigidStates.empty(), "fixture has no parent bindings");
        for (unsigned i = 0; i < displays.size(); ++i) {
            ik[i].resize(chains.size());
            displays[i].ikStates = ik[i].data();
        }
        for (unsigned i = 0; i < source.morphKeys.dense.size(); ++i) {
            morphs[i].value = source.morphKeys.dense[i].value;
            morphs[i].allocated = source.morphKeys.dense[i].allocated;
        }
        const auto& display = source.displayKeys.dense[0];
        displays[0].visible = display.visible;
        displays[0].allocated = display.allocated;
        std::copy(display.ikEnabled.begin(), display.ikEnabled.end(), ik[0].begin());
    }
    void Register(OriginalPhysicsModule& original, const PmmSceneModel& source, Track kind,
                  unsigned frame) {
        model->loadComplete = source.selection.loadComplete;
        model->maxFrame = source.selection.maxFrame;
        for (unsigned i = 0; i < poses.size(); ++i)
            poses[i].value = source.morphPose[i];
        for (unsigned i = 0; i < chains.size(); ++i)
            chains[i].enabled = source.ikPose[i];
        // The command dispatcher clears key marks before calling these kernels.
        // This harness isolates registration; it does not execute the UI tail.
        for (auto& key : morphs)
            key.allocated = 0;
        for (auto& key : displays)
            key.allocated = 0;
        if (kind == Track::Morph)
            original.Function<void (*)(mdl::ModelRecord*, int, int)>(0xca9c0)(model.get(), 0,
                                                                              int(frame));
        else
            original.Function<void (*)(mdl::ModelRecord*, int)>(0xcaf80)(model.get(), int(frame));
    }
    void Compare(const PmmSceneModel& source) const {
        Check(model->maxFrame == source.selection.maxFrame, "native maximum frame differs");
        Check(model->UndoCursor() == 29 && model->LatestUndoCursor() == 29,
              "auxiliary registration altered native bone history");
        std::map<unsigned, PmmMorphKey> expectedMorphs;
        for (unsigned i = 0; i < source.morphKeys.dense.size(); ++i)
            expectedMorphs[i] = source.morphKeys.dense[i];
        for (const auto& key : source.morphKeys.sparse)
            expectedMorphs[unsigned(key.index)] = key.value;
        for (unsigned i = 0; i < morphs.size(); ++i) {
            const auto& actual = morphs[i];
            const auto found = expectedMorphs.find(i);
            Check((i < poses.size() || actual.frame != 0) == (found != expectedMorphs.end()),
                  "native morph slot differs");
            if (found == expectedMorphs.end())
                continue;
            const auto& expected = found->second;
            Check(actual.frame == expected.frame && actual.previous == expected.previous &&
                      actual.next == expected.next && actual.allocated == expected.allocated &&
                      !std::memcmp(&actual.value, &expected.value, sizeof(float)),
                  "native morph registration differs");
        }
        std::map<unsigned, PmmDisplayKey> expectedDisplays{{0, source.displayKeys.dense[0]}};
        for (const auto& key : source.displayKeys.sparse)
            expectedDisplays[unsigned(key.index)] = key.value;
        for (unsigned i = 0; i < displays.size(); ++i) {
            const auto& actual = displays[i];
            const auto found = expectedDisplays.find(i);
            Check((i == 0 || actual.frame != 0) == (found != expectedDisplays.end()),
                  "native display slot differs");
            if (found == expectedDisplays.end())
                continue;
            const auto& expected = found->second;
            Check(actual.frame == expected.frame && actual.previous == expected.previous &&
                      actual.next == expected.next && actual.allocated == expected.allocated &&
                      actual.visible == expected.visible && ik[i] == expected.ikEnabled,
                  "native display registration differs");
        }
    }
};
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 3, "usage: editor_auxiliary_oracle_test original.exe generated-fixture");
    OriginalPhysicsModule original(argv[1]);
    unsigned count = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx"}) {
        EditorSession session;
        std::string reason;
        Check(session.ImportModel(std::filesystem::path(argv[2]) / file, reason), reason.c_str());
        const bool pmx = session.Runtime().Resource(0)->SourceKind() == ModelKind::Pmx;
        NativeAuxiliary native(session.Document().models[0], pmx);
        const unsigned frames[]{0, 20, 5, 30, 20, 1, 0, 60};
        for (unsigned cycle = 0; cycle < 3; ++cycle)
            for (unsigned i = 0; i < std::size(frames); ++i)
                for (auto kind : {Track::Morph, Track::Display}) {
                    if (!pmx && kind == Track::Morph)
                        continue; // PMD fixture contains only the non-editable base morph.
                    Check(session.Edit(
                              "auxiliary-input",
                              [&](auto& d, auto&) {
                                  auto& model = d.models[0];
                                  model.selection.loadComplete = (i + cycle) % 2;
                                  model.morphPose[0] = float((i * 3 + cycle) % 9) / 8;
                                  for (auto& enabled : model.ikPose)
                                      enabled = (i + cycle / 2) % 2;
                                  return true;
                              },
                              reason),
                          reason.c_str());
                    native.Register(original, session.Document().models[0], kind, frames[i]);
                    const auto* runtime = &session.Runtime();
                    const auto updates = runtime->Counters().frames;
                    const bool undo = session.CanUndo(), redo = session.CanRedo();
                    Check(session.Register({{kind, 0, 0, frames[i]}}, reason), reason.c_str());
                    native.Compare(session.Document().models[0]);
                    Check(runtime == &session.Runtime() && updates == runtime->Counters().frames,
                          "registration advanced or replaced the CPU runtime");
                    Check(undo == session.CanUndo() && redo == session.CanRedo(),
                          "registration altered portable bone history");
                    ++count;
                }
    }
    std::printf("%u original morph/display registrations: exact slots, values, IK and visibility\n",
                count);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
