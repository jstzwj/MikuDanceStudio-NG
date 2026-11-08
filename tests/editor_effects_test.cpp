#include "mikudancestudio/editor_session.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
using namespace mikudancestudio;
using namespace editor;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ": " << reason << '\n';                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
bool Write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
           std::string&) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(file);
}
std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    EditorSession session;
    CHECK(session.ImportModel(root / "articulated.pmx", reason));
    CHECK(session.ImportAccessory(root / "bound.x", reason));
    const auto frames = session.Runtime().Counters().frames;
    const EffectAddress model{false, 0, -1}, accessory{true, 0, -1};
    mme::ParameterValue tint;
    tint.columns = 4;
    const float rgba[] = {.1f, .2f, .3f, .4f};
    tint.words.resize(4);
    std::memcpy(tint.words.data(), rgba, 16);
    CHECK(!session.AssignEffect({false, 200, -1}, root / "missing.fx", reason));
    CHECK(session.AssignEffect(model, root / "material.fx", reason));
    CHECK(session.SetEffectParameter(model, "Tint", tint, reason));
    CHECK(session.AssignEffect(accessory, root / "scene.fx", reason));
    CHECK(session.EnableEffect(accessory, false, reason));
    CHECK(session.Runtime().Counters().frames == frames);
    CHECK(session.Save(root / "effects.pmm", Write, reason));
    CHECK(!session.Dirty());
    CHECK(std::filesystem::exists(EffectSidecar(root / "effects.pmm")));
    CHECK(session.ReloadEffect(model, reason));
    CHECK(!session.Dirty());
    CHECK(session.UndoEffects(reason));
    CHECK(session.RedoEffects(reason));
    EditorSession reopened;
    CHECK(reopened.Open(root / "effects.pmm", reason));
    CHECK(reopened.Effects().assignments.size() == 2);
    CHECK(reopened.Effects().assignments.at(model).parameters.at("Tint") == tint);
    CHECK(!reopened.Effects().assignments.at(accessory).enabled);
    std::filesystem::create_directory(root / "relocated");
    CHECK(session.Save(root / "relocated/save-as.pmm", Write, reason));
    CHECK(reopened.Open(root / "relocated/save-as.pmm", reason));
    CHECK(reopened.Effects().assignments.at(model).source == root / "material.fx");
    // The PMM remains independently readable by the original format reader.
    auto originalPmm = Read(root / "effects.pmm");
    CHECK(!originalPmm.empty());
    std::vector<std::uint8_t> bytes;
    CHECK(EncodeEffects(session.Effects(), root / "effects.pmm", 123, bytes, reason));
    EffectDocument preserved;
    preserved.basicMaterials = true;
    CHECK(!DecodeEffects(bytes, root / "effects.pmm", 124, preserved, reason));
    CHECK(preserved.basicMaterials);
    CHECK(DecodeEffects(bytes, root / "effects.pmm", 123, preserved, reason));
    auto malformed = bytes;
    malformed.push_back('x');
    CHECK(!DecodeEffects(malformed, root / "effects.pmm", 123, preserved, reason));
    // Failed sidecar-only save cannot alter the persisted assignment state.
    CHECK(session.SetEffectParameter(model, "Tint", tint, reason));
    unsigned writes = 0;
    const auto failed = [&](const auto& path, const auto& data, std::string& r) {
        if (++writes == 2) {
            r = "injected";
            return false;
        }
        return Write(path, data, r);
    };
    CHECK(!session.Save(root / "relocated/save-as.pmm", failed, reason));
    CHECK(session.Dirty());
    CHECK(reopened.Open(root / "relocated/save-as.pmm", reason));
    CHECK(reopened.Effects().assignments.size() == 2);
    // Inject a failure after the successor sidecar, before a changed PMM. The
    // last matching pair is recovered without applying assignments to wrong data.
    CHECK(session.Seek(3, reason));
    writes = 0;
    const auto interrupted = [&](const auto& path, const auto& data, std::string& r) {
        if (++writes == 3) {
            r = "interrupted";
            return false;
        }
        return Write(path, data, r);
    };
    CHECK(!session.Save(root / "relocated/save-as.pmm", interrupted, reason));
    CHECK(reopened.Open(root / "relocated/save-as.pmm", reason));
    CHECK(reopened.Effects().assignments.size() == 2);
    CHECK(session.UnloadModel(0, reason));
    CHECK(!session.Effects().assignments.count(model));
    CHECK(session.ImportModel(root / "articulated.pmx", reason));
    while (session.CanUndoEffects())
        CHECK(session.UndoEffects(reason));
    CHECK(!session.Effects().assignments.count(model));
    CHECK(session.New(reason));
    CHECK(session.Effects().assignments.empty() && !session.CanUndoEffects());
    std::cout
        << "Editor effects: persistence, Save As, interrupted publication, unload/history and no extra CPU update passed\n";
}
