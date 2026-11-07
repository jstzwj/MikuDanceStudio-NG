#include "schedule_internal.hpp"
namespace mikudancestudio::mme {
namespace {
std::string Quoted(const std::string& s) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : s) {
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
std::uint32_t Word(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
void Attachment(std::ostream& out, const rhi::FrameAttachment& a) {
    out << "{\"id\":" << a.target << ",\"load\":" << unsigned(a.load)
        << ",\"store\":" << unsigned(a.store) << ",\"clearWords\":[" << Word(a.clear.red) << ','
        << Word(a.clear.green) << ',' << Word(a.clear.blue) << ',' << Word(a.clear.alpha)
        << "],\"depthWord\":" << Word(a.clearDepth) << '}';
}
}
std::string ScheduledFrame::Report() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema\":1,\"executionValidated\":false,\"effects\":[";
    bool comma = false;
    for (const auto& [id, revision] : state_->effects) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"id\":" << id << ",\"revision\":" << revision << '}';
    }
    out << "],\"bindings\":[";
    comma = false;
    for (const auto& [key, lease] : state_->bindings) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"effect\":" << key.first << ",\"name\":" << Quoted(key.second)
            << ",\"id\":" << lease.id << ",\"generation\":" << lease.generation
            << ",\"epoch\":" << lease.epoch << '}';
    }
    out << "],\"targets\":[";
    comma = false;
    for (const auto& t : state_->dependencies.targets) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"id\":" << t.id << ",\"width\":" << t.description.width
            << ",\"height\":" << t.description.height << ",\"mips\":" << t.description.mips
            << ",\"format\":" << unsigned(t.description.format)
            << ",\"access\":" << t.description.access << '}';
    }
    out << "],\"finalColor\":" << state_->dependencies.finalColor << ",\"steps\":[";
    comma = false;
    for (const auto& s : state_->steps) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"kind\":" << unsigned(s.kind) << ",\"scope\":" << s.scope
            << ",\"object\":" << s.object << ",\"subset\":" << s.subset
            << ",\"effect\":" << s.effect << ",\"technique\":" << s.technique
            << ",\"pass\":" << s.pass << ",\"colors\":[";
        bool c = false;
        for (const auto& a : s.resources.colors) {
            if (c)
                out << ',';
            c = true;
            Attachment(out, a);
        }
        out << "],\"depth\":";
        if (s.resources.depthStencil)
            Attachment(out, *s.resources.depthStencil);
        else
            out << "null";
        out << ",\"sampled\":[";
        c = false;
        for (auto id : s.resources.sampled) {
            if (c)
                out << ',';
            c = true;
            out << id;
        }
        out << "],\"generateMips\":" << s.resources.generateMips << ",\"parameters\":{";
        c = false;
        for (const auto& [name, v] : s.parameters.values) {
            if (c)
                out << ',';
            c = true;
            out << Quoted(name) << ":{\"type\":" << unsigned(v.type) << ",\"shape\":[" << v.rows
                << ',' << v.columns << ',' << v.elements << "],\"value\":";
            if (v.type == ParameterValue::Type::String)
                out << Quoted(v.text);
            else {
                out << '[';
                for (std::size_t i = 0; i < v.words.size(); ++i) {
                    if (i)
                        out << ',';
                    out << v.words[i];
                }
                out << ']';
            }
            out << '}';
        }
        out << "}}";
    }
    out << "]}\n";
    return out.str();
}
}
