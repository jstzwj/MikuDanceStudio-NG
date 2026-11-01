#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/vmd_bone_interpolation.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
using namespace mikudancestudio;
using namespace editor;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " reason=" << reason << '\n';        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
bool Write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
           std::string& error) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    f.close();
    if (!f) {
        error = "write-failed";
        return false;
    }
    return true;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    // Frozen legacy byte layout: PMM curve followed by position.x=1.1f.
    const std::array<std::uint8_t, 16> curve{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const std::array<std::uint8_t, 64> expected{
        1, 2, 0x63, 0x0f, 5, 6, 7,  8,  9,  10, 11, 12, 13, 14,   15,   16,
        2, 3, 4,    5,    6, 7, 8,  9,  10, 11, 12, 13, 14, 15,   16,   0xcd,
        3, 4, 5,    6,    7, 8, 9,  10, 11, 12, 13, 14, 15, 16,   0xcd, 0xcc,
        4, 5, 6,    7,    8, 9, 10, 11, 12, 13, 14, 15, 16, 0xcd, 0xcc, 0x8c};
    CHECK(format::EncodeVmdBoneInterpolation(curve.data(), 1.1f, true) == expected);
    CHECK(format::DecodeVmdBoneInterpolation(expected) == curve);
    const auto root = std::filesystem::absolute(argv[1]);
    {
        // Original image fixture, top-origin TGA; no GUI or GPU decoder.
        std::vector<std::uint8_t> tga(18);
        tga[2] = 2;
        tga[12] = 3;
        tga[14] = 2;
        tga[16] = 32;
        tga[17] = 0x28;
        for (unsigned i = 0; i < 6; ++i)
            tga.insert(tga.end(), {std::uint8_t(i * 31), 91, 173, std::uint8_t(i * 51)});
        CHECK(Write(root / "picture.tga", tga, reason));
        EditorSession pictures;
        CHECK(!pictures.ShowPicture(true, reason));
        CHECK(pictures.ImportPicture(root / "picture.tga", 640, 480, reason));
        const auto image = pictures.Assets().picture;
        CHECK(image && image->width == 3 && image->height == 2);
        CHECK(image->pixels[0] == 173 && image->pixels[2] == 0 && image->pixels[3] == 0);
        CHECK(pictures.Document().picture.offsetX == 0 &&
              pictures.Document().picture.offsetY == 26);
        CHECK(std::abs(pictures.Document().picture.scale - 426.6666667f) < .0001f);
        CHECK(pictures.ShowPicture(false, reason));
        CHECK(pictures.Assets().picture == image);
        std::filesystem::create_directories(root / "picture-save");
        CHECK(pictures.Save(root / "picture-save/scene.pmm", Write, reason));
        EditorSession loaded;
        CHECK(loaded.Open(root / "picture-save/scene.pmm", reason));
        CHECK(!loaded.Document().picture.enabled && loaded.Assets().picture);
        CHECK(loaded.Assets().picturePath == root / "picture.tga");
        CHECK(loaded.Assets().picture->pixels == image->pixels);
        CHECK(loaded.ShowPicture(true, reason));
        CHECK(loaded.Undo(reason) && !loaded.Document().picture.enabled);
        CHECK(loaded.Redo(reason) && loaded.Document().picture.enabled);
        const auto retained = loaded.Assets().picture;
        CHECK(!loaded.ImportPicture(root / "missing-picture.tga", 640, 480, reason));
        CHECK(loaded.Assets().picture == retained);
        CHECK(loaded.Assets().picturePath.empty() && !loaded.Document().picture.pathBytes[0]);
        CHECK(loaded.Document().picture.enabled);
        CHECK(Write(root / "bad-picture.tga", {1, 2, 3}, reason));
        CHECK(!loaded.ImportPicture(root / "bad-picture.tga", 640, 480, reason));
        CHECK(!loaded.Assets().picture && loaded.Document().picture.enabled);
        CHECK(loaded.Undo(reason) && loaded.Assets().picture == retained);
        CHECK(loaded.Undo(reason) && loaded.Assets().picturePath == root / "picture.tga");
        const auto revision = loaded.Revision();
        CHECK(!loaded.ImportPicture(root / "picture.tga", 0, 480, reason));
        CHECK(loaded.Revision() == revision);
    }
    {
        EditorSession curves;
        CHECK(curves.ImportModel(root / "bone-only.pmd", reason));
        curves.Selected().model = 0;
        curves.Selected().keys = {{Track::Bone, 0, 0, 0}};
        const auto original = curves.Document().models[0].boneKeys.dense[0].interpolation;
        const auto initialRevision = curves.Revision();
        CHECK(curves.Interpolation(4).uniform);
        CHECK(curves.BeginGesture());
        CHECK(!curves.BeginGesture());
        CHECK(curves.MoveInterpolationPoint(0, 0, 31, 47, reason));
        CHECK(curves.MoveInterpolationPoint(0, 0, 42, 54, reason));
        curves.EndGesture();
        CHECK(curves.Document().models[0].boneKeys.dense[0].interpolation[0] == 42);
        CHECK(curves.Undo(reason));
        CHECK(curves.Revision() == initialRevision);
        CHECK(curves.Document().models[0].boneKeys.dense[0].interpolation == original);
        CHECK(curves.Redo(reason));
        CHECK(curves.Interpolation(4).canCopy && !curves.Interpolation(4).uniform);
        CHECK(!curves.MoveInterpolationPoint(4, 0, 50, 50, reason));
        CHECK(curves.CopyInterpolation(4, reason));
        CHECK(curves.Register({{Track::Bone, 0, 0, 30}}, reason));
        curves.Selected().keys = {{Track::Bone, 0, 0, 30}};
        CHECK(curves.LinearInterpolation(4, reason));
        curves.Selected().keys.insert({Track::Bone, 0, 0, 0});
        CHECK(!curves.Interpolation(4).canCopy);
        CHECK(!curves.CopyInterpolation(4, reason));
        CHECK(curves.PasteInterpolation(4, reason));
        CHECK(curves.Interpolation(4).canCopy && !curves.Interpolation(4).uniform);
        // A single copied channel populates all six clipboard slots, including
        // the two camera-only channels when copying from a model.
        CHECK(curves.CopyInterpolation(0, reason));
        curves.Selected().model = 255;
        curves.Selected().keys = {{Track::Camera, 255, 0, 0}};
        CHECK(curves.PasteInterpolation(6, reason));
        CHECK(curves.Interpolation(6).uniform);
        CHECK(curves.Interpolation(6).curves.front() == (InterpolationCurve{42, 54, 107, 107}));
        CHECK(curves.MoveInterpolationPoint(6, 1, 200, -10, reason));
        CHECK(curves.Interpolation(6).curves.front() == (InterpolationCurve{42, 54, 127, 0}));
        CHECK(curves.Save(root / "curves.pmm", Write, reason));
        CHECK(curves.Open(root / "curves.pmm", reason));
        curves.Selected().model = 255;
        curves.Selected().keys = {{Track::Camera, 255, 0, 0}};
        CHECK(curves.Interpolation(6).curves.front() == (InterpolationCurve{42, 54, 127, 0}));
        CHECK(curves.LinearInterpolation(6, reason));
        CHECK(curves.Interpolation(6).curves.front() == (InterpolationCurve{20, 20, 107, 107}));
        const auto linearRevision = curves.Revision();
        CHECK(curves.BeginGesture());
        CHECK(curves.MoveInterpolationPoint(6, 0, 20, 20, reason));
        curves.EndGesture();
        CHECK(curves.Revision() == linearRevision);
        curves.Selected().keys = {{Track::Light, 255, 0, 0}};
        CHECK(!curves.Interpolation(6).found);
        CHECK(!curves.PasteInterpolation(6, reason));
    }
    EditorSession editor;
    {
        EditorSession roots;
        CHECK(roots.ImportModel(root / "grouped.pmx", reason));
        CHECK(roots.SetBone(0, 0, {3, 4, 5}, {0, 0, 0, 1}, reason));
        CHECK(roots.Register({{Track::Bone, 0, 0, 0}, {Track::Bone, 0, 0, 20}}, reason));
        roots.Selected().keys = {{Track::Bone, 0, 0, 0}};
        CHECK(roots.MoveInterpolationPoint(0, 0, 35, 60, reason));
        const auto savedCurve = roots.Document().models[0].boneKeys.dense[0].interpolation;
        CHECK(roots.Delete({{Track::Bone, 0, 0, 0}}, reason));
        CHECK(roots.Document().models[0].boneKeys.dense[0].position == (std::array<float, 3>{}));
        CHECK(roots.Document().models[0].boneKeys.dense[0].interpolation == savedCurve);
        CHECK(roots.Document().models[0].boneKeys.sparse.size() == 1);
        CHECK(roots.Runtime().Snapshot(0)->bones[0].translation == (std::array<float, 3>{}));
        CHECK(roots.Selected().keys.empty());
        CHECK(roots.Undo(reason));
        CHECK(roots.Document().models[0].boneKeys.dense[0].position[0] == 3);
        CHECK(roots.Redo(reason));
        CHECK(roots.Seek(20, reason));
        CHECK(roots.Runtime().Snapshot(0)->bones[0].translation[0] == 3);
        CHECK(roots.SetMorph(0, 0, .75f, reason));
        CHECK(roots.Register({{Track::Morph, 0, 0, 0}}, reason));
        CHECK(roots.Delete({{Track::Morph, 0, 0, 0}}, reason));
        CHECK(roots.Document().models[0].morphKeys.dense[0].value == 0);
        auto camera = roots.Runtime().Session()->Pose().camera;
        camera.distance = -12;
        camera.position = {1, 2, 3};
        camera.fov = 63;
        camera.perspective = 1;
        CHECK(roots.SetCamera(camera, reason));
        CHECK(roots.Register({{Track::Camera, 255, 0, 0}, {Track::Camera, 255, 0, 20}}, reason));
        CHECK(roots.Delete({{Track::Camera, 255, 0, 0}}, reason));
        const auto resetCamera = roots.Document().cameraKeys.dense[0];
        CHECK(resetCamera.distance == -45 && resetCamera.eye[1] == 10);
        CHECK(resetCamera.fov == 63 && resetCamera.perspective == 1);
        CHECK(resetCamera.next != 0);
        CHECK(roots.ImportAccessory(root / "bound.x", reason));
        auto accessory = roots.Document().accessories[0].state;
        accessory.position = {2, 3, 4};
        accessory.opacity = .5f;
        accessory.scale = 2;
        accessory.shadowEnabled = 1;
        CHECK(roots.SetAccessory(0, accessory, reason));
        CHECK(roots.Register({{Track::Accessory, 0, 0, 0}, {Track::Accessory, 0, 0, 20}}, reason));
        CHECK(roots.Delete({{Track::Accessory, 0, 0, 0}}, reason));
        const auto resetAccessory = roots.Document().accessories[0].keyRecords.dense[0];
        CHECK(resetAccessory.position == (std::array<float, 3>{}));
        CHECK(resetAccessory.opacity == 1 && resetAccessory.scale == 1 &&
              !resetAccessory.shadowEnabled);
        CHECK(resetAccessory.next != 0);
        CHECK(roots.Register({{Track::Light, 255, 0, 20}}, reason));
        const auto beforeRejected = roots.Revision();
        CHECK(!roots.Delete({{Track::Light, 255, 0, 0}}, reason));
        CHECK(reason == "native-root-reset-detached-keys-unsupported");
        CHECK(roots.Revision() == beforeRejected && roots.Document().lightKeys.sparse.size() == 1);
        CHECK(roots.Delete({{Track::Light, 255, 0, 0},
                            {Track::Light, 255, 0, 20},
                            {Track::Shadow, 255, 0, 0},
                            {Track::Gravity, 255, 0, 0}},
                           reason));
        CHECK(roots.Document().lightKeys.sparse.empty());
        CHECK(roots.Document().lightKeys.dense[0].color[0] == .602f);
        CHECK(roots.Document().selfShadowKeys.dense[0].distance == .01125f);
        CHECK(roots.Document().gravityKeys.dense[0].acceleration == 9.8000002f);
        CHECK(roots.Save(root / "deleted-roots.pmm", Write, reason));
        CHECK(roots.Open(root / "deleted-roots.pmm", reason));
        CHECK(roots.Document().cameraKeys.dense[0].fov == 63);
    }
    {
        EditorSession selection;
        CHECK(selection.ImportModel(root / "grouped.pmx", reason));
        CHECK(selection.Register(
            {{Track::Bone, 0, 0, 10}, {Track::Morph, 0, 0, 10}, {Track::Display, 0, 0, 10}},
            reason));
        selection.Selected().bones = {0};
        const auto revision = selection.Revision(), updates = selection.Runtime().Counters().frames;
        selection.SelectKeys(10, 10, {KeyScopeKind::SelectedBones, {}});
        CHECK(selection.Selected().keys.size() == 1);
        selection.SelectKeys(0, 0, {KeyScopeKind::Track, {Track::Morph, 0, 0, 0}});
        CHECK(selection.Selected().keys.size() == 2);
        selection.SelectKeys(0, 0, {KeyScopeKind::SelectedBones, {}});
        CHECK(selection.Selected().keys.count({Track::Bone, 0, 0, 10}));
        selection.SelectKeys(10, 10, {KeyScopeKind::Bones, {}});
        CHECK(selection.Selected().keys.size() == 2);
        CHECK(!selection.Selected().keys.count({Track::Bone, 0, 0, 0}));
        selection.ExtendSelectedColumns();
        CHECK(selection.Selected().keys.size() == 6);
        CHECK(selection.Revision() == revision && selection.Runtime().Counters().frames == updates);
        selection.SelectKeys(20, 10, {KeyScopeKind::Morphs, {}});
        CHECK(selection.Selected().keys.size() == 4);
        selection.Selected().model = 255;
        CHECK(
            selection.Register({{Track::Camera, 255, 0, 10}, {Track::Light, 255, 0, 10}}, reason));
        selection.Selected().keys.clear();
        selection.SelectKeys(0, 10, {KeyScopeKind::Track, {Track::Camera, 255, 0, 0}});
        CHECK(selection.Selected().keys.size() == 2);
        selection.SelectKeys(10, 10, {KeyScopeKind::Track, {Track::Light, 255, 0, 0}});
        CHECK(selection.Selected().keys.size() == 3);
        selection.SelectKeys(10, 10, {KeyScopeKind::Track, {Track::Camera, 255, 0, 0}});
        CHECK(selection.Selected().keys.size() == 2);
        CHECK(selection.Seek(99, reason));
        selection.ExtendSelectedColumns();
        CHECK(selection.Selected().keys.size() == 2); // selected columns, not cursor 99
        selection.Selected().keys.insert({Track::Camera, 255, 0, 0});
        selection.ExtendSelectedColumns();
        CHECK(selection.Selected().keys.size() == 6);
    }
    CHECK(!editor.Dirty());
    CHECK(editor.Document().models.empty());
    CHECK(editor.ImportModel(root / "bone-only.pmd", reason));
    CHECK(editor.Dirty());
    CHECK(editor.Runtime().Resource(0));
    const auto resource = editor.Runtime().Resource(0);
    CHECK(editor.Edit(
        "physics-off",
        [](auto& d, auto&) {
            d.physics.playbackMode = 0;
            return true;
        },
        reason));
    CHECK(editor.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
    CHECK(editor.Runtime().Resource(0) == resource);
    CHECK(editor.Document().models[0].bonePose[0].components[1] == 2);
    CHECK(std::abs(editor.Runtime().Snapshot(0)->bones[0].translation[1] - 2) < 1e-6f);
    CHECK(editor.Register({{Track::Bone, 0, 0, 0}}, reason));
    CHECK(editor.Seek(30, reason));
    CHECK(editor.SetBone(0, 0, {5, 8, 11}, {0, 0, 0, 1}, reason));
    CHECK(editor.Register({{Track::Bone, 0, 0, 30}}, reason));
    CHECK(editor.Seek(15, reason));
    CHECK(std::abs(editor.Runtime().Snapshot(0)->bones[0].translation[1] - 5) < .001f);
    CHECK(editor.Copy({{Track::Bone, 0, 0, 30}}, reason));
    CHECK(editor.Paste(60, false, reason));
    CHECK(editor.Seek(60, reason));
    CHECK(editor.Document().models[0].bonePose[0].components[1] == 8);
    CHECK(editor.Delete({{Track::Bone, 0, 0, 60}}, reason));
    CHECK(editor.Document().models[0].boneKeys.sparse.size() == 1);
    CHECK(editor.Undo(reason));
    CHECK(editor.Document().models[0].boneKeys.sparse.size() == 2);
    CHECK(editor.Redo(reason));
    CHECK(editor.Document().models[0].boneKeys.sparse.size() == 1);
    // Invalid transaction neither alters pose nor consumes the redo branch.
    CHECK(editor.Undo(reason));
    auto revision = editor.Revision();
    CHECK(!editor.SetBone(0, 0, {0, 0, 0}, {0, 0, 0, 0}, reason));
    CHECK(editor.Revision() == revision);
    CHECK(editor.CanRedo());
    format::VmdDocument motion;
    CHECK(editor.ExportMotion(0, motion, reason));
    format::VpdDocument pose;
    CHECK(editor.ExportPose(0, pose, reason));
    CHECK(editor.SetBone(0, 0, {1, 4, 9}, {0, 0, 0, 1}, reason));
    CHECK(editor.Save(root / "editor.pmm", Write, reason));
    CHECK(!editor.Dirty());
    EditorSession reopened;
    CHECK(reopened.Open(root / "editor.pmm", reason));
    CHECK(reopened.Document().models[0].bonePose[0].components[1] == 4);
    CHECK(std::abs(reopened.Runtime().Snapshot(0)->bones[0].translation[1] - 4) < 1e-6f);
    CHECK(reopened.Seek(15, reason));
    CHECK(std::abs(reopened.Runtime().Snapshot(0)->bones[0].translation[1] - 5) < .001f);
    CHECK(reopened.ImportPose(pose, 0, reason));
    CHECK(reopened.Dirty());
    CHECK(!reopened.Save(
        root / "rejected.pmm",
        [](const auto&, const auto&, auto& r) {
            r = "injected-disk-failure";
            return false;
        },
        reason));
    CHECK(reopened.Dirty());
    CHECK(reopened.Path().filename() == "editor.pmm");
    CHECK(reopened.ImportMotion(motion, 0, reason));
    CHECK(reopened.Seek(15, reason));
    CHECK(std::abs(reopened.Runtime().Snapshot(0)->bones[0].translation[1] - 5) < .001f);
    CHECK(reopened.UnloadModel(0, reason));
    CHECK(!reopened.Runtime().Snapshot(0));
    CHECK(reopened.Undo(reason));
    CHECK(reopened.Runtime().Snapshot(0));
    CHECK(reopened.Play(reason));
    auto count = reopened.Runtime().Counters().frames;
    CHECK(reopened.Advance(1. / 30., reason));
    CHECK(reopened.Runtime().Counters().frames == count + 1);
    reopened.Pause();
    count = reopened.Runtime().Counters().frames;
    CHECK(reopened.Advance(1. / 30., reason));
    CHECK(reopened.Runtime().Counters().frames == count);
    // An unregistered camera attachment is independent of the frame-zero key.
    auto attached = reopened.Runtime().Session()->Pose().camera;
    attached.parentModel = 0;
    attached.parentBone = 0;
    CHECK(reopened.SetCamera(attached, reason));
    CHECK(reopened.Runtime().Session()->Pose().camera.parentModel == 0);
    CHECK(reopened.Save(root / "attached.pmm", Write, reason));
    CHECK(reopened.Open(root / "attached.pmm", reason));
    CHECK(reopened.Runtime().Session()->Pose().camera.parentModel == 0);
    const auto attachmentRevision = reopened.Revision();
    attached.parentModel = 254;
    CHECK(!reopened.SetCamera(attached, reason));
    CHECK(reopened.Revision() == attachmentRevision);
    // New and existing v1/v2 scenes use the same editing/write path.
    for (const char* name :
         {"v1.pmm", "v2.pmm", "pmx.pmm", "ik-v1.pmm", "physics-v2.pmm", "sdef-physics-v2.pmm"}) {
        std::cout << "open " << name << std::endl;
        CHECK(reopened.Open(root / name, reason));
        CHECK(reopened.Save(root / "edited-fixture.pmm", Write, reason));
        CHECK(reopened.Open(root / "edited-fixture.pmm", reason));
        CHECK(reopened.Seek(15, reason));
    }
    std::string encoded = "unchanged", decoded;
    CHECK(text::EncodeCp932("\xe5\xb7\xa6\xe8\x85\x95", encoded));
    CHECK(text::DecodeCp932(encoded, decoded));
    CHECK(decoded == "\xe5\xb7\xa6\xe8\x85\x95");
    const auto valid = encoded;
    CHECK(!text::EncodeCp932("\xf0\x9f\x98\x80", encoded));
    CHECK(encoded == valid);
    CHECK(!text::EncodeCp932("\xe0\x80\x80", encoded));
    CHECK(encoded == valid);
    std::cout
        << "editor: pose, tracks, undo/redo, playback, import/export and saved reload passed\n";
}
