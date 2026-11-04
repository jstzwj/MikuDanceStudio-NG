// Explicit, never-shown D3DX import comparison; no Present or desktop input.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3dx9.h>
#include <wrl/client.h>
#include "mikudancestudio/accessory_resource.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using mikudancestudio::runtime::AccessoryResource;
void Check(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("DX9 HRESULT=" + std::to_string(unsigned(result)));
}
// D3DXComputeNormals operates on the documented CPU mesh interface. This
// independent buffer implementation permits normal qualification even when a
// disconnected Windows session cannot create a DX9 device. No vtable is patched.
template <class Interface, class Description> struct CpuBuffer final : Interface {
    Description description{};
    void* bytes = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output)
            return E_POINTER;
        *output = iid == __uuidof(IUnknown) || iid == __uuidof(Interface) ? this : nullptr;
        return *output ? S_OK : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return description.Type; }
    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT count, void** output, DWORD) override {
        if (!output || offset > description.Size || count > description.Size - offset)
            return D3DERR_INVALIDCALL;
        *output = static_cast<unsigned char*>(bytes) + offset;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDesc(Description* output) override {
        if (!output)
            return E_POINTER;
        *output = description;
        return S_OK;
    }
};
struct CpuMesh final : ID3DXMesh {
    std::vector<mikudancestudio::runtime::AccessoryVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DWORD> attributes;
    CpuBuffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC> vertexBuffer;
    CpuBuffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC> indexBuffer;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value)
            return E_POINTER;
        // D3DXComputeNormals queries the documented ID3DXMesh interface.
        const GUID meshId{
            0x4020e5c2, 0x1403, 0x4929, {0x88, 0x3f, 0xe2, 0xe8, 0x49, 0xfa, 0xc1, 0x95}};
        *value = iid == __uuidof(IUnknown) || iid == meshId ? this : nullptr;
        return *value ? S_OK : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE DrawSubset(DWORD) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    DWORD STDMETHODCALLTYPE GetNumFaces() override { return DWORD(indices.size() / 3); }
    DWORD STDMETHODCALLTYPE GetNumVertices() override { return DWORD(vertices.size()); }
    DWORD STDMETHODCALLTYPE GetFVF() override { return 274; }
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* output) override {
        const D3DVERTEXELEMENT9 declaration[]{
            {0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0},
            {0, 24, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()};
        std::memcpy(output, declaration, sizeof(declaration));
        return S_OK;
    }
    DWORD STDMETHODCALLTYPE GetNumBytesPerVertex() override { return sizeof(vertices[0]); }
    DWORD STDMETHODCALLTYPE GetOptions() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CloneMeshFVF(DWORD, DWORD, IDirect3DDevice9*, ID3DXMesh**) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CloneMesh(DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*,
                                        ID3DXMesh**) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetVertexBuffer(IDirect3DVertexBuffer9** output) override {
        vertexBuffer.bytes = vertices.data();
        vertexBuffer.description = {D3DFMT_VERTEXDATA,
                                    D3DRTYPE_VERTEXBUFFER,
                                    0,
                                    D3DPOOL_SYSTEMMEM,
                                    DWORD(vertices.size() * sizeof(vertices[0])),
                                    274};
        *output = &vertexBuffer;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIndexBuffer(IDirect3DIndexBuffer9** output) override {
        indexBuffer.bytes = indices.data();
        indexBuffer.description = {D3DFMT_INDEX32, D3DRTYPE_INDEXBUFFER, 0, D3DPOOL_SYSTEMMEM,
                                   DWORD(indices.size() * sizeof(indices[0]))};
        *output = &indexBuffer;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE LockVertexBuffer(DWORD, void** output) override {
        *output = vertices.data();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockVertexBuffer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LockIndexBuffer(DWORD, void** output) override {
        *output = indices.data();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockIndexBuffer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetAttributeTable(D3DXATTRIBUTERANGE* table, DWORD* count) override {
        if (!count || (table && !*count))
            return D3DERR_INVALIDCALL;
        if (table)
            *table = {0, 0, GetNumFaces(), 0, GetNumVertices()};
        *count = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConvertPointRepsToAdjacency(const DWORD* points,
                                                          DWORD* output) override {
        if (!output)
            return D3DERR_INVALIDCALL;
        std::fill(output, output + indices.size(), UINT32_MAX);
        std::map<std::pair<DWORD, DWORD>, DWORD> edges;
        for (DWORD i = 0; i < indices.size(); ++i) {
            const auto a = indices[i], b = indices[i / 3 * 3 + (i % 3 + 1) % 3];
            const auto edge = std::make_pair(points ? points[a] : a, points ? points[b] : b);
            const auto other = edges.find({edge.second, edge.first});
            if (other == edges.end())
                edges.emplace(edge, i);
            else {
                output[i] = other->second / 3;
                output[other->second] = i / 3;
                edges.erase(other);
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConvertAdjacencyToPointReps(const DWORD*, DWORD*) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GenerateAdjacency(FLOAT, DWORD*) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE UpdateSemantics(D3DVERTEXELEMENT9*) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE LockAttributeBuffer(DWORD, DWORD** output) override {
        attributes.resize(GetNumFaces());
        *output = attributes.data();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockAttributeBuffer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Optimize(DWORD, const DWORD*, DWORD*, DWORD*, ID3DXBuffer**,
                                       ID3DXMesh**) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE OptimizeInplace(DWORD, const DWORD*, DWORD*, DWORD*,
                                              ID3DXBuffer**) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetAttributeTable(const D3DXATTRIBUTERANGE*, DWORD) override {
        std::fprintf(stderr, "unsupported CPU mesh call: %s\n", __FUNCTION__);
        return E_NOTIMPL;
    }
};
int Normals(const char* directory) {
    const auto dll = LoadLibraryW(L"d3dx9_43.dll");
    const auto compute = reinterpret_cast<decltype(&D3DXComputeNormals)>(
        dll ? GetProcAddress(dll, "D3DXComputeNormals") : nullptr);
    if (!compute)
        return 77;
    unsigned cases = 0, mismatches = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(std::filesystem::u8path(directory))) {
        if (entry.path().extension() != ".x")
            continue;
        std::ifstream file(entry.path(), std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(file), {}};
        std::shared_ptr<const AccessoryResource> resource;
        std::string reason;
        if (!AccessoryResource::Parse(bytes.data(), bytes.size(), resource, reason)) {
            std::fprintf(stderr, "%s: %s\n", entry.path().filename().u8string().c_str(),
                         reason.c_str());
            ++mismatches;
            continue;
        }
        CpuMesh mesh;
        mesh.vertices = resource->Vertices();
        mesh.indices = resource->Indices();
        for (auto& vertex : mesh.vertices)
            vertex.normal = {};
        Check(compute(&mesh, nullptr));
        float maximum = 0;
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
            for (unsigned c = 0; c < 3; ++c)
                maximum = std::max(maximum, std::abs(mesh.vertices[i].normal[c] -
                                                     resource->Vertices()[i].normal[c]));
        std::printf("%s normals=%zu max=%g\n", entry.path().filename().u8string().c_str(),
                    mesh.vertices.size(), double(maximum));
        mismatches += maximum > 1e-5f;
        ++cases;
    }
    FreeLibrary(dll);
    return !cases || mismatches ? 1 : 0;
}
int main(int argc, char** argv) try {
    if (argc == 3 && std::string(argv[1]) == "--normals-only")
        return Normals(argv[2]);
    if (argc != 2)
        return 2;
    struct Window {
        HWND handle = CreateWindowExW(0, L"STATIC", L"Accessory import reference", 0, 0, 0, 1, 1,
                                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Window() { DestroyWindow(handle); }
    } window;
    ComPtr<IDirect3D9> api;
    api.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    ComPtr<IDirect3DDevice9> device;
    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.BackBufferWidth = parameters.BackBufferHeight = 1;
    parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
    parameters.hDeviceWindow = window.handle;
    if (!api || !window.handle)
        return 77;
    const auto created = api->CreateDevice(
        0, D3DDEVTYPE_HAL, window.handle,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &parameters, &device);
    if (FAILED(created)) {
        std::fprintf(stderr, "hidden DX9 device unavailable: 0x%08lx\n", created);
        return 77;
    }
    struct Runtime {
        HMODULE dll = LoadLibraryW(L"d3dx9_43.dll");
        ~Runtime() {
            if (dll)
                FreeLibrary(dll);
        }
    } runtime;
    const auto load = reinterpret_cast<decltype(&D3DXLoadMeshFromXW)>(
        GetProcAddress(runtime.dll, "D3DXLoadMeshFromXW"));
    const auto compute = reinterpret_cast<decltype(&D3DXComputeNormals)>(
        GetProcAddress(runtime.dll, "D3DXComputeNormals"));
    if (!load || !compute)
        return 77;
    unsigned cases = 0, mismatches = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(std::filesystem::u8path(argv[1]))) {
        if (entry.path().extension() != ".x")
            continue;
        std::ifstream file(entry.path(), std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(file), {}};
        std::shared_ptr<const AccessoryResource> portable;
        std::string reason;
        if (!AccessoryResource::Parse(bytes.data(), bytes.size(), portable, reason)) {
            std::fprintf(stderr, "%s: %s\n", entry.path().filename().u8string().c_str(),
                         reason.c_str());
            ++mismatches;
            continue;
        }
        ComPtr<ID3DXMesh> mesh;
        DWORD materialCount = 0;
        Check(load(entry.path().c_str(), 0x110, device.Get(), nullptr, nullptr, nullptr,
                   &materialCount, &mesh));
        const auto originalFvf = mesh->GetFVF();
        if (originalFvf != 274) {
            ComPtr<ID3DXMesh> clone;
            Check(mesh->CloneMeshFVF(mesh->GetOptions(), 274, device.Get(), &clone));
            mesh = std::move(clone);
            Check(compute(mesh.Get(), nullptr));
        }
        struct Vertex {
            float position[3], normal[3], uv[2];
        };
        Vertex* vertices = nullptr;
        Check(mesh->LockVertexBuffer(D3DLOCK_READONLY, reinterpret_cast<void**>(&vertices)));
        // Match corners by position/UV without requiring D3DX's vertex ordering.
        float maximum = 0, maximumPosition = 0, maximumNormal = 0;
        for (const auto& vertex : portable->Vertices()) {
            float best = INFINITY, bestPosition = 0, bestNormal = 0;
            for (unsigned i = 0; i < mesh->GetNumVertices(); ++i) {
                float difference = 0, position = 0, normal = 0;
                for (unsigned c = 0; c < 3; ++c) {
                    const auto p = std::abs(vertices[i].position[c] - vertex.position[c]);
                    position = std::max(position, p);
                    difference =
                        std::max(difference, p / std::max(1.f, std::abs(vertex.position[c])));
                    normal = std::max(normal, std::abs(vertices[i].normal[c] - vertex.normal[c]));
                }
                for (unsigned c = 0; c < 2; ++c)
                    difference = std::max(difference, std::abs(vertices[i].uv[c] - vertex.uv[c]));
                difference = std::max(difference, normal);
                if (difference < best) {
                    best = difference;
                    bestPosition = position;
                    bestNormal = normal;
                }
            }
            maximum = std::max(maximum, best);
            maximumPosition = std::max(maximumPosition, bestPosition);
            maximumNormal = std::max(maximumNormal, bestNormal);
        }
        Check(mesh->UnlockVertexBuffer());
        const bool equal = maximum <= 2e-5f &&
                           portable->Indices().size() == mesh->GetNumFaces() * 3 &&
                           portable->Materials().size() == materialCount;
        std::printf(
            "%s fvf=%lu vertices=%zu/%lu triangles=%zu/%lu max=%g position=%g normal=%g accepted=%d\n",
            entry.path().filename().u8string().c_str(), originalFvf, portable->Vertices().size(),
            mesh->GetNumVertices(), portable->Indices().size() / 3, mesh->GetNumFaces(),
            double(maximum), double(maximumPosition), double(maximumNormal), equal);
        mismatches += !equal;
        ++cases;
    }
    return !cases || mismatches ? 1 : 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
