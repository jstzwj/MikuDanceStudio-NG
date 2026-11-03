#include "mikudancestudio/rhi_render_pass.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main() {
    auto d = CreateNullDevice(), other = CreateNullDevice();
    RenderTextureDesc color{16, 16, TextureFormat::Rgba8,
                            TextureAccess::ColorAttachment | TextureAccess::Sampled |
                                TextureAccess::CopySource};
    auto depth = color;
    depth.format = TextureFormat::Depth24Stencil8;
    depth.access = TextureAccess::DepthStencilAttachment | TextureAccess::CopySource;
    const auto a = d->CreateRenderTexture(color), b = d->CreateRenderTexture(color),
               z = d->CreateRenderTexture(depth);
    CHECK(a && b && z);
    auto portableDepth = depth;
    portableDepth.format = TextureFormat::DepthStencil;
    CHECK(!d->CreateRenderTexture(portableDepth)); // No defined transfer representation.
    portableDepth.access = unsigned(TextureAccess::DepthStencilAttachment);
    const auto portableZ = d->CreateRenderTexture(portableDepth);
    CHECK(portableZ && d->FormatFeatures(portableDepth.format).render &&
          !d->FormatFeatures(portableDepth.format).readback);
    portableDepth.access |= unsigned(TextureAccess::Sampled);
    CHECK(!d->CreateRenderTexture(portableDepth) && d->Destroy(portableZ));
    auto invalid = color;
    invalid.samples = 4;
    CHECK(!d->CreateRenderTexture(invalid));
    invalid = depth;
    invalid.access |= std::uint32_t(TextureAccess::Sampled);
    CHECK(!d->CreateRenderTexture(invalid));
    RenderPassDesc write;
    write.color = PassAttachment{a};
    write.depthStencil = PassAttachment{z};
    AttachmentReadback out;
    CHECK(!d->ReadRenderTexture(a, out) && !d->BeginRenderPass(write));
    CHECK(d->BeginFrame({}));
    auto read = write;
    read.color->texture = b;
    read.sampled = {a};
    CHECK(!d->BeginRenderPass(read)); // read before first write
    auto load = write;
    load.color->load = AttachmentLoad::Load;
    CHECK(!d->BeginRenderPass(load));
    auto feedback = write;
    feedback.sampled = {a};
    CHECK(!d->BeginRenderPass(feedback));
    auto foreign = write;
    foreign.color->texture = other->CreateRenderTexture(color);
    CHECK(!d->BeginRenderPass(foreign));
    auto badClear = write;
    badClear.depthStencil->clearDepth = std::numeric_limits<float>::quiet_NaN();
    CHECK(!d->BeginRenderPass(badClear));
    auto small = color;
    small.width = 8;
    CHECK(!d->CreateRenderTexture(small)); // allocation during a frame
    CHECK(d->BeginRenderPass(write) && !d->BeginRenderPass(write) && !d->EndFrame());
    CHECK(!d->Destroy(a) && !d->PrepareForReset() && !d->ReadRenderTexture(a, out));
    CHECK(d->EndRenderPass() && !d->EndRenderPass() && d->BeginRenderPass(read) &&
          d->EndRenderPass());
    CHECK(d->BeginRenderPass(read));
    CHECK(d->SetFixedPipeline(HudSpritePipeline()) && d->BindTexture(0, a));
    CHECK(!d->BindTexture(0, b)); // undeclared write target
    CHECK(d->EndRenderPass());
    auto discard = write;
    discard.color->store = AttachmentStore::Discard;
    CHECK(d->BeginRenderPass(discard) && d->EndRenderPass() && !d->BeginRenderPass(read));
    CHECK(d->EndFrame() && d->Present());
    CHECK(!d->ReadRenderTexture(a, out) && d->ReadRenderTexture(b, out) &&
          out.bytes.size() == 16 * 16 * 4);
    CHECK(d->Destroy(a) && !d->ReadRenderTexture(a, out));
    const auto replacement = d->CreateRenderTexture(color);
    CHECK(replacement && replacement.generation != a.generation);
    const auto narrow = d->CreateRenderTexture(small);
    CHECK(narrow);
    CHECK(d->BeginFrame({}));
    write.color->texture = replacement;
    auto mismatch = write;
    mismatch.color->texture = narrow;
    CHECK(!d->BeginRenderPass(mismatch));
    read.sampled = {a};
    CHECK(!d->BeginRenderPass(read));
    CHECK(d->BeginRenderPass(write) && d->SetFixedPipeline(HudSpritePipeline()));
    CHECK(!d->BindTexture(0, a) && !d->BindTexture(0, replacement));
    CHECK(d->EndRenderPass());
    write.color->clear = {.25f, .5f, .75f, 1};
    CHECK(d->BeginRenderPass(write) && d->EndRenderPass() && d->EndFrame() && d->Present());
    CHECK(d->ReadRenderTexture(replacement, out));
    CHECK(out.bytes[0] == 64 && out.bytes[1] == 128 && out.bytes[2] == 191 && out.bytes[3] == 255);
    const auto before = d->Status();
    CHECK(d->PrepareForReset() && !d->ReadRenderTexture(replacement, out) &&
          d->RestoreAfterReset());
    CHECK(d->Status().epoch == before.epoch + 1 && !d->ReadRenderTexture(replacement, out));
    CHECK(d->BeginFrame({}));
    write.color->load = AttachmentLoad::Load;
    CHECK(!d->BeginRenderPass(write));
    write.color->load = AttachmentLoad::Clear;
    CHECK(d->BeginRenderPass(write) && d->EndRenderPass() && d->EndFrame() && d->Present());
    CHECK(d->ReadRenderTexture(replacement, out));
    CHECK(d->Destroy(replacement) && d->Destroy(b) && d->Destroy(z) && d->Destroy(narrow));
    // Resolve is an ordered write, never an implicit sample/readback of MSAA.
    auto msaa = color;
    msaa.access = unsigned(TextureAccess::ColorAttachment);
    msaa.samples = 4;
    auto multiDepth = portableDepth;
    multiDepth.access = unsigned(TextureAccess::DepthStencilAttachment);
    multiDepth.samples = 4;
    const auto source = d->CreateRenderTexture(msaa), resolved = d->CreateRenderTexture(color),
               multiZ = d->CreateRenderTexture(multiDepth), singleZ = d->CreateRenderTexture(depth);
    CHECK(source && resolved && multiZ && singleZ);
    CHECK(!d->ReadRenderTexture(source, out) && !d->ReadRenderTexture(resolved, out));
    RenderPassDesc resolve;
    resolve.color = PassAttachment{source};
    resolve.color->resolve = resolved;
    resolve.color->clear = {.25f, .5f, .75f, .5f};
    resolve.depthStencil = PassAttachment{multiZ};
    CHECK(d->BeginFrame({}));
    auto bad = resolve;
    bad.depthStencil->texture = singleZ;
    CHECK(!d->BeginRenderPass(bad));
    bad = resolve;
    bad.color->resolve = source;
    CHECK(!d->BeginRenderPass(bad));
    bad = resolve;
    bad.color->texture = resolved;
    CHECK(!d->BeginRenderPass(bad));
    bad = resolve;
    bad.sampled = {resolved};
    CHECK(!d->BeginRenderPass(bad)); // Resolve destination is written in this pass.
    bad = resolve;
    bad.depthStencil->resolve = resolved;
    CHECK(!d->BeginRenderPass(bad));
    resolve.color->store = AttachmentStore::Discard;
    CHECK(d->BeginRenderPass(resolve) && d->EndRenderPass());
    CHECK(d->BeginRenderPass(RenderPassDesc{PassAttachment{resolved, AttachmentLoad::Load}}) &&
          d->EndRenderPass() && d->EndFrame() && d->Present());
    CHECK(!d->ReadRenderTexture(source, out) && d->ReadRenderTexture(resolved, out));
    CHECK(out.bytes[0] == 64 && out.bytes[1] == 128 && out.bytes[2] == 191 && out.bytes[3] == 128);
    CHECK(d->Destroy(source));
    const auto recycled = d->CreateRenderTexture(msaa);
    CHECK(recycled && recycled.generation != source.generation);
    CHECK(d->BeginFrame({}) && !d->BeginRenderPass(resolve) && d->EndFrame() && d->Present());
    CHECK(d->Destroy(recycled) && d->Destroy(resolved) && d->Destroy(multiZ) &&
          d->Destroy(singleZ));
    std::puts(
        "explicit passes: dependencies, feedback, discard, generations, clear/readback and reset passed");
}
