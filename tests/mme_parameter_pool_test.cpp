#include "mme_runtime_test_support.hpp"
using namespace mme_test;
int main() try {
    EffectPool pool;
    std::string reason;
    auto a = Instance(
        pool,
        "shared float X=2; float Local=8; const float Writable=5; static const float Fixed=4;");
    auto b = Instance(pool, "shared float X=7; float Local=10;");
    Check(Number(Read(*b, "X")) == 2, "first live initializer wins");
    Check(a->Write("X", Float(9), reason) && Number(Read(*b, "X")) == 9,
          "host writes immediately visible");
    auto semantic = Instance(pool, "shared float X:DIFFUSE=11;");
    auto lower = Instance(pool, "shared float X:diffuse=17;");
    auto shape = Instance(pool, "shared float3 X={1,2,3};");
    auto array = Instance(pool, "shared float X[1]={19};");
    Check(Number(Read(*semantic, "X")) == 11 && Read(*lower, "X") == Read(*semantic, "X") &&
              Number(Read(*array, "X")) == 19 && Number(Read(*shape, "X"), 2) == 3,
          "semantic/type/array identity and semantic case folding");
    std::shared_ptr<EffectInstance> clone;
    Check(a->Clone(clone, reason), reason);
    Check(clone->Write("Local", Float(12), reason) && Number(Read(*a, "Local")) == 8,
          "clone private values detached");
    Check(clone->Write("X", Float(15), reason) && Number(Read(*b, "X")) == 15,
          "clone shares pool cells");
    auto wrong = Float(100);
    wrong.array = true;
    Check(!a->Write("X", wrong, reason) && !a->Write("Fixed", Float(3), reason) &&
              !a->Write("Absent", Float(1), reason) && Number(Read(*b, "X")) == 15,
          "invalid shape, immutable and missing writes preserve cells");
    Check(a->Write("Writable", Float(13), reason), "const parameter remains host-writable");
    auto saved = b;
    Check(!EffectInstance::Create(Definition("shared float X=42; float Bad=sin(1);"), pool, b,
                                  reason) &&
              b == saved && Number(Read(*a, "X")) == 15,
          "failed creation cannot publish cells or replace output");
    saved.reset();
    a.reset();
    b.reset();
    clone.reset();
    auto reloaded = Instance(pool, "shared float X=12;");
    Check(Number(Read(*reloaded, "X")) == 12, "last consumer release resets shared initializer");
    EffectPool isolated;
    auto separate = Instance(isolated, "shared float X=23;");
    Check(Number(Read(*separate, "X")) == 23, "pools isolate scenes");
    std::cout
        << "Parameter pool: load order, identity, immediate writes, clones, atomic failure and unload passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
