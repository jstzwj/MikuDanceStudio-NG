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
    Check(
        MmeLegacyDescribe("shared texture Shadow:OFFSCREENRENDERTARGET<int2 Dimensions={2048,1024};"
                          "string Format=\"R32F\";>; technique T {}",
                          result, reason) &&
            result.textures.size() == 1 &&
            result.textures.front().annotations.at("DIMENSIONS") == "{2048,1024}",
        "preserve integer vector target annotations in the metadata inventory");
    std::string samplers = "texture Image;";
    for (int i = 0; i < 17; ++i)
        samplers += "sampler S" + std::to_string(i) +
                    "=sampler_state { Texture=<Image>; MinFilter=NONE; MagFilter=NONE; "
                    "MipFilter=NONE; AddressU=CLAMP; AddressV=CLAMP; };";
    Check(MmeLegacyDescribe(samplers + "technique T {}", result, reason) &&
              result.samplers.size() == 17 &&
              result.samplers.front().states.at("MINFILTER") == "NONE" &&
              result.samplers.front().states.at("MAGFILTER") == "NONE" &&
              result.samplers.front().states.at("MIPFILTER") == "NONE",
          "metadata retains NONE and inventories more than one pass can bind");
    std::vector<MmeLegacyConstant> layout;
    unsigned bytes = 0;
    Check(!MmeLegacyGenerate(result, strict, layout, bytes, reason) &&
              reason == "MME_shader_sampler_budget",
          "shader generation still enforces the binding limit");
    result.samplers.resize(1);
    Check(!MmeLegacyGenerate(result, strict, layout, bytes, reason) &&
              reason == "MME_legacy_sampler_filter",
          "shader generation rejects unqualified minification and magnification modes");
    Check(MmeLegacyDescribe("texture Image; sampler S=sampler_state { Texture=<Image>; "
                            "MinFilter=LINEAR; MagFilter=LINEAR; MipFilter=NONE; "
                            "AddressU=BORDER; AddressV=BORDER; BorderColor=0.0; }; "
                            "technique T {}",
                            result, reason) &&
              result.samplers.front().states.at("BORDERCOLOR") == "0.0" &&
              result.samplers.front().unsupportedState == "MME_legacy_sampler_address",
          "inventory retains bloom border color and reports execution eligibility");
    const std::string specialized =
        "float4 V(float4 p:POSITION):POSITION{return p;}"
        "float4 P(float2 uv:TEXCOORD0, uniform sampler image, uniform float2 offset):COLOR0"
        "{return tex2D(image, uv + offset);}"
        "technique T {pass Draw{VertexShader=compile vs_3_0 V();"
        "PixelShader=compile ps_3_0 P(ColorSampler, float2(1, 0));}}";
    Check(MmeLegacyDescribe(specialized, result, reason) &&
              result.functions.at("P").parameters[1].uniform &&
              result.passes[0].pixelArguments.size() == 2 &&
              result.passes[0].pixelArguments[1] == "float2 ( 1 , 0 ) ",
          "retain FX9 pass specialization without treating uniform parameters as interpolants");
    std::string hlsl;
    std::vector<MmeLegacyInput> inputs;
    Check(!MmeLegacyShader(result, result.passes[0], hlsl, inputs, reason) &&
              reason == "MME_legacy_compile_argument",
          "unspecialized shader generation is rejected");
    std::cout
        << "FX definitions: defaults, shared flags, annotations, techniques, empty passes and diagnostics passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
