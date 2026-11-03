#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/vac_format.hpp"
#include "vac_fixture.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " " << reason << '\n';               \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
bool Text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    return bool(file.write(text.data(), std::streamsize(text.size())));
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    const auto directory = root / "vac";
    std::filesystem::create_directories(directory / "mesh");
    std::filesystem::copy_file(root / "bound.x", directory / "mesh/bound.x");
    const auto cases = VacCases();
    const float scales[]{1, 0, -1.75f, 44, .000125f, .1f};
    const bool shadows[]{true, false, false, false, false, true, true, true};
    for (std::size_t i = 0; i < cases.size(); ++i) {
        format::VacAccessory parsed;
        CHECK(format::ReadVac(cases[i], parsed, reason));
        CHECK(parsed.meshPath == "mesh\\bound.x" && parsed.scale == scales[i / 16] &&
              parsed.shadowEnabled == shadows[(i / 2) % 8]);
        CHECK(parsed.position == (std::array<float, 3>{.6f, -1.9f, -.3f}) &&
              parsed.rotation == (std::array<float, 3>{0, -0.f, 3.14f}) &&
              std::signbit(parsed.rotation[1]));
        const auto path = directory / "fixture.vac";
        CHECK(Text(path, cases[i]));
        editor::EditorSession session;
        CHECK(session.ImportAccessory(path, reason));
        const auto& a = session.Document().accessories[0];
        const auto& k = a.keyRecords.dense[0];
        CHECK(a.state.position == parsed.position && a.state.rotation == parsed.rotation &&
              a.state.scale == parsed.scale &&
              a.state.shadowEnabled == std::uint8_t(parsed.shadowEnabled) &&
              a.state.parentModel == -1 && a.state.parentBone == 0 && a.state.opacity == 1);
        CHECK(k.position == a.state.position && k.rotation == a.state.rotation &&
              k.scale == a.state.scale && k.shadowEnabled == a.state.shadowEnabled &&
              k.parentModel == -1 && k.parentBone == 0 && k.visible && k.opacity == 1);
        CHECK(std::string(a.name.data()) == "mesh\\bound.x" &&
              session.Assets().accessoryPaths.at(0) == path &&
              session.Assets().accessories.at(0)->Vertices().size() == 4);
    }
    for (const auto& invalid : {std::string(), std::string(65537, 'x'), std::string("x\0y", 3),
                                std::string("title\n\n"), std::string(100, 't') + cases[0],
                                std::string("title\nmesh.x\nnan\n0,0,0\n0,0,0\nbone\n0\n"),
                                std::string("title\nmesh.x\n1\n0, 0,inf\n0,0,0\nbone\n0\n"),
                                std::string("title\nmesh.x\n1\n0, 0\n0,0,0\nbone\n0\n")}) {
        format::VacAccessory parsed;
        parsed.meshPath = "keep";
        CHECK(!format::ReadVac(invalid, parsed, reason) && parsed.meshPath == "keep");
    }
    // Explicit CP932 mesh names; the VAC title and bone are not display names
    // or automatic model attachment commands.
    const std::string unicode = u8"\u30c6\u30b9\u30c8.x";
    std::string encoded;
    CHECK(text::EncodeCp932(unicode, encoded));
    std::filesystem::copy_file(root / "bound.x", directory / std::filesystem::u8path(unicode));
    const auto path = directory / "fixture.vac";
    CHECK(Text(path, "ignored\n" + encoded + "\n1.5\n1,2,3\n.1,.2,.3\nignored bone\n1\n"));
    editor::EditorSession session;
    CHECK(session.ImportAccessory(path, reason));
    CHECK(std::string(session.Document().accessories[0].name.data()) == encoded);
    auto state = session.Document().accessories[0].state;
    state.position = {7, 8, 9};
    state.rotation = {1, 2, 3};
    state.scale = 2;
    state.shadowEnabled = 1;
    CHECK(session.SetAccessory(0, state, reason));
    std::filesystem::create_directories(root / "saved");
    const auto saved = root / "saved/accessory.pmm";
    CHECK(session.Save(
        saved,
        [](const auto& target, const auto& bytes, auto&) {
            std::ofstream file(target, std::ios::binary);
            return bool(file.write(reinterpret_cast<const char*>(bytes.data()),
                                   std::streamsize(bytes.size())));
        },
        reason));
    // Opening restores PMM's current state, not subsequently edited VAC defaults.
    CHECK(Text(path, "ignored\n" + encoded + "\n9\n0,0,0\n0,0,0\nbone\n0\n"));
    editor::EditorSession reopened;
    CHECK(reopened.Open(saved, reason));
    const auto& restored = reopened.Document().accessories[0];
    CHECK(restored.state.position == state.position && restored.state.rotation == state.rotation &&
          restored.state.scale == 2 && restored.state.shadowEnabled);
    CHECK(reopened.Assets().accessoryPaths.at(0) == path &&
          restored.keyRecords.dense[0].scale == 1.5f &&
          !restored.keyRecords.dense[0].shadowEnabled);
    CHECK(reopened.Seek(0, reason) && reopened.Document().accessories[0].state.scale == 1.5f);
    const auto revision = reopened.Revision();
    const auto resource = reopened.Assets().accessories.at(0);
    CHECK(Text(directory / "bad.vac", "bad\nmissing.x\n1\n0,0,0\n0,0,0\nbone\n0\n"));
    CHECK(!reopened.ImportAccessory(directory / "bad.vac", reason) &&
          reopened.Revision() == revision && reopened.Assets().accessories.at(0) == resource);
    CHECK(Text(directory / "upper.VAC", cases[0]));
    CHECK(!reopened.ImportAccessory(directory / "upper.VAC", reason) &&
          reopened.Revision() == revision);
    CHECK(reopened.ImportAccessory(root / "bound.x", reason));
    const auto& plain = reopened.Document().accessories[1];
    CHECK(std::string(plain.name.data()) == "bound.x" && !plain.state.shadowEnabled &&
          plain.state.parentBone == 0);
    std::puts(
        "VAC: 96 parser/import cases, CP932, radians, optional shadow, atomic failure and PMM state passed");
}
