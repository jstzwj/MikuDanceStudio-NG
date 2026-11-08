#include "mikudancestudio/editor_effects.hpp"
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <cstring>
namespace mikudancestudio::editor {
bool ValidEffectParameter(const std::string& name, const mme::ParameterValue& value) {
    if (name.empty() || name.size() > 256 || name.find('\0') != std::string::npos ||
        unsigned(value.type) > unsigned(mme::ParameterValue::Type::Bool) || !value.rows ||
        value.rows > 4 || !value.columns || value.columns > 4 || !value.elements ||
        value.elements > 256 || (!value.array && value.elements != 1) ||
        value.words.size() != std::size_t(value.rows) * value.columns * value.elements)
        return false;
    for (auto word : value.words) {
        if (value.type == mme::ParameterValue::Type::Bool && word > 1)
            return false;
        if (value.type == mme::ParameterValue::Type::Float) {
            float f;
            std::memcpy(&f, &word, 4);
            if (!std::isfinite(f))
                return false;
        }
    }
    return true;
}
std::uint64_t EffectProjectIdentity(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (auto c : bytes) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}
std::filesystem::path EffectSidecar(const std::filesystem::path& project) {
    auto path = project;
    path += ".ng-effects";
    return path;
}
bool EncodeEffects(const EffectDocument& doc, const std::filesystem::path& project,
                   std::uint64_t identity, std::vector<std::uint8_t>& output,
                   std::string& reason) try {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "MDSNG_EFFECTS 1 " << identity << ' ' << doc.basicMaterials << ' '
           << doc.assignments.size() << '\n';
    if (doc.assignments.size() > 4096)
        throw std::runtime_error("effect-document-budget");
    for (const auto& [address, e] : doc.assignments) {
        auto relative = e.source.lexically_relative(project.parent_path());
        const auto path = (relative.empty() ? e.source : relative).generic_u8string();
        if (path.size() > 32768 || e.parameters.size() > 256)
            throw std::runtime_error("effect-document-budget");
        stream << address.accessory << ' ' << unsigned(address.slot) << ' ' << address.subset << ' '
               << e.enabled << ' ' << std::quoted(path) << ' ' << e.parameters.size() << '\n';
        for (const auto& [name, v] : e.parameters) {
            if (!ValidEffectParameter(name, v))
                throw std::runtime_error("effect-document-parameter");
            stream << std::quoted(name) << ' ' << unsigned(v.type) << ' ' << v.rows << ' '
                   << v.columns << ' ' << v.elements << ' ' << v.array << ' ' << v.words.size();
            for (auto word : v.words)
                stream << ' ' << word;
            stream << '\n';
        }
    }
    const auto text = stream.str();
    if (text.size() > 16 * 1024 * 1024)
        throw std::runtime_error("effect-document-budget");
    output.assign(text.begin(), text.end());
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
bool DecodeEffects(const std::vector<std::uint8_t>& bytes, const std::filesystem::path& project,
                   std::uint64_t identity, EffectDocument& output, std::string& reason) try {
    if (bytes.size() > 16 * 1024 * 1024)
        throw std::runtime_error("effect-document-budget");
    std::istringstream stream(std::string(bytes.begin(), bytes.end()));
    stream.imbue(std::locale::classic());
    std::string magic;
    unsigned version, count;
    std::uint64_t hash;
    EffectDocument next;
    if (!(stream >> magic >> version >> hash >> next.basicMaterials >> count) ||
        magic != "MDSNG_EFFECTS" || version != 1 || count > 4096)
        throw std::runtime_error("effect-document-header");
    if (hash != identity)
        throw std::runtime_error("effect-project-identity-mismatch");
    for (unsigned i = 0; i < count; ++i) {
        EffectAddress a;
        EffectAssignment e;
        unsigned slot, parameters;
        std::string path;
        if (!(stream >> a.accessory >> slot >> a.subset >> e.enabled >> std::quoted(path) >>
              parameters) ||
            slot >= 255 || a.subset < -1 || a.subset > 65535 || path.empty() ||
            path.size() > 32768 || path.find('\0') != std::string::npos || parameters > 256)
            throw std::runtime_error("effect-document-assignment");
        a.slot = std::uint8_t(slot);
        e.source = (project.parent_path() / std::filesystem::u8path(path)).lexically_normal();
        for (unsigned j = 0; j < parameters; ++j) {
            std::string name;
            unsigned type, words;
            mme::ParameterValue v;
            if (!(stream >> std::quoted(name) >> type >> v.rows >> v.columns >> v.elements >>
                  v.array >> words) ||
                name.empty() || name.size() > 256 || type > 2 || !v.rows || v.rows > 4 ||
                !v.columns || v.columns > 4 || !v.elements || v.elements > 256 ||
                words != v.rows * v.columns * v.elements)
                throw std::runtime_error("effect-document-parameter");
            v.type = mme::ParameterValue::Type(type);
            v.words.resize(words);
            for (auto& word : v.words)
                if (!(stream >> word))
                    throw std::runtime_error("effect-document-value");
            if (!ValidEffectParameter(name, v))
                throw std::runtime_error("effect-document-parameter");
            if (!e.parameters.emplace(name, std::move(v)).second)
                throw std::runtime_error("effect-document-duplicate");
        }
        if (!next.assignments.emplace(a, std::move(e)).second)
            throw std::runtime_error("effect-document-duplicate");
    }
    stream >> std::ws;
    if (!stream.eof())
        throw std::runtime_error("effect-document-trailing-data");
    output = std::move(next);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
}
