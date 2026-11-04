#include "mikudancestudio/accessory_resource.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <cmath>
using namespace mikudancestudio::runtime;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " reason=" << reason << '\n';        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
template <class T> void Put(std::string& bytes, T value) {
    bytes.append(reinterpret_cast<const char*>(&value), sizeof value);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    for (const char* filename :
         {"bound.x", "bound-varied.x", "bound-tzip.x", "bound-tzip-multi.x"}) {
        std::ifstream file(std::filesystem::path(argv[1]) / filename, std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(file), {}};
        std::shared_ptr<const AccessoryResource> resource;
        CHECK(AccessoryResource::Parse(bytes.data(), bytes.size(), resource, reason));
        CHECK(resource->Vertices().size() == 4 && resource->Indices().size() == 6);
        CHECK(resource->Materials().size() == 2 && resource->Subsets().size() == 2);
        CHECK(resource->Materials()[0].texture == "checker.bmp");
        CHECK(resource->Materials()[0].diffuse[3] == .8f &&
              resource->Materials()[0].ambient[0] == .7f);
        const auto saved = resource;
        CHECK(!AccessoryResource::Parse(bytes.data(), bytes.size() - 3, resource, reason) &&
              resource == saved);
        if (bytes.substr(8, 4) == "tzip") {
            for (std::size_t cut = 16; cut < bytes.size(); ++cut)
                CHECK(!AccessoryResource::Parse(bytes.data(), cut, resource, reason) &&
                      resource == saved);
            bytes[16] = 0;
            bytes[17] = 0;
            bytes[18] = 0;
            bytes[19] = 0;
            CHECK(!AccessoryResource::Parse(bytes.data(), bytes.size(), resource, reason) &&
                  resource == saved && reason == "x-decompressed-size-limit");
        }
    }
    // Standard binary tokens with integer/float lists, independent of text parsing.
    std::string binary = "xof 0303bin 0032";
    const auto word = [&](const char* s) {
        Put<std::uint16_t>(binary, 1);
        Put<std::uint32_t>(binary, std::uint32_t(std::strlen(s)));
        binary += s;
    };
    const auto open = [&](const char* s) {
        word(s);
        Put<std::uint16_t>(binary, 10);
    };
    const auto close = [&] { Put<std::uint16_t>(binary, 11); };
    const auto ints = [&](std::initializer_list<unsigned> values) {
        Put<std::uint16_t>(binary, 6);
        Put<std::uint32_t>(binary, unsigned(values.size()));
        for (auto v : values)
            Put<std::uint32_t>(binary, v);
    };
    const auto floats = [&](std::initializer_list<float> values) {
        Put<std::uint16_t>(binary, 7);
        Put<std::uint32_t>(binary, unsigned(values.size()));
        for (auto v : values)
            Put<float>(binary, v);
    };
    open("Mesh");
    ints({3});
    floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
    ints({1, 3, 0, 1, 2});
    open("MeshNormals");
    ints({1});
    floats({0, 0, 1});
    ints({1, 3, 0, 0, 0});
    close();
    open("MeshMaterialList");
    ints({1, 1, 0});
    open("Material");
    floats({1, 1, 1, 1, 4, 0, 0, 0, 0, 0, 0});
    close();
    close();
    close();
    std::shared_ptr<const AccessoryResource> resource;
    CHECK(AccessoryResource::Parse(binary.data(), binary.size(), resource, reason));
    CHECK(resource->Vertices().size() == 3 && resource->Indices().size() == 3 &&
          resource->Vertices()[2].normal[2] == 1);
    // Frozen D3DX9_43 x64 root identity-coordinate transform observations.
    CHECK(resource->Vertices()[1].position[0] == 0x1.fffffep-1f &&
          resource->Vertices()[2].position[1] == 0x1.fffffep-1f);
    const std::string polygons = R"(xof 0303txt 0032
Mesh {
 5; 0;0;0;, 2;0;0;, 3;1;0;, 2;2;0;, 0;2;0;;
 2; 4;0,1,3,4;, 5;0,1,2,3,4;;
 MeshNormals { 2; 0;0;1;, 0;0;-1;; 2; 4;0,0,0,0;, 5;1,1,1,1,1;; }
 MeshTextureCoords { 5; 0;0;, 1;0;, 1;.5;, 1;1;, 0;1;; }
 MeshMaterialList { 2;2;1,0;;
   Material { 1;0;0;1;; 0; 0;0;0;; 0;0;0;; }
   Material { 0;1;0;.5;; 0; 0;0;0;; 0;0;0;; }
 }
})";
    CHECK(AccessoryResource::Parse(polygons.data(), polygons.size(), resource, reason));
    CHECK(resource->Vertices().size() == 9 && resource->Indices().size() == 15);
    CHECK(resource->Subsets().size() == 2 && resource->Subsets()[0].indexCount == 9 &&
          resource->Subsets()[1].indexCount == 6);
    for (const auto& subset : resource->Subsets())
        for (unsigned i = subset.firstIndex; i < subset.firstIndex + subset.indexCount; ++i) {
            const auto& vertex = resource->Vertices()[resource->Indices()[i]];
            CHECK(vertex.normal[2] == (subset.material == 0 ? -1 : 1));
        }
    const auto savedPolygon = resource;
    auto badPolygon = polygons;
    badPolygon.replace(badPolygon.find("5;1,1,1,1,1"), 11, "4;1,1,1,1");
    CHECK(!AccessoryResource::Parse(badPolygon.data(), badPolygon.size(), resource, reason) &&
          resource == savedPolygon && reason == "x-normal-face-corners");
    // Unequal corner angles, not face areas or triangle counts, weight the
    // generated smooth normal. Preserve explicit FVF-274 normals unless the
    // native loader would clone the mesh (e.g. to drop vertex colors).
    const std::string smooth = R"(xof 0303txt 0032
