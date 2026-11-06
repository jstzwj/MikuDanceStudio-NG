#include "mikudancestudio/mme_runtime.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace mikudancestudio::mme;
std::string Json(const std::string& value) {
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c < 32) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else
            out += char(c);
    }
    return out + '"';
}
std::string Map(const std::map<std::string, std::string>& values) {
    std::string out = "{";
    for (const auto& [key, value] : values) {
        if (out.size() > 1)
            out += ',';
        out += Json(key) + ':' + Json(value);
    }
    return out + '}';
}
void Describe(std::ostream& out, const EffectDefinition& definition) {
    const auto& p = definition.metadata;
    out << ",\"scene\":" << p.scene << ",\"parameters\":[";
    bool comma = false;
    for (const auto& v : p.variables) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"name\":" << Json(v.name) << ",\"type\":" << Json(v.type)
            << ",\"semantic\":" << Json(v.semantic) << ",\"rows\":" << v.rows
            << ",\"columns\":" << v.columns << ",\"elements\":" << v.elements
            << ",\"array\":" << v.array << ",\"shared\":" << v.shared
            << ",\"constant\":" << v.constant << ",\"immutable\":" << v.immutable
            << ",\"defaultExpression\":" << Json(v.initializer)
            << ",\"annotations\":" << Map(v.annotations) << '}';
    }
    out << "],\"textures\":[";
    comma = false;
    for (const auto& t : p.textures) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"name\":" << Json(t.name) << ",\"semantic\":" << Json(t.semantic)
            << ",\"shared\":" << t.shared << ",\"annotations\":" << Map(t.annotations) << '}';
    }
    out << "],\"samplers\":[";
    comma = false;
    for (const auto& s : p.samplers) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"name\":" << Json(s.name) << ",\"texture\":" << Json(s.texture)
            << ",\"linearMin\":" << s.linearMin << ",\"linearMag\":" << s.linearMag
            << ",\"linearMip\":" << s.linearMip << ",\"mipNone\":" << s.mipNone
            << ",\"clampU\":" << s.clampU << ",\"clampV\":" << s.clampV
            << ",\"anisotropicMin\":" << s.anisotropicMin
            << ",\"anisotropicMag\":" << s.anisotropicMag
            << ",\"anisotropicMip\":" << s.anisotropicMip
            << ",\"maxAnisotropy\":" << s.maxAnisotropy << '}';
    }
    out << "],\"techniques\":[";
    comma = false;
    for (const auto& t : p.techniques) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"name\":" << Json(t.name) << ",\"annotations\":" << Map(t.annotations)
            << ",\"passes\":[";
        for (std::size_t i = 0; i < t.passes.size(); ++i) {
            if (i)
                out << ',';
            const auto& pass = p.passes.at(t.passes[i]);
            out << "{\"name\":" << Json(pass.name) << ",\"vertex\":" << Json(pass.vertex)
                << ",\"pixel\":" << Json(pass.pixel) << ",\"states\":" << Map(pass.states)
                << ",\"annotations\":" << Map(pass.annotations) << '}';
        }
        out << "]}";
    }
    out << ']';
}
int Run(const std::vector<std::string>& args) {
    if (args.size() == 2 && (args[1] == "--help" || args[1] == "help")) {
        std::cout
            << "mds_effect_cli inspect|initialize FILE --dxc EXECUTABLE [-I DIRECTORY] [-D NAME=VALUE] [--output JSON]\n";
        return 0;
    }
    const bool initialize = args.size() > 1 && args[1] == "initialize";
    EffectPool pool;
    std::shared_ptr<EffectInstance> instance;
    SourceOptions options;
    fs::path input, output;
    std::string reason;
    MmeLegacyDiagnostic diagnostic;
    std::shared_ptr<const EffectDefinition> definition;
    try {
        if (args.size() < 3 || (args[1] != "inspect" && !initialize))
            throw std::runtime_error(
                "usage: mds_effect_cli inspect|initialize FILE --dxc EXECUTABLE");
        input = fs::u8path(args[2]);
        for (std::size_t n = 3; n < args.size(); ++n) {
            if (n + 1 == args.size())
                throw std::runtime_error("missing_option_value: " + args[n]);
            const auto option = args[n++];
            const auto& value = args[n];
            if (option == "--dxc")
                options.preprocessor = fs::u8path(value);
            else if (option == "--output")
                output = fs::u8path(value);
            else if (option == "-I")
                options.includeDirectories.push_back(fs::absolute(fs::u8path(value)));
            else if (option == "-D") {
                const auto equal = value.find('=');
                options.macros[value.substr(0, equal)] =
                    equal == value.npos ? "1" : value.substr(equal + 1);
            } else
                throw std::runtime_error("unknown_option: " + option);
        }
        if (LoadEffectDefinition(input, options, definition, reason, &diagnostic) && initialize &&
            !EffectInstance::Create(definition, pool, instance, reason)) {
            diagnostic.stage = "initialization";
            diagnostic.message = reason;
            definition.reset();
        }
    } catch (const std::exception& e) {
        reason = e.what();
        diagnostic.stage = "arguments";
        diagnostic.message = reason;
    }
    std::ostringstream report;
    report << std::boolalpha
           << "{\"schema\":1,\"operation\":" << Json(initialize ? "initialize" : "inspect")
           << ",\"executionValidated\":false,\"status\":"
           << Json(definition ? (initialize ? "initialized" : "described") : "failed");
    if (definition) {
        const auto& source = definition->source;
        report << ",\"preprocessorIdentity\":" << Json(source.preprocessorIdentity)
               << ",\"macros\":" << Map(source.macros) << ",\"files\":[";
        const auto root = source.files.at(0).path.parent_path();
        for (std::size_t i = 0; i < source.files.size(); ++i) {
            if (i)
                report << ',';
            const auto& file = source.files[i];
            report << "{\"id\":" << i
                   << ",\"path\":" << Json(file.path.lexically_relative(root).generic_u8string())
                   << ",\"encoding\":" << Json(file.encoding)
                   << ",\"utf8Bytes\":" << file.utf8.size() << '}';
        }
        report << "],\"includes\":[";
        for (std::size_t i = 0; i < source.includes.size(); ++i) {
            if (i)
                report << ',';
            const auto& edge = source.includes[i];
            report << "{\"parent\":" << edge.parent << ",\"child\":" << edge.child
                   << ",\"line\":" << edge.line << '}';
        }
        report << ']';
        Describe(report, *definition);
        if (instance) {
            report << ",\"values\":[";
            bool comma = false;
            for (const auto& v : definition->metadata.variables) {
                if (v.immutable)
                    continue;
                ParameterValue value;
                if (!instance->Read(v.name, value, reason))
                    throw std::runtime_error(reason);
                if (comma)
                    report << ',';
                comma = true;
                report << "{\"name\":" << Json(v.name) << ",\"words\":[";
                for (std::size_t n = 0; n < value.words.size(); ++n) {
                    if (n)
                        report << ',';
                    report << value.words[n];
                }
                report << "],\"text\":" << Json(value.text) << '}';
            }
            report << ']';
        }
    } else {
        report << ",\"reason\":" << Json(reason)
               << ",\"diagnostic\":{\"stage\":" << Json(diagnostic.stage)
               << ",\"file\":" << Json(diagnostic.file) << ",\"line\":" << diagnostic.line
               << ",\"column\":" << diagnostic.column << ",\"token\":" << Json(diagnostic.token)
               << ",\"declaration\":" << Json(diagnostic.declaration)
               << ",\"technique\":" << Json(diagnostic.technique)
               << ",\"pass\":" << Json(diagnostic.pass)
               << ",\"message\":" << Json(diagnostic.message) << '}';
    }
    report << "}\n";
    if (output.empty())
        std::cout << report.str();
    else {
        std::ofstream file(output, std::ios::binary);
        file << report.str();
        if (!file) {
            std::cerr << "effect_report_write_failed\n";
            return 2;
        }
    }
    return definition ? 0 : 1;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
        args.push_back(fs::path(argv[i]).u8string());
    return Run(args);
}
#else
int main(int argc, char** argv) {
    return Run({argv, argv + argc});
}
#endif
