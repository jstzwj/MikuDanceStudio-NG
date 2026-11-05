#include "mikudancestudio/mme_frontend.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace mikudancestudio::mme;
namespace fs = std::filesystem;
void Check(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), std::streamsize(text.size()));
    Check(bool(file), "write fixture");
}
int main(int argc, char** argv) try {
    Check(argc == 3, "DXC and output directory required");
    const auto directory = fs::absolute(argv[2]);
    const auto cwd = fs::current_path();
    SourceOptions options;
    options.preprocessor = fs::absolute(argv[1]);
    options.includeDirectories = {directory / "search"};
    options.macros["MME_MIPMAP"] = "";
    Write(directory / "root.fx", "/*\n#include \"ignore.fx\"\n*/\n"
                                 "#define JOIN(a,b) a##b\n#define SCALE(x) ((x)*2)\n"
                                 "#if defined(MME_MIPMAP) && !defined(DISABLED)\n"
                                 "#include \"local.fxsub\"\n#include <global.fxsub>\n#endif\n"
                                 "#if 0\n#include \"missing.fxsub\"\n#include MACRO_FILE\n#endif\n"
                                 "float JOIN(Val,ue)=SCALE(3);\nfloat Tag : _INDEX;\n"
                                 "technique Main <string MmdPass=\"object\";>{}\n");
    Write(directory / "local.fxsub", "#pragma once\n#include \"local.fxsub\"\n"
                                     "#include \"guard.fxsub\"\nfloat Local=1;\n");
    Write(directory / "guard.fxsub", "#ifndef MY_GUARD\n#define MY_GUARD\n"
                                     "#include \"guard.fxsub\"\nfloat Guarded=4;\n#endif\n");
    Write(directory / "search/global.fxsub", "float Global=5;\n");
    std::shared_ptr<const EffectDefinition> definition;
    std::string reason;
    MmeLegacyDiagnostic diagnostic;
    Check(LoadEffectDefinition(directory / "root.fx", options, definition, reason, &diagnostic),
          reason);
    Check(fs::current_path() == cwd, "source loading cannot change host cwd");
    Check(definition->source.files.size() == 4 && definition->source.includes.size() == 5,
          "immutable source graph includes guarded cycles");
    Check(definition->metadata.variables.size() == 5 &&
              definition->metadata.variables[3].name == "Value" &&
              definition->metadata.variables[4].semantic == "PSIZE15",
          "function macro, token paste and native host macros");
    Check(definition->source.expanded.find("missing.fxsub") == std::string::npos,
          "inactive includes do not fail preprocessing");
    const auto previous = definition;
    Write(directory / "local.fxsub", "#include \"missing.fxsub\"\n");
    const bool changed =
        LoadEffectDefinition(directory / "root.fx", options, definition, reason, &diagnostic);
    Check(!changed && definition == previous && diagnostic.stage == "preprocess" &&
              diagnostic.file.find("local.fxsub") != std::string::npos && diagnostic.line == 1,
          "mapped include diagnostics and failure atomicity: " + reason + " at " + diagnostic.file +
              ":" + std::to_string(diagnostic.line));
    Check(previous->source.files[1].utf8.find("float Local=1;") != std::string::npos,
          "loaded source revision survives file changes");
    Write(directory / "cycle.fx", "#include \"cycle.fx\"\n");
    EffectSource source;
    Check(!PreprocessEffect(directory / "cycle.fx", options, source, reason), "unguarded cycle");
    Write(directory / "macro.fx", "#define FILE \"guard.fxsub\"\n#include FILE\n");
    Check(!PreprocessEffect(directory / "macro.fx", options, source, reason) &&
              reason.find("MME_macro_include_not_supported") != std::string::npos,
          "macro includes explicitly rejected in this profile");
    const std::string cp932 = "// \x83\x65\x83\x58\x83\x67\nfloat A=1;\n";
    Write(directory / "cp932.fx", cp932);
    Check(PreprocessEffect(directory / "cp932.fx", options, source, reason) &&
              source.files[0].encoding == "cp932",
          "explicit CP932 decoding");
    std::string utf16 = "\xff\xfe";
    for (const auto c : std::string("float A=1;\n")) {
        utf16 += c;
        utf16 += '\0';
    }
    Write(directory / "utf16.fx", utf16);
    Check(PreprocessEffect(directory / "utf16.fx", options, source, reason) &&
              source.files[0].encoding == "utf-16le",
          "explicit UTF16LE decoding");
    auto bounded = options;
    bounded.maxSourceBytes = 4;
    Check(!PreprocessEffect(directory / "root.fx", bounded, source, reason), "source size limit");
    bounded = options;
    bounded.maxFiles = 1;
    Check(!PreprocessEffect(directory / "root.fx", bounded, source, reason), "source file limit");
    bounded = options;
    bounded.macros["BAD\nMACRO"] = "1";
    Check(!PreprocessEffect(directory / "cp932.fx", bounded, source, reason), "macro validation");
    std::cout
        << "FX source: frozen includes, guards, native macros, locations, encodings and budgets passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
