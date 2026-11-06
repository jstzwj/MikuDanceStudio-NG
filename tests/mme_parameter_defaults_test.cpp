#include "mme_runtime_test_support.hpp"
using namespace mme_test;
int main() try {
    std::string reason;
    auto def = Definition(R"(
        static const int Scale=3;
        float3 Tint=float3(1,2,3)*(Scale+1)/2;
        float4x4 Matrix={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        float Values[2]={0.25f, 1e-2};
        float One[1]={9};
        bool Enabled=(3>2) && !false;
        int Truncated=int(-3.8);
        float Mixed=16777217 + 1.0f;
        float Zero;
        string Caption="line\n\"quoted\"";
    )");
    std::map<std::string, ParameterValue> values;
    Check(EvaluateDefaults(*def, values, reason), reason);
    Check(Number(values.at("Tint"), 2) == 6 && Number(values.at("Matrix"), 12) == 13,
          "typed constructors, prior constant and row-major matrix");
    Check(Number(values.at("Values"), 1) == .01f && values.at("One").array,
          "array defaults preserve single-element array identity");
    Check(values.at("Enabled").words[0] == 1 &&
              values.at("Truncated").words[0] == std::uint32_t(-3),
          "bool and signed integer values");
    Check(Number(values.at("Mixed")) == 16777218.f && Number(values.at("Zero")) == 0,
          "float32 promotion and implicit zero");
    Check(values.at("Caption").text == "line\n\"quoted\"", "string escapes");
    const auto saved = values;
    for (const auto* source :
         {"float Bad=1/0;", "float Bad=sqrt(4);", "float Bad=Later; static const float Later=2;",
          "float2 Bad={1,2,3};", "int Bad=2147483647+1;", "float Bad=1e100;", "float Bad=010;",
          "float Bad=Other; float Other=1;"}) {
        Check(!EvaluateDefaults(*Definition(source), values, reason) && values == saved &&
                  reason.find("Bad/") != reason.npos,
              "unsupported/invalid default is atomic: " + std::string(source) + reason);
    }
    Check(!EvaluateDefaults(*Definition("float Bad=" + std::string(100, '-') + "1;"), values,
                            reason) &&
              values == saved,
          "bounded recursion");
    std::cout
        << "Parameter defaults: typed values, arithmetic, arrays, strings, precision, bounded failures passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
