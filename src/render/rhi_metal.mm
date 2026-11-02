#include "mikudancestudio/rhi_metal.hpp"
#include "mikudancestudio/rhi_shader.hpp"
#include "rhi_builtin_spirv.hpp"
#include "rhi_model_constants.hpp"
#include "rhi_shader_spirv.hpp"
#include <spirv_msl.hpp>
#import <Metal/Metal.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <sstream>
#include <thread>

namespace mikudancestudio::rhi {
namespace {
constexpr std::size_t UniformBytes = 2 * 1024 * 1024;
constexpr unsigned VertexBufferSlot = 16, MaxDraws = 4096;
MTLPixelFormat PixelFormat(TextureFormat f) {
    switch (f) {
    case TextureFormat::Rgba8:
        return MTLPixelFormatRGBA8Unorm;
    case TextureFormat::Rgba16Float:
        return MTLPixelFormatRGBA16Float;
    case TextureFormat::R32Float:
        return MTLPixelFormatR32Float;
    case TextureFormat::Depth24Stencil8:
        return MTLPixelFormatDepth24Unorm_Stencil8;
    case TextureFormat::DepthStencil:
        return MTLPixelFormatDepth32Float_Stencil8;
    }
    return MTLPixelFormatInvalid;
}
MTLBlendFactor Blend(BlendFactor v) {
    static const MTLBlendFactor values[] = {MTLBlendFactorZero,
                                            MTLBlendFactorOne,
                                            MTLBlendFactorSourceColor,
                                            MTLBlendFactorOneMinusSourceColor,
                                            MTLBlendFactorSourceAlpha,
                                            MTLBlendFactorOneMinusSourceAlpha,
                                            MTLBlendFactorDestinationAlpha,
                                            MTLBlendFactorOneMinusDestinationAlpha,
                                            MTLBlendFactorDestinationColor,
                                            MTLBlendFactorOneMinusDestinationColor,
                                            MTLBlendFactorSourceAlphaSaturated};
    return values[unsigned(v)];
}
MTLStencilOperation Stencil(ModelStencilOp v) {
    static const MTLStencilOperation values[] = {
        MTLStencilOperationKeep,           MTLStencilOperationKeep,
        MTLStencilOperationZero,           MTLStencilOperationReplace,
        MTLStencilOperationIncrementClamp, MTLStencilOperationDecrementClamp,
        MTLStencilOperationInvert,         MTLStencilOperationIncrementWrap,
        MTLStencilOperationDecrementWrap};
    return values[unsigned(v)];
}
MTLLoadAction Load(AttachmentLoad v) {
    return v == AttachmentLoad::Clear  ? MTLLoadActionClear
           : v == AttachmentLoad::Load ? MTLLoadActionLoad
                                       : MTLLoadActionDontCare;
}
MTLStoreAction Store(AttachmentStore v) {
    return v == AttachmentStore::Store ? MTLStoreActionStore : MTLStoreActionDontCare;
}
struct Resource {
    unsigned generation = 1;
    bool alive = false, uploaded = false, srgb = false;
    BufferDesc bufferDesc;
    TextureDesc textureDesc;
    id<MTLBuffer> buffer = nil;
    id<MTLTexture> texture = nil;
    std::uint64_t lastUse = 0;
};
struct NativeProgram {
    id<MTLFunction> vertex = nil, pixel = nil;
};
struct NativePipeline {
    id<MTLRenderPipelineState> render = nil;
    id<MTLDepthStencilState> depth = nil;
};
class MetalDevice final : public Device {
public:
    id<MTLDevice> native = nil;
    id<MTLCommandQueue> queue = nil;
    std::string error;
    unsigned width = 0, height = 0;
    DeviceStatus status{detail::NextDeviceIdentity(), 1, DeviceState::Ready};
    struct Submission {
        std::uint64_t serial;
        id<MTLCommandBuffer> command;
    };
    std::deque<Submission> submissions;
    std::uint64_t issued = 0, completed = 0;
    struct Frame {
        id<MTLBuffer> uniforms = nil;
        std::size_t offset = 0;
        std::uint64_t serial = 0;
    };
    std::array<Frame, 3> frames;
    unsigned frameIndex = 0, draws = 0;
    id<MTLCommandBuffer> commands = nil;
    id<MTLRenderCommandEncoder> encoder = nil;
    id<MTLTexture> color = nil, depth = nil, white = nil;
    std::vector<Resource> resources;
    RenderPassState passes;
    std::map<std::string, NativeProgram> programs;
    std::map<std::string, NativePipeline> pipelines;
    std::map<std::string, id<MTLSamplerState>> samplers;
    ModelPipeline model;
    ShaderPipeline generic;
    ShaderResourceBindings bindings;
    ModelViewport viewport;
    std::array<Handle, 3> textures{};
    Handle shadow;
    bool inFrame = false, modelReady = false, genericReady = false, genericBound = false;

