#include "mme_legacy_api.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

void Check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() try {
    std::string reason;
    MmeLegacyProgram result;
    for (const auto* fixture : {"LegacyMaterial.fx", "LegacyScene.fx"}) {
        std::ifstream file(std::string(MDS_TEST_SOURCE_DIR) + "/assets/effects/portable/" +
                           fixture);
        const std::string source{std::istreambuf_iterator<char>(file), {}};
        Check(!source.empty() && MmeLegacyDescribe(source, result, reason), fixture + reason);
        Check(!result.techniques.empty(), "preserve technique order");
        Check(result.techniques[0].passes[0] == 0, "technique owns its ordered passes");
        if (result.scene) {
            Check(result.variables[0].semantic == "STANDARDSGLOBAL" &&
                      result.variables[0].annotations.at("SCRIPTCLASS") == "scene",
                  "SAS global metadata retained");
            Check(result.techniques[0].annotations.at("SCRIPT").find("ScriptExternal=Color;") !=
                      std::string::npos,
                  "adjacent script strings retained");
            Check(result.passes[0].annotations.at("SCRIPT") == "Draw=Buffer;",
                  "buffer pass annotation retained");
        } else {
            Check(result.variables[6].initializer == "{ 0.75 , 1 , 0.5 , 1 } ",
                  "retain typed source default expression");
            Check(result.variables[7].shared && result.variables[7].initializer == "1 ",
                  "shared parameter and initial value");
            Check(result.variables[10].annotations.at("NAME") == "(self)",
                  "control object metadata");
            Check(result.techniques[0].annotations.at("MMDPASS") == "object" &&
                      result.techniques[1].annotations.at("MMDPASS") == "object_ss",
                  "technique selection metadata");
        }
    }
    const std::string metadata = "static const float Gain=2; string Note=\"text\";"
                                 "technique Empty <string MmdPass=\"edge\";>{}"
                                 "technique Next {pass P {FutureState=Unknown;}}";
    Check(MmeLegacyDescribe(metadata, result, reason), reason);
    Check(result.variables[0].immutable && result.variables[1].type == "string" &&
              result.techniques.size() == 2 && result.techniques[0].passes.empty() &&
              result.passes[0].states.at("FUTURESTATE") == "UNKNOWN" && result.hlsl.empty(),
          "metadata is distinct from execution eligibility");
    MmeLegacyProgram strict;
    Check(!MmeLegacyImport(metadata, strict, reason), "strict importer remains strict");
    const auto saved = result.techniques.size();
    MmeLegacyDiagnostic diagnostic;
    Check(!MmeLegacyDescribe("#line 30 \"bad.fx\"\nfloat X=1; technique T {pass P {Bad 0;}}",
                             result, reason, &diagnostic),
          "invalid declaration is rejected");
    Check(result.techniques.size() == saved && diagnostic.file == "bad.fx" &&
              diagnostic.line == 30 && diagnostic.technique == "T" && diagnostic.pass == "P",
          "failure preserves previous definition and source location");
    Check(!MmeLegacyDescribe("float X=; technique T {}", result, reason, &diagnostic) &&
              diagnostic.declaration == "X",
          "empty initializer is not a valid default");
    std::cout
        << "FX definitions: defaults, shared flags, annotations, techniques, empty passes and diagnostics passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
