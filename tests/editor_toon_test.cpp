#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmd_writer.hpp"
#include "editor_model_save_fixture.hpp"
#include <fstream>
#include <iostream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ' ' << reason << '\n';                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
bool Write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
           std::string&) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(f);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    std::filesystem::copy_file(root / "checker.bmp", root / "custom.bmp",
                               std::filesystem::copy_options::overwrite_existing);
    editor::EditorSession session;
    std::array<std::string, 10> names;
    CHECK(!session.ModelToonNames(0, names, reason));
    CHECK(session.ImportModel(root / "triangle.pmd", reason));
    CHECK(session.ModelToonNames(0, names, reason));
    for (unsigned i = 0; i < 10; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "toon%02u.bmp", i + 1);
        names[i] = name;
    }
    const auto defaults = names;
    const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
    const auto resource = session.Runtime().Resource(0);
    const auto undo = session.CanUndo(), redo = session.CanRedo();
    names[0] = "custom.bmp";
    names[2] = "missing.bmp";
    names[7] = "missing-again.bmp";
    std::vector<unsigned> missing;
    CHECK(!session.SetModelToonNames(0, names, missing, reason));
    CHECK(missing == (std::vector<unsigned>{2, 7}));
    CHECK(!session.EnhancedModelDirty());
    std::array<std::string, 10> read;
    CHECK(session.ModelToonNames(0, read, reason) && read[0] == "custom.bmp");
    CHECK(session.Assets().modelToons.at(0)->images[0]);
    const auto image = session.Assets().modelToons.at(0)->images[0];
    CHECK(session.SetModelToonNames(0, defaults, missing, reason));
    CHECK(session.ModelToonNames(0, read, reason) && read[0] == "custom.bmp");
    CHECK(session.EnhancedModelDirty());
    CHECK(session.Revision() == revision && session.Runtime().Counters().frames == frames);
    CHECK(session.Runtime().Resource(0) == resource && session.CanUndo() == undo &&
          session.CanRedo() == redo);
    std::filesystem::remove(root / "custom.bmp");
    names = defaults;
    names[0] = "custom.bmp";
    CHECK(session.SetModelToonNames(0, names, missing, reason));
    CHECK(session.Assets().modelToons.at(0)->images[0] == image);
    CHECK(session.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
    CHECK(session.Undo(reason));
    CHECK(session.ModelToonNames(0, read, reason) && read[0] == "custom.bmp");
    std::vector<std::uint8_t> saved;
    CHECK(session.SaveEnhancedModel(
        0, root / "saved-toons.pmd",
        [&](const auto& path, const auto& bytes, auto& error) {
            saved = bytes;
            return Write(path, bytes, error);
        },
        reason));
    CHECK(!session.EnhancedModelDirty());
    editor::EditorSession reopened;
    CHECK(reopened.ImportModel(root / "saved-toons.pmd", reason));
    CHECK(reopened.ModelToonNames(0, read, reason) && read[0] == "custom.bmp");
    session.RefreshModelToons();
    CHECK(session.ModelToonNames(0, read, reason) && read[0] == "custom.bmp");
    CHECK(!session.Assets().modelToons.at(0)->images[0]);
    CHECK(!session.SetModelToonNames(0, names, missing, reason) &&
          missing == std::vector<unsigned>{0});
    CHECK(!session.EnhancedModelDirty());
    CHECK(session.Play(reason));
    CHECK(!session.SetModelToonNames(0, defaults, missing, reason));
    session.Pause();
    CHECK(session.UnloadModel(0, reason) && !session.Assets().modelToons.count(0));
    CHECK(session.ImportModel(root / "triangle.pmx", reason));
    CHECK(!session.SetModelToonNames(0, defaults, missing, reason));
    CHECK(session.New(reason) && session.Assets().modelToons.empty());
    std::cout
        << "Toon partial commit, default skip, cache, metadata/save, history and lifecycle passed\n";
}
