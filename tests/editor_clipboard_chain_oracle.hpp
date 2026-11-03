#pragma once
#include "editor_clipboard_chain_fixture.hpp"

namespace {
template <class Out, class In> Out GlobalClipboardRecord(const In& key) {
    Out result{};
#define FIELD(name) std::memcpy(&result.name, &key.name, sizeof(result.name))
    FIELD(frame);
    FIELD(previous);
    FIELD(next);
    FIELD(selected);
    if constexpr (std::is_same_v<In, PmmCameraKey>) {
        FIELD(eye);
        FIELD(target);
        FIELD(distance);
        FIELD(fov);
        FIELD(interpolation);
        FIELD(perspective);
        FIELD(parentModel);
        FIELD(parentBone);
    } else if constexpr (std::is_same_v<In, PmmLightKey>) {
        FIELD(color);
        FIELD(direction);
    } else if constexpr (std::is_same_v<In, PmmSelfShadowKey>) {
        FIELD(mode);
        FIELD(distance);
    } else if constexpr (std::is_same_v<In, PmmGravityKey>) {
        FIELD(acceleration);
        FIELD(direction);
        FIELD(noise);
        FIELD(noiseEnabled);
    } else {
        static_assert(std::is_same_v<In, PmmAccessoryKey>);
        FIELD(position);
        FIELD(rotation);
        FIELD(scale);
        FIELD(opacity);
        FIELD(parentModel);
        FIELD(parentBone);
        FIELD(visible);
        FIELD(shadowEnabled);
    }
#undef FIELD
    return result;
}
unsigned GlobalClipboardChains(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    // Only presentation/pose-evaluation tails are replaced. Copy, registration,
    // allocation, mark clearing and the complete command dispatcher remain native.
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch controls(original.Function<void*>(0x62bb0), &ret, 1);
    Patch reload(original.Function<void*>(0x59c30), &ret, 1);
    Patch seekCamera(original.Function<void*>(0x5ba90), &ret, 1);
    Patch seekLight(original.Function<void*>(0x5caf0), &ret, 1);
    Patch seekShadow(original.Function<void*>(0x5d320), &ret, 1);
    Patch syncAccessory(original.Function<void*>(0x5e510), &ret, 1);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    auto free = *original.Function<void (**)(void*)>(0x128450);
    std::uint64_t hash = 14695981039346656037ull;
    unsigned checkpoints = 0;
    for (unsigned accessoryCount : {0u, 1u, 2u})
        for (bool reversed : {false, true}) {
            EditorSession portable;
            std::string reason;
            Check(clipboard_chain_fixture::Initialize(portable, root, accessoryCount, reversed,
                                                      reason),
                  reason.c_str());
            std::memset(app.get(), 0, sizeof(*app));
            std::vector<mdl::CameraKey> camera(10000);
            std::vector<mdl::LightKey> light(10000);
            std::vector<mdl::SelfShadowKey> shadow(10000);
            std::vector<mdl::GravityKey> gravity(10000);
            std::array<std::vector<mdl::AccessoryKey>, 3> accessories;
            std::array<mdl::AccessoryRecord, 2> objects{};
            for (auto& pool : accessories)
                pool.resize(10000);
            app->cameraKeyTrack = camera.data();
            app->lightKeyTrack = light.data();
            app->selfShadowKeyTrack = shadow.data();
            app->gravityKeyTrack = gravity.data();
            for (unsigned slot = 0; slot < 255; ++slot)
                app->accKeyTracks[slot] = accessories[slot < accessoryCount ? slot : 2].data();
            for (unsigned slot = 0; slot < accessoryCount; ++slot)
                app->objectSlots[slot] = &objects[slot];
            auto* raw = reinterpret_cast<unsigned char*>(app.get());
            raw[0x328] = raw[0xa1b90] = 1;
            app->lastRegisteredFrame = portable.Document().timeline.lastRegisteredFrame;
            const auto load = [](auto& pool, const auto& track) {
                using T = typename std::decay_t<decltype(pool)>::value_type;
                for (const auto& [index, key] : frame_line_fixture::Records(track))
                    pool.at(index) = GlobalClipboardRecord<T>(key);
            };
            load(camera, portable.Document().cameraKeys);
            load(light, portable.Document().lightKeys);
            load(shadow, portable.Document().selfShadowKeys);
            load(gravity, portable.Document().gravityKeys);
            for (unsigned slot = 0; slot < accessoryCount; ++slot)
                load(accessories[slot], portable.Document().accessories[slot].keyRecords);
            const auto compare = [&](const auto& pool, const auto& track, const char* name) {
                using T = typename std::decay_t<decltype(pool)>::value_type;
                std::vector<T> expected(pool.size());
                load(expected, track);
                if (std::memcmp(expected.data(), pool.data(), pool.size() * sizeof(T))) {
                    for (unsigned i = 0; i < pool.size(); ++i)
                        if (std::memcmp(&expected[i], &pool[i], sizeof(T))) {
                            std::fprintf(stderr,
                                         "checkpoint=%u accessories=%u reversed=%u %s slot=%u\n",
                                         checkpoints, accessoryCount, reversed, name, i);
                            break;
                        }
                    Check(false, "native chained clipboard complete key pool differs");
                }
            };
            // Original-CRT allocations must be freed by that same module, even
            // after an empty copy has cleared counts but retained old buffers.
            struct Buffers {
                unsigned char* raw;
                void (*release)(void*);
                ~Buffers() {
                    for (unsigned offset : {0x3a8u, 0x3b0u, 0x3b8u, 0x3c0u, 0x3c8u}) {
                        void* buffer;
                        std::memcpy(&buffer, raw + offset, sizeof buffer);
                        if (buffer)
                            release(buffer);
                    }
                }
            } buffers{raw, free};
            const auto clipboard = [&] {
                std::vector<std::uint8_t> bytes;
                const auto& counts = app->reservedClipboardCounts;
                const unsigned sizes[]{
                    sizeof(mdl::CameraClipboardRecord), sizeof(mdl::LightClipboardRecord),
                    sizeof(mdl::ShadowClipboardRecord), sizeof(mdl::GravityClipboardRecord),
                    sizeof(mdl::AccessoryClipboardKey)};
                const unsigned numbers[]{counts.cameras, counts.lights, counts.shadows,
                                         counts.gravity, counts.accessories};
                for (unsigned i = 0; i < 5; ++i) {
                    void* buffer;
                    std::memcpy(&buffer, raw + 0x3a8 + 8 * i, sizeof buffer);
                    frame_line_fixture::Bytes(bytes, numbers[i]);
                    frame_line_fixture::Bytes(bytes, std::uintptr_t(buffer));
                    if (numbers[i]) {
                        const auto* begin = static_cast<const std::uint8_t*>(buffer);
                        bytes.insert(bytes.end(), begin, begin + numbers[i] * sizes[i]);
                    }
                }
                return bytes;
            };
            for (const auto& command : clipboard_chain_fixture::Commands()) {
                if (command.copy) {
                    const auto selection =
                        clipboard_chain_fixture::Selection(command, accessoryCount);
                    const auto mark = [&](auto& pool, Track track, unsigned slot) {
                        for (unsigned i = 0; i < pool.size(); ++i)
                            pool[i].selected = std::uint8_t(
                                (i == 0 || pool[i].frame != 0) &&
                                selection.count({track, std::uint8_t(slot), 0, pool[i].frame}));
                    };
                    mark(camera, Track::Camera, 255);
                    mark(light, Track::Light, 255);
                    mark(shadow, Track::Shadow, 255);
                    mark(gravity, Track::Gravity, 255);
                    for (unsigned slot = 0; slot < accessoryCount; ++slot)
                        mark(accessories[slot], Track::Accessory, slot);
                } else
                    app->currentFrame = int(command.destination);
                const auto beforeClipboard = clipboard();
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(
                    nullptr, command.copy ? panel::kCopyFramesButton : panel::kPasteButton);
                if (command.copy) {
                    const auto& counts = app->reservedClipboardCounts;
                    Check(counts.cameras == command.frames.size() &&
                              counts.lights == command.frames.size() &&
                              counts.shadows == command.frames.size() &&
                              counts.gravity == command.frames.size() &&
                              counts.accessories == command.frames.size() * accessoryCount,
                          "native chained copy counts differ");
                } else
                    Check(beforeClipboard == clipboard(),
                          "native paste mutated immutable global clipboard");
                Check(clipboard_chain_fixture::Execute(portable, command, accessoryCount, reason),
                      reason.c_str());
                compare(camera, portable.Document().cameraKeys, "camera");
                compare(light, portable.Document().lightKeys, "light");
                compare(shadow, portable.Document().selfShadowKeys, "shadow");
                compare(gravity, portable.Document().gravityKeys, "gravity");
                for (unsigned slot = 0; slot < accessoryCount; ++slot)
                    compare(accessories[slot], portable.Document().accessories[slot].keyRecords,
                            "accessory");
                Check(app->lastRegisteredFrame == portable.Document().timeline.lastRegisteredFrame,
                      "native chained clipboard frame watermark differs");
                clipboard_chain_fixture::Hash(hash, portable.Document());
                ++checkpoints;
            }
        }
    Check(hash == 0xe5501b1a254b32e5ull, "native clipboard chain frozen result differs");
    std::printf("Original clipboard chain frozen hash: %016llx\n",
                static_cast<unsigned long long>(hash));
    return checkpoints;
}
} // namespace
