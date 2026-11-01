#include "mikudancestudio/model_runtime.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <new>
#include <limits>
#ifdef _MSC_VER
#include <malloc.h>
#endif
using namespace mikudancestudio;
namespace {
bool counting = false;
std::size_t allocations = 0, allocatedBytes = 0;
void Count(std::size_t n) {
    if (counting) {
        ++allocations;
        allocatedBytes += n;
    }
}
}
void* operator new(std::size_t n) {
    Count(n);
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void* operator new(std::size_t n, std::align_val_t alignment) {
    Count(n);
    const auto a = std::size_t(alignment);
#ifdef _MSC_VER
    void* p = _aligned_malloc(n ? n : a, a);
#else
    void* p = std::aligned_alloc(a, ((std::max(n, a) + a - 1) / a) * a);
#endif
    if (p)
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            counting = false;                                                                      \
            std::cerr << "line " << __LINE__ << ": " << #x << " " << reason << '\n';               \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int Read(void* context, void* target, std::size_t n) {
    auto& file = *static_cast<std::istream*>(context);
    file.read(static_cast<char*>(target), std::streamsize(n));
    return int(file.gcount());
}
format::PmxText Name(const std::string& value) {
    format::PmxText result;
    for (char c : value) {
        result.push_back(c);
        result.push_back(0);
    }
    return result;
}
format::ModelDocument Generated() {
    format::ModelDocument document;
    document.kind = format::ModelKind::Pmx;
    auto& m = document.pmx;
    m.header.version = 2;
    m.header.additionalUvCount = 1;
    m.bones.resize(4);
    for (unsigned i = 0; i < 4; ++i) {
        m.bones[i].names[0] = Name("bone" + std::to_string(i));
        m.bones[i].fields.layer = int(i);
    }
    m.materials.resize(2);
    for (auto& material : m.materials) {
        material.fields.diffuse[0] = .7f;
        material.fields.diffuse[3] = 1;
    }
    m.vertices.resize(4096);
    for (unsigned i = 0; i < m.vertices.size(); ++i) {
        auto& v = m.vertices[i];
        v.weightType = std::uint8_t(i % 4);
        v.position[0] = float(i % 128) * .1f;
        v.position[1] = float(i / 128) * .1f;
        v.normal[2] = 1;
        for (unsigned b = 0; b < 4; ++b) {
            v.bone[b] = b;
            v.weight[b] = .25f;
        }
        if (v.weightType == 1 || v.weightType == 3)
            v.weight[0] = .4f;
    }
    m.morphs.resize(3);
    for (unsigned i = 0; i < 3; ++i) {
        auto& morph = m.morphs[i];
        morph.names[0] = Name("morph" + std::to_string(i));
        morph.header.type = std::uint8_t(i == 0 ? 1 : i == 1 ? 4 : 8);
        morph.offsets.resize(1);
        morph.offsets[0].index = 0;
        morph.offsets[0].values[0] = .3f;
        morph.offsets[0].operation = 1;
    }
    return document;
}
bool Same(const runtime::ModelSnapshot& a, const runtime::ModelSnapshot& b) {
    if (a.frame != b.frame || a.morphWeights != b.morphWeights ||
        a.vertices.size() != b.vertices.size() || a.bones.size() != b.bones.size())
        return false;
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        const auto& x = a.vertices[i];
        const auto& y = b.vertices[i];
        if (x.position != y.position || x.normal != y.normal || x.uv != y.uv ||
            x.additionalUv != y.additionalUv)
            return false;
    }
    for (std::size_t i = 0; i < a.bones.size(); ++i)
        if (a.bones[i].local != b.bones[i].local || a.bones[i].skin != b.bones[i].skin ||
            a.bones[i].world != b.bones[i].world)
            return false;
    for (std::size_t i = 0; i < a.materials.size(); ++i) {
        const auto& x = a.materials[i];
        const auto& y = b.materials[i];
        if (x.textureTint != y.textureTint || x.textureAdd != y.textureAdd ||
            x.sphereTint != y.sphereTint || x.sphereAdd != y.sphereAdd ||
            x.toonTint != y.toonTint || x.toonAdd != y.toonAdd)
            return false;
        for (unsigned c = 0; c < 4; ++c)
            if (x.material.diffuse[c] != y.material.diffuse[c])
                return false;
    }
    return true;
}
int main(int argc, char** argv) {
    auto document = Generated();
    if (argc == 2) {
        std::ifstream input(std::filesystem::u8path(argv[1]), std::ios::binary);
        CHECK(input && format::ReadModelDocument(Read, &input, document));
    }
    std::shared_ptr<const runtime::ModelResource> resource;
    CHECK(runtime::ModelResource::Create(document, resource, reason));
    runtime::ModelInstance instance(resource), reference(resource);
    runtime::ModelEvaluation scratch;
    std::vector<format::PmmEvaluatedBone> bones(resource->BoneNames().size());
    std::vector<float> morphs(resource->MorphNames().size());
    for (auto& bone : bones)
        bone.rotation[3] = 1;
    double reuseSeconds = 0, freshSeconds = 0;
    std::size_t freshAllocations = 0, freshBytes = 0;
    for (unsigned frame = 0; frame < 122; ++frame) {
        if (!bones.empty())
            bones[0].translation[0] = float(frame % 7) * .2f;
        for (auto& weight : morphs)
            weight = float(frame % 4) * .125f;
        counting = frame >= 2;
        const auto begin = std::chrono::steady_clock::now();
        CHECK(instance.PreparePoseReusing(frame, bones, morphs, scratch, reason));
        CHECK(scratch.Solve(false, nullptr, 0, reason) && scratch.Solve(true, nullptr, 0, reason));
        CHECK(scratch.Finish(reason) && instance.Publish(std::move(scratch), reason));
        const auto end = std::chrono::steady_clock::now();
        counting = false;
        CHECK(allocations == 0 && allocatedBytes == 0);
        runtime::ModelEvaluation fresh;
        counting = frame >= 2;
        const auto freshBegin = std::chrono::steady_clock::now();
        CHECK(reference.PreparePose(frame, bones, morphs, fresh, reason));
        CHECK(fresh.Solve(false, nullptr, 0, reason) && fresh.Solve(true, nullptr, 0, reason));
        CHECK(fresh.Finish(reason) && reference.Publish(std::move(fresh), reason));
        const auto freshEnd = std::chrono::steady_clock::now();
        counting = false;
        freshAllocations += allocations;
        freshBytes += allocatedBytes;
        allocations = allocatedBytes = 0;
        if (frame >= 2) {
            reuseSeconds += std::chrono::duration<double>(end - begin).count();
            freshSeconds += std::chrono::duration<double>(freshEnd - freshBegin).count();
        }
        CHECK(Same(instance.Snapshot(), reference.Snapshot()));
    }
    const auto saved = instance.Snapshot();
    CHECK(!instance.Publish(std::move(scratch), reason));
    if (!bones.empty()) {
        bones[0].translation[0] = std::numeric_limits<float>::quiet_NaN();
        CHECK(!instance.PreparePoseReusing(123, bones, morphs, scratch, reason));
        CHECK(!scratch.Solve(false, nullptr, 0, reason));
        CHECK(Same(instance.Snapshot(), saved));
        bones[0].translation[0] = 0;
        CHECK(instance.PreparePoseReusing(124, bones, morphs, scratch, reason));
        CHECK(scratch.Solve(false, nullptr, 0, reason) && scratch.Solve(true, nullptr, 0, reason));
        CHECK(scratch.Finish(reason) && instance.Publish(std::move(scratch), reason));
    }
    std::cout << "CPU evaluation reuse: 120 exact frames, vertices="
              << resource->Document().vertices.size() << " reuse_ms=" << reuseSeconds * 1000 / 120
              << " fresh_ms=" << freshSeconds * 1000 / 120
              << " reuse_allocations=0 fresh_allocations=" << freshAllocations
              << " fresh_bytes=" << freshBytes << '\n';
}
