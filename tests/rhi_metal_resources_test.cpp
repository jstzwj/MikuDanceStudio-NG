#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_render_pass.hpp"
#include "material_sampling_fixture.hpp"
#include <iostream>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
int main() try {
    BackendOptions options;
    options.backend = Backend::Metal;
    auto native = CreateOffscreenBackendDevice(64, 64, options);
    if (!native)
        throw std::runtime_error(native.reason);
    auto& d = *native.device;
    CHECK(d.BackendType() == Backend::Metal && d.ModelFeatures().offscreen &&
          !d.ModelFeatures().presentation);
    CHECK(!d.Present() && !d.Features().compute && d.ShaderFeatures().textureSlots == 16);
    CHECK(!d.FormatFeatures(TextureFormat::DepthStencil).sampled);
    options.driver = BackendDriver::Software;
    CHECK(CreateOffscreenBackendDevice(16, 16, options).reason ==
          "metal_software_driver_not_supported");
    options.driver = BackendDriver::Hardware;
    auto other = CreateOffscreenBackendDevice(16, 16, options);
    CHECK(other);
    auto frame = sampling_fixture::Fixture(0, SamplerFilter::Linear, 1, SamplerAddress::Clamp, 1,
                                           true, false);
    const auto& vertices = frame.resources[0].bytes;
    const auto vb = d.CreateBuffer({BufferUsage::Vertex, vertices.size()}, vertices.data());
    const auto dynamic =
        d.CreateBuffer({BufferUsage::Vertex, vertices.size(), true}, vertices.data());
    const auto texture = d.CreateModelTexture(frame.resources[1].texture);
    CHECK(vb && dynamic && texture && !other.device->Destroy(vb));
    auto pipeline = frame.passes.back().draws[0].pipeline;
    NonIndexedDraw draw{vb, 0, 6};
    draw.layout = pipeline.vertexLayout;
    auto render = [&] {
        CHECK(d.BeginFrame({1, 1, 1, 1}));
        if (!d.SetModelPipeline(pipeline))
            throw std::runtime_error(d.ShaderDiagnostic());
        CHECK(d.BindTexture(0, texture) && d.Draw(d.Record(draw)));
        // Even a fast GPU cannot have consumed an unsubmitted command buffer.
        CHECK(!d.Upload(vb, 0, vertices.data(), vertices.size()));
        CHECK(d.EndFrame());
    };
    render();
    const auto first = d.SignalCompletion();
    CHECK(first && d.WaitCompletion(first, 5000));
    CHECK(other.device->PollCompletion(first) == CompletionState::Invalid);
    RgbaImage expected, actual;
    CHECK(d.ReadModelTarget(expected));
    const auto stale = d.Record(draw);
    CHECK(d.PrepareForReset() && d.Status().state == DeviceState::ResetPending);
    CHECK(!d.BeginFrame({}) && !d.Upload(dynamic, 0, vertices.data(), vertices.size()) &&
          d.PollCompletion(first) == CompletionState::Complete);
    CHECK(d.RestoreAfterReset() && d.Status().state == DeviceState::Ready);
    CHECK(d.Upload(dynamic, 0, vertices.data(), vertices.size()) && d.Destroy(dynamic));
    CHECK(d.BeginFrame({}) && d.SetModelPipeline(pipeline) && d.BindTexture(0, texture));
    CHECK(!d.Draw(stale) && d.Draw(d.Record(draw)) && d.EndFrame());
    render();
    CHECK(d.ReadModelTarget(actual) && actual.pixels == expected.pixels);
    render();
    // Invalidate public identities immediately while native commands retain storage.
    CHECK(d.Destroy(vb) && !d.Destroy(vb) && d.Destroy(texture));
    CHECK(d.WaitCompletion(d.SignalCompletion(), 5000));
    CHECK(d.ReadModelTarget(actual) && actual.pixels == expected.pixels);
    CHECK(d.PollCompletion({first.device, 0}) == CompletionState::Invalid);
    std::cout
        << "Metal resources: ownership, in-flight writes, logical reset and deferred release passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
