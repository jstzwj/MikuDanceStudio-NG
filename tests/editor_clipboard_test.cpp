#include "mikudancestudio/editor_session.hpp"
#include "bone_clipboard_fixture.hpp"
#include "editor_clipboard_chain_fixture.hpp"
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
const PmmBoneKey* KeyAt(const EditorSession& editor, unsigned channel, unsigned frame) {
    static PmmBoneKey key;
    std::vector<std::vector<PmmBoneKey>> chains;
    if (!PmmTrackChains(editor.Document().models[0].boneKeys, chains) || channel >= chains.size())
        return nullptr;
    for (const auto& value : chains[channel])
        if (value.frame == frame) {
            key = value;
            return &key;
        }
    return nullptr;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned order = 0; order < 3; ++order)
        for (unsigned channel : {0, 1})
            for (bool writePosition : {false, true})
                for (bool mirror : {false, true})
                    for (unsigned frame : {0, 1, 4, 15, 29, 30, 40}) {
                        auto track = bone_clipboard_fixture::Make(order);
                        const auto offset = std::min(frame, 3u);
                        auto copied = bone_clipboard_fixture::Copied(frame - offset);
                        std::map<std::uint32_t, PmmBoneKey> captured;
                        unsigned maximum = 9;
                        CHECK(RegisterPmmBoneClipboard(track, channel, copied, offset,
                                                       writePosition, mirror, captured, maximum));
                        bone_clipboard_fixture::Hash(hash, track, captured, maximum);
                    }
    CHECK(hash == 0x205473a38f34f6f0ull);
    for (const auto& input : bone_clipboard_fixture::Names())
        CHECK(FindPmmClipboardBone(
                  input.copied, input.names.size(), [&](std::size_t i) { return input.names[i]; },
                  true) == input.target);
    for (bool mirror : {false, true}) {
        const float axis[3]{.25f, .5f, -1};
        auto rotation = bone_clipboard_fixture::Copied(0).rotation;
        CHECK(ConstrainPmmClipboardRotation(rotation, axis, mirror));
        if (mirror) {
            rotation[1] = -rotation[1];
            rotation[2] = -rotation[2];
        }
        const double scale = std::sqrt(1. - .75 * .75) * (mirror ? 1 : -1);
        const double length = std::sqrt(scale * scale * 1.3125 + .75 * .75);
        for (unsigned i = 0; i < 3; ++i)
            CHECK(std::abs(rotation[i] - scale * axis[i] / length) <= 2e-6);
        CHECK(std::abs(rotation[3] - .75 / length) <= 2e-6);
    }
    const auto root = std::filesystem::absolute(argv[1]);
    std::uint64_t chainHash = 14695981039346656037ull;
    for (unsigned accessories : {0u, 1u, 2u})
        for (bool reversed : {false, true}) {
            EditorSession chain;
            CHECK(clipboard_chain_fixture::Initialize(chain, root, accessories, reversed, reason));
            for (const auto& command : clipboard_chain_fixture::Commands()) {
                CHECK(clipboard_chain_fixture::Execute(chain, command, accessories, reason));
                clipboard_chain_fixture::Hash(chainHash, chain.Document());
            }
        }
    CHECK(chainHash == 0xe5501b1a254b32e5ull);
    // A seek between opacity keys has more precision than PMM's packed current
    // pose. Subsequent editing must preserve it; saving rounds only disk bytes.
    EditorSession interpolated;
    CHECK(clipboard_chain_fixture::Initialize(interpolated, root, 1, false, reason));
    CHECK(interpolated.Seek(8, reason));
    const float opacity = interpolated.Document().accessories[0].state.opacity;
    CHECK(opacity == .625f);
    CHECK(interpolated.SetSelectedKeys({{Track::Camera, 255, 0, 5}}, reason));
    CHECK(interpolated.Copy({{Track::Camera, 255, 0, 5}}, reason));
    CHECK(interpolated.Paste(80, false, reason));
    CHECK(interpolated.Document().accessories[0].state.opacity == opacity);
    const auto writeInterpolated = [](const auto& path, const auto& bytes, auto&) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        return bool(file);
    };
    CHECK(interpolated.Save(root / "interpolated-opacity.pmm", writeInterpolated, reason));
    CHECK(interpolated.Document().accessories[0].state.opacity == opacity);
    EditorSession diskOpacity;
    CHECK(diskOpacity.Open(root / "interpolated-opacity.pmm", reason));
    CHECK(diskOpacity.Document().accessories[0].state.opacity == .63f);
    const auto revisionOpacity = interpolated.Revision();
    CHECK(!interpolated.Edit(
        "unrepresentable key opacity",
        [](auto& d, auto&) {
            d.accessories[0].keyRecords.dense[0].opacity = .625f;
            return true;
        },
        reason));
    CHECK(reason == "pmm-opacity-precision" && interpolated.Revision() == revisionOpacity);
    for (const char* file : {"articulated.pmd", "articulated.pmx"}) {
        EditorSession editor;
        CHECK(editor.ImportModel(root / file, reason));
        CHECK(editor.Runtime().Resource(0)->EditingBones()[1].type == cpu_bone::BoneType::UnderIk);
        const auto source = bone_clipboard_fixture::Copied(10);
        auto displaced = bone_clipboard_fixture::Copied(30);
        displaced.position = {-4, 5, 6};
        displaced.rotation = {0, 0, 0, 1};
        displaced.physicsDisabled = 0;
        displaced.interpolation.fill(20);
        CHECK(editor.Edit(
            "clipboard fixture",
            [&](auto& doc, auto&) {
                doc.physics.playbackMode = 0;
                auto& model = doc.models[0];
                return RegisterPmmKey(model.boneKeys, 1, source) &&
                       RegisterPmmKey(model.boneKeys, 1, displaced);
            },
            reason));
        CHECK(editor.Copy({{Track::Bone, 0, 1, 10}}, reason));
        editor.SetRegistrationPhysics(true);
        editor.SetAutoInterpolation(true);
        CHECK(editor.Paste(30, false, reason));
        CHECK(editor.Selected().keys.size() == 1 &&
              editor.Selected().keys.count({Track::Bone, 0, 1, 30}));
        CHECK(KeyAt(editor, 1, 30));
        auto pasted = *KeyAt(editor, 1, 30);
        CHECK(pasted.position == displaced.position && pasted.rotation == source.rotation &&
              pasted.interpolation == source.interpolation && pasted.physicsDisabled == 1 &&
              pasted.allocated == 1);
        CHECK(!KeyAt(editor, 1, 10)->allocated);
        for (unsigned cycle = 0; cycle < 3; ++cycle) {
            CHECK(editor.Undo(reason));
            CHECK(KeyAt(editor, 1, 30)->rotation == displaced.rotation &&
                  !KeyAt(editor, 1, 30)->allocated);
            CHECK(editor.Redo(reason));
            CHECK(frame_line_fixture::Bytes(*KeyAt(editor, 1, 30)) ==
                  frame_line_fixture::Bytes(pasted));
        }
        CHECK(editor.Paste(40, true, reason));
        CHECK((KeyAt(editor, 1, 40)->position == std::array<float, 3>({-7.25f, -2.5f, 3})));
        CHECK((KeyAt(editor, 1, 40)->rotation == std::array<float, 4>({.25f, .5f, -.125f, .75f})));
        const auto revision = editor.Revision();
        const auto count = editor.Document().models[0].boneKeys.sparse.size();
        CHECK(!editor.Paste(0xffffffffU, false, reason));
        CHECK(editor.Revision() == revision &&
              editor.Document().models[0].boneKeys.sparse.size() == count);
        CHECK(editor.Undo(reason));
        CHECK(!KeyAt(editor, 1, 40));
    }
    EditorSession ordered;
    CHECK(ordered.ImportModel(root / "triangle.pmd", reason));
    CHECK(ordered.Edit(
        "interleaved clipboard",
        [&](auto& doc, auto&) {
            doc.physics.playbackMode = 0;
            auto& track = doc.models[0].boneKeys;
            auto key = track.dense[0];
            key.frame = 20;
            key.position[0] = 20;
            if (!RegisterPmmKey(track, 0, key))
                return false;
            key.frame = 10;
            key.position[0] = 10;
            return RegisterPmmKey(track, 0, key);
        },
        reason));
    CHECK(ordered.Seek(105, reason));
    CHECK(ordered.Copy({{Track::Bone, 0, 0, 10}, {Track::Bone, 0, 0, 20}}, reason));
    CHECK(ordered.Paste(100, false, reason));
    const auto& track = ordered.Document().models[0].boneKeys;
    CHECK(track.sparse.size() == 4 && track.sparse[2].index == 3 &&
          track.sparse[2].value.frame == 110 && track.sparse[3].index == 4 &&
          track.sparse[3].value.frame == 100);
    CHECK(std::abs(ordered.Document().models[0].bonePose[0].components[0] - 15) < 2e-5f);
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        CHECK(ordered.Undo(reason));
        CHECK(ordered.Document().models[0].boneKeys.sparse.size() == 2);
        CHECK(ordered.Redo(reason));
        CHECK(ordered.Document().models[0].boneKeys.sparse[2].value.frame == 110);
    }
    const auto revision = ordered.Revision();
    CHECK(!ordered.Paste(0x7fffffff, false, reason));
    CHECK(ordered.Revision() == revision &&
          ordered.Document().models[0].boneKeys.sparse.size() == 4);
    // Copying another model's unmatched bone is a native successful no-op;
    // clipboard data survives both a failed paste and switching the model.
    CHECK(ordered.ImportModel(root / "articulated.pmd", reason));
    CHECK(ordered.Copy({{Track::Bone, 1, 0, 0}}, reason));
    CHECK(ordered.SelectModel(0, reason));
    CHECK(ordered.Paste(200, false, reason));
    CHECK(ordered.Document().models[0].boneKeys.sparse.size() == 4);
    EditorSession scopes;
    CHECK(scopes.ImportModel(root / "triangle.pmd", reason));
    CHECK(scopes.ImportModel(root / "triangle.pmx", reason));
    CHECK(scopes.SelectModel(0, reason));
    CHECK(!scopes.CanPasteFrames() && !scopes.CanPasteFrames(true));
    auto unchanged = scopes.Revision();
    CHECK(scopes.Paste(10, false, reason) && scopes.Revision() == unchanged);
    CHECK(scopes.Copy({{Track::Bone, 0, 0, 0}, {Track::Bone, 1, 0, 0}, {Track::Camera, 255, 0, 0}},
                      reason));
    CHECK(scopes.CanPasteFrames() && scopes.CanPasteFrames(true));
    CHECK(scopes.SelectModel(255, reason));
    CHECK(!scopes.CanPasteFrames() && !scopes.CanPasteFrames(true));
    CHECK(scopes.Copy({{Track::Camera, 255, 0, 0}, {Track::Bone, 0, 0, 0}}, reason));
    CHECK(scopes.CanPasteFrames() && !scopes.CanPasteFrames(true));
    unchanged = scopes.Revision();
    CHECK(scopes.Paste(9, true, reason) && scopes.Revision() == unchanged);
    CHECK(scopes.Paste(10, false, reason));
    CHECK(scopes.Document().cameraKeys.sparse.size() == 1);
    CHECK(scopes.Document().models[0].boneKeys.sparse.empty() &&
          scopes.Document().models[1].boneKeys.sparse.empty());
    CHECK(scopes.SelectModel(0, reason));
    CHECK(scopes.CanPasteFrames(true) && scopes.Paste(20, false, reason));
    CHECK(scopes.Document().models[0].boneKeys.sparse.size() == 1 &&
          scopes.Document().models[1].boneKeys.sparse.empty());
    // An expired active-mode selection rejects atomically, retaining both copies.
    CHECK(!scopes.Copy({{Track::Bone, 0, 0, 999}}, reason));
    CHECK(scopes.CanPasteFrames(true));
    CHECK(scopes.SelectModel(1, reason));
    CHECK(scopes.Paste(30, false, reason)); // Model clipboard is shared between models.
    CHECK(scopes.Document().models[1].boneKeys.sparse.size() == 1);
    unchanged = scopes.Revision();
    CHECK(scopes.Copy({}, reason) && !scopes.CanPasteFrames());
    CHECK(scopes.Paste(40, false, reason) && scopes.Revision() == unchanged);
    CHECK(scopes.SelectModel(255, reason) && scopes.CanPasteFrames());
    CHECK(scopes.Paste(50, false, reason));
    CHECK(scopes.Document().cameraKeys.sparse.size() == 2);
    CHECK(scopes.Play(reason));
    CHECK(!scopes.CanPasteFrames() && !scopes.Copy({}, reason) && !scopes.Paste(60, false, reason));
    scopes.Pause();
    CHECK(scopes.CanPasteFrames());
    CHECK(scopes.Copy({}, reason) && !scopes.CanPasteFrames());
    CHECK(scopes.SelectModel(0, reason) && !scopes.CanPasteFrames());
    // Morph-only copy cannot enable reverse paste.
    CHECK(scopes.Copy({{Track::Morph, 0, 0, 0}}, reason));
    CHECK(scopes.CanPasteFrames() && !scopes.CanPasteFrames(true));
    CHECK(scopes.New(reason) && !scopes.CanPasteFrames());
    CHECK(scopes.ImportModel(root / "triangle.pmd", reason) && !scopes.CanPasteFrames());
    EditorSession globals;
    CHECK(globals.ImportModel(root / "triangle.pmx", reason));
    CHECK(globals.ImportModel(root / "triangle.pmx", reason));
    CHECK(globals.ImportAccessory(root / "bound.x", reason));
    CHECK(globals.SelectModel(255, reason));
    CHECK(globals.Edit(
        "global source keys",
        [](auto& d, auto&) {
            auto camera = d.cameraKeys.dense[0];
            auto light = d.lightKeys.dense[0];
            auto shadow = d.selfShadowKeys.dense[0];
            auto gravity = d.gravityKeys.dense[0];
            auto accessory = d.accessories[0].keyRecords.dense[0];
            camera.frame = light.frame = shadow.frame = gravity.frame = accessory.frame = 10;
            camera.eye = {11, 12, 13};
            light.color = {.25f, .5f, .75f};
            shadow.mode = 1;
            shadow.distance = .015f;
            gravity.acceleration = 3;
            accessory.position = {19, 20, 21};
            return RegisterPmmKey(d.cameraKeys, 0, camera) &&
                   RegisterPmmKey(d.lightKeys, 0, light) &&
                   RegisterPmmKey(d.selfShadowKeys, 0, shadow) &&
                   RegisterPmmKey(d.gravityKeys, 0, gravity) &&
                   RegisterPmmKey(d.accessories[0].keyRecords, 0, accessory);
        },
        reason));
    CHECK(globals.Seek(30, reason));
    CHECK(globals.Copy({{Track::Camera, 255, 0, 10},
                        {Track::Light, 255, 0, 10},
                        {Track::Shadow, 255, 0, 10},
                        {Track::Gravity, 255, 0, 10},
                        {Track::Accessory, 0, 0, 10}},
                       reason));
    CHECK(globals.Edit(
        "pending values and marks",
        [](auto& d, auto&) {
            d.cameraState[0] = d.cameraState[3] = d.cameraState[4] = 99;
            d.lightState[0] = .9f;
            d.selfShadowMode = 2;
            d.physics.gravityMagnitude = 8;
            d.accessories[0].state.position[0] = 100;
            d.models[0].bonePose[0].components[0] = 77;
            d.models[1].bonePose[0].components[0] = 88;
            d.cameraKeys.dense[0].selected = d.lightKeys.dense[0].selected =
                d.selfShadowKeys.dense[0].selected = d.gravityKeys.dense[0].selected =
                    d.accessories[0].keyRecords.dense[0].selected = 1;
            return true;
        },
        reason));
    CHECK(globals.Document().envelope.selectedModelSlot == 1);
    globals.Selected().keys = {
        {Track::Camera, 255, 0, 0}, {Track::Light, 255, 0, 0}, {Track::Bone, 0, 0, 0}};
    const auto* live = &globals.Runtime();
    const auto frames = live->Counters().frames;
    CHECK(globals.Paste(30, false, reason));
    const auto& current = globals.Document();
    CHECK(&globals.Runtime() == live && live->Counters().frames == frames);
    CHECK(live->Simulation().resetPending && live->Simulation().gravityMagnitude == 3);
    CHECK(current.timeline.currentFrame == 30 && current.cameraState[0] == 11 &&
          current.cameraState[3] == 0 && current.cameraState[4] == 0);
    CHECK(current.lightState[0] == .25f && current.selfShadowMode == 1 &&
          current.physicsInterval == .015f && current.accessories[0].state.position[0] == 19);
    CHECK(current.models[0].bonePose[0].components[0] == 77 &&
          current.models[1].bonePose[0].components[0] == 0);
    CHECK(!current.cameraKeys.dense[0].selected && !current.lightKeys.dense[0].selected &&
          !current.selfShadowKeys.dense[0].selected && !current.gravityKeys.dense[0].selected &&
          !current.accessories[0].keyRecords.dense[0].selected);
    CHECK(globals.Selected().keys.size() == 6 &&
          globals.Selected().keys.count({Track::Bone, 0, 0, 0}) &&
          !globals.Selected().keys.count({Track::Camera, 255, 0, 0}));
    CHECK(globals.TickIdle(1. / 60, reason) && !live->Simulation().resetPending);
    const auto lastRevision = globals.Revision();
    CHECK(!globals.Paste(0xffffffffU, false, reason));
    CHECK(globals.Revision() == lastRevision && !live->Simulation().resetPending);
    // Command 250 ignores copied names/slots, but keeps the offset relative to
    // the minimum of the complete copied selection (including other tracks).
    EditorSession selectedTrack;
    CHECK(selectedTrack.ImportModel(root / "articulated.pmx", reason));
    CHECK(selectedTrack.Edit(
        "selected-track source",
        [](auto& d, auto&) {
            d.physics.playbackMode = 0;
            auto& model = d.models[0];
            auto a = model.boneKeys.dense[0], b = model.boneKeys.dense[1];
            a.frame = 8;
            a.position = {2, 3, 4};
            b.frame = 13;
            b.position = {5, 6, 7};
            auto morph = model.morphKeys.dense[0];
            morph.frame = 5;
            morph.value = .5f;
            return RegisterPmmKey(model.boneKeys, 0, a) && RegisterPmmKey(model.boneKeys, 1, b) &&
                   RegisterPmmKey(model.morphKeys, 0, morph);
        },
        reason));
    CHECK(selectedTrack.Copy(
        {{Track::Bone, 0, 0, 8}, {Track::Bone, 0, 1, 13}, {Track::Morph, 0, 0, 5}}, reason));
    CHECK(selectedTrack.CanPasteToSelectedTrack());
    CHECK(selectedTrack.SelectBones({0}, 0, reason));
    CHECK(selectedTrack.Seek(20, reason));
    const auto* retained = &selectedTrack.Runtime();
    const auto updates = retained->Counters().frames;
    CHECK(selectedTrack.PasteToSelectedTrack(20, reason));
    CHECK(&selectedTrack.Runtime() == retained && retained->Counters().frames == updates);
    CHECK(KeyAt(selectedTrack, 0, 23) &&
          (KeyAt(selectedTrack, 0, 23)->position == std::array<float, 3>{2, 3, 4}));
    CHECK(KeyAt(selectedTrack, 0, 28) &&
          (KeyAt(selectedTrack, 0, 28)->position == std::array<float, 3>{5, 6, 7}));
    CHECK(!KeyAt(selectedTrack, 1, 28));
    for (const auto& key : selectedTrack.Keys())
        CHECK(key.track != Track::Morph || key.frame == 0 || key.frame == 5);
    CHECK(selectedTrack.Undo(reason) && !KeyAt(selectedTrack, 0, 23) &&
          !KeyAt(selectedTrack, 0, 28));
    CHECK(selectedTrack.Redo(reason) && KeyAt(selectedTrack, 0, 23) && KeyAt(selectedTrack, 0, 28));
    CHECK(selectedTrack.SelectBones({}, -1, reason));
    const auto noBoneRevision = selectedTrack.Revision();
    CHECK(!selectedTrack.PasteToSelectedTrack(40, reason) && reason == "clipboard-select-bone");
    CHECK(selectedTrack.Revision() == noBoneRevision);
    CHECK(selectedTrack.SelectBones({0}, 0, reason));
    CHECK(!selectedTrack.PasteToSelectedTrack(0x7fffffff, reason) &&
          selectedTrack.Revision() == noBoneRevision);
    CHECK(selectedTrack.Paste(40, false, reason) && KeyAt(selectedTrack, 1, 48));
    CHECK(selectedTrack.Play(reason) && !selectedTrack.CanPasteToSelectedTrack());
    CHECK(!selectedTrack.PasteToSelectedTrack(0, reason));
    selectedTrack.Pause();
    // Forced targeting also works across models with different bone names;
    // returning to ordinary paste still uses the original source names.
    CHECK(selectedTrack.ImportModel(root / "triangle.pmd", reason));
    CHECK(selectedTrack.SelectBones({0}, 0, reason));
    CHECK(selectedTrack.PasteToSelectedTrack(60, reason));
    std::vector<std::vector<PmmBoneKey>> targetChains;
    CHECK(PmmTrackChains(selectedTrack.Document().models[1].boneKeys, targetChains));
    CHECK(targetChains[0].back().frame == 68);
    const auto write = [](const auto& path, const auto& bytes, auto& error) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        file.close();
        if (!file)
            error = "write-failed";
        return bool(file);
    };
    CHECK(selectedTrack.Save(root / "selected-track.pmm", write, reason));
    EditorSession reopened;
    CHECK(reopened.Open(root / "selected-track.pmm", reason));
    CHECK(PmmTrackChains(reopened.Document().models[1].boneKeys, targetChains) &&
          targetChains[0].back().frame == 68);
    CHECK(KeyAt(reopened, 0, 23) && KeyAt(reopened, 0, 28));

    EditorSession selectedAccessory;
    CHECK(selectedAccessory.ImportModel(root / "triangle.pmd", reason));
    CHECK(selectedAccessory.ImportAccessory(root / "bound.x", reason));
    CHECK(selectedAccessory.ImportAccessory(root / "bound.x", reason));
    CHECK(selectedAccessory.Edit(
        "accessory source",
        [](auto& d, auto&) {
            d.physics.playbackMode = 1;
            auto camera = d.cameraKeys.dense[0];
            camera.frame = 2;
            auto a = d.accessories[0].keyRecords.dense[0];
            a.frame = 7;
            a.position = {3, 4, 5};
            return RegisterPmmKey(d.cameraKeys, 0, camera) &&
                   RegisterPmmKey(d.accessories[0].keyRecords, 0, a);
        },
        reason));
    CHECK(selectedAccessory.SelectModel(255, reason));
    CHECK(
        selectedAccessory.Copy({{Track::Camera, 255, 0, 2}, {Track::Accessory, 0, 0, 7}}, reason));
    CHECK(selectedAccessory.SelectModel(0, reason));
    CHECK(selectedAccessory.TickIdle(1. / 60, reason));
    CHECK(selectedAccessory.SelectModel(255, reason));
    CHECK(!selectedAccessory.Runtime().Simulation().resetPending);
    CHECK(selectedAccessory.Edit(
        "pending model pose",
        [](auto& d, auto&) {
            d.models[0].bonePose[0].components[0] = 77;
            return true;
        },
        reason));
    CHECK(selectedAccessory.CanPasteToSelectedTrack() &&
          selectedAccessory.PasteToSelectedTrack(30, reason));
    CHECK(!selectedAccessory.Runtime().Simulation().resetPending &&
          selectedAccessory.Document().models[0].bonePose[0].components[0] == 77);
    std::vector<std::vector<PmmAccessoryKey>> accessoryChains;
    CHECK(PmmTrackChains(selectedAccessory.Document().accessories[1].keyRecords, accessoryChains));
    CHECK(accessoryChains[0].back().frame == 35 &&
          (accessoryChains[0].back().position == std::array<float, 3>{3, 4, 5}));
    for (const auto& key : selectedAccessory.Keys())
        CHECK(key.track != Track::Camera || key.frame == 0 || key.frame == 2);
    CHECK(selectedAccessory.Save(root / "selected-accessory.pmm", write, reason));
    CHECK(reopened.Open(root / "selected-accessory.pmm", reason));
    CHECK(PmmTrackChains(reopened.Document().accessories[1].keyRecords, accessoryChains) &&
          accessoryChains[0].back().frame == 35 &&
          (accessoryChains[0].back().position == std::array<float, 3>{3, 4, 5}));
    CHECK(selectedAccessory.UnloadAccessory(1, reason));
    const auto noAccessoryRevision = selectedAccessory.Revision();
    CHECK(!selectedAccessory.PasteToSelectedTrack(40, reason) &&
          reason == "clipboard-select-accessory" &&
          selectedAccessory.Revision() == noAccessoryRevision);
    std::cout
        << "168 original clipboard tables, PMD/PMX paste, partial history and mode scopes passed\n";
}
