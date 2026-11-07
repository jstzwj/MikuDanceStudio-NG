#include "mme_render_fixture.hpp"
#include "rhi_test_platform.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_float.hpp"
#include <cmath>
using namespace mme_test;
namespace {
float Component(const AttachmentReadback& image, unsigned x, unsigned y, unsigned c) {
    std::uint16_t word;
    std::memcpy(&word,
                image.bytes.data() + ((std::size_t(y) * image.description.width + x) * 4 + c) * 2,
                2);
    return HalfToFloat(word);
}
void Save(const std::filesystem::path& path, const AttachmentReadback& image) {
    std::ofstream raw(path, std::ios::binary);
    raw.write(reinterpret_cast<const char*>(image.bytes.data()), image.bytes.size());
    Check(bool(raw), "write image evidence");
    if (image.description.format != TextureFormat::Rgba8)
        return;
    std::ofstream ppm(path.string() + ".ppm", std::ios::binary);
    ppm << "P6\n" << image.description.width << ' ' << image.description.height << "\n255\n";
    for (std::size_t n = 0; n < image.bytes.size(); n += 4)
        ppm.write(reinterpret_cast<const char*>(image.bytes.data() + n), 3);
}
ScheduleInput Input(EffectPool& pool, const std::filesystem::path& dxc, unsigned kind) {
    if (kind == 1)
        return NestedInput(pool, dxc);
    ScheduleInput input;
    input.effects.push_back(RenderEffect(
        pool, 10, kind == 0 ? "execution/LayoutMaterial.fx" : "portable/LegacyMrtMaterial.fx",
        dxc));
    input.objects = {{3, "/models/leaf.pmx", 10, false, true, {1}}};
    input.assignments.effects[{0, 3, -1}] = {Assignment::Kind::Effect, 10};
    if (kind == 2) {
        input.effects.push_back(RenderEffect(pool, 12, "execution/MrtScene.fx", dxc));
        input.objects.push_back({1, "/scene.x", 30, true, true, {0}, true});
        input.assignments.effects[{0, 1, -1}] = {Assignment::Kind::Effect, 12};
    }
    return input;
}
}
int main(int argc, char** argv) try {
    Check(argc == 4, "usage: mme_render_gpu_test api dxc evidence-directory");
    const std::string api = argv[1];
    const std::filesystem::path directory = argv[3];
    std::filesystem::create_directories(directory);
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    ConfigureVulkanTest(options, api);
    auto tool = CreateDxcSpirvCompiler({argv[2]});
    Check(tool->Available(), tool->Identity());
    EffectCompiler compiler({tool, api});
    unsigned draws = 0, submissions = 0, mips = 0;
    for (unsigned kind = 0; kind < 3; ++kind) {
        EffectPool pool;
        FrameClock clock;
        TargetRegistry registry;
        auto input = Input(pool, argv[2], kind);
        auto compiled = CompileEffects(input, compiler);
        if (kind == 0)
            input.context = [](auto, int, auto) {
                auto context = Context();
                context.standard.world[12] = .25f;
                context.standard.world[13] = -.125f;
                return context;
            };
        auto native = CreateOffscreenBackendDevice(64, 48, options);
        Check(bool(native), native.reason);
        OffscreenBackendDevice replacement;
        SceneRenderer renderer(*native.device);
        std::string reason;
        for (unsigned iteration = 0; iteration < 3; ++iteration) {
            const unsigned width = iteration == 1 ? 80 : 64, height = iteration == 1 ? 64 : 48;
            if (iteration == 1) {
                replacement = CreateOffscreenBackendDevice(width, height, options);
                Check(bool(replacement), replacement.reason);
                Check(renderer.Rebind(*replacement.device), "replace native device");
            }
            Check(registry.Reconcile(input.effects, width, height, iteration + 1, reason), reason);
            if (kind == 0 && iteration == 2)
                Check(input.effects[0].binding.instance->Write("host", Float(.75f), reason),
                      reason);
            std::unique_ptr<ScheduledFrame> scheduled;
            Check(ScheduledFrame::Prepare(input, registry, pool, clock, iteration + 1, 10, 0, true,
                                          scheduled, reason),
                  reason);
            RenderFrame frame;
            auto geometry = Geometry();
            if (kind == 0) {
                const float wide[][8] = {{-4, 4, 0, 0, 0, 1, 0, 0},
                                         {8, 4, 0, 0, 0, 1, 2, 0},
                                         {-4, -8, 0, 0, 0, 1, 0, 2}};
                geometry.resources[0].bytes.resize(sizeof(wide));
                std::memcpy(geometry.resources[0].bytes.data(), wide, sizeof(wide));
            }
            const auto assembled =
                AssembleRenderFrame(*scheduled, compiled, registry, geometry, frame, reason);
            Check(assembled, "assemble case " + std::to_string(kind) + ":" + reason);
            for (const auto& pass : frame.passes) {
                draws += unsigned(pass.draws.size());
                mips += !!pass.generateMips;
            }
            AttachmentReadback first;
            for (unsigned repeat = 0; repeat < 3; ++repeat) {
                PreparedModelFrame prepared;
                CompletionToken token;
                auto result = renderer.Prepare(1, frame, prepared);
                Check(bool(result), "prepare:" + result.reason);
                result = renderer.Submit(prepared, token);
                Check(bool(result), "submit:" + result.reason);
                ++submissions;
                AttachmentReadback image;
                Check(renderer.ReadTarget(1, frame.finalColor, image), "final readback");
                if (!repeat)
                    first = image;
                else
                    Check(image.bytes == first.bytes, "strict repeated submission comparison");
            }
            if (kind < 2) {
                const int expected[4] = {kind == 0 ? 128 : 64, kind == 0 ? 96 : 128, 191,
                                         kind == 0 ? (iteration == 2 ? 191 : 128) : 255};
                for (std::size_t p = 0; p < first.bytes.size(); ++p)
                    Check(std::abs(int(first.bytes[p]) - expected[p % 4]) <= 1,
                          "layout/nested pixel " + std::to_string(p) + "=" +
                              std::to_string(first.bytes[p]));
            } else {
                TargetLease lease;
                FrameTarget target;
                Check(registry.Resolve(12, "Tone", lease, target, reason), reason);
                AttachmentReadback tone;
                Check(renderer.ReadTarget(1, target.id, tone), "tone readback");
                Check(tone.bytes == first.bytes,
                      "strict FX copy comparison including alpha and edges");
                for (const auto& name : {"CaptureA", "CaptureB", "QuarterHdr"}) {
                    Check(registry.Resolve(12, name, lease, target, reason), reason);
                    AttachmentReadback hdr;
                    Check(renderer.ReadTarget(1, target.id, hdr), "native float readback");
                    Check(hdr.description.format == TextureFormat::Rgba16Float,
                          "float readback format");
                    Save(directory / (std::to_string(kind) + "-" + std::to_string(width) + "-" +
                                      name + ".bin"),
                         hdr);
                    const auto half = [](float value) { return HalfToFloat(FloatToHalf(value)); };
                    for (unsigned y = 0; y < hdr.description.height; ++y)
                        for (unsigned x = 0; x < hdr.description.width; ++x) {
                            std::array<float, 4> expected{};
                            if (std::string(name) == "CaptureA")
                                expected = {half(2 + float(x) / width), -.5f, .25f, .5f};
                            else if (std::string(name) == "CaptureB")
                                expected = {-1, half(3 + float(y) / height), .5f, .25f};
                            else
                                expected = {
                                    half(half(half(2 + float(4 * x + 3) / width) * .5f) - .75f),
                                    half(half(half(3 + float(4 * y + 3) / height) * .25f) - 1),
                                    .25f, .3125f};
                            for (unsigned c = 0; c < 4; ++c)
                                Check(std::abs(Component(hdr, x, y, c) - expected[c]) <= .002f,
                                      std::string(name) + " float reference mismatch at " +
                                          std::to_string(x) + "," + std::to_string(y) + "," +
                                          std::to_string(c) +
                                          " actual=" + std::to_string(Component(hdr, x, y, c)) +
                                          " expected=" + std::to_string(expected[c]));
                        }
                    if (std::string(name) == "QuarterHdr")
                        for (unsigned y = 0; y < height; ++y)
                            for (unsigned x = 0; x < width; ++x)
                                for (unsigned c = 0; c < 4; ++c) {
                                    float v = Component(hdr, x / 4, y / 4, c);
                                    if (c < 3)
                                        v = std::abs(v) / (1 + std::abs(v));
                                    const int expected = int(std::round(v * 255));
                                    Check(
                                        std::abs(
                                            int(first.bytes[(std::size_t(y) * width + x) * 4 + c]) -
                                            expected) <= 1,
                                        "full tone image/axis/alpha reference mismatch");
                                }
                    const float value = Component(hdr, target.description.width / 2,
                                                  target.description.height / 2, 0);
                    if (std::string(name) == "CaptureA")
                        Check(value > 2 && Component(hdr, 1, 1, 1) < 0,
                              "HDR signed and >1 data retained");
                    if (std::string(name) == "CaptureB")
                        Check(value == -1, "second MRT semantic maps to COLOR1");
                    if (std::string(name) == "QuarterHdr")
                        Check(value > .45f && value < .6f, "relative-size HDR compose");
                }
                const auto center = (std::size_t(height / 2) * width + width / 2) * 4;
                Check(first.bytes[center] > 80 && first.bytes[center] < 100 &&
                          first.bytes[center + 3] == 80,
                      "tone RGB/alpha");
                Check(first.bytes[(std::size_t(height / 4) * width + width / 4) * 4] <
                          first.bytes[(std::size_t(height * 3 / 4) * width + width * 3 / 4) * 4],
                      "horizontal direction");
            }
            Save(directory / (std::to_string(kind) + "-" + std::to_string(width) + ".rgba"), first);
            Check(scheduled->Commit(reason), reason);
            Check(renderer.Unload(1), "unload before next reload");
        }
    }
    Check(mips == 6, "nested scopes generate mips at completion");
    std::ofstream report(directory / "report.json");
    report << "{\"backend\":\"" << api << "\",\"draws\":" << draws
           << ",\"submissions\":" << submissions << ",\"mipOperations\":" << mips
           << ",\"floatAbsoluteTolerance\":0.002,\"toneByteTolerance\":1"
           << ",\"strictCopy\":true,\"strictRepeat\":true,\"passed\":true}\n";
    std::cout
        << api
        << ": raw FX layout/material, shared nested/mips, MRT/HDR/relative targets, strict copy/repeat, resize/device/unload passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
