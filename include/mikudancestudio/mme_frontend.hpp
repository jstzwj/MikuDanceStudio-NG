#pragma once
#include "mme_legacy_api.h"
#include <filesystem>
#include <memory>

namespace mikudancestudio::mme {
struct SourceOptions {
    std::filesystem::path preprocessor;
    std::vector<std::filesystem::path> includeDirectories;
    std::map<std::string, std::string> macros{{"_INDEX", "PSIZE15"}};
    unsigned timeoutMilliseconds = 30000, maxIncludeDepth = 64, maxFiles = 256;
    std::size_t maxSourceBytes = 16 * 1024 * 1024;
};
struct SourceFile {
    std::filesystem::path path;
    std::string encoding, utf8;
};
struct IncludeEdge {
    unsigned parent = 0, child = 0, line = 0;
};
struct EffectSource {
    // Files are frozen once per invocation, including dependencies scanned in
    // inactive branches. The preprocessor determines the active source text.
    std::vector<SourceFile> files;
    std::vector<IncludeEdge> includes;
    std::map<std::string, std::string> macros;
    std::string expanded, preprocessorIdentity;
};
struct EffectDefinition {
    EffectSource source;
    MmeLegacyProgram metadata;
};
// Both operations preserve output on failure. Literal quoted/angle includes
// are supported; active macro includes receive an explicit diagnostic.
bool PreprocessEffect(const std::filesystem::path&, const SourceOptions&, EffectSource&,
                      std::string& reason, MmeLegacyDiagnostic* diagnostic = nullptr);
bool LoadEffectDefinition(const std::filesystem::path&, const SourceOptions&,
                          std::shared_ptr<const EffectDefinition>&, std::string& reason,
                          MmeLegacyDiagnostic* diagnostic = nullptr);
}
