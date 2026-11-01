#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/text_encoding.hpp"
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
    const auto root = std::filesystem::absolute(argv[1]);
    EditorSession editor;
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
