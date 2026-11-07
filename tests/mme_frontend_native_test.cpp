// Independent Windows oracle. D3DPreprocess and ID3DXEffectCompiler's common
// ID3DXBaseEffect interface need neither a graphics device nor a window.
#define NOMINMAX
#include "d3dx9.h"
#include "mikudancestudio/mme_frontend.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace mikudancestudio::mme;
void Check(bool ok, const std::string& reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p)
            p->Release();
    }
};
std::string Read(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    Check(bool(input), "native source file");
    return {std::istreambuf_iterator<char>(input), {}};
}
struct Includes : ID3DInclude {
    fs::path root;
    std::map<const void*, std::pair<fs::path, std::unique_ptr<std::string>>> opened;
    explicit Includes(fs::path directory) : root(std::move(directory)) {}
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data,
                           UINT* count) override {
        try {
            const auto it = opened.find(parent);
            const auto directory = it == opened.end() ? root : it->second.first.parent_path();
            const auto file = directory / fs::u8path(name);
            auto bytes = std::make_unique<std::string>(Read(file));
            const auto key = bytes->data();
            *data = key;
            *count = UINT(bytes->size());
            opened.emplace(key, std::make_pair(file, std::move(bytes)));
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }
    HRESULT __stdcall Close(LPCVOID data) override {
        opened.erase(data);
        return S_OK;
    }
};
std::string Tokens(std::string text) {
    text = std::regex_replace(text, std::regex("(^|\n)[ \t]*#[^\n]*"), "\n");
    // Compare actual tokens, preserving quoted strings and identifier
    // boundaries. Removing all spaces could hide a token-pasting difference.
    if (!text.empty() && text.back() == '\0')
        text.pop_back();
    std::string result;
    const std::regex token(
        R"fx("(?:\\.|[^"\\])*"|[A-Za-z_][A-Za-z_0-9]*|(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?[fFuUlL]*|==|!=|<=|>=|&&|\|\||<<|>>|\+\+|--|[-+*/&|^%]=|\S)fx");
    for (std::sregex_iterator it(text.begin(), text.end(), token), end; it != end; ++it) {
        const auto value = it->str();
        result += std::to_string(value.size()) + ':' + value;
    }
    return result;
}
std::string Upper(std::string value) {
    for (auto& c : value)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return value;
}
void Annotations(ID3DXBaseEffect* effect, D3DXHANDLE owner, unsigned count,
                 const std::map<std::string, std::string>& expected) {
    Check(count == expected.size(), "native annotation count");
    for (unsigned i = 0; i < count; ++i) {
        const auto a = effect->GetAnnotation(owner, i);
        D3DXPARAMETER_DESC desc{};
        Check(SUCCEEDED(effect->GetParameterDesc(a, &desc)), "native annotation descriptor");
        const auto found = expected.find(Upper(desc.Name));
        Check(found != expected.end(), "native annotation name");
        if (desc.Type == D3DXPT_STRING) {
            const char* value = nullptr;
            Check(SUCCEEDED(effect->GetString(a, &value)) && value && found->second == value,
                  "native string annotation value: " + found->first);
        } else if (desc.Type == D3DXPT_BOOL) {
            BOOL value = FALSE;
            Check(SUCCEEDED(effect->GetBool(a, &value)) &&
                      bool(value) == (Upper(found->second) == "TRUE" || found->second == "1"),
                  "native bool annotation");
        } else if (desc.Type == D3DXPT_FLOAT) {
            auto text = found->second;
            for (auto& c : text)
                if (c == '{' || c == '}' || c == ',')
                    c = ' ';
            std::istringstream input(text);
            input.imbue(std::locale::classic());
            const auto count = desc.Rows * desc.Columns * (desc.Elements ? desc.Elements : 1);
            std::vector<float> values(count);
            Check(SUCCEEDED(effect->GetFloatArray(a, values.data(), count)),
                  "native float annotation");
            for (float value : values) {
                float expected = 0;
                Check(bool(input >> expected) && value == expected,
                      "native annotation numeric value");
            }
        }
    }
}
int main(int argc, char** argv) try {
    Check(argc == 4, "dxc, source directory and optional native runtime path required");
    const auto cwd = fs::current_path();
    SourceOptions options;
    options.preprocessor = fs::absolute(argv[1]);
    options.macros["MME_MIPMAP"] = "";
    for (const auto* relative :
         {"frontend/Inspect.fx", "portable/LegacyMaterial.fx", "portable/LegacyScene.fx",
          "schedule/Material.fx", "schedule/Child.fx", "schedule/OuterScene.fx",
          "schedule/InnerScene.fx", "schedule/ReaderMaterial.fx"}) {
        const auto fixture = fs::path(argv[2]) / "assets/effects" / relative;
        std::shared_ptr<const EffectDefinition> definition;
        std::string reason;
        const bool loaded = LoadEffectDefinition(fixture, options, definition, reason);
        Check(loaded, reason);
        const auto raw = Read(fixture);
        Includes includes(fixture.parent_path());
        const D3D_SHADER_MACRO macros[] = {
            {"_INDEX", "PSIZE15"}, {"MME_MIPMAP", ""}, {nullptr, nullptr}};
        Com<ID3DBlob> expanded, errors;
        const auto hr = D3DPreprocess(raw.data(), raw.size(), fixture.string().c_str(), macros,
                                      &includes, &expanded.p, &errors.p);
        Check(SUCCEEDED(hr), errors.p ? static_cast<const char*>(errors.p->GetBufferPointer())
                                      : "D3DPreprocess failed");
        const std::string native(static_cast<const char*>(expanded.p->GetBufferPointer()),
                                 expanded.p->GetBufferSize());
        const auto nativeTokens = Tokens(native),
                   modernTokens = Tokens(definition->source.expanded);
        const auto mismatch = std::mismatch(nativeTokens.begin(), nativeTokens.end(),
                                            modernTokens.begin(), modernTokens.end());
        const auto at = std::size_t(mismatch.first - nativeTokens.begin());
        Check(nativeTokens == modernTokens,
              "FX9/DXC preprocessor token mismatch at " + std::to_string(at) +
                  ": native=" + nativeTokens.substr(at, 120) +
                  " modern=" + modernTokens.substr(std::min(at, modernTokens.size()), 120));
        struct Unload {
            HMODULE handle;
            ~Unload() {
                if (handle)
                    FreeLibrary(handle);
            }
        } compilerModule{nullptr}, runtimeModule{nullptr};
        const auto runtimePath = fs::u8path(argv[3]);
        // D3DX loads this compiler dynamically. An explicit oracle path must
        // work without an SDK installed elsewhere on the machine.
        const auto compilerPath = runtimePath.empty()
                                      ? fs::path(L"D3DCompiler_43.dll")
                                      : runtimePath.parent_path() / L"D3DCompiler_43.dll";
        Check(runtimePath.empty() || fs::is_regular_file(compilerPath),
              "D3DX oracle compiler missing: " + compilerPath.u8string());
        compilerModule.handle = LoadLibraryW(compilerPath.c_str());
        Check(compilerModule.handle != nullptr, "D3DX oracle compiler load failed");
        runtimeModule.handle =
            LoadLibraryW(runtimePath.empty() ? L"d3dx9_43.dll" : runtimePath.c_str());
        const auto runtime = runtimeModule.handle;
        Check(runtime != nullptr, "D3DX oracle runtime missing");
        // The constructor returns ID3DXEffectCompiler, derived from the same SDK
        // base interface as ID3DXEffect. Only base methods are called here.
        using Create = HRESULT(WINAPI*)(LPCSTR, UINT, const D3DXMACRO*, ID3DXInclude*, DWORD,
                                        ID3DXBaseEffect**, ID3DXBuffer**);
        const auto create =
            reinterpret_cast<Create>(GetProcAddress(runtime, "D3DXCreateEffectCompiler"));
        Check(create != nullptr, "native compiler export");
        Com<ID3DXBaseEffect> effect;
        Com<ID3DXBuffer> compilerErrors;
        const auto compiled = create(native.data(), UINT(native.size()), nullptr, nullptr, 0,
                                     &effect.p, &compilerErrors.p);
        std::ostringstream failure;
        failure << fixture.generic_u8string() << ": native effect compiler HRESULT=0x" << std::hex
                << static_cast<unsigned long>(compiled);
        if (compilerErrors.p)
            failure << ": " << static_cast<const char*>(compilerErrors.p->GetBufferPointer());
        Check(SUCCEEDED(compiled), failure.str());
        const auto& p = definition->metadata;
        D3DXEFFECT_DESC desc{};
        Check(SUCCEEDED(effect.p->GetDesc(&desc)) &&
                  desc.Parameters == p.variables.size() + p.textures.size() + p.samplers.size() &&
                  desc.Techniques == p.techniques.size(),
              "native global counts");
        for (const auto& v : p.variables) {
            const auto parameter = effect.p->GetParameterByName(nullptr, v.name.c_str());
            D3DXPARAMETER_DESC d{};
            Check(parameter && SUCCEEDED(effect.p->GetParameterDesc(parameter, &d)),
                  "native parameter: " + v.name);
            Check(v.rows == d.Rows && v.columns == d.Columns &&
                      v.elements == (d.Elements ? d.Elements : 1) && v.shared == bool(d.Flags & 1),
                  "native shape/shared: " + v.name);
            Check(v.semantic == (d.Semantic ? Upper(d.Semantic) : ""),
                  "native semantic: " + v.name);
            const auto type = v.type == "string" ? D3DXPT_STRING
                              : v.type == "bool" ? D3DXPT_BOOL
                              : v.type == "int"  ? D3DXPT_INT
                                                 : D3DXPT_FLOAT;
            Check(d.Type == type, "native parameter type: " + v.name);
            Annotations(effect.p, parameter, d.Annotations, v.annotations);
        }
        for (const auto& t : p.textures) {
            const auto parameter = effect.p->GetParameterByName(nullptr, t.name.c_str());
            D3DXPARAMETER_DESC d{};
            Check(parameter && SUCCEEDED(effect.p->GetParameterDesc(parameter, &d)),
                  "native texture");
            Check(t.semantic == (d.Semantic ? Upper(d.Semantic) : ""), "native target semantic");
            Check(d.Type == (t.type == "texture2D" ? D3DXPT_TEXTURE2D : D3DXPT_TEXTURE),
                  "native texture type identity");
            Annotations(effect.p, parameter, d.Annotations, t.annotations);
        }
        for (unsigned i = 0; i < p.techniques.size(); ++i) {
            const auto handle = effect.p->GetTechnique(i);
            D3DXTECHNIQUE_DESC t{};
            const auto& expected = p.techniques[i];
            Check(SUCCEEDED(effect.p->GetTechniqueDesc(handle, &t)) && expected.name == t.Name &&
                      expected.passes.size() == t.Passes,
                  "native technique order/count");
            Annotations(effect.p, handle, t.Annotations, expected.annotations);
            for (unsigned j = 0; j < t.Passes; ++j) {
                const auto pass = effect.p->GetPass(handle, j);
                D3DXPASS_DESC pd{};
                const auto& ep = p.passes[expected.passes[j]];
                Check(SUCCEEDED(effect.p->GetPassDesc(pass, &pd)) && ep.name == pd.Name,
                      "native pass order");
                Annotations(effect.p, pass, pd.Annotations, ep.annotations);
            }
        }
        if (std::string(relative) == "frontend/Inspect.fx") {
            float value = 0, tint[4]{};
            Check(SUCCEEDED(effect.p->GetFloat("Value", &value)) && value == 6,
                  "native macro default");
            Check(SUCCEEDED(effect.p->GetFloatArray("Tint", tint, 4)) && tint[0] == 0.25f &&
                      tint[3] == 1,
                  "native vector default");
            Check(p.variables[0].initializer == "{ 0.25 , 0.5 , 0.75 , 1 } " &&
                      p.variables[p.variables.size() - 2].initializer == "( ( 3 ) * ( 2 ) ) ",
                  "source default expressions");
        }
        Check(fs::current_path() == cwd, "native oracle cannot change cwd");
    }
    std::cout
        << "Independent FX9/DXC macro tokens and D3DX native metadata/default/script oracle passed (no device/window)\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
