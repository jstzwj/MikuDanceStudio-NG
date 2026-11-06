#include "mme_runtime_test_support.hpp"
#include <limits>
#include <fstream>
using namespace mme_test;
int main(int argc, char** argv) try {
    EffectPool pool;
    FrameClock clock;
    std::string reason;
    auto a = Instance(pool, R"fx(shared float X=2; float T:TIME; float Dt:ELAPSEDTIME;
        float4x4 W:WORLD; float2 Size:VIEWPORTPIXELSIZE;
        float4 Control:CONTROLOBJECT <string name="miku";>;
        float Missing:CONTROLOBJECT <string name="absent.x";>;
        float Weight:CONTROLOBJECT <string name="(self)";string item="morph";>;
        float Bone:CONTROLOBJECT <string name="(AttachedModel)";string item="(AttachedBone)";>;
    )fx");
    auto b = Instance(pool, "shared float X=7;");
    auto c = Context();
    c.owner = 1;
    ControlObject owner;
    owner.id = 1;
    owner.name = "owner.x";
    owner.order = 5;
    owner.accessory = true;
    owner.attachedModel = 2;
    owner.attachedBone = "center";
    owner.world = mikudancestudio::cpu_math::Identity();
    owner.items["morph"].value = {.75f, 0, 0};
    ControlObject target;
    target.id = 2;
    target.name = "miku";
    target.order = 3;
    target.world = owner.world;
    target.world[12] = 20;
    ControlItem bone;
    bone.matrix = true;
    bone.transform = owner.world;
    bone.transform[0] = 3;
    target.items["center"] = bone;
    auto later = target;
    later.id = 3;
    later.order = 8;
    later.world[12] = 80;
    c.objects = {owner, target, later};
    std::unique_ptr<EffectFrame> frame;
    Check(EffectFrame::Begin(pool, clock, 1, 100, 10, true, frame, reason), reason);
    const auto firstClock = frame->Clock();
    Check(firstClock[0] == 0 && firstClock[1] > 0, "native initial edit clock");
    Check(frame->Write(*a, "X", Float(4), reason), reason);
    ParameterSnapshot sa, sb;
    Check(frame->Prepare(*a, c, sa, reason), reason);
    Check(Number(sa.values.at("Control")) == 20 && Number(sa.values.at("Missing")) == 10 &&
              Number(sa.values.at("Weight")) == .75f && Number(sa.values.at("Bone")) == 3,
          "ordered names, accessory defaults, morph and attached bone");
    Check(frame->Write(*b, "X", Float(6), reason) && frame->Prepare(*b, Context(), sb, reason),
          reason);
    Check(Number(sa.values.at("X")) == 4 && Number(sb.values.at("X")) == 6 &&
              Number(Read(*b, "X")) == 2,
          "draw snapshots freeze; staged sharing follows assignment order");
    frame.reset();
    Check(EffectFrame::Begin(pool, clock, 1, 999, 99, false, frame, reason) &&
              frame->Clock() == firstClock,
          "aborted retry reuses original clock sample");
    Check(frame->Prepare(*a, c, sa, reason) && frame->Commit(reason) && Number(Read(*a, "X")) == 2,
          "aborted writes never commit");
    Check(!frame->Commit(reason) &&
              !EffectFrame::Begin(pool, clock, 1, 999, 99, false, frame, reason),
          "closed frames and committed IDs rejected");
    Check(EffectFrame::Begin(pool, clock, 2, 130, 11, true, frame, reason), reason);
    auto invalid = c;
    invalid.objects.push_back(owner);
    const auto frozen = sa.values;
    Check(!frame->Prepare(*a, invalid, sa, reason) && sa.values == frozen,
          "semantic preparation failure atomic");
    Check(frame->Prepare(*a, c, sa, reason) && a->Write("X", Float(9), reason) &&
              !frame->Commit(reason),
          "interleaved host writes invalidate preparation");
    Check(EffectFrame::Begin(pool, clock, 2, 130, 11, true, frame, reason), reason);
    c.objects.erase(c.objects.begin() + 1); // named object disappears; later duplicate wraps.
    Check(frame->Prepare(*a, c, sa, reason) && Number(sa.values.at("Control")) == 80 &&
              Number(sa.values.at("Bone")) == 0,
          "name re-resolution after unload, missing attached item scale");
    Check(frame->Commit(reason), reason);
    if (argc == 2) {
        std::ofstream report(argv[1], std::ios::binary);
        report << "{\"frame\":" << sa.frame << ",\"values\":{";
        bool comma = false;
        for (const auto& [name, value] : sa.values) {
            if (comma)
                report << ',';
            comma = true;
            report << '"' << name << "\":[";
            for (std::size_t n = 0; n < value.words.size(); ++n) {
                if (n)
                    report << ',';
                report << value.words[n];
            }
            report << ']';
        }
        report << "}}\n";
        Check(bool(report), "snapshot report write");
    }
    Check(EffectFrame::Begin(pool, clock, 3, 160, 12, false, frame, reason), reason);
    auto temporary = Instance(pool, "float Local=1;"); // creation invalidates old preparation.
    Check(!frame->Prepare(*temporary, c, sb, reason), "load during preparation rejected");
    Check(EffectFrame::Begin(pool, clock, 3, 160, 12, false, frame, reason) &&
              frame->Prepare(*temporary, c, sb, reason),
          reason);
    temporary.reset();
    Check(!frame->Commit(reason), "unloaded assignment rejected even though staged cells survive");
    auto shared = Instance(pool, "shared float Zombie=3;");
    Check(EffectFrame::Begin(pool, clock, 3, 160, 12, false, frame, reason) &&
              frame->Write(*shared, "Zombie", Float(8), reason),
          reason);
    shared.reset();
    shared = Instance(pool, "shared float Zombie=17;");
    Check(Number(Read(*shared, "Zombie")) == 17 && !frame->Commit(reason),
          "pending frame storage cannot resurrect an unloaded shared slot");
    EffectPool foreign;
    auto other = Instance(foreign, "float X=1;");
    Check(EffectFrame::Begin(pool, clock, 3, 160, 12, false, frame, reason) &&
              !frame->Write(*other, "X", Float(2), reason),
          "cross-pool write rejected");
    Check(frame->Clock()[0] == 12 && frame->Clock()[2] == 12, "playback raw time");
    Check(frame->Commit(reason), reason);
    Check(EffectFrame::Begin(pool, clock, 4, 190, 1, false, frame, reason) &&
              frame->Clock()[1] == -11,
          "seek preserves native signed delta");
    std::map<std::string, ParameterValue> writes;
    auto controls = Definition(
        "bool V:CONTROLOBJECT <string name=\"absent\";>;float4 M:CONTROLOBJECT <string name=\"absent\";>;");
    Check(EvaluateSemantics(*controls, Context(), writes, reason) && writes.at("V").words[0] == 0 &&
              Number(writes.at("M"), 3) == 1,
          "missing object visibility and homogeneous translation");
    auto bad = Definition("float4x4 Bad:WORLDINVERSE;");
    auto singular = Context();
    singular.standard.world = {};
    const auto old = writes;
    Check(!EvaluateSemantics(*bad, singular, writes, reason) && writes == old,
          "singular inverse preserves prior semantic output");
    Check(!EffectFrame::Begin(pool, clock, 5, 0, std::numeric_limits<float>::infinity(), false,
                              frame, reason),
          "invalid clock rejected");
    std::cout
        << "Frame parameters: controls, clock retry/seek, detached snapshots, atomic failure and stale lifetimes passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
