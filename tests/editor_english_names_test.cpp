#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include <iostream>
#include <fstream>
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
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    CHECK(EnglishNameBytes(std::string(18, 'a') + "\xe3\x81\x82", 19) ==
          std::string(18, 'a') + '\x82');
    CHECK(EnglishNameDisplay(std::string(18, 'a') + '\x82') ==
          std::string(18, 'a') + "\xef\xbf\xbd");
    CHECK(EnglishNameBytes("A\xf0\x9f\x8e\xb5Z", 19) == "A?Z");
    for (const char* file : {"lips.pmd", "lips.pmx", "bone-only.pmd"}) {
        EditorSession session;
        format::PmmModelEnglishNames names;
        CHECK(!session.ModelEnglishNames(0, names, reason));
        CHECK(session.ImportModel(root / file, reason));
        const auto resource = session.Runtime().Resource(0);
        CHECK(session.ModelEnglishNames(0, names, reason));
        CHECK(names.bones.size() == resource->Document().bones.size());
        if (resource->SourceKind() == format::ModelKind::Pmd) {
            CHECK(!resource->Document().texts.fields[0].empty());
            CHECK(names.bones[0] == EnglishNameBytes(resource->BoneNames()[0], 19));
        }
        CHECK(session.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
        CHECK(session.Undo(reason));
        const auto frames = session.Runtime().Counters().frames;
        const auto revision = session.Revision();
        const auto dirty = session.Dirty(), undo = session.CanUndo(), redo = session.CanRedo();
        CHECK(session.SetEnglishName(0, EnglishNameKind::Model, 0, "English model label 123456789",
                                     reason));
        CHECK(session.SetEnglishName(0, EnglishNameKind::Bone, 0, "English bone label 123456789",
                                     reason));
        CHECK(session.SetEnglishName(0, EnglishNameKind::Comment, 0,
                                     std::string(250, 'c') + "\r\nA\r\nB", reason));
        if (names.morphs.size() > 1)
            CHECK(session.SetEnglishName(0, EnglishNameKind::Morph, 1, "new morph", reason));
        if (!names.groups.empty())
            CHECK(session.SetEnglishName(0, EnglishNameKind::Group,
                                         unsigned(names.groups.size() - 1), std::string(80, 'g'),
                                         reason));
        CHECK(session.MarkEnhancedModel(0, reason));
        CHECK(session.EnhancedModelDirty());
        CHECK(session.Revision() == revision && session.Dirty() == dirty &&
              session.CanUndo() == undo && session.CanRedo() == redo);
        CHECK(session.Runtime().Resource(0) == resource &&
              session.Runtime().Counters().frames == frames);
        CHECK(session.ModelEnglishNames(0, names, reason));
        CHECK(names.name == "English model label" && names.bones[0] == "English bone label ");
        CHECK(names.comment == std::string(250, 'c') + "\nA\n" && names.enhanced);
        const auto metadata = session.Document().models[0].englishNames;
        CHECK(!session.SetEnglishName(0, EnglishNameKind::Bone, unsigned(names.bones.size()),
                                      "invalid", reason));
        CHECK(session.Document().models[0].englishNames == metadata);
        CHECK(session.Redo(reason));
        CHECK(session.Document().models[0].englishNames == metadata);
        CHECK(session.Document().models[0].bonePose[0].components[0] == 1);
        CHECK(session.Undo(reason));
        CHECK(session.Document().models[0].englishNames == metadata);
        CHECK(session.Seek(10, reason));
        CHECK(session.Document().models[0].englishNames == metadata);
        CHECK(session.Play(reason));
        CHECK(session.SetEnglishName(0, EnglishNameKind::Bone, 0, "during playback", reason));
        session.Pause();
        const auto output = root / "names.pmm";
        CHECK(session.Save(
            output,
            [](const auto& path, const auto& bytes, auto&) {
                std::ofstream file(path, std::ios::binary);
                file.write(reinterpret_cast<const char*>(bytes.data()),
                           std::streamsize(bytes.size()));
                file.close();
                return bool(file);
            },
            reason));
        CHECK(session.ModelEnglishNames(0, names, reason));
        CHECK(names.bones[0] == "during playback" && names.enhanced);
        CHECK(session.Open(output, reason));
        CHECK(!session.EnhancedModelDirty());
        CHECK(!session.Document().models[0].englishNames);
        CHECK(session.ModelEnglishNames(0, names, reason));
        CHECK(names.name == "English model label" && !names.enhanced &&
              names.bones[0] != "during playback");
        CHECK(session.UnloadModel(0, reason));
        CHECK(!session.SetEnglishName(0, EnglishNameKind::Model, 0, "stale", reason));
    }
    std::cout
        << "English-name metadata: native byte widths, live edits, history/physics retention and PMM lifetime passed\n";
    return 0;
}
