// Independent D3DX parameter oracle. NULLREF creates no GPU rendering workload;
// its owning window is hidden, never activated, and never presented.
#define NOMINMAX
#include "d3dx9.h"
#include "mme_runtime_test_support.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace mme_test;
template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p)
            p->Release();
    }
    Com() = default;
    Com(const Com&) = delete;
    void Reset() {
        if (p)
            p->Release();
        p = nullptr;
    }
};
void Compare(ID3DXBaseEffect* native, const EffectInstance& instance) {
    for (const auto& v : instance.Definition().metadata.variables) {
        // FX compilers may fold immutable shader constants out of the instance.
        if (v.immutable)
            continue;
        auto value = Read(instance, v.name);
        const auto handle = native->GetParameterByName(nullptr, v.name.c_str());
        D3DXPARAMETER_DESC desc{};
        Check(handle && SUCCEEDED(native->GetParameterDesc(handle, &desc)),
              "native parameter " + v.name);
        Check(bool(desc.Elements) == v.array, "native array identity " + v.name);
        if (value.type == ParameterValue::Type::String) {
            const char* text = nullptr;
            Check(SUCCEEDED(native->GetString(handle, &text)) && value.text == (text ? text : ""),
                  "native string " + v.name);
        } else {
            std::vector<std::uint32_t> words(value.words.size());
            HRESULT hr = E_FAIL;
            if (value.type == ParameterValue::Type::Float) {
                std::vector<float> floats(words.size());
                hr = native->GetFloatArray(handle, floats.data(), UINT(floats.size()));
                std::memcpy(words.data(), floats.data(), words.size() * 4);
            } else if (value.type == ParameterValue::Type::Int) {
                std::vector<INT> ints(words.size());
                hr = native->GetIntArray(handle, ints.data(), UINT(ints.size()));
                std::memcpy(words.data(), ints.data(), words.size() * 4);
            } else {
                std::vector<BOOL> bools(words.size());
                hr = native->GetBoolArray(handle, bools.data(), UINT(bools.size()));
                for (std::size_t n = 0; n < words.size(); ++n)
                    words[n] = bools[n] != 0;
            }
            if (words != value.words) {
                std::cerr << "mismatch " << v.name << " native:";
                for (auto w : words)
                    std::cerr << ' ' << w;
                std::cerr << " actual:";
                for (auto w : value.words)
                    std::cerr << ' ' << w;
                std::cerr << '\n';
            }
            Check(SUCCEEDED(hr) && words == value.words, "native logical value " + v.name);
        }
    }
}
int main(int argc, char** argv) try {
    Check(argc == 3, "oracle DLL and source directory required");
    const auto path = std::filesystem::u8path(argv[1]);
    struct Module {
        HMODULE p = nullptr;
        ~Module() {
            if (p)
                FreeLibrary(p);
        }
    } compiler, runtime;
    compiler.p = LoadLibraryW((path.parent_path() / L"D3DCompiler_43.dll").c_str());
    runtime.p = LoadLibraryW(path.c_str());
    Check(compiler.p && runtime.p, "native oracle libraries unavailable");
    using Create =
        HRESULT(WINAPI*)(IDirect3DDevice9*, LPCVOID, UINT, const D3DXMACRO*, ID3DXInclude*, DWORD,
                         ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
    const auto create = reinterpret_cast<Create>(GetProcAddress(runtime.p, "D3DXCreateEffect"));
    const auto createPool = reinterpret_cast<HRESULT(WINAPI*)(ID3DXEffectPool**)>(
        GetProcAddress(runtime.p, "D3DXCreateEffectPool"));
    Check(create && createPool, "native runtime exports");
    struct HiddenWindow {
        HWND p = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"MME parameter oracle", WS_POPUP, 0,
                                 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~HiddenWindow() {
            if (p)
                DestroyWindow(p);
        }
    } window;
    Check(window.p != nullptr, "hidden oracle window");
    Com<IDirect3D9> d3d;
    d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
    Check(d3d.p != nullptr, "native d3d runtime");
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = pp.BackBufferHeight = 16;
    pp.hDeviceWindow = window.p;
    Com<IDirect3DDevice9> device;
    Check(SUCCEEDED(d3d.p->CreateDevice(0, D3DDEVTYPE_NULLREF, window.p,
                                        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device.p)),
          "NULLREF oracle device unavailable");
    Com<ID3DXEffectPool> nativePool;
    Check(SUCCEEDED(createPool(&nativePool.p)), "native pool");
    const auto load = [&](const std::string& source, Com<ID3DXEffect>& out) {
        Com<ID3DXBuffer> errors;
        const auto hr = create(device.p, source.data(), UINT(source.size()), nullptr, nullptr, 0,
                               nativePool.p, &out.p, &errors.p);
        Check(SUCCEEDED(hr), errors.p ? static_cast<const char*>(errors.p->GetBufferPointer())
                                      : "native effect creation");
    };
    std::ifstream file(std::filesystem::u8path(argv[2]) / "assets/effects/frontend/Runtime.fx");
    const std::string source{std::istreambuf_iterator<char>(file), {}};
    Check(!source.empty(), "public runtime fixture");
    EffectPool pool;
    std::string reason;
    auto definition = std::make_shared<EffectDefinition>();
    Check(MmeLegacyDescribe(source, definition->metadata, reason), reason);
    std::shared_ptr<EffectInstance> actual;
    Check(EffectInstance::Create(definition, pool, actual, reason), reason);
    Com<ID3DXEffect> expected;
    load(source, expected);
    Compare(expected.p, *actual);
    // Pool identities and first-live initializer are read from native effects,
    // rather than generating expected values with the portable parser.
    Com<ID3DXEffect> a, b, c, lower, shape, array, clone;
    load("shared float X=2;float Local=8;technique T{}", a);
    load("shared float X=7;technique T{}", b);
    load("shared float X:DIFFUSE=11;technique T{}", c);
    load("shared float X:diffuse=17;technique T{}", lower);
    load("shared float3 X={1,2,3};technique T{}", shape);
    load("shared float X[1]={19};technique T{}", array);
    auto pa = Instance(pool, "shared float X=2;float Local=8;");
    auto pb = Instance(pool, "shared float X=7;");
    auto pc = Instance(pool, "shared float X:DIFFUSE=11;");
    auto pl = Instance(pool, "shared float X:diffuse=17;");
    auto ps = Instance(pool, "shared float3 X={1,2,3};");
    auto pr = Instance(pool, "shared float X[1]={19};");
    Compare(a.p, *pa);
    Compare(b.p, *pb);
    Compare(c.p, *pc);
    Compare(lower.p, *pl);
    Compare(shape.p, *ps);
    Compare(array.p, *pr);
    Check(SUCCEEDED(a.p->SetFloat("X", 9)) && pa->Write("X", Float(9), reason), reason);
    Compare(b.p, *pb);
    Check(SUCCEEDED(a.p->CloneEffect(device.p, &clone.p)), "native clone");
    std::shared_ptr<EffectInstance> pcl;
    Check(pa->Clone(pcl, reason), reason);
    Check(SUCCEEDED(clone.p->SetFloat("Local", 12)) && pcl->Write("Local", Float(12), reason),
          reason);
    Check(SUCCEEDED(clone.p->SetFloat("X", 15)) && pcl->Write("X", Float(15), reason), reason);
    Compare(clone.p, *pcl);
    Compare(a.p, *pa);
    Compare(b.p, *pb);
    a.Reset();
    b.Reset();
    clone.Reset();
    pa.reset();
    pb.reset();
    pcl.reset();
    load("shared float X=12;technique T{}", a);
    pa = Instance(pool, "shared float X=12;");
    Compare(a.p, *pa);
    // Independent native matrix storage/transpose and translation reference.
    const std::string semanticSource =
        "float4x4 W:WORLDTRANSPOSE;float4 Position:POSITION <string Object=\"Camera\";>;float4 L:DIFFUSE <string Object=\"Light\";>;float P:SPECULARPOWER <string Object=\"Geometry\";>;technique T{}";
    auto sd = std::make_shared<EffectDefinition>();
    Check(MmeLegacyDescribe(semanticSource, sd->metadata, reason), reason);
    std::shared_ptr<EffectInstance> si;
    Check(EffectInstance::Create(sd, pool, si, reason), reason);
    Com<ID3DXEffect> se;
    load(semanticSource, se);
    auto context = Context();
    context.standard.world[12] = 7;
    context.standard.view[12] = -3;
    context.standard.view[13] = -4;
    context.standard.lightColor[0] = {.2f, .3f, .4f, 0};
    std::map<std::string, ParameterValue> semantic;
    Check(EvaluateSemantics(*sd, context, semantic, reason), reason);
    D3DXMATRIX w;
    std::memcpy(&w, context.standard.world.data(), 64);
    Check(SUCCEEDED(se.p->SetMatrixTranspose("W", &w)), "native matrix transpose storage");
    const D3DXVECTOR4 position{3, 4, 0, 1}, light{.2f, .3f, .4f, 1};
    se.p->SetVector("Position", &position);
    se.p->SetVector("L", &light);
    se.p->SetFloat("P", .1f);
    for (const auto& [name, value] : semantic)
        Check(si->Write(name, value, reason), reason);
    Compare(se.p, *si);
    // The original SAS conversion's per-shape setters are the independent
    // reference here; portable values are never used to generate these writes.
    const std::string controlSource =
        "bool B;float S;float2 V2;float3 V3;float4 V4;float4x4 M;technique T{}";
    auto cd = std::make_shared<EffectDefinition>();
    Check(MmeLegacyDescribe(controlSource, cd->metadata, reason), reason);
    std::shared_ptr<EffectInstance> ci;
    Check(EffectInstance::Create(cd, pool, ci, reason), reason);
    Com<ID3DXEffect> ce;
    load(controlSource, ce);
    for (int scenario = 0; scenario < 6; ++scenario) {
        ControlSample sample;
        sample.found = scenario >= 3;
        sample.accessory = scenario == 1 || scenario == 2;
        sample.hasItem = scenario == 2 || scenario == 4;
        sample.visible = scenario == 5;
        sample.transform = mikudancestudio::cpu_math::Identity();
        sample.matrix = scenario == 5;
        if (sample.found) {
            sample.transform[0] = 3;
            sample.transform[1] = 4;
            sample.transform[12] = 7;
            sample.transform[13] = 8;
            sample.transform[14] = 9;
        }
        D3DXMATRIX m;
        std::memset(&m, 0, sizeof(m));
        if (sample.found)
            std::memcpy(&m, sample.transform.data(), 64);
        else {
            m._11 = m._22 = m._33 = sample.accessory ? 10.f : 1.f;
            m._44 = 1;
        }
        const float scale = sample.found ? (sample.matrix ? 5.f : 7.f)
                                         : (sample.hasItem ? 0.f : (sample.accessory ? 10.f : 1.f));
        D3DXVECTOR4 vector{m._41, m._42, m._43, m._44};
        ce.p->SetBool("B", sample.found && sample.visible);
        ce.p->SetFloat("S", scale);
        Check(SUCCEEDED(ce.p->SetVector("V2", &vector)), "native SetVector V2");
        ce.p->SetVector("V3", &vector);
        ce.p->SetVector("V4", &vector);
        ce.p->SetMatrix("M", &m);
        for (const auto& variable : cd->metadata.variables) {
            ParameterValue value;
            Check(EvaluateControl(variable, sample, value, reason), reason);
            Check(ci->Write(variable.name, value, reason), reason);
        }
        Compare(ce.p, *ci);
    }
    std::cout
        << "Independent D3DX NULLREF: default bytes, array identity, shared pool, clone, unload and semantic storage passed (hidden, no Present)\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
