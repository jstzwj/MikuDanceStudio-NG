#include <Windows.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/bone_key_interpolation.hpp"
#include "bone_key_interpolation_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
static void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
int main(int argc, char** argv) try {
    Check(argc == 2, "provide original x64 executable");
    OriginalPhysicsModule original(argv[1]);
    auto model = std::make_unique<mikudancestudio::mdl::ModelRecord>();
    model->boneCount = 2;
    std::uint64_t hash = 14695981039346656037ull;
    unsigned cases = 0;
    bone_curve_fixture::Visit([&](auto expected, auto index, auto lane) {
        auto actual = expected;
        model->boneKeys = expected.data();
        original.Function<void (*)(mikudancestudio::mdl::ModelRecord*, int, int)>(0xc9070)(
            model.get(), int(index), int(lane));
        mikudancestudio::keyframe_edit::RebuildBoneCurve([&](auto i) -> auto& { return actual[i]; },
                                                         2, index, lane);
        Check(!std::memcmp(expected.data(), actual.data(), sizeof expected),
              "original automatic interpolation table differs");
        bone_curve_fixture::Hash(hash, expected);
        ++cases;
    });
    std::printf("Original automatic interpolation: %u complete tables, hash 0x%016llx; no UI/GPU\n",
                cases, static_cast<unsigned long long>(hash));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
