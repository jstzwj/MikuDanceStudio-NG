#pragma once
#include "scene_render_fixture.hpp"
namespace {
struct NativeAccessories {
    OriginalModel& original;
    std::array<std::unique_ptr<mdl::AccessoryRecord>, 2> objects;
    explicit NativeAccessories(OriginalModel& value) : original(value) {}
    void Free(void* memory) {
        if (memory)
            original.module.Function<void(__cdecl*)(void*)>(sizeof(void*) == 8 ? 0x11A39C
                                                                               : 0x104E31)(memory);
    }
    void ClearRecord(mdl::AccessoryRecord& a) {
        if (a.mesh)
            static_cast<ID3DXMesh*>(a.mesh)->Release();
        Free(a.materials);
        Free(a.textureTypes);
        Free(a.texturePaths);
        a = {};
    }
    void Clear(int index) {
        if (objects[index])
            ClearRecord(*objects[index]);
        objects[index].reset();
    }
    void Load(Fixture& f, int index, const std::filesystem::path& path) {
        objects[index] = std::make_unique<mdl::AccessoryRecord>();
        auto& a = *objects[index];
        InitAccessoryRecord(&a);
        std::array<wchar_t, 32768> paths{};
        const auto cwd = std::filesystem::current_path();
#if defined(_M_X64)
        using Load =
            bool (*)(mdl::AccessoryRecord*, HWND, D3DRenderer*, const wchar_t*, char, void*);
        const auto address = 0xDC560;
#else
        using Load = bool(__thiscall*)(mdl::AccessoryRecord*, HWND, D3DRenderer*, const wchar_t*,
                                       char, void*);
        const auto address = 0xC5F40;
#endif
        const bool loaded = original.module.Function<Load>(address)(
            &a, f.window, original.renderer.get(), path.c_str(), 1, paths.data());
        std::filesystem::current_path(cwd);
        Check(loaded, "native accessory loader");
    }
    ~NativeAccessories() {
        for (int i = 0; i < 2; ++i)
            Clear(i);
        for (auto& resource : original.renderer->resourcePool) {
            if (resource.comObject)
                resource.comObject->Release();
            Free(resource.heapBuffer);
            resource = {};
        }
    }
};

}