Mesh {4;0;0;0;,2;0;0;,0;1;0;,1;0;1;;2;3;0,1,2;,3;0,3,1;;
 MeshTextureCoords {4;0;0;,1;0;,0;1;,1;1;;}
 MeshMaterialList {1;2;0,0;; Material {1;1;1;1;;0;0;0;0;;0;0;0;;}}
})";
    const auto checkSmooth = [&] {
        for (const auto& vertex : resource->Vertices())
            if (vertex.position == mikudancestudio::cpu_math::Vec3{})
                return std::abs(vertex.normal[0]) < 1e-6f &&
                       std::abs(vertex.normal[1] - 1 / std::sqrt(5.f)) < 1e-6f &&
                       std::abs(vertex.normal[2] - 2 / std::sqrt(5.f)) < 1e-6f;
        return false;
    };
    CHECK(AccessoryResource::Parse(smooth.data(), smooth.size(), resource, reason) &&
          checkSmooth());
    auto explicitNormals = smooth;
    explicitNormals.insert(explicitNormals.rfind('}'),
                           "MeshNormals {1;1;0;0;;2;3;0,0,0;,3;0,0,0;;}");
    CHECK(
        AccessoryResource::Parse(explicitNormals.data(), explicitNormals.size(), resource, reason));
    for (const auto& vertex : resource->Vertices())
        CHECK(vertex.normal == (mikudancestudio::cpu_math::Vec3{1, 0, 0}));
    explicitNormals.insert(explicitNormals.rfind('}'), "MeshVertexColors {1;0;1;0;0;1;;}");
    CHECK(AccessoryResource::Parse(explicitNormals.data(), explicitNormals.size(), resource,
                                   reason) &&
          checkSmooth());
    std::ifstream compressed(std::filesystem::path(argv[1]) / "triangle-bzip.x", std::ios::binary);
    std::string zipped{std::istreambuf_iterator<char>(compressed), {}};
    CHECK(AccessoryResource::Parse(zipped.data(), zipped.size(), resource, reason));
    CHECK(resource->Vertices().size() == 3 && resource->Indices().size() == 3);
    zipped[24] = 'X';
    CHECK(!AccessoryResource::Parse(zipped.data(), zipped.size(), resource, reason) &&
          reason == "x-invalid-mszip-magic");
    std::cout
        << "portable static X text/binary/MSZIP import, material groups, bounds and transaction passed\n";
}