    ~MetalDevice() override {
        if (encoder)
            [encoder endEncoding];
        // Uncommitted commands are discarded. Committed buffers retain their
        // referenced resources; wait before releasing the device/host lease.
        for (const auto& s : submissions)
            [s.command waitUntilCompleted];
    }
    bool Live() const { return status.state == DeviceState::Ready; }
    Backend BackendType() const override { return Backend::Metal; }
    Capabilities Features() const override { return {true, false, true, true}; }
    RenderTargetLimits TargetLimits() const override { return {16384, 16384}; }
    ShaderCapabilities ShaderFeatures() const override {
        auto c = GraphicsShaderProfile(detail::RuntimeSpirvCompilerAvailable(), true);
        c.constantAlignment = 256;
        c.legacyDrawConstantBytes = 2560;
        c.maxFrameConstantBytes = UniformBytes;
        c.maxColorAttachments = 4;
        return c;
    }
    TextureFormatCapabilities FormatFeatures(TextureFormat f) const override {
        if (f == TextureFormat::DepthStencil)
            return {true, false, false, false};
        if (f == TextureFormat::Depth24Stencil8)
            return {bool(native.depth24Stencil8PixelFormatSupported), false, false, false};
        if (f == TextureFormat::Rgba8)
            return {true, true, true, true, true};
        if (f == TextureFormat::Rgba16Float || f == TextureFormat::R32Float)
            return {true, true, true, true};
        return {};
    }
    ModelCapabilities ModelFeatures() const override {
        return {true, true, true, true, true, true, true, true, true, true, false, MaxDraws};
    }
    ResourceLifecycleCapabilities ResourceLifecycleFeatures() const override {
        return {true, true, true, true, true};
    }
    DeviceStatus Status() const override { return status; }
    const char* ShaderDiagnostic() const override { return error.c_str(); }
    Resource* Find(Handle h) {
        if (h.device != status.identity || h.index >= resources.size())
            return nullptr;
        auto& r = resources[h.index];
        return r.alive && r.generation == h.generation ? &r : nullptr;
    }
    Handle Add(Resource r) {
        r.alive = true;
        for (unsigned i = 0; i < resources.size(); ++i)
            if (!resources[i].alive) {
                r.generation = resources[i].generation;
                resources[i] = std::move(r);
                return {i, resources[i].generation, status.identity};
            }
        resources.push_back(std::move(r));
        return {unsigned(resources.size() - 1), 1, status.identity};
    }
    void Collect() {
        while (!submissions.empty()) {
            const auto& s = submissions.front();
            if (s.command.status < MTLCommandBufferStatusCompleted)
                break;
            if (s.command.status == MTLCommandBufferStatusError) {
                error = s.command.error.localizedDescription.UTF8String;
                status.state = DeviceState::Removed;
            }
            completed = s.serial;
            submissions.pop_front();
        }
    }
    CompletionToken SignalCompletion() override {
        return !inFrame && issued ? CompletionToken{status.identity, issued} : CompletionToken{};
    }
    CompletionState PollCompletion(CompletionToken t) override {
        if (t.device != status.identity || !t.value || t.value > issued)
            return CompletionState::Invalid;
        Collect();
        if (status.state == DeviceState::Removed)
            return CompletionState::DeviceLost;
        return t.value <= completed ? CompletionState::Complete : CompletionState::Pending;
    }
    bool WaitCompletion(CompletionToken t, std::uint32_t timeout) override {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        for (;;) {
            const auto state = PollCompletion(t);
            if (state != CompletionState::Pending)
                return state == CompletionState::Complete;
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    bool Complete(id<MTLCommandBuffer> cmd) {
        if (!cmd)
            return false;
        [cmd commit];
        [cmd waitUntilCompleted];
        if (cmd.status == MTLCommandBufferStatusError) {
            error = cmd.error.localizedDescription.UTF8String;
            status.state = DeviceState::Removed;
            return false;
        }
        Collect();
        return Live();
    }
    id<MTLTexture> MakeTexture(unsigned w, unsigned h, MTLPixelFormat f, unsigned mips,
                               MTLTextureUsage usage) {
        @autoreleasepool {
            auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f
                                                                        width:w
                                                                       height:h
                                                                    mipmapped:NO];
            d.mipmapLevelCount = mips;
            d.storageMode = MTLStorageModePrivate;
            d.usage = usage;
            return [native newTextureWithDescriptor:d];
        }
    }
    bool UploadImage(id<MTLTexture> target, const std::vector<RgbaImage>& levels) {
        @autoreleasepool {
            if (inFrame)
                return false;
            auto cmd = [queue commandBuffer];
            auto blit = [cmd blitCommandEncoder];
            if (!blit)
                return false;
            for (unsigned n = 0; n < levels.size(); ++n) {
                const auto& image = levels[n];
                const std::size_t pitch = (std::size_t(image.width) * 4 + 255) & ~std::size_t(255);
                auto staging = [native newBufferWithLength:pitch * image.height
                                                   options:MTLResourceStorageModeShared];
                if (!staging) {
                    [blit endEncoding];
                    return false;
                }
                for (unsigned y = 0; y < image.height; ++y)
                    std::memcpy(static_cast<char*>(staging.contents) + y * pitch,
                                image.pixels.data() + std::size_t(y) * image.width * 4,
                                image.width * 4);
                [blit copyFromBuffer:staging
                           sourceOffset:0
                      sourceBytesPerRow:pitch
                    sourceBytesPerImage:pitch * image.height
                             sourceSize:MTLSizeMake(image.width, image.height, 1)
                              toTexture:target
                       destinationSlice:0
                       destinationLevel:n
                      destinationOrigin:MTLOriginMake(0, 0, 0)];
            }
            [blit endEncoding];
            return Complete(cmd);
        }
    }
    Handle CreateBuffer(BufferDesc d, const void* bytes) override {
        if (!Live() || !d.size || d.size > native.maxBufferLength || unsigned(d.usage) > 2 ||
            unsigned(d.indexFormat) > 1)
            return {};
        Resource r;
        r.bufferDesc = d;
        r.buffer = [native newBufferWithLength:d.size options:MTLResourceStorageModeShared];
        if (!r.buffer)
            return {};
        if (bytes)
            std::memcpy(r.buffer.contents, bytes, d.size);
        else
            std::memset(r.buffer.contents, 0, d.size);
        r.uploaded = true;
        return Add(std::move(r));
    }
    bool Upload(Handle h, std::size_t offset, const void* data, std::size_t size) override {
        Collect();
        auto* r = Find(h);
        if (!Live() || !r || !r->buffer || !data || !size || offset > r->bufferDesc.size ||
            size > r->bufferDesc.size - offset || r->lastUse > completed)
            return false;
        std::memcpy(static_cast<char*>(r->buffer.contents) + offset, data, size);
        r->uploaded = true;
        return true;
    }
    Handle CreateTexture(TextureDesc d) override {
        if (!Live() || !d.width || !d.height || d.width > 4096 || d.height > 4096 ||
            d.format != TextureFormat::Rgba8 || unsigned(d.mips) > 1)
            return {};
        unsigned levels = 1;
        if (d.mips == TextureDesc::Mips::GenerateLinear)
            for (auto size = std::max(d.width, d.height); size > 1; size >>= 1)
                ++levels;
        Resource r;
        r.textureDesc = d;
        r.texture = MakeTexture(d.width, d.height, MTLPixelFormatRGBA8Unorm, levels,
                                MTLTextureUsageShaderRead);
        return r.texture ? Add(std::move(r)) : Handle{};
    }
    bool UploadTexture(Handle h, TexturePixels pixels) override {
        auto* r = Find(h);
        if (!Live() || !r || !r->texture || passes.Description(h) || !pixels.data ||
            pixels.rowPitch < std::size_t(r->textureDesc.width) * 4 ||
            pixels.bytes < pixels.rowPitch * (r->textureDesc.height - 1) + r->textureDesc.width * 4)
            return false;
        RgbaImage image{r->textureDesc.width, r->textureDesc.height, {}};
        image.pixels.resize(std::size_t(image.width) * image.height * 4);
        for (unsigned y = 0; y < image.height; ++y)
            std::memcpy(image.pixels.data() + std::size_t(y) * image.width * 4,
                        static_cast<const char*>(pixels.data) + y * pixels.rowPitch,
                        image.width * 4);
        PreparedMipChain prepared;
        if (!PreparePortableMipChain(image,
                                     r->textureDesc.mips == TextureDesc::Mips::GenerateLinear
                                         ? MipPreparationPolicy::BoxLinear
                                         : MipPreparationPolicy::SingleLevel,
                                     prepared))
            return false;
        return r->uploaded = UploadImage(r->texture, prepared.levels);
    }
    Handle CreateModelTexture(const ModelTextureData& data) override {
        if (!Live() || inFrame || data.levels.empty() ||
            (data.generateMips && data.levels.size() != 1))
            return {};
        auto levels = data.levels;
        unsigned w = levels[0].width, h = levels[0].height;
        for (const auto& image : levels) {
            if (!w || !h || w > 4096 || h > 4096 || image.width != w || image.height != h ||
                image.pixels.size() != std::size_t(w) * h * 4)
                return {};
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        if (data.generateMips) {
            PreparedMipChain chain;
            if (!PreparePortableMipChain(levels[0], MipPreparationPolicy::BoxLinear, chain))
                return {};
            levels = std::move(chain.levels);
        }
        Resource r;
        r.textureDesc = {levels[0].width, levels[0].height, TextureFormat::Rgba8};
        r.srgb = data.srgb;
        r.texture =
            MakeTexture(r.textureDesc.width, r.textureDesc.height,
                        data.srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm,
                        unsigned(levels.size()), MTLTextureUsageShaderRead);
        if (!r.texture || !UploadImage(r.texture, levels))
            return {};
        r.uploaded = true;
        return Add(std::move(r));
    }
    Handle CreateRenderTexture(const RenderTextureDesc& d) override {
        if (!Live() || !ValidRenderTextureDesc(d) || d.width > TargetLimits().width ||
            d.height > TargetLimits().height || !QueryShaderTextureSupport(*this, d))
            return {};
        Resource r;
        r.textureDesc = {d.width, d.height, d.format};
        MTLTextureUsage usage = MTLTextureUsageRenderTarget;
        if (d.access & unsigned(TextureAccess::Sampled))
            usage |= MTLTextureUsageShaderRead;
        r.texture = MakeTexture(d.width, d.height, PixelFormat(d.format), d.mips, usage);
        if (!r.texture)
            return {};
        const auto handle = Add(std::move(r));
        if (!passes.Register(handle, d)) {
            Destroy(handle);
            return {};
        }
        return handle;
    }
    bool Destroy(Handle h) override {
        auto* r = Find(h);
        if (!r)
            return false;
        if (passes.Description(h) && !passes.Erase(h))
            return false;
        // MTLCommandBuffer retains native resources until GPU completion.
        const auto generation = r->generation + 1;
        *r = Resource{};
        r->generation = generation ? generation : 1;
        return true;
    }
    bool EnsureModelTarget(const HistoryDesc& d) override {
        if (!Live() || inFrame || !ValidHistoryDesc(d) || d.samples != 1 || !d.color || !d.depth ||
            !d.stencil || d.width > 4096 || d.height > 4096)
            return false;
        if (width == d.width && height == d.height && color && depth)
            return true;
        auto c = MakeTexture(d.width, d.height, MTLPixelFormatRGBA8Unorm, 1,
                             MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        auto z = MakeTexture(d.width, d.height, MTLPixelFormatDepth32Float_Stencil8, 1,
                             MTLTextureUsageRenderTarget);
        if (!c || !z)
            return false;
        color = c;
        depth = z;
        ++status.epoch;
        passes.InvalidateContents();
        width = d.width;
        height = d.height;
        return true;
    }
    unsigned RenderWidth() {
        return passes.Active()
                   ? passes.Description(PrimaryAttachment(passes.Current())->texture)->width
                   : width;
    }
    unsigned RenderHeight() {
        return passes.Active()
                   ? passes.Description(PrimaryAttachment(passes.Current())->texture)->height
                   : height;
    }
    id<MTLTexture> ActiveDepth() {
        if (!passes.Active())
            return depth;
        return passes.Current().depthStencil ? Find(passes.Current().depthStencil->texture)->texture
                                             : nil;
    }
    std::vector<TextureFormat> ColorFormats() {
        return passes.Active() ? passes.ColorFormats()
                               : std::vector<TextureFormat>{TextureFormat::Rgba8};
    }
    void EndEncoder() {
        if (encoder) {
            [encoder endEncoding];
            encoder = nil;
        }
    }
    bool BeginNative(const RenderPassDesc* pass, ClearColor clear = {}) {
        @autoreleasepool {
            EndEncoder();
            auto desc = [MTLRenderPassDescriptor renderPassDescriptor];
            auto colors =
                pass ? ColorAttachments(*pass) : std::vector<PassAttachment>{PassAttachment{}};
            for (unsigned n = 0; n < colors.size(); ++n) {
                const auto& attachment = colors[n];
                auto nativeAttachment = desc.colorAttachments[n];
                auto* resource = pass ? Find(attachment.texture) : nullptr;
                nativeAttachment.texture = resource ? resource->texture : color;
                if (resource)
                    resource->lastUse = issued + 1;
                nativeAttachment.loadAction = Load(attachment.load);
                nativeAttachment.storeAction = Store(attachment.store);
                auto c = pass ? attachment.clear : clear;
                nativeAttachment.clearColor = MTLClearColorMake(c.red, c.green, c.blue, c.alpha);
            }
            if (!pass || pass->depthStencil) {
                const auto attachment = pass ? *pass->depthStencil : PassAttachment{};
                auto* resource = pass ? Find(attachment.texture) : nullptr;
                auto z = resource ? resource->texture : depth;
                if (resource)
                    resource->lastUse = issued + 1;
                desc.depthAttachment.texture = desc.stencilAttachment.texture = z;
                desc.depthAttachment.loadAction = desc.stencilAttachment.loadAction =
                    Load(attachment.load);
                desc.depthAttachment.storeAction = desc.stencilAttachment.storeAction =
                    Store(attachment.store);
                desc.depthAttachment.clearDepth = attachment.clearDepth;
                desc.stencilAttachment.clearStencil = attachment.clearStencil;
            }
            encoder = [commands renderCommandEncoderWithDescriptor:desc];
            return encoder != nil;
        }
    }
    bool BeginFrame(ClearColor clear) override {
        @autoreleasepool {
            Collect();
            if (!Live() || inFrame || !color)
                return false;
            frameIndex = unsigned(issued % frames.size());
            auto& f = frames[frameIndex];
            if (f.serial && !WaitCompletion({status.identity, f.serial}, 5000))
                return false;
            f.offset = 0;
            draws = 0;
            commands = [queue commandBuffer];
            if (!commands)
                return false;
            inFrame = true;
            modelReady = genericReady = genericBound = false;
            return BeginNative(nullptr, clear);
        }
    }
    bool BeginRenderPass(const RenderPassDesc& p) override {
        if (!Live() || !inFrame || !passes.Begin(p))
            return false;
        modelReady = genericReady = genericBound = false;
        return BeginNative(&p);
    }
    bool EndRenderPass() override {
        if (!inFrame || !passes.Active())
            return false;
        EndEncoder();
        modelReady = genericReady = genericBound = false;
        for (const auto& attachment : ColorAttachments(passes.Current()))
            Find(attachment.texture)->uploaded = attachment.store == AttachmentStore::Store;
        return passes.End();
    }
    bool GenerateRenderMips(Handle h) override {
        auto* r = Find(h);
        if (!Live() || !inFrame || !r || !passes.CanGenerateMips(h) ||
            !FormatFeatures(r->textureDesc.format).renderMips)
            return false;
        EndEncoder();
        auto blit = [commands blitCommandEncoder];
        if (!blit)
            return false;
        [blit generateMipmapsForTexture:r->texture];
        [blit endEncoding];
        r->lastUse = issued + 1;
        return passes.GenerateMips(h);
    }
    bool CopyToFrameOutput(Handle h) override {
        auto* r = Find(h);
        if (!Live() || !inFrame || passes.Active() || !r || !passes.CanRead(h) ||
            r->textureDesc.format != TextureFormat::Rgba8 || r->textureDesc.width != width ||
            r->textureDesc.height != height)
            return false;
        EndEncoder();
        auto blit = [commands blitCommandEncoder];
        if (!blit)
            return false;
        [blit copyFromTexture:r->texture
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(width, height, 1)
                    toTexture:color
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        r->lastUse = issued + 1;
        return true;
    }
    bool EndFrame() override {
        @autoreleasepool {
            if (!Live() || !inFrame || passes.Active())
                return false;
            EndEncoder();
            [commands commit];
            submissions.push_back({++issued, commands});
            frames[frameIndex].serial = issued;
            commands = nil;
            inFrame = false;
            modelReady = genericReady = genericBound = false;
            return true;
        }
    }
    bool Present() override { return false; }
    bool Read(id<MTLTexture> texture, unsigned bytesPerPixel, std::vector<std::uint8_t>& bytes) {
        @autoreleasepool {
            if (!Live() || inFrame || !texture)
                return false;
            const auto pitch = (texture.width * bytesPerPixel + 255) & ~NSUInteger(255);
            auto staging = [native newBufferWithLength:pitch * texture.height
                                               options:MTLResourceStorageModeShared];
            if (!staging)
                return false;
            auto cmd = [queue commandBuffer];
            auto blit = [cmd blitCommandEncoder];
            if (!blit)
                return false;
            [blit copyFromTexture:texture
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(texture.width, texture.height, 1)
                                toBuffer:staging
                       destinationOffset:0
                  destinationBytesPerRow:pitch
                destinationBytesPerImage:pitch * texture.height];
            [blit endEncoding];
            if (!Complete(cmd))
                return false;
            const auto rowBytes = texture.width * bytesPerPixel;
            bytes.resize(rowBytes * texture.height);
            for (unsigned y = 0; y < texture.height; ++y)
                std::memcpy(bytes.data() + y * rowBytes,
                            static_cast<const char*>(staging.contents) + y * pitch, rowBytes);
            return true;
        }
    }
    bool ReadModelTarget(RgbaImage& image) override {
        std::vector<std::uint8_t> bytes;
        if (!Read(color, 4, bytes))
            return false;
        image = {width, height, {}};
        image.pixels.resize(bytes.size());
        std::memcpy(image.pixels.data(), bytes.data(), bytes.size());
        return true;
    }
    bool ReadRenderTexture(Handle h, AttachmentReadback& output) override {
        auto* r = Find(h);
        if (!r || !passes.CanRead(h) || !FormatFeatures(r->textureDesc.format).readback)
            return false;
        output.description = *passes.Description(h);
        return Read(r->texture, TexturePixelBytes(r->textureDesc.format), output.bytes);
    }
    bool PrepareForReset() override {
        if (!Live() || inFrame)
            return false;
        if (issued && !WaitCompletion({status.identity, issued}, 5000))
            return false;
        for (unsigned i = 0; i < resources.size(); ++i)
            if (resources[i].alive && resources[i].buffer && resources[i].bufferDesc.dynamic)
                Destroy({i, resources[i].generation, status.identity});
        passes.InvalidateContents();
        status.state = DeviceState::ResetPending;
        ++status.epoch;
        return true;
    }
    bool RestoreAfterReset() override {
        if (status.state != DeviceState::ResetPending)
            return false;
        status.state = DeviceState::Ready;
        return true;
    }
    bool SetFixedPipeline(FixedPipelineState) override { return false; }
    id<MTLFunction> Function(const std::uint32_t* words, std::size_t count,
                             const std::string& entry, bool vertex, bool genericAbi) {
        @autoreleasepool {
            try {
                spirv_cross::CompilerMSL compiler(words, count);
                const auto stage = vertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment;
                compiler.set_entry_point(entry, stage);
                auto options = compiler.get_msl_options();
                options.platform = spirv_cross::CompilerMSL::Options::macOS;
                options.set_msl_version(2, 3);
                compiler.set_msl_options(options);
                for (unsigned i = 0; i < 4; ++i) {
                    spirv_cross::MSLResourceBinding b;
                    b.stage = stage;
                    b.binding = i;
                    b.msl_buffer = i;
                    compiler.add_msl_resource_binding(b);
                }
                for (unsigned i = 0; i < (genericAbi ? GraphicsTextureSlots : 4); ++i) {
                    spirv_cross::MSLResourceBinding b;
                    b.stage = stage;
                    b.binding = 4 + i;
                    b.msl_texture = i;
                    compiler.add_msl_resource_binding(b);
                    b.binding = (genericAbi ? 20 : 8) + i;
                    b.msl_sampler = i;
                    compiler.add_msl_resource_binding(b);
                }
                const auto msl = compiler.compile();
                auto compile = [MTLCompileOptions new];
                compile.languageVersion = MTLLanguageVersion2_3;
                compile.fastMathEnabled = NO;
                NSError* failure = nil;
                auto library =
                    [native newLibraryWithSource:[NSString stringWithUTF8String:msl.c_str()]
                                         options:compile
                                           error:&failure];
                if (!library) {
                    error = failure.localizedDescription.UTF8String;
                    return nil;
                }
                const auto name = compiler.get_cleansed_entry_point_name(entry, stage);
                auto function =
                    [library newFunctionWithName:[NSString stringWithUTF8String:name.c_str()]];
                if (!function)
                    error = "metal_entrypoint_not_found";
                return function;
            } catch (const std::exception& e) {
                error = e.what();
                return nil;
            }
        }
    }
    bool PrepareShaderPipeline(const ShaderPipeline& p) override {
        auto valid = ValidateShaderPipeline(p, ShaderFeatures());
        if (valid)
            valid = ValidateShaderTargetSupport(*this, p);
        if (!Live() || !valid) {
            error = valid.reason;
            return false;
        }
        const auto& desc = p.program->description;
        if (programs.count(p.program->key))
            return true;
        SnapshotBytes vs, ps;
        std::map<unsigned, std::vector<unsigned>> vout, pin;
        if (!detail::CompileSpirv(desc.vertex, vs, error) ||
            !detail::CompileSpirv(desc.pixel, ps, error) ||
            !detail::ReflectSpirv(desc, desc.vertex, vs, error, &vout) ||
            !detail::ReflectSpirv(desc, desc.pixel, ps, error, &pin))
            return false;
        for (const auto& input : pin)
            if (vout[input.first] != input.second) {
                error = "shader_stage_interface_mismatch";
                return false;
            }
        NativeProgram out;
        std::vector<std::uint32_t> words(vs.size() / 4);
        std::memcpy(words.data(), vs.data(), vs.size());
        out.vertex = Function(words.data(), words.size(), desc.vertex.entry, true, true);
        words.resize(ps.size() / 4);
        std::memcpy(words.data(), ps.data(), ps.size());
        out.pixel = Function(words.data(), words.size(), desc.pixel.entry, false, true);
        if (!out.vertex || !out.pixel)
            return false;
        programs.emplace(p.program->key, out);
        error.clear();
        return true;
    }
    bool Builtin(const std::string& key, NativeProgram& out) {
        auto found = programs.find(key);
        if (found != programs.end()) {
            out = found->second;
            return true;
        }
        const auto make = [&](const auto& code, const char* entry, bool vertex) {
            return Function(code, std::size(code), entry, vertex, false);
        };
        if (key == "post") {
            out.vertex = make(spirv::post_vertex, "post_vs", true);
            out.pixel = make(spirv::post_pixel, "post_ps", false);
        } else if (key == "shadow" || key == "shadow_uv" || key == "outline") {
            out.vertex = key == "outline" ? make(spirv::outline_vertex, "shadow_outline_vs", true)
                         : key == "shadow_uv" ? make(spirv::shadow_uv_vertex, "shadow_uv_vs", true)
                                              : make(spirv::shadow_vertex, "shadow_vs", true);
            out.pixel = make(spirv::shadow_pixel, "shadow_ps", false);
        } else {
            out.vertex = key == "color"      ? make(spirv::color_vertex, "main", true)
                         : key == "model_uv" ? make(spirv::model_uv_vertex, "main", true)
                                             : make(spirv::model_vertex, "main", true);
            out.pixel = make(spirv::model_pixel, "main", false);
        }
        if (!out.vertex || !out.pixel)
            return false;
        programs.emplace(key, out);
        return true;
    }
    bool Select(const ModelPipeline& p, const ShaderProgram* shader = nullptr) {
        @autoreleasepool {
            std::string programKey = shader                                        ? shader->key
                                     : p.postProcess.mode != PostProcessMode::None ? "post"
                                     : p.shadow.mode != BuiltinShadowMode::None
                                         ? (p.shadow.mode == BuiltinShadowMode::Outline ? "outline"
                                            : AdditionalVertexUvs(p.vertexLayout) ? "shadow_uv"
                                                                                  : "shadow")
                                     : p.vertexLayout == VertexLayout::PositionColor ? "color"
                                     : AdditionalVertexUvs(p.vertexLayout)           ? "model_uv"
                                                                                     : "model";
            NativeProgram program;
            if (shader)
                program = programs.at(programKey);
            else if (!Builtin(programKey, program))
                return false;
            std::ostringstream k;
            k << programKey << ':';
            for (auto value : {unsigned(p.vertexLayout),
                               unsigned(p.raster.depthTest),
                               unsigned(p.raster.depthWrite),
                               unsigned(p.raster.depthCompare),
                               unsigned(p.raster.alphaBlend),
                               unsigned(p.raster.sourceBlend),
                               unsigned(p.raster.destinationBlend),
                               unsigned(p.separateAlpha),
                               unsigned(p.sourceAlpha),
                               unsigned(p.destinationAlpha),
                               unsigned(p.colorOperation),
                               unsigned(p.alphaOperation),
                               unsigned(p.stencil),
                               unsigned(p.stencilReadMask),
                               unsigned(p.stencilWriteMask),
                               unsigned(p.stencilCompare),
                               unsigned(p.stencilFail),
                               unsigned(p.stencilDepthFail),
                               unsigned(p.stencilPass),
                               unsigned(p.colorWriteMask),
                               unsigned(ActiveDepth().pixelFormat)})
                k << value << ',';
            auto formats = ColorFormats();
            for (auto f : formats)
                k << unsigned(f) << ',';
            const auto key = k.str();
            auto found = pipelines.find(key);
            if (found == pipelines.end()) {
                auto d = [MTLRenderPipelineDescriptor new];
                d.vertexFunction = program.vertex;
                d.fragmentFunction = program.pixel;
                auto vd = [MTLVertexDescriptor vertexDescriptor];
                auto inputs =
                    shader ? shader->description.inputs : StandardShaderInputs(p.vertexLayout);
                // Builtin postprocess and shadow shaders consume only their declared inputs.
                if (!shader && p.postProcess.mode != PostProcessMode::None)
                    inputs = {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3},
                              {"TEXCOORD", 0, 1, 24, ShaderVertexFormat::Float2}};
                for (const auto& input : inputs) {
                    auto a = vd.attributes[input.location];
                    a.bufferIndex = VertexBufferSlot;
                    a.offset = input.offset;
                    a.format = input.format == ShaderVertexFormat::Float2   ? MTLVertexFormatFloat2
                               : input.format == ShaderVertexFormat::Float3 ? MTLVertexFormatFloat3
                               : input.format == ShaderVertexFormat::Float4
                                   ? MTLVertexFormatFloat4
                                   : MTLVertexFormatUChar4Normalized;
                }
                vd.layouts[VertexBufferSlot].stride = ModelVertexStride(p.vertexLayout);
                d.vertexDescriptor = vd;
                for (unsigned n = 0; n < formats.size(); ++n) {
                    auto a = d.colorAttachments[n];
                    a.pixelFormat = PixelFormat(formats[n]);
                    a.writeMask = MTLColorWriteMaskNone;
                    if (p.colorWriteMask & 1)
                        a.writeMask |= MTLColorWriteMaskRed;
                    if (p.colorWriteMask & 2)
                        a.writeMask |= MTLColorWriteMaskGreen;
                    if (p.colorWriteMask & 4)
                        a.writeMask |= MTLColorWriteMaskBlue;
                    if (p.colorWriteMask & 8)
                        a.writeMask |= MTLColorWriteMaskAlpha;
                    a.blendingEnabled = p.raster.alphaBlend;
                    a.sourceRGBBlendFactor = Blend(p.raster.sourceBlend);
                    a.destinationRGBBlendFactor = Blend(p.raster.destinationBlend);
                    a.sourceAlphaBlendFactor =
                        Blend(p.separateAlpha ? p.sourceAlpha : p.raster.sourceBlend);
                    a.destinationAlphaBlendFactor =
                        Blend(p.separateAlpha ? p.destinationAlpha : p.raster.destinationBlend);
                    a.rgbBlendOperation = MTLBlendOperation(unsigned(p.colorOperation) - 1);
                    a.alphaBlendOperation = MTLBlendOperation(unsigned(p.alphaOperation) - 1);
                }
                if (ActiveDepth())
                    d.depthAttachmentPixelFormat = d.stencilAttachmentPixelFormat =
                        ActiveDepth().pixelFormat;
                NSError* failure = nil;
                NativePipeline pipeline;
                pipeline.render = [native newRenderPipelineStateWithDescriptor:d error:&failure];
                if (!pipeline.render) {
                    error = failure.localizedDescription.UTF8String;
                    return false;
                }
                auto ds = [MTLDepthStencilDescriptor new];
                ds.depthCompareFunction = p.raster.depthTest
                                              ? MTLCompareFunction(unsigned(p.raster.depthCompare))
                                              : MTLCompareFunctionAlways;
                ds.depthWriteEnabled = p.raster.depthWrite && p.raster.depthTest;
                if (p.stencil) {
                    auto s = [MTLStencilDescriptor new];
                    s.stencilCompareFunction = MTLCompareFunction(unsigned(p.stencilCompare));
                    s.stencilFailureOperation = Stencil(p.stencilFail);
                    s.depthFailureOperation = Stencil(p.stencilDepthFail);
                    s.depthStencilPassOperation = Stencil(p.stencilPass);
                    s.readMask = p.stencilReadMask;
                    s.writeMask = p.stencilWriteMask;
                    ds.frontFaceStencil = ds.backFaceStencil = s;
                }
                pipeline.depth = [native newDepthStencilStateWithDescriptor:ds];
                if (!pipeline.depth)
                    return false;
                found = pipelines.emplace(key, pipeline).first;
            }
            if (!encoder) {
                // Continue the already cleared legacy frame after an explicit pass.
                RenderPassDesc legacy;
                auto d = [MTLRenderPassDescriptor renderPassDescriptor];
                d.colorAttachments[0].texture = color;
                d.colorAttachments[0].loadAction = MTLLoadActionLoad;
                d.colorAttachments[0].storeAction = MTLStoreActionStore;
                d.depthAttachment.texture = d.stencilAttachment.texture = depth;
                d.depthAttachment.loadAction = d.stencilAttachment.loadAction = MTLLoadActionLoad;
                d.depthAttachment.storeAction = d.stencilAttachment.storeAction =
                    MTLStoreActionStore;
                encoder = [commands renderCommandEncoderWithDescriptor:d];
                if (!encoder)
                    return false;
            }
            [encoder setRenderPipelineState:found->second.render];
            [encoder setDepthStencilState:found->second.depth];
            [encoder setStencilReferenceValue:p.stencilReference];
            [encoder setFrontFacingWinding:MTLWindingClockwise];
            [encoder setCullMode:p.raster.cull == BatchCull::None               ? MTLCullModeNone
                                 : p.raster.cull == BatchCull::CounterClockwise ? MTLCullModeBack
                                                                                : MTLCullModeFront];
            const auto v =
                p.viewport.value_or(ModelViewport{0, 0, RenderWidth(), RenderHeight(), 0, 1});
            const double offset = p.shadow.mode == BuiltinShadowMode::None ? 0 : .5;
            [encoder setViewport:MTLViewport{v.x + offset, v.y + offset, double(v.width),
                                             double(v.height), v.minDepth, v.maxDepth}];
            [encoder setScissorRect:MTLScissorRect{0, 0, RenderWidth(), RenderHeight()}];
            viewport = v;
            return true;
        }
    }
    bool SetModelPipeline(const ModelPipeline& p) override;
    bool SetShaderPipeline(const ShaderPipeline& p) override {
        modelReady = genericReady = genericBound = false;
        bindings = {};
        if (!Live() || !inFrame || !PrepareShaderPipeline(p))
            return false;
        const auto valid = ValidateShaderAttachments(p, ColorFormats());
        if (!valid) {
            error = valid.reason;
            return false;
        }
        auto raster = ShaderRasterAdapter(p);
        if (!ValidModelViewport(
                p.viewport.value_or(ModelViewport{0, 0, RenderWidth(), RenderHeight(), 0, 1}),
                RenderWidth(), RenderHeight()) ||
            ((p.raster.depthTest || p.raster.depthWrite) && !ActiveDepth()) ||
            !Select(raster, p.program.get()))
            return false;
        generic = p;
        model = raster;
        genericReady = true;
        return true;
    }
    ShaderTextureInfo TextureInfo(Handle h) {
        auto* r = Find(h);
        const auto* d = passes.Description(h);
        return {bool(r && r->texture), bool(r && r->uploaded),
                bool(r && r->texture && (!d || passes.CanSample(h))), bool(r && r->srgb),
                r ? r->textureDesc.format : TextureFormat::Rgba8};
    }
    bool BindShaderResources(const ShaderResourceBindings& b) override {
        genericBound = false;
        if (!genericReady)
            return false;
        auto valid = ValidateShaderBindings(generic, b, [&](Handle h) { return TextureInfo(h); });
        if (!valid) {
            error = valid.reason;
            return false;
        }
        bindings = b;
        genericBound = true;
        return true;
    }
    bool BindTexture(std::uint32_t slot, Handle h) override {
        if (!modelReady || slot >= textures.size())
            return false;
        const auto info = TextureInfo(h);
        if (!info.valid || !info.initialized || !info.sampleable ||
            info.srgb != model.shader.srgbSample[slot])
            return false;
        textures[slot] = h;
        return true;
    }
    bool BindShadowTexture(Handle h) override {
        const auto info = TextureInfo(h);
        if (!modelReady || !info.valid || !info.initialized || !info.sampleable ||
            info.format != TextureFormat::R32Float)
            return false;
        shadow = h;
        return true;
    }
    id<MTLSamplerState> Sampler(const SamplerDesc& d) {
        @autoreleasepool {
            std::ostringstream k;
            for (auto v : {unsigned(d.min), unsigned(d.mag), unsigned(d.mip), unsigned(d.u),
                           unsigned(d.v), d.maxAnisotropy})
                k << v << ',';
            auto found = samplers.find(k.str());
            if (found != samplers.end())
                return found->second;
            auto s = [MTLSamplerDescriptor new];
            s.minFilter = d.min == SamplerFilter::Point ? MTLSamplerMinMagFilterNearest
                                                        : MTLSamplerMinMagFilterLinear;
            s.magFilter = d.mag == SamplerFilter::Point ? MTLSamplerMinMagFilterNearest
                                                        : MTLSamplerMinMagFilterLinear;
            s.mipFilter = d.mip == SamplerFilter::Point ? MTLSamplerMipFilterNearest
                                                        : MTLSamplerMipFilterLinear;
            s.sAddressMode = d.u == SamplerAddress::Wrap ? MTLSamplerAddressModeRepeat
                                                         : MTLSamplerAddressModeClampToEdge;
            s.tAddressMode = d.v == SamplerAddress::Wrap ? MTLSamplerAddressModeRepeat
                                                         : MTLSamplerAddressModeClampToEdge;
            s.maxAnisotropy =
                d.min == SamplerFilter::Anisotropic || d.mag == SamplerFilter::Anisotropic
                    ? d.maxAnisotropy
                    : 1;
            auto sampler = [native newSamplerStateWithDescriptor:s];
            if (sampler)
                samplers.emplace(k.str(), sampler);
            return sampler;
        }
    }
    bool Constant(unsigned slot, const void* bytes, std::size_t size, unsigned stages) {
        auto& f = frames[frameIndex];
        const auto offset = (f.offset + 255) & ~std::size_t(255);
        if (offset + size > UniformBytes)
            return false;
        std::memcpy(static_cast<char*>(f.uniforms.contents) + offset, bytes, size);
        if (stages & 1)
            [encoder setVertexBuffer:f.uniforms offset:offset atIndex:slot];
        if (stages & 2)
            [encoder setFragmentBuffer:f.uniforms offset:offset atIndex:slot];
        f.offset = offset + size;
        return true;
    }
    bool BindDraw() {
        if (draws >= MaxDraws)
            return false;
        if (genericReady) {
            if (!genericBound)
                return false;
            auto valid =
                ValidateShaderBindings(generic, bindings, [&](Handle h) { return TextureInfo(h); });
            if (!valid) {
                error = valid.reason;
                return false;
            }
            for (const auto& c : bindings.constants) {
                unsigned stages = 0;
                for (const auto& b : generic.program->description.bindings)
                    if (b.kind == ShaderBindingKind::Constants && b.slot == c.slot)
                        stages = b.stages;
                if (!Constant(c.slot, c.bytes.data(), c.bytes.size(), stages))
                    return false;
            }
            for (const auto& t : bindings.textures) {
                auto* r = Find(t.texture);
                r->lastUse = issued + 1;
                [encoder setFragmentTexture:r->texture atIndex:t.slot];
            }
            for (const auto& s : bindings.samplers) {
                auto sampler = Sampler(s.sampler);
                if (!sampler)
                    return false;
                [encoder setFragmentSamplerState:sampler atIndex:s.slot];
            }
        } else {
            if (!modelReady)
                return false;
            detail::ModelConstants c;
            if (!detail::BuildModelConstants(model, viewport, false, c))
                return false;
            const auto sc = detail::BuildBuiltinShadowConstants(model, viewport);
            const auto pc = detail::BuildPostProcessConstants(model);
            if (!Constant(0, &c.vertex, sizeof(c.vertex), 1) ||
                !Constant(1, &c.pixel, sizeof(c.pixel), 2) || !Constant(2, &sc, sizeof(sc), 1) ||
                !(model.postProcess.mode == PostProcessMode::None
                      ? Constant(3, &sc, sizeof(sc), 2)
                      : Constant(3, &pc, sizeof(pc), 2)))
                return false;
            for (unsigned i = 0; i < 4; ++i) {
                const bool active =
                    i == 3 ? model.shadow.mode == BuiltinShadowMode::Material
                           : model.shader.layout.stages[i].role != MaterialTextureRole::None;
                auto tex = white;
                SamplerDesc sd{SamplerFilter::Linear, SamplerFilter::Linear, SamplerFilter::Point,
                               SamplerAddress::Clamp, SamplerAddress::Clamp, 1};
                if (active) {
                    const auto h = i == 3 ? shadow : textures[i];
                    const auto info = TextureInfo(h);
                    if (!info.valid || !info.initialized || !info.sampleable)
                        return false;
                    auto* r = Find(h);
                    tex = r->texture;
                    r->lastUse = issued + 1;
                    if (i < 3)
                        sd = model.shader.samplers[i];
                }
                auto sampler = Sampler(sd);
                if (!sampler)
                    return false;
                [encoder setFragmentTexture:tex atIndex:i];
                [encoder setFragmentSamplerState:sampler atIndex:i];
            }
        }
        ++draws;
        return true;
    }
    bool Draw(NonIndexedDraw d) override {
        auto* v = Find(d.vertices);
        const auto stride = ModelVertexStride(model.vertexLayout);
        const auto arity = ModelPrimitiveArity(model.topology);
        if (!Live() || !inFrame || !IsCurrentSubmission(status, d.submission) ||
            (!genericReady && !modelReady) || !v || !v->buffer ||
            v->bufferDesc.usage != BufferUsage::Vertex || !d.layout ||
            *d.layout != model.vertexLayout || !stride || !arity || !d.vertexCount ||
            d.vertexCount % arity ||
            std::uint64_t(d.firstVertex) + d.vertexCount > v->bufferDesc.size / stride ||
            !BindDraw())
            return false;
        v->lastUse = issued + 1;
        [encoder setVertexBuffer:v->buffer offset:0 atIndex:VertexBufferSlot];
        [encoder
            drawPrimitives:model.topology == PrimitiveTopology::LineList ? MTLPrimitiveTypeLine
                                                                         : MTLPrimitiveTypeTriangle
               vertexStart:d.firstVertex
               vertexCount:d.vertexCount];
        return true;
    }
    bool DrawIndexed(IndexedDraw d) override {
        auto* v = Find(d.vertices);
        auto* i = Find(d.indices);
        const auto stride = ModelVertexStride(model.vertexLayout),
                   arity = ModelPrimitiveArity(model.topology);
        if (!Live() || !inFrame || !IsCurrentSubmission(status, d.submission) ||
            (!genericReady && !modelReady) || !v || !i || !v->buffer || !i->buffer ||
            v->bufferDesc.usage != BufferUsage::Vertex ||
            i->bufferDesc.usage != BufferUsage::Index || d.layout != model.vertexLayout ||
            !stride || !arity || !d.vertexCount || d.vertexCount > v->bufferDesc.size / stride ||
            !d.indexCount || d.indexCount % arity)
            return false;
        const auto size = i->bufferDesc.indexFormat == IndexFormat::UInt32 ? 4u : 2u;
        if (std::uint64_t(d.firstIndex) + d.indexCount > i->bufferDesc.size / size)
            return false;
        // Validate index bounds from the coherent CPU upload before native access.
        const auto* bytes = static_cast<const char*>(i->buffer.contents);
        for (unsigned n = 0; n < d.indexCount; ++n) {
            std::uint32_t index = 0;
            std::memcpy(&index, bytes + std::size_t(d.firstIndex + n) * size, size);
            if (index >= d.vertexCount)
                return false;
        }
        if (!BindDraw())
            return false;
        v->lastUse = i->lastUse = issued + 1;
        [encoder setVertexBuffer:v->buffer offset:0 atIndex:VertexBufferSlot];
        [encoder drawIndexedPrimitives:model.topology == PrimitiveTopology::LineList
                                           ? MTLPrimitiveTypeLine
                                           : MTLPrimitiveTypeTriangle
                            indexCount:d.indexCount
                             indexType:size == 4 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16
                           indexBuffer:i->buffer
                     indexBufferOffset:std::size_t(d.firstIndex) * size];
        return true;
    }
    bool Init(unsigned w, unsigned h) {
        @autoreleasepool {
            native = MTLCreateSystemDefaultDevice();
            if (!native) {
                error = "metal_device_unavailable";
                return false;
            }
            queue = [native newCommandQueue];
            if (!queue) {
                error = "metal_queue_creation_failed";
                return false;
            }
            for (auto& f : frames) {
                f.uniforms =
                    [native newBufferWithLength:UniformBytes options:MTLResourceStorageModeShared];
                if (!f.uniforms) {
                    error = "metal_uniform_arena_failed";
                    return false;
                }
            }
            white = MakeTexture(1, 1, MTLPixelFormatRGBA8Unorm, 1, MTLTextureUsageShaderRead);
            if (!white || !UploadImage(white, {RgbaImage{1, 1, {255, 255, 255, 255}}}))
                return false;
            HistoryDesc target;
            target.width = w;
            target.height = h;
            if (!EnsureModelTarget(target)) {
                error = "metal_target_creation_failed";
                return false;
            }
            return true;
        }
    }
};
bool MetalDevice::SetModelPipeline(const ModelPipeline& p) {
    genericReady = genericBound = false;
    modelReady = false;
    textures = {};
    shadow = {};
    const auto view = p.viewport.value_or(ModelViewport{0, 0, RenderWidth(), RenderHeight(), 0, 1});
    const auto& s = p.shader;
    std::array<float, 4> evaluated;
    std::array<std::array<float, 4>, 3> samples{};
    if (!Live() || !inFrame || !ValidPostProcess(p) || !ValidBuiltinShadowState(p.shadow) ||
        (p.shadow.mode != BuiltinShadowMode::None &&
         (!passes.Active() || !ValidBuiltinShadowVertexLayout(p.shadow.mode, p.vertexLayout) ||
          p.topology != PrimitiveTopology::TriangleList)) ||
        !ValidModelViewport(view, RenderWidth(), RenderHeight()) ||
        !ModelVertexStride(p.vertexLayout) || !ModelPrimitiveArity(p.topology) ||
        !ValidateMaterialShaderInputs(s) ||
        !EvaluateMaterialTextureCascade(s.layout, {1, 1, 1, 1}, samples, evaluated) ||
        unsigned(p.raster.depthCompare) > 7 || unsigned(p.raster.cull) > 2 ||
        unsigned(p.raster.sourceBlend) > 10 || unsigned(p.raster.destinationBlend) > 10 ||
        unsigned(p.sourceAlpha) > 10 || unsigned(p.destinationAlpha) > 10 ||
        unsigned(p.colorOperation) < 1 || unsigned(p.colorOperation) > 5 ||
        unsigned(p.alphaOperation) < 1 || unsigned(p.alphaOperation) > 5 || p.colorWriteMask > 15 ||
        unsigned(p.stencilCompare) > 7 || unsigned(p.stencilFail) < 1 ||
        unsigned(p.stencilFail) > 8 || unsigned(p.stencilDepthFail) < 1 ||
        unsigned(p.stencilDepthFail) > 8 || unsigned(p.stencilPass) < 1 ||
        unsigned(p.stencilPass) > 8 || ((p.raster.depthTest || p.stencil) && !ActiveDepth()))
        return false;
    if (p.shadow.mode == BuiltinShadowMode::Depth &&
        (ColorFormats().empty() || ColorFormats()[0] != TextureFormat::R32Float))
        return false;
    const auto alphaFactor = [](BlendFactor b) {
        return b != BlendFactor::SourceColor && b != BlendFactor::InvSourceColor &&
               b != BlendFactor::DestinationColor && b != BlendFactor::InvDestinationColor &&
               b != BlendFactor::SourceAlphaSaturate;
    };
    if (p.raster.alphaBlend &&
        (!alphaFactor(p.separateAlpha ? p.sourceAlpha : p.raster.sourceBlend) ||
         !alphaFactor(p.separateAlpha ? p.destinationAlpha : p.raster.destinationBlend)))
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& t = s.layout.stages[i];
        const auto& a = s.samplers[i];
        if (t.role != MaterialTextureRole::None &&
            ((t.coordinates == MaterialCoordinates::MeshUv1 &&
              !AdditionalVertexUvs(p.vertexLayout)) ||
             (p.vertexLayout == VertexLayout::PositionColor &&
              t.coordinates != MaterialCoordinates::MeshUv0) ||
             a.maxAnisotropy > 16 ||
             (a.mip == SamplerFilter::Anisotropic && a.min != SamplerFilter::Anisotropic &&
              a.mag != SamplerFilter::Anisotropic)))
            return false;
    }
    if (!Select(p))
        return false;
    model = p;
    viewport = view;
    modelReady = true;
    return true;
}
} // namespace
OffscreenBackendDevice CreateMetalOffscreenDevice(unsigned width, unsigned height,
                                                  const BackendOptions& options) {
    @autoreleasepool {
        OffscreenBackendDevice result;
        if (options.driver != BackendDriver::Hardware) {
            result.reason = "metal_software_driver_not_supported";
            return result;
        }
        if (options.model.compatibility != ModelCompatibility::Default ||
            options.model.diagnosticAlpha12 || options.model.diagnosticTextureFma ||
            options.model.diagnosticSpecularFma) {
            result.reason = "model_compatibility_not_supported";
            return result;
        }
        if (!width || !height || width > 4096 || height > 4096) {
            result.reason = "invalid_target_size";
            return result;
        }
        auto device = std::make_unique<MetalDevice>();
        if (!device->Init(width, height)) {
            result.reason = device->error;
            return result;
        }
        result.adapter = device->native.name.UTF8String;
        result.device = std::move(device);
        return result;
    }
}
} // namespace mikudancestudio::rhi
