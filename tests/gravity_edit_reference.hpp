#pragma once
#include <array>
// Captured by executing the original gravity initializer and its real hidden
// EDIT notifications. Values are float inputs, not decimal double literals.
struct GravityMirrorReference {
    float input;
    const char* text;
    float output;
};
inline constexpr std::array<GravityMirrorReference, 8> kGravityMirrorReference{
    {{.125f, "0.13", .13f},
     {.375f, "0.38", .38f},
     {.625f, "0.63", .63f},
     {.875f, "0.88", .88f},
     {-.125f, "-0.13", -.13f},
     {-.625f, "-0.63", -.63f},
     {.005f, "0.00", 0.f},
     {.015f, "0.01", .01f}}};
inline constexpr std::array<int, 9> kDialogWheelDeltas{{15, 15, 15, 15, 30, 60, -120, -30, 120}};
inline constexpr std::array<int, 9> kDialogWheelPositions{{0, 0, 0, 0, 0, -4, -4, 0, -4}};
template <class Document> void FillGravityCapacity(Document& d) {
    d.gravityKeys.dense[0].next = 1;
    d.gravityKeys.dense[0].selected = 1;
    d.gravityKeys.sparse.clear();
    d.gravityKeys.unused.clear();
    for (unsigned i = 1; i < 10000; ++i) {
        auto key = d.gravityKeys.dense[0];
        key.frame = i;
        key.previous = i - 1;
        key.next = i == 9999 ? 0 : i + 1;
        d.gravityKeys.sparse.push_back({int(i), 0, key});
    }
    d.cameraKeys.dense[0].selected = d.lightKeys.dense[0].selected =
        d.selfShadowKeys.dense[0].selected = 1;
    d.timeline.currentFrame = d.timeline.playStopFrame = 10000;
    d.timeline.lastRegisteredFrame = 9999;
}
