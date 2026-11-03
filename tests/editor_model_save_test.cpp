#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmd_writer.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include "editor_model_save_fixture.hpp"
#include <fstream>
#include <iostream>
using namespace mikudancestudio;
using namespace editor;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ' ' << reason << '\n';                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
namespace {
int Read(void* context, void* output, std::size_t size) {
    auto& file = *static_cast<std::ifstream*>(context);
    file.read(static_cast<char*>(output), std::streamsize(size));
    return int(file.gcount());
}
bool Write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
           std::string&) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    file.close();
    return bool(file);
}
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    for (unsigned profile = 0; profile < 4; ++profile) {
        const auto source = EnhancedSaveFixture(profile);
        std::vector<std::uint8_t> bytes;
        CHECK(format::WriteEnhancedPmd(source, bytes, reason));
        CHECK(EnhancedSaveHash(bytes) == EnhancedNativeSaveHashes[profile]);
        std::cout << "Enhanced PMD profile " << profile << " hash " << std::hex
                  << EnhancedSaveHash(bytes) << std::dec << '\n';
        CHECK(Write(root / "format-output.pmd", bytes, reason));
        std::ifstream file(root / "format-output.pmd", std::ios::binary);
        format::ModelDocument read;
        CHECK(format::ReadModelDocument(Read, &file, read));
        CHECK(read.kind == format::ModelKind::Pmd && read.pmd.englishPresence.hasEnglish &&
              read.pmd.englishPresence.hasToonFields && read.pmd.hasPhysicsSection);
        CHECK(read.pmd.rigids.size() == source.rigids.size() &&
              read.pmd.joints.size() == source.joints.size());
        if (!source.joints.empty())
            for (unsigned i = 0; i < 12; ++i)
                CHECK(read.pmd.joints[0].limits[i] == source.joints[0].limits[i]);
        if (!source.morphs.empty()) {
            const auto& final = source.morphs[0].offsets.back();
            CHECK(read.pmd.vertices[final.vertexIndex].position[1] == final.offset[1]);
            CHECK(source.vertices[final.vertexIndex].position[1] == -.25f);
            CHECK(read.pmd.morphs[1].offsets[1].vertexIndex == (profile == 2 ? 0u : 1u));
        }
        const auto unchanged = bytes;
        auto invalid = source;
        invalid.englishBoneNames.clear();
        CHECK(!format::WriteEnhancedPmd(invalid, bytes, reason) && bytes == unchanged);
        if (!source.morphs.empty()) {
            invalid = source;
            invalid.morphs[1].offsets[0].vertexIndex = 0xffffffffu;
            CHECK(!format::WriteEnhancedPmd(invalid, bytes, reason) && bytes == unchanged);
        }
    }
    for (const char* name : {"triangle.pmd", "english.pmd", "bone-only.pmd", "triangle.pmx"}) {
        EditorSession session;
        const auto input = root / "save-source.pmd";
        std::filesystem::copy_file(root / name, input,
                                   std::filesystem::copy_options::overwrite_existing);
        CHECK(session.ImportModel(input, reason));
        const auto resource = session.Runtime().Resource(0);
        CHECK(session.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
        CHECK(session.SetEnglishName(0, EnglishNameKind::Model, 0, "saved name", reason));
        CHECK(session.SetEnglishName(0, EnglishNameKind::Bone, 0, "saved bone", reason));
        CHECK(session.SetEnglishName(0, EnglishNameKind::Comment, 0, "saved\ncomment", reason));
        CHECK(session.MarkEnhancedModel(0, reason));
        const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
        const auto undo = session.CanUndo(), redo = session.CanRedo();
        const auto dirty = session.Dirty();
        const auto originalPath = session.Document().models[0].path;
        const auto pose = session.Document().models[0].bonePose[0].components;
        std::vector<std::uint8_t> saved;
        auto save = [&](const auto& path, const auto& bytes, auto& r) {
            saved = bytes;
            return Write(path, bytes, r);
        };
        std::filesystem::create_directories(root / "different-folder");
        const auto output = root / "different-folder/enhanced.pmd";
        if (resource->SourceKind() != format::ModelKind::Pmd) {
            CHECK(!resource->PmdSource());
            CHECK(!session.SaveEnhancedModel(0, output, save, reason));
            CHECK(reason == "enhanced-model-requires-pmd" && session.EnhancedModelDirty() &&
                  saved.empty());
            continue;
        }
        // Saving must not read a changed/unavailable original model.
        CHECK(Write(input, {}, reason));
        CHECK(!session.SaveEnhancedModel(0, output, {}, reason) && session.EnhancedModelDirty());
        CHECK(!session.SaveEnhancedModel(
            0, output,
            [](const auto&, const auto&, auto& r) {
                r = "injected-write-error";
                return false;
            },
            reason));
        CHECK(reason == "injected-write-error" && !session.EnhancedModelDirty());
        CHECK(session.Revision() != revision && session.Dirty());
        CHECK(session.Document().models[0].path == originalPath &&
              session.Assets().modelPaths.at(0) == input);
        CHECK(session.MarkEnhancedModel(0, reason));
        CHECK(session.SaveEnhancedModel(0, output, save, reason));
        CHECK(!saved.empty() && !session.EnhancedModelDirty());
        CHECK(session.Assets().modelPaths.at(0) == output &&
              session.Assets().modelTexturePaths.at(0) == input);
        CHECK(session.Document().models[0].path != originalPath);
        CHECK(session.Revision() != revision && session.Dirty() && session.CanUndo() == undo &&
              session.CanRedo() == redo && session.Runtime().Counters().frames == frames &&
              session.Runtime().Resource(0) == resource &&
              session.Document().models[0].bonePose[0].components == pose);
        EditorSession reloaded;
        CHECK(reloaded.ImportModel(output, reason));
        format::PmmModelEnglishNames names;
        CHECK(reloaded.ModelEnglishNames(0, names, reason));
        CHECK(names.name == "saved name" && names.bones[0] == "saved bone" &&
              names.comment == "saved\ncomment");
        CHECK(reloaded.Runtime().Resource(0)->Document().vertices.size() ==
              resource->Document().vertices.size());
        CHECK(session.Save(root / "enhanced-project.pmm", Write, reason));
        CHECK(reloaded.Open(root / "enhanced-project.pmm", reason));
        CHECK(reloaded.ModelEnglishNames(0, names, reason) && names.bones[0] == "saved bone");
        const auto split = std::string(18, 'a') + "\xe3\x81\x82";
        CHECK(session.SetEnglishName(0, EnglishNameKind::Model, 0, split, reason));
        CHECK(session.SetEnglishName(0, EnglishNameKind::Bone, 0, split, reason));
        CHECK(session.SaveEnhancedModel(0, output, save, reason));
        CHECK(reloaded.New(reason) && reloaded.ImportModel(output, reason));
        CHECK(reloaded.ModelEnglishNames(0, names, reason));
        CHECK(names.name == std::string(18, 'a') + '\x82' && names.bones[0] == names.name &&
              EnglishNameDisplay(names.name) == std::string(18, 'a') + "\xef\xbf\xbd");
        CHECK(session.Play(reason));
        CHECK(!session.SaveEnhancedModel(0, output, save, reason) && reason == "editor-is-playing");
        session.Pause();
        CHECK(session.UnloadModel(0, reason));
        CHECK(!session.SaveEnhancedModel(0, output, save, reason));
    }
    std::cout << "Enhanced PMD serialization and live save/reload passed\n";
}
