#include "mikudancestudio/editor_session.hpp"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace mikudancestudio;
namespace fs = std::filesystem;
void Check(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
bool Write(const fs::path& path, const std::vector<std::uint8_t>& bytes, std::string& reason) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    stream.close();
    if (!stream)
        reason = "test-write-failed";
    return bool(stream);
}
int main(int argc, char** argv) try {
    Check(argc >= 3, "arguments: output directory, input PMMs");
    const auto output = fs::absolute(fs::u8path(argv[1]));
    fs::create_directories(output);
    for (int i = 2; i < argc; ++i) {
        const auto path = fs::u8path(argv[i]);
        editor::EditorSession session;
        std::string reason;
        const bool opened = session.Open(path, reason);
        Check(opened, path.u8string() + ": " + reason);
        Check(!session.Document().models.empty(), "loaded models");
        for (const auto& model : session.Document().models) {
            std::set<int> mapped;
            for (unsigned bone = 0; bone < model.boneNames.size(); ++bone) {
                int index = -1;
                Check(session.Runtime().DocumentBoneIndex(model.slot, int(bone), index, reason),
                      reason);
                Check(mapped.insert(index).second,
                      "duplicate names must retain distinct bone indices");
            }
            Check(mapped.size() == session.Runtime().Resource(model.slot)->BoneNames().size(),
                  "complete bone mapping");
        }
        Check(session.Seek(0, reason), reason);
        Check(session.Play(reason), reason);
        Check(session.Advance(1. / 30, reason), reason);
        session.Pause();
        Check(session.Seek(10, reason), reason);
        const auto saved = output / (std::to_string(i) + ".pmm");
        Check(session.Save(saved, Write, reason), reason);
        editor::EditorSession reopened;
        Check(reopened.Open(saved, reason), reason);
        Check(reopened.Document().timeline.currentFrame == session.Document().timeline.currentFrame,
              "saved frame");
        Check(reopened.Document().models.size() == session.Document().models.size() &&
                  reopened.Document().accessories.size() == session.Document().accessories.size(),
              "saved scene resources");
        for (const auto& model : session.Document().models) {
            const auto& before = *session.Runtime().Resource(model.slot);
            const auto& after = *reopened.Runtime().Resource(model.slot);
            Check(before.BoneNames() == after.BoneNames(), "saved bone identity");
            Check(session.Assets().modelPaths.at(model.slot) ==
                      reopened.Assets().modelPaths.at(model.slot),
                  "save-as resource rebasing");
        }
        const auto revision = reopened.Revision();
        const auto slot = reopened.Document().models.front().slot;
        const auto resource = reopened.Runtime().Resource(slot);
        Check(!reopened.Open(output / "missing.pmm", reason), "missing scene must fail");
        Check(reopened.Revision() == revision && reopened.Runtime().Resource(slot) == resource,
              "failed open preserves the active scene");
        std::cout << path.filename().u8string() << ": models=" << session.Document().models.size()
                  << " accessories=" << session.Document().accessories.size()
                  << " load/play/seek/save-as/reopen passed\n";
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
