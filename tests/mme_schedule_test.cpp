#include "mme_schedule_fixture.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
using namespace mme_test;
using namespace mikudancestudio::rhi;
void NullReplay(const RenderFrame& graph) {
    const auto valid = ValidateRenderFrame(graph);
    Check(bool(valid), valid.reason);
    auto device = CreateNullDevice();
    std::map<std::uint64_t, Handle> handles;
    for (const auto& target : graph.targets) {
        auto h = device->CreateRenderTexture(target.description);
        Check(bool(h), "Null target allocation");
        handles[target.id] = h;
    }
    Check(device->BeginFrame({}), "Null begin");
    for (const auto& pass : graph.passes) {
        if (pass.generateMips) {
            Check(device->GenerateRenderMips(handles.at(pass.generateMips)),
                  "Null explicit mip timing");
            continue;
        }
        RenderPassDesc p;
        const auto convert = [&](const FrameAttachment& a) {
            PassAttachment v;
            v.texture = handles.at(a.target);
            v.load = a.load;
            v.store = a.store;
            v.clear = a.clear;
            v.clearDepth = a.clearDepth;
            return v;
        };
        for (const auto& a : pass.colors)
            p.colors.push_back(convert(a));
        if (pass.depthStencil)
            p.depthStencil = convert(*pass.depthStencil);
        for (auto id : pass.sampled)
            p.sampled.push_back(handles.at(id));
        Check(device->BeginRenderPass(p) && device->EndRenderPass(), "Null graph operation");
    }
    Check(device->EndFrame(), "Null end");
}
int main(int argc, char** argv) try {
    EffectPool pool;
    FrameClock clock;
    TargetRegistry registry;
    std::string reason;
    auto input = ScheduleFixture(pool, argc == 3 ? std::filesystem::u8path(argv[2])
                                                 : std::filesystem::path{});
    Check(registry.Reconcile(input.effects, 128, 96, 1, reason), reason);
    std::unique_ptr<ScheduledFrame> frame;
    Check(ScheduledFrame::Prepare(input, registry, pool, clock, 1, 100, 0, true, frame, reason),
          reason);
    std::vector<std::string> order;
    for (const auto& s : frame->Steps())
        if (s.kind != PlannedStep::Kind::Clear)
            order.push_back(std::to_string(unsigned(s.kind)) + ":" + std::to_string(s.effect));
    const std::vector<std::string> expected = {"1:10", "4:11", "1:11", "4:12",
                                               "1:14", "2:13", "2:12", "2:12"};
    Check(order == expected,
          "child-first turns, mip-after-turn, nested enter/resume and loop draw order");
    for (const auto& s : frame->Steps())
        if (s.kind == PlannedStep::Kind::Material && s.effect == 10) {
            const auto& size = s.parameters.values.at("TargetSize");
            Check(Number(size) == 64 && Number(size, 1) == 48,
                  "offscreen viewport semantic uses actual relative target extent");
        } else if (s.kind == PlannedStep::Kind::Buffer && s.effect == 12) {
            const auto& size = s.parameters.values.at("TargetSize");
            Check(Number(size) == 128 && Number(size, 1) == 96,
                  "scene viewport semantic uses actual output extent");
        }
    std::vector<float> index;
    for (const auto& s : frame->Steps())
        if (s.effect == 12 && s.kind == PlannedStep::Kind::Buffer)
            index.push_back(float(s.parameters.values.at("Index").words[0]));
    Check(index == std::vector<float>{0, 1} &&
              Read(*input.effects[2].binding.instance, "Index").words[0] == 9,
          "script snapshots without early writeback");
    NullReplay(frame->Dependencies());
    Check(registry.Reconcile(input.effects, 128, 96, 1, reason) && frame->Ready(reason),
          "identical inventory keeps prepared frame valid");
    if (argc >= 2) {
        std::ofstream out(argv[1], std::ios::binary);
        out << frame->Report();
        Check(bool(out), "plan report");
    }
    Check(frame->Ready(reason) && frame->Commit(reason), reason);
    Check(Read(*input.effects[2].binding.instance, "Index").words[0] == 1,
          "script final write committed");
    TargetLease lease;
    FrameTarget target;
    Check(registry.Resolve(12, "ParentMap", lease, target, reason), reason);
    TargetLease alias;
    FrameTarget aliased;
    Check(registry.Resolve(14, "ParentMap", alias, aliased, reason) && lease.id == alias.id,
          "cross-effect physical alias");
    auto prepared = std::move(frame);
    const auto savedReport = prepared->Report();
    Check(ScheduledFrame::Prepare(input, registry, pool, clock, 2, 130, 0, true, frame, reason),
          reason);
    Check(registry.Reconcile(input.effects, 256, 192, 1, reason) &&
              !registry.Validate(lease, reason) && !frame->Ready(reason) && !frame->Commit(reason),
          "resize invalidates leases and complete preparation");
    Check(prepared->Report() == savedReport,
          "execution report stays frozen after resource rebuilding");
    Check(ScheduledFrame::Prepare(input, registry, pool, clock, 2, 130, 0, true, frame, reason),
          reason);
    NullReplay(frame->Dependencies());
    Check(registry.Reconcile(input.effects, 256, 192, 2, reason) && !frame->Commit(reason),
          "device replacement invalidates preparation");
    auto bad = input;
    bad.effects[1].targets[0].defaults = {
        {"leaf.pmx", bad.effects[1].binding.path.generic_u8string()}};
    const auto saved = frame.get();
    Check(!ScheduledFrame::Prepare(bad, registry, pool, clock, 2, 130, 0, true, frame, reason) &&
              reason.find("cycle") != reason.npos && frame.get() == saved,
          "cycle failure preserves old output");
    bad = input;
    bad.assignments.effects[{TargetScope(11, 0), 3, -1}] = {Assignment::Kind::Effect, 14};
    Check(!ScheduledFrame::Prepare(bad, registry, pool, clock, 2, 130, 0, true, frame, reason),
          "read before parent production rejected");
    Check(Read(*input.effects[2].binding.instance, "Index").words[0] == 1,
          "failed graph publishes no parameter writes");
    bad = input;
    bad.assignments.effects[{TargetScope(12, 0), 3, -1}] = {Assignment::Kind::Effect, 14};
    Check(!ScheduledFrame::Prepare(bad, registry, pool, clock, 2, 130, 0, true, frame, reason),
          "feedback read/write rejected before submission");
    auto lost = input.effects;
    lost.erase(lost.begin() + 2);
    Check(!registry.Reconcile(lost, 128, 96, 2, reason), "unbound reader cannot invent publisher");
    Check(registry.Reconcile(input.effects, 128, 96, 2, reason) &&
              ScheduledFrame::Prepare(input, registry, pool, clock, 2, 130, 0, true, frame, reason),
          reason);
    Check(frame->Commit(reason), reason);
    std::cout
        << "Schedule CPU/Null: public material, two scenes, descendant turns, mips, loops, aliases, resize/recovery and atomic failures passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
