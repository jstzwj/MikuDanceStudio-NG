#include "mikudancestudio/mme_frontend.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/tool_process.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mikudancestudio::mme {
namespace {
namespace fs = std::filesystem;
bool Utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]);
        if (!c)
            return false;
        if (c < 128)
            continue;
        unsigned count = c >= 0xc2 && c <= 0xdf   ? 1
                         : c >= 0xe0 && c <= 0xef ? 2
                         : c >= 0xf0 && c <= 0xf4 ? 3
                                                  : 0;
        if (!count || text.size() - i < count)
            return false;
        const auto next = static_cast<unsigned char>(text[i]);
        if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next >= 0xa0) ||
            (c == 0xf0 && next < 0x90) || (c == 0xf4 && next >= 0x90))
            return false;
        while (count--)
            if ((static_cast<unsigned char>(text[i++]) & 0xc0) != 0x80)
                return false;
    }
    return true;
}
std::string Read(const fs::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("MME_source_open_failed");
    std::string bytes(limit + 1, '\0');
    input.read(bytes.data(), std::streamsize(bytes.size()));
    if (input.bad())
        throw std::runtime_error("MME_source_read_failed");
    bytes.resize(std::size_t(input.gcount()));
    if (bytes.size() > limit)
        throw std::runtime_error("MME_source_budget");
    return bytes;
}
std::string Quote(const std::string& text) {
    std::string value = "\"";
    for (const auto c : text) {
        if (c == '\\' || c == '"')
            value += '\\';
        value += c;
    }
    return value + '"';
}
// Directive recognition must not interpret #include text inside comments or
// strings. DXC still owns actual HLSL macro expansion and condition evaluation.
std::string WithoutComments(const std::string& line, bool& block) {
    std::string result;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size();) {
        if (block) {
            if (line.compare(i, 2, "*/") == 0) {
                block = false;
                i += 2;
            } else
                ++i;
        } else if (quoted) {
            const auto c = line[i++];
            result += c;
            if (c == '\\' && i < line.size())
                result += line[i++];
            else if (c == '"')
                quoted = false;
        } else if (line.compare(i, 2, "//") == 0)
            break;
        else if (line.compare(i, 2, "/*") == 0) {
            block = true;
            result += ' ';
            i += 2;
        } else {
            const auto c = line[i++];
            result += c;
            quoted = c == '"';
        }
    }
    return result;
}
struct Bundle {
    const SourceOptions& options;
    EffectSource source;
    std::map<fs::path, unsigned> ids;
    std::set<unsigned> stack;
    std::size_t bytes = 0;
    std::string failureFile;
    unsigned failureLine = 1;
    unsigned Load(const fs::path& requested) {
        const auto path = fs::canonical(requested);
        if (const auto found = ids.find(path); found != ids.end())
            return found->second;
        if (source.files.size() >= options.maxFiles)
            throw std::runtime_error("MME_source_file_budget");
        SourceFile file;
        file.path = path;
        failureFile = path.u8string();
        auto raw = Read(path, options.maxSourceBytes - bytes);
        bytes += raw.size();
        if (raw.compare(0, 2, "\xff\xfe") == 0) {
            file.encoding = "utf-16le";
            if (!text::DecodeUtf16Le(reinterpret_cast<const std::uint8_t*>(raw.data() + 2),
                                     raw.size() - 2, file.utf8) ||
                !Utf8(file.utf8))
                throw std::runtime_error("MME_source_encoding");
        } else {
            const bool utf8Bom = raw.compare(0, 3, "\xef\xbb\xbf") == 0;
            if (utf8Bom)
                raw.erase(0, 3);
            if (Utf8(raw)) {
                file.encoding = "utf-8";
                file.utf8 = std::move(raw);
            } else {
                file.encoding = "cp932";
                if (utf8Bom || !text::DecodeCp932(raw, file.utf8) || !Utf8(file.utf8))
                    throw std::runtime_error("MME_source_encoding");
            }
        }
        const auto id = unsigned(source.files.size());
        source.files.push_back(std::move(file));
        ids.emplace(path, id);
        return id;
    }
    std::string Expand(unsigned id, unsigned depth) {
        if (depth > options.maxIncludeDepth || stack.count(id))
            return "#error MME_include_cycle_or_depth\n";
        stack.insert(id);
        // Recursion may reallocate files, so retain value copies of this file.
        const auto file = source.files[id];
        if (file.utf8.find("MDS_FX_INCLUDE_ONCE_") != std::string::npos)
            throw std::runtime_error("MME_source_reserved_include_marker");
        const auto marker = "MDS_FX_INCLUDE_ONCE_" + std::to_string(id);
        std::string output = "#line 1 " + Quote(file.path.generic_u8string()) + '\n';
        std::istringstream input(file.utf8);
        std::string line;
        unsigned number = 0;
        bool block = false, once = false;
        const std::regex directive("^\\s*#\\s*([A-Za-z_][A-Za-z_0-9]*)\\s*(.*?)\\s*$");
        const std::regex literal("^([\"<])([^\">]+)([\">])\\s*$");
        while (std::getline(input, line)) {
            const auto firstLine = ++number;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            while (!line.empty() && line.back() == '\\') {
                line.pop_back();
                std::string next;
                if (!std::getline(input, next))
                    throw std::runtime_error("MME_source_line_splice");
                ++number;
                if (!next.empty() && next.back() == '\r')
                    next.pop_back();
                line += next;
            }
            failureFile = file.path.u8string();
            failureLine = firstLine;
            const auto visible = WithoutComments(line, block);
            std::smatch match;
            if (std::regex_match(visible, match, directive) && match[1] == "include") {
                const std::string operand = match[2];
                std::smatch include;
                if (!std::regex_match(operand, include, literal) ||
                    (include[1] == "\"" ? include[3] != "\"" : include[3] != ">"))
                    output += "#error MME_macro_include_not_supported\n";
                else {
                    auto name = include[2].str();
                    std::replace(name.begin(), name.end(), '\\', '/');
                    const auto relative = fs::u8path(name);
                    fs::path found;
                    std::vector<fs::path> roots = options.includeDirectories;
                    if (include[1] == "\"")
                        roots.insert(roots.begin(), file.path.parent_path());
                    if (!relative.is_absolute() && !relative.has_root_name())
                        for (const auto& root : roots)
                            if (fs::is_regular_file(root / relative)) {
                                found = root / relative;
                                break;
                            }
                    if (found.empty())
                        output += "#error MME_include_missing_or_absolute\n";
                    else {
                        const auto child = Load(found);
                        source.includes.push_back({id, child, firstLine});
                        // A cycle may be legal behind an include guard. Keep
                        // the repeated declaration; DXC decides its active path.
                        if (stack.count(child)) {
                            const auto& content = source.files[child].utf8;
                            const std::regex guard(
                                "^\\s*(?://[^\\n]*\\n|/\\*[\\s\\S]*?\\*/\\s*)*#\\s*ifndef\\s+([A-Za-z_][A-Za-z_0-9]*)");
                            std::smatch guarded;
                            if (std::regex_search(content, guarded, guard))
                                output += "#ifndef " + guarded[1].str() +
                                          "\n#error MME_include_cycle_or_depth\n#endif\n";
                            else
                                output += std::string("#ifndef MDS_FX_INCLUDE_ONCE_") +
                                          std::to_string(child) +
                                          "\n#error MME_include_cycle_or_depth\n#endif\n";
                        } else
                            output += Expand(child, depth + 1);
                    }
                }
                output += "#line " + std::to_string(number + 1) + " " +
                          Quote(file.path.generic_u8string()) + '\n';
            } else if (std::regex_match(visible, match, directive) && match[1] == "pragma" &&
                       match[2] == "once") {
                once = true;
                output += "#define " + marker + "\n";
            } else {
                // No directive is ever allowed to reopen unfrozen files.
                if (std::regex_match(visible, match, directive) &&
                    (match[1] == "include_next" || match[1] == "import"))
                    output += "#error MME_include_directive_not_supported\n";
                else
                    output += visible + '\n';
                if (number != firstLine)
                    output += "#line " + std::to_string(number + 1) + " " +
                              Quote(file.path.generic_u8string()) + '\n';
            }
            if (output.size() > options.maxSourceBytes)
                throw std::runtime_error("MME_source_budget");
        }
        if (block)
            throw std::runtime_error("MME_legacy_unclosed_comment");
        stack.erase(id);
        if (once)
            output = "#ifndef " + marker + '\n' + output + "#endif\n";
        return output;
    }
};
}
bool PreprocessEffect(const fs::path& path, const SourceOptions& options, EffectSource& output,
                      std::string& reason, MmeLegacyDiagnostic* diagnostic) {
    if (diagnostic)
        *diagnostic = {};
    Bundle bundle{options};
    try {
        if (!options.maxSourceBytes || options.maxSourceBytes > 64 * 1024 * 1024 ||
            !options.maxIncludeDepth || options.maxIncludeDepth > 256 || !options.maxFiles ||
            options.maxFiles > 4096 || !options.timeoutMilliseconds ||
            options.timeoutMilliseconds > 60000 || options.preprocessor.empty())
            throw std::runtime_error("MME_source_options");
        const auto compiler = fs::absolute(options.preprocessor);
        const auto root = bundle.Load(path);
        auto frozen = bundle.Expand(root, 0);
        platform::ToolWorkspace workspace;
        std::string identity;
        if (!platform::RunToolProcess(compiler, {"--version"}, workspace,
                                      options.timeoutMilliseconds, identity))
            throw std::runtime_error("MME_preprocessor_unavailable: " + identity);
        const auto input = workspace.path / "frozen.fx",
                   preprocessed = workspace.path / "expanded.fx";
        std::ofstream file(input, std::ios::binary);
        file.write(frozen.data(), std::streamsize(frozen.size()));
        file.close();
        if (!file)
            throw std::runtime_error("MME_source_write_failed");
        std::vector<std::string> args{"-P",  "-Fi",  preprocessed.u8string(),
                                      "-HV", "2016", "-flegacy-macro-expansion"};
        const std::regex identifier("[A-Za-z_][A-Za-z_0-9]*");
        for (const auto& [name, value] : options.macros) {
            if (!std::regex_match(name, identifier) || name.rfind("MDS_FX_INCLUDE_ONCE_", 0) == 0 ||
                value.find_first_of("\r\n") != std::string::npos)
                throw std::runtime_error("MME_source_macro");
            args.push_back("-D");
            args.push_back(name + "=" + value);
        }
        args.push_back(input.u8string());
        std::string log;
        if (!platform::RunToolProcess(compiler, args, workspace, options.timeoutMilliseconds,
                                      log) ||
            !fs::is_regular_file(preprocessed))
            throw std::runtime_error("MME_preprocess_failed: " + log);
        bundle.source.expanded = Read(preprocessed, options.maxSourceBytes);
        bundle.source.preprocessorIdentity = identity;
        bundle.source.macros = options.macros;
        output = std::move(bundle.source);
        reason.clear();
        return true;
    } catch (const std::exception& error) {
        reason = error.what();
        if (diagnostic) {
            diagnostic->stage = "preprocess";
            diagnostic->file = bundle.failureFile.empty() ? path.u8string() : bundle.failureFile;
            diagnostic->line = bundle.failureLine;
            diagnostic->message = reason;
            std::smatch location;
            if (std::regex_search(reason, location,
                                  std::regex("(?:^|\\n)([^\\n]+?):([0-9]+):([0-9]+): error:"))) {
                diagnostic->file = location[1];
                diagnostic->line = unsigned(std::stoul(location[2]));
                diagnostic->column = unsigned(std::stoul(location[3]));
            }
        }
        return false;
    }
}
bool LoadEffectDefinition(const fs::path& path, const SourceOptions& options,
                          std::shared_ptr<const EffectDefinition>& output, std::string& reason,
                          MmeLegacyDiagnostic* diagnostic) {
    auto next = std::make_shared<EffectDefinition>();
    if (!PreprocessEffect(path, options, next->source, reason, diagnostic) ||
        !MmeLegacyDescribe(next->source.expanded, next->metadata, reason, diagnostic))
        return false;
    output = std::move(next);
    return true;
}
}
