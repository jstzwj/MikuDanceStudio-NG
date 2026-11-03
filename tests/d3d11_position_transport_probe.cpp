// Explicit diagnostic, never a P4 acceptance substitute. Captures the unchanged
// production default model VS before viewport setup, interpolation and depth.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "mikudancestudio/rhi_model_shader_constants.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <iterator>
#include <cmath>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace mikudancestudio::rhi;
namespace {
void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::cerr << operation << ": 0x" << std::hex << unsigned(result) << '\n';
        throw std::runtime_error(operation);
    }
}
std::vector<char> Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read " + path.u8string());
    return {(std::istreambuf_iterator<char>(file)), {}};
}
std::vector<char> Capture(ID3D11Device& device, ID3D11DeviceContext& context,
                          const std::vector<char>& vertices, const std::vector<char>& constants,
                          const std::vector<char>& source, unsigned layout, unsigned topology) {
    const bool color = layout == unsigned(VertexLayout::PositionColor);
    const bool extra = layout >= unsigned(VertexLayout::PositionNormalUv1) &&
                       layout <= unsigned(VertexLayout::PositionNormalUv4);
    if ((!color && layout != unsigned(VertexLayout::PositionNormalUv) && !extra) || topology > 1)
        throw std::runtime_error("Not a default model vertex layout/topology");
    const UINT stride = color ? 16 : extra ? 32 + 16 * (layout - 3) : 32;
    if (vertices.empty() || vertices.size() % stride ||
        constants.size() != sizeof(ModelVertexConstants) || vertices.size() > 0x10000000)
        throw std::runtime_error("Invalid position probe input sizes");
    const UINT count = UINT(vertices.size() / stride);
    if (count % (topology ? 2 : 3))
        throw std::runtime_error("Position probe input is not a complete primitive list");
    const D3D_SHADER_MACRO macros[] = {{"MDS_MODEL_VERTEX_COLOR", color ? "1" : "0"},
                                       {"MDS_MODEL_EXTRA_UV", extra ? "1" : "0"},
                                       {nullptr, nullptr}};
    ComPtr<ID3DBlob> code, errors;
    const auto compiled = D3DCompile(source.data(), source.size(), nullptr, macros, nullptr, "main",
                                     "vs_5_0", D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &errors);
    if (FAILED(compiled) && errors)
        std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize());
    Check(compiled, "compile production VS");
    ComPtr<ID3D11VertexShader> vertex;
    Check(device.CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                    &vertex),
          "VS");
    const D3D11_SO_DECLARATION_ENTRY declaration{0, "SV_POSITION", 0, 0, 4, 0};
    const UINT outputStride = 16;
    ComPtr<ID3D11GeometryShader> stream;
    // D3D11 accepts unchanged VS bytecode for a pass-through SO shader.
    Check(device.CreateGeometryShaderWithStreamOutput(
              code->GetBufferPointer(), code->GetBufferSize(), &declaration, 1, &outputStride, 1,
              D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &stream),
          "SO shader");
    D3D11_INPUT_ELEMENT_DESC inputs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {color ? "COLOR" : "NORMAL", 0,
         color ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    ComPtr<ID3D11InputLayout> inputLayout;
    Check(device.CreateInputLayout(inputs,
                                   color   ? 2
                                   : extra ? 4
                                           : 3,
                                   code->GetBufferPointer(), code->GetBufferSize(), &inputLayout),
          "input layout");
    ComPtr<ID3D11Buffer> cb, vb, output, readback;
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = UINT(constants.size());
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{constants.data(), 0, 0};
    Check(device.CreateBuffer(&description, &initial, &cb), "constants");
    description.ByteWidth = UINT(vertices.size());
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    initial.pSysMem = vertices.data();
    Check(device.CreateBuffer(&description, &initial, &vb), "vertices");
    description.ByteWidth = count * outputStride;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_STREAM_OUTPUT;
    Check(device.CreateBuffer(&description, nullptr, &output), "stream output");
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(device.CreateBuffer(&description, nullptr, &readback), "readback");
    const UINT offset = 0;
    context.IASetInputLayout(inputLayout.Get());
    context.IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);
    context.IASetPrimitiveTopology(topology ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST
                                            : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.VSSetShader(vertex.Get(), nullptr, 0);
    context.VSSetConstantBuffers(0, 1, cb.GetAddressOf());
    context.GSSetShader(stream.Get(), nullptr, 0);
    context.SOSetTargets(1, output.GetAddressOf(), &offset);
    const D3D11_QUERY_DESC queryDescription{D3D11_QUERY_SO_STATISTICS, 0};
    ComPtr<ID3D11Query> statistics;
    Check(device.CreateQuery(&queryDescription, &statistics), "SO statistics query");
    context.Begin(statistics.Get());
    context.Draw(count, 0);
    context.End(statistics.Get());
    context.SOSetTargets(0, nullptr, nullptr);
    context.CopyResource(readback.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context.Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "map");
    const auto* bytes = static_cast<const char*>(mapped.pData);
    std::vector<char> result(bytes, bytes + count * outputStride);
    context.Unmap(readback.Get(), 0);
    D3D11_QUERY_DATA_SO_STATISTICS observed{};
    const auto available = context.GetData(statistics.Get(), &observed, sizeof(observed), 0);
    if (available != S_OK || observed.NumPrimitivesWritten != count / (topology ? 2 : 3) ||
        observed.PrimitivesStorageNeeded != observed.NumPrimitivesWritten)
        throw std::runtime_error("Incomplete stream-output position capture");
    return result;
}
}
int main(int argc, char** argv) try {
    if (argc != 3)
        throw std::runtime_error(
            "usage: d3d11_position_transport_probe input-positions.tsv output-directory");
    const auto manifest = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    const auto output = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    std::filesystem::create_directories(output);
    const auto source = Read(std::filesystem::u8path(MDS_MODEL_VERTEX_SOURCE));
    ComPtr<ID3D11Device> devices[2];
    ComPtr<ID3D11DeviceContext> contexts[2];
    for (unsigned driver = 0; driver < 2; ++driver) {
        D3D_FEATURE_LEVEL level{};
        Check(D3D11CreateDevice(nullptr, driver ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
                                nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &devices[driver], &level,
                                &contexts[driver]),
              "device");
        if (level < D3D_FEATURE_LEVEL_11_0)
            throw std::runtime_error("Feature level 11 required");
    }
    std::ifstream input(manifest);
    if (!input)
        throw std::runtime_error("Cannot read probe manifest");
    std::ofstream report(output / "report.tsv");
    std::ofstream adapters(output / "adapters.tsv");
    adapters << "driver\tvendor_id\tdevice_id\tdescription\n";
    for (unsigned driver = 0; driver < 2; ++driver) {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        Check(devices[driver].As(&dxgi), "DXGI device");
        Check(dxgi->GetAdapter(&adapter), "DXGI adapter");
        DXGI_ADAPTER_DESC description{};
        Check(adapter->GetDesc(&description), "DXGI description");
        char name[512]{};
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description.Description, -1, name,
                                 sizeof(name), nullptr, nullptr))
            throw std::runtime_error("Cannot encode adapter name");
        adapters << (driver ? "warp" : "hardware") << '\t' << description.VendorId << '\t'
                 << description.DeviceId << '\t' << name << '\n';
    }
    adapters.flush();
    if (!adapters)
        throw std::runtime_error("Cannot write adapter identities");
    report << "draw\tvertices\tchanged_components\n";
    unsigned cases = 0, failures = 0;
    std::string name;
    unsigned layout, topology;
    while (input >> name >> layout >> topology) {
        if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos)
            throw std::runtime_error("Invalid probe input basename");
        const auto vertices = Read(manifest.parent_path() / (name + ".vertices"));
        const auto constants = Read(manifest.parent_path() / (name + ".constants"));
        std::vector<char> captured[2];
        for (unsigned driver = 0; driver < 2; ++driver) {
            captured[driver] = Capture(*devices[driver].Get(), *contexts[driver].Get(), vertices,
                                       constants, source, layout, topology);
            const auto repeat = Capture(*devices[driver].Get(), *contexts[driver].Get(), vertices,
                                        constants, source, layout, topology);
            if (captured[driver] != repeat)
                throw std::runtime_error("Nonrepeatable position capture");
            for (std::size_t at = 0; at < captured[driver].size(); at += sizeof(float)) {
                float value;
                std::memcpy(&value, captured[driver].data() + at, sizeof(value));
                if (!std::isfinite(value))
                    throw std::runtime_error("Nonfinite position output");
            }
            std::ofstream file(output / (name + (driver ? "-warp.f32" : "-hardware.f32")),
                               std::ios::binary);
            file.write(captured[driver].data(), captured[driver].size());
            if (!file)
                throw std::runtime_error("Cannot write position output");
        }
        unsigned changed = 0;
        for (std::size_t at = 0; at < captured[0].size(); at += sizeof(float))
            changed +=
                std::memcmp(captured[0].data() + at, captured[1].data() + at, sizeof(float)) != 0;
        report << name << '\t' << captured[0].size() / 16 << '\t' << changed << '\n';
        std::cout << name << " vertices=" << captured[0].size() / 16 << " changed=" << changed
                  << '\n';
        ++cases;
        failures += changed != 0;
    }
    report.flush();
    if (!input.eof() || !cases || !report)
        throw std::runtime_error("Incomplete position probe manifest/report");
    return failures ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
