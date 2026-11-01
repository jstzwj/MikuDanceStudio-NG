#include "allocation_counter.hpp"
#include "editor_window.hpp"
#include <QApplication>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace mikudancestudio;
using Clock = std::chrono::steady_clock;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::to_string(__LINE__) + ": " + #x + ": " + reason);        \
    } while (false)
double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
struct Sample {
    double update, prepare, scene, overlay, total;
    std::size_t allocations, bytes;
};
template <class F> double Percentile(const std::vector<Sample>& samples, F field, double p) {
    std::vector<double> values;
    for (const auto& sample : samples)
        values.push_back(double(field(sample)));
    std::sort(values.begin(), values.end());
    return values[std::size_t(std::ceil(p * values.size())) - 1];
}
int main(int argc, char** argv) try {
    QApplication app(argc, argv);
    CHECK(argc == 4);
    const auto input = qt::Path(QString::fromLocal8Bit(argv[1]));
    qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
    auto& session = window.Session();
    auto* viewport = window.RenderWindow();
    viewport->SetFrameProfiling(true);
    CHECK(session.ImportModel(input, reason));
    CHECK(session.SetBone(0, 0, {0, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(session.Register({{editor::Track::Bone, 0, 0, 0}}, reason));
    CHECK(session.SetBone(0, 0, {2, 0, 0}, {0, 0, 0, 1}, reason));
    CHECK(session.Register({{editor::Track::Bone, 0, 0, 120}}, reason));
    CHECK(session.Edit(
        "benchmark-playback",
        [](auto& d, auto&) {
            d.timeline.playStartFrame = 0;
            d.timeline.playStopFrame = 300;
            d.timeline.playbackLoop = d.timeline.startAtCurrent = 0;
            return true;
        },
        reason));
    CHECK(session.Seek(0, reason));
    window.Refresh();
    const auto coldStart = Clock::now();
    CHECK(viewport->PrepareFrame(640, 480, reason));
    const auto coldMs = Ms(coldStart, Clock::now());
    CHECK(rhi::ValidateRenderFrame(viewport->PreparedFrame()));
    const auto retained = viewport->PreparedFrame();
    std::vector<rhi::SnapshotBytes> retainedBytes;
    for (const auto& resource : retained.resources)
        retainedBytes.emplace_back(resource.bytes.begin(), resource.bytes.end());
    // Budgets are fixed before sampling. CI is a shared virtual-machine profile;
    // these bound CPU stalls, not physical GPU or 60fps release qualification.
    const bool ci = qEnvironmentVariableIsSet("CI");
    const double p95Budget = ci ? 100 : 33.333334, maxBudget = ci ? 500 : 100;
    std::ofstream report(std::filesystem::u8path(argv[3]));
    CHECK(report);
    report.imbue(std::locale::classic());
    report << std::setprecision(8) << "{\"schema\":1,\"profile\":\"" << (ci ? "ci" : "local")
           << "\",\"vertices\":" << session.Runtime().Snapshot(0)->vertices.size()
           << ",\"cold_prepare_ms\":" << coldMs << ",\"p95_budget_ms\":" << p95Budget
           << ",\"max_budget_ms\":" << maxBudget << ",\"cases\":[";
    bool withinBudget = true;
    for (unsigned mode = 0; mode < 5; ++mode) {
        const char* name = mode == 0   ? "static"
                           : mode == 1 ? "navigation"
                           : mode == 2 ? "animation"
                           : mode == 3 ? "paused_physics"
                                       : "bone_edit";
        if (mode == 2)
            CHECK(session.Play(reason));
        else if (mode == 3)
            session.Pause();
        if (mode == 4) {
            CHECK(session.SelectBones({0}, 0, reason));
            CHECK(session.BeginGesture());
        }
        unsigned editStep = 0;
        const auto step = [&] {
            if (mode == 1) {
                CHECK(session.DragView(false, 1, 0, false, false, reason));
                window.Refresh();
            } else if (mode == 4) {
                CHECK(
                    session.SetBone(0, 0, {float(++editStep) * .001f, 0, 0}, {0, 0, 0, 1}, reason));
                window.Refresh();
            } else
                window.Tick(mode >= 2 ? 1. / 60 : 0);
        };
        for (unsigned i = 0; i < 10; ++i) {
            step();
            CHECK(viewport->PrepareFrame(640, 480, reason));
        }
        std::vector<Sample> samples;
        samples.reserve(90);
        const auto updates = session.Runtime().Counters().frames;
        const auto editorUpdates = session.Runtime().Counters().editorUpdates;
        const auto cursor = session.CursorSeconds();
        const auto revision = session.Revision();
        const auto builds = viewport->FrameBuilds();
        for (unsigned i = 0; i < 90; ++i) {
            allocation_counter::Begin();
            const auto start = Clock::now();
            step();
            const auto update = Clock::now();
            CHECK(viewport->PrepareFrame(640, 480, reason));
            const auto end = Clock::now();
            allocation_counter::End();
            samples.push_back({Ms(start, update), Ms(update, end), viewport->CpuTimings().scene,
                               viewport->CpuTimings().overlay, Ms(start, end),
                               allocation_counter::count, allocation_counter::bytes});
        }
        CHECK(session.Runtime().Counters().frames - updates == (mode == 2 || mode == 3 ? 90 : 0));
        CHECK(session.Runtime().Counters().editorUpdates - editorUpdates == (mode == 4 ? 90 : 0));
        if (mode == 3)
            CHECK(session.CursorSeconds() == cursor && session.Revision() == revision);
        CHECK(viewport->FrameBuilds() - builds == (mode ? 90 : 0));
        CHECK(session.Playing() == (mode == 2));
        CHECK(viewport->Submissions() == 0 && !window.isVisible());
        if (!mode)
            for (const auto& sample : samples)
                CHECK(sample.allocations == 0 && sample.bytes == 0);
        CHECK(rhi::ValidateRenderFrame(viewport->PreparedFrame()));
        const double p95 = Percentile(samples, [](const auto& s) { return s.total; }, .95);
        const double maximum = Percentile(samples, [](const auto& s) { return s.total; }, 1);
        if (mode)
            report << ',';
        report << "{\"name\":\"" << name << "\",\"samples\":90,\"update_p95_ms\":"
               << Percentile(
                      samples, [](const auto& s) { return s.update; }, .95)
               << ",\"prepare_p95_ms\":"
               << Percentile(
                      samples, [](const auto& s) { return s.prepare; }, .95)
               << ",\"scene_p95_ms\":"
               << Percentile(
                      samples, [](const auto& s) { return s.scene; }, .95)
               << ",\"overlay_p95_ms\":"
               << Percentile(
                      samples, [](const auto& s) { return s.overlay; }, .95)
               << ",\"total_p95_ms\":" << p95 << ",\"total_max_ms\":" << maximum
               << ",\"cpp_allocations_p95\":"
               << Percentile(
                      samples, [](const auto& s) { return s.allocations; }, .95)
               << ",\"cpp_bytes_p95\":"
               << Percentile(samples, [](const auto& s) { return s.bytes; }, .95) << '}';
        std::cout << name << ": p95=" << p95 << "ms max=" << maximum << "ms\n";
        withinBudget = withinBudget && p95 <= p95Budget && maximum <= maxBudget;
        if (mode == 4)
            session.EndGesture();
    }
    for (std::size_t i = 0; i < retained.resources.size(); ++i)
        CHECK(retained.resources[i].bytes == retainedBytes[i]);
    session.Pause();
    CHECK(session.UnloadModel(0, reason));
    CHECK(viewport->PrepareFrame(641, 479, reason));
    CHECK(rhi::ValidateRenderFrame(viewport->PreparedFrame()));
    CHECK(viewport->Submissions() == 0);
    report << "],\"retained_frame_immutable\":true,\"gpu_submissions\":0}\n";
    CHECK(report.good());
    CHECK(withinBudget);
    return 0;
} catch (const std::exception& e) {
    allocation_counter::End();
    std::cerr << e.what() << '\n';
    return 1;
}
