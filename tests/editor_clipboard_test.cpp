#include "mikudancestudio/editor_session.hpp"
#include "bone_clipboard_fixture.hpp"
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
    std::cout
        << "168 original clipboard tables, PMD/PMX paste, partial history and mode scopes passed\n";
}
