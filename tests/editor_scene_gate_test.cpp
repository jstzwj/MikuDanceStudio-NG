#ifdef MDS_NATIVE_SCENE_GATE_ORACLE
#include <Windows.h>
#include "mikudancestudio/app_layout.hpp"
#endif
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

using namespace mikudancestudio;
void Require(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
#ifdef MDS_NATIVE_SCENE_GATE_ORACLE
static void Check(bool value, const char* reason) {
    Require(value, reason);
}
#include "original_physics_module.hpp"
class NativeGate {
    void* code_ = nullptr;

public:
    explicit NativeGate(OriginalPhysicsModule& original) {
        const auto* gate = original.Function<const unsigned char*>(0x2a427);
        const unsigned char begin[]{0x41, 0x83, 0xbc, 0x24, 0xe4, 0x13, 0, 0, 2};
        const unsigned char end[]{0x41, 0x84, 0x8c, 0x24, 0xf8, 0x10, 0x0a, 0};
        Require(!std::memcmp(gate, begin, sizeof begin) &&
                    !std::memcmp(gate + 0x2a, end, sizeof end),
                "original frame gate identity");
        // Preserve native nonvolatile inputs, then execute the original 50
        // bytes unchanged. No graphics, window or original entry point runs.
        std::vector<unsigned char> code{0x41, 0x54, 0x41, 0x57, 0x4c, 0x8b, 0xe1, 0x45, 0x33, 0xff};
        code.insert(code.end(), gate, gate + 0x32);
        const unsigned char done[]{0x0f, 0x95, 0xc0, 0x0f, 0xb6, 0xc0,
                                   0x41, 0x5f, 0x41, 0x5c, 0xc3};
        code.insert(code.end(), std::begin(done), std::end(done));
        code_ = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        Require(code_ != nullptr, "gate wrapper allocation");
        std::memcpy(code_, code.data(), code.size());
        DWORD prior;
        Require(VirtualProtect(code_, code.size(), PAGE_EXECUTE_READ, &prior),
                "gate wrapper executable");
        FlushInstructionCache(GetCurrentProcess(), code_, code.size());
    }
    ~NativeGate() {
        if (code_)
            VirtualFree(code_, 0, MEM_RELEASE);
    }
    bool operator()(int edit, bool playing, unsigned master, int mode) const {
        auto app = std::make_unique<MMDAppState>();
        app->editMode = edit;
        app->playbackActive = playing;
        app->selfShadowEnabled = std::uint8_t(master);
        app->selfShadowMode = mode;
        const auto before = std::make_unique<MMDAppState>(*app);
        const auto result = reinterpret_cast<int (*)(const void*)>(code_)(app.get());
        Require(!std::memcmp(app.get(), before.get(), sizeof(*app)), "native gate mutated state");
        return result != 0;
    }
};
#endif

int main(int argc, char** argv) try {
#ifdef MDS_NATIVE_SCENE_GATE_ORACLE
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Require(argc == 4, "usage: original.exe fixture assets");
    OriginalPhysicsModule original(std::filesystem::u8path(argv[1]));
    NativeGate reference(original);
    const auto root = std::filesystem::u8path(argv[2]);
    rhi::FileAssetSource assets(std::filesystem::u8path(argv[3]));
#else
    Require(argc == 3, "usage: fixture assets");
    const auto root = std::filesystem::u8path(argv[1]);
    rhi::FileAssetSource assets(std::filesystem::u8path(argv[2]));
    const auto reference = [](int edit, bool playing, unsigned master, int mode) {
        return (edit >= 2 || playing) && (master & 1) && mode > 0;
    };
#endif
    editor::EditorSession session;
    std::string reason;
    Require(session.ImportModel(root / "outline.pmd", reason), reason);
    render::SceneRenderSources sources;
    sources.models = session.Assets().modelPaths;
    render::SceneFrameProducer producer;
    Require(producer.Load(session.Runtime(), sources, assets, reason), reason);
    render::SceneRenderSettings settings;
    settings.width = 63;
    settings.height = 47;
    settings.shadow.mapSize = 32;
    settings.editorModel = 0;
    unsigned cases = 0;
    for (int edit : {0, 1, 2, 3, 4})
        for (bool playing : {false, true})
            for (unsigned master : {0u, 1u, 2u, 7u})
                for (int mode : {0, 1, 2}) {
                    session.Pause();
                    Require(session.Edit(
                                "frame-gate",
                                [&](auto& doc, auto&) {
                                    doc.timeline.editMode = edit;
                                    doc.view.groundGrid = 0;
                                    doc.view.groundShadow = 1;
                                    doc.selfShadowMode = std::uint8_t(mode);
                                    doc.selfShadowEnabled = std::uint8_t(master);
                                    doc.lightState[4] = -1;
                                    return true;
                                },
                                reason),
                            reason);
                    if (playing)
                        Require(session.Play(reason), reason);
                    Require(session.Document().timeline.editMode == edit, "play changed edit mode");
                    settings.shadow.mode = mode;
                    const auto updates = session.Runtime().Counters();
                    rhi::RenderFrame frame;
                    Require(producer.Build(session.Runtime(), settings, frame, reason), reason);
                    const bool effect = reference(edit, playing, master, mode);
                    const bool viewport = reference(edit, playing, 1, 1);
                    Require(frame.targets.size() == (effect ? 5 : 3), "shadow target gate");
                    const auto& main = frame.passes[effect ? 1 : 0];
                    Require(main.draws.size() == (viewport ? 3 : 1),
                            "outline/projected frame gate");
                    Require(bool(rhi::ValidateRenderFrame(frame)), "gated frame validation");
                    Require(session.Runtime().Counters().frames == updates.frames &&
                                session.Runtime().Counters().editorUpdates == updates.editorUpdates,
                            "frame gate advanced simulation");
                    ++cases;
                }
    for (const auto* file : {"outline.pmd", "alpha98.pmd", "tint-shadow.pmx"}) {
        Require(session.New(reason) && session.ImportModel(root / file, reason), reason);
        sources.models = session.Assets().modelPaths;
        Require(producer.Load(session.Runtime(), sources, assets, reason), reason);
        for (int mode : {0, 1})
            for (unsigned flags = 0; flags < 4; ++flags) {
                const bool toon = (flags & 1) != 0, additive = (flags & 2) != 0;
                Require(session.Edit(
                            "render-flags",
                            [&](auto& d, auto&) {
                                d.view.groundGrid = d.view.groundShadow = 0;
                                d.timeline.editMode = 2;
                                d.selfShadowEnabled = 1;
                                d.models[0].toonFlag = toon;
                                d.models[0].postLoadFlag = additive;
                                return true;
                            },
                            reason),
                        reason);
                settings.shadow.mode = mode;
                rhi::RenderFrame frame;
                Require(producer.Build(session.Runtime(), settings, frame, reason), reason);
                const auto& main = frame.passes[mode ? 1 : 0];
                const bool effect = mode && toon;
                const bool backface = additive && !effect && std::string(file) == "alpha98.pmd";
                Require(main.draws.size() == 1u + backface + !additive,
                        "model additive backface/outline contract");
                for (unsigned i = 0; i < 1u + backface; ++i) {
                    const auto& pipeline = main.draws[i].pipeline;
                    Require(pipeline.shadow.mode == (effect ? rhi::BuiltinShadowMode::Material
                                                            : rhi::BuiltinShadowMode::None),
                            "per-model shadow material selection");
                    Require(
                        pipeline.raster.destinationBlend ==
                            (additive ? rhi::BlendFactor::One : rhi::BlendFactor::InvSourceAlpha),
                        "per-model additive blend");
                }
                if (backface)
                    Require(main.draws[0].pipeline.raster.cull == rhi::BatchCull::Clockwise &&
                                main.draws[1].pipeline.raster.cull ==
                                    rhi::BatchCull::CounterClockwise,
                            "additive back/front ordering");
                if (mode && !toon)
                    Require(frame.passes[0].draws.empty(), "disabled model cast a shadow");
                Require(bool(rhi::ValidateRenderFrame(frame)), "model flag frame validation");
            }
    }
    std::printf(
        "%u editor frame gates: select/box/tools, playback, raw master byte, shadow modes; no window\n",
        cases);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
