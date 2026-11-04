#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "model_reader_fixture.hpp"
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace mikudancestudio;
using namespace format;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << '\n';                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

std::array<char, 256> Path(const std::string& utf8) {
    std::string bytes;
    if (!text::EncodeCp932(utf8, bytes) || bytes.size() >= 256)
        std::abort();
    std::array<char, 256> result{};
    std::copy(bytes.begin(), bytes.end(), result.begin());
    return result;
}
void Write(const fs::path& file, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!output)
        std::abort();
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto base = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    const auto project = base / "project";
    const auto install = base / "install";
    const auto model = model_reader_fixture::Pmd(false, false).data;
    // U+8868 ends in CP932 byte 0x5c: treating raw bytes as separators corrupts it.
    const auto name = fs::u8path(u8"\u8868\u30e2\u30c7\u30eb.pmd");
    Write(project / "UserFile/Model" / name, model);
    Write(install / "UserFile/Model" / name, model);
    Write(project / "UserFile/Accessory/test.x", {1});
    Write(project / "UserFile/Wave/test.wav", {2});
    Write(project / "UserFile/BackGround/test.bmp", {3});
    SceneResourceResolver resolver(project / "UserFile/Scenes/sample.pmm");
    ModelDocument parsed;
    // Both drive-qualified and pre-drive-letter legacy paths occur in MMD's
    // distributed PMMs. Decode CP932 before interpreting Windows separators.
    for (const auto& prefix : {"G:\\MMD\\", "HB\\DownLoad\\MMD\\", "//server/share/"}) {
        const auto raw = Path(prefix + std::string("UserFile\\Model\\") + name.u8string());
        CHECK(resolver.ResolveModel(0, raw, parsed) && parsed.kind == ModelKind::Pmd);
        CHECK(resolver.Resolved().at(0) == project / "UserFile/Model" / name);
    }
    // The fallback folder also works when no UserFile suffix was saved.
    CHECK(resolver.ResolveModel(0, Path("Z:/removed/" + name.u8string()), parsed));
    fs::path result;
    for (const auto& item : {std::pair{SceneResourceKind::Accessory, "Accessory/test.x"},
                             {SceneResourceKind::Wave, "Wave/test.wav"},
                             {SceneResourceKind::Background, "BackGround/test.bmp"}}) {
        CHECK(resolver.ResolveFile(item.first, Path("Z:/MMD/UserFile/" + std::string(item.second)),
                                   result));
        CHECK(result == project / "UserFile" / item.second);
    }
    CHECK(resolver.ResolveAccessory(0, Path("Z:/MMD/UserFile/Accessory/test.x"), result));
    // An explicit installation root has the native executable-root precedence.
    resolver.AddSearchRoot(install);
    CHECK(resolver.ResolveModel(0, Path("Z:/MMD/UserFile/Model/" + name.u8string()), parsed));
    CHECK(resolver.Resolved().at(0) == install / "UserFile/Model" / name);
    resolver.ReselectModel(0, project / "UserFile/Model" / name);
    CHECK(resolver.ResolveModel(0, Path("unavailable"), parsed));
    CHECK(resolver.Resolved().at(0) == project / "UserFile/Model" / name);
    // An explicit failed reselection is not silently replaced by a namesake.
    resolver.ReselectModel(0, base / "missing.pmd");
    CHECK(!resolver.ResolveModel(0, Path("Z:/MMD/UserFile/Model/" + name.u8string()), parsed));
    CHECK(resolver.Missing().back().tried == std::vector<fs::path>{base / "missing.pmd"});
    result = "unchanged";
    CHECK(!resolver.ResolveFile(SceneResourceKind::Wave, Path(""), result));
    CHECK(result == "unchanged");
    CHECK(!resolver.ResolveFile(SceneResourceKind::Wave, Path("bad\npath.wav"), result));
    std::cout
        << "CP932 UserFile relocation, typed media lookup, precedence and reselection passed\n";
}
