#include "mikudancestudio/mme_runtime.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace mikudancestudio::mme {
bool ParameterValue::operator==(const ParameterValue& v) const {
    return type == v.type && rows == v.rows && columns == v.columns && elements == v.elements &&
           array == v.array && words == v.words && text == v.text;
}
namespace {
struct Number {
    double value = 0;
    bool floating = false;
};
using Values = std::vector<Number>;
[[noreturn]] void Fail(const char* code) {
    throw std::runtime_error(code);
}
struct Parser {
    const std::string& source;
    const std::map<std::string, Values>& constants;
    std::size_t at = 0;
    unsigned depth = 0, operations = 0;
    std::string token;
    Parser(const std::string& s, const std::map<std::string, Values>& c) : source(s), constants(c) {
        if (s.size() > 65536)
            Fail("default_budget");
        Next();
    }
    void Next() {
        if (++operations > 65536)
            Fail("default_budget");
        while (at < source.size() && std::isspace(static_cast<unsigned char>(source[at])))
            ++at;
        const auto begin = at;
        if (at == source.size()) {
            token.clear();
            return;
        }
        const auto c = source[at++];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            while (at < source.size() &&
                   (std::isalnum(static_cast<unsigned char>(source[at])) || source[at] == '_'))
                ++at;
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            while (at < source.size()) {
                const auto n = source[at];
                if (std::isalnum(static_cast<unsigned char>(n)) || n == '.' ||
                    ((n == '+' || n == '-') && (source[at - 1] == 'e' || source[at - 1] == 'E')))
                    ++at;
                else
                    break;
            }
        } else if (at < source.size()) {
            const auto pair = source.substr(begin, 2);
            if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" || pair == "&&" ||
                pair == "||")
                ++at;
        }
        token = source.substr(begin, at - begin);
    }
    bool Eat(const char* s) {
        if (token != s)
            return false;
        Next();
        return true;
    }
    void Need(const char* s) {
        if (!Eat(s))
            Fail("default_syntax");
    }
    static Number Checked(double v, bool floating) {
        if (!std::isfinite(v) || (!floating && (v < INT32_MIN || v > INT32_MAX)))
            Fail("default_nonfinite_or_overflow");
        return {v, floating};
    }
    static bool Type(const std::string& t, ParameterValue::Type& kind, unsigned& count) {
        count = 1;
        if (t == "int")
            kind = ParameterValue::Type::Int;
        else if (t == "bool")
            kind = ParameterValue::Type::Bool;
        else if (t == "float")
            kind = ParameterValue::Type::Float;
        else if (t == "float4x4") {
            kind = ParameterValue::Type::Float;
            count = 16;
        } else if (t.size() == 6 && t.substr(0, 5) == "float" && t[5] >= '2' && t[5] <= '4') {
            kind = ParameterValue::Type::Float;
            count = unsigned(t[5] - '0');
        } else
            return false;
        return true;
    }
    static void Convert(Values& v, ParameterValue::Type type) {
        for (auto& n : v) {
            if (type == ParameterValue::Type::Bool)
                n = {n.value != 0 ? 1.0 : 0.0, false};
            else if (type == ParameterValue::Type::Int)
                n = Checked(std::trunc(n.value), false);
            else
                n = Checked(n.value, true);
        }
    }
    Values Primary() {
        if (++depth > 64)
            Fail("default_depth");
        Values v;
        if (Eat("{")) {
            if (!Eat("}")) {
                do {
                    auto a = Expression();
                    v.insert(v.end(), a.begin(), a.end());
                } while (Eat(",") && token != "}");
                Need("}");
            }
        } else if (Eat("(")) {
            ParameterValue::Type kind;
            unsigned count;
            if (Type(token, kind, count)) {
                Next();
                Need(")");
                v = Unary();
                if (v.size() == 1)
                    v.resize(count, v[0]);
                if (v.size() != count)
                    Fail("default_constructor_shape");
                Convert(v, kind);
            } else {
                v = Expression();
                Need(")");
            }
        } else {
            const auto name = token;
            Next();
            ParameterValue::Type kind;
            unsigned count;
            if (Type(name, kind, count)) {
                Need("(");
                if (!Eat(")")) {
                    do {
                        auto a = Expression();
                        v.insert(v.end(), a.begin(), a.end());
                    } while (Eat(","));
                    Need(")");
                }
                if (v.size() == 1)
                    v.resize(count, v[0]);
                if (v.size() != count)
                    Fail("default_constructor_shape");
                Convert(v, kind);
            } else if (name == "true" || name == "false" || name == "TRUE" || name == "FALSE")
                v.push_back({name == "true" || name == "TRUE" ? 1.0 : 0.0, false});
            else if (!name.empty() &&
                     (std::isdigit(static_cast<unsigned char>(name[0])) || name[0] == '.')) {
                auto literal = name;
                bool floating = literal.find_first_of(".eE") != std::string::npos;
                if (literal.size() > 2 && literal[0] == '0' &&
                    (literal[1] == 'x' || literal[1] == 'X')) {
                    std::size_t used = 0;
                    auto n = std::stoll(literal, &used, 16);
                    if (used != literal.size())
                        Fail("default_literal");
                    v.push_back(Checked(double(n), false));
                } else {
                    if (!literal.empty() && (literal.back() == 'f' || literal.back() == 'F')) {
                        floating = true;
                        literal.pop_back();
                    }
                    if (!floating && literal.size() > 1 && literal[0] == '0')
                        Fail("default_octal_unsupported");
                    double n = 0;
                    std::istringstream in(literal);
                    in.imbue(std::locale::classic());
                    if (!(in >> n) || in.peek() != std::char_traits<char>::eof())
                        Fail("default_literal");
                    v.push_back(Checked(n, floating));
                }
            } else {
                const auto found = constants.find(name);
                if (found == constants.end())
                    Fail("default_reference_or_function");
                v = found->second;
            }
        }
        --depth;
        if (v.empty() || v.size() > 4096)
            Fail("default_shape_or_budget");
        return v;
    }
    Values Unary() {
        if (token != "+" && token != "-" && token != "!")
            return Primary();
        if (++depth > 64)
            Fail("default_depth");
        const auto op = token;
        Next();
        auto v = Unary();
        --depth;
        for (auto& n : v) {
            if (op == "-")
                n = Checked(-n.value, n.floating);
            if (op == "!")
                n = {n.value == 0 ? 1.0 : 0.0, false};
        }
        return v;
    }
    static int Precedence(const std::string& op) {
        if (op == "||")
            return 1;
        if (op == "&&")
            return 2;
        if (op == "==" || op == "!=")
            return 3;
        if (op == "<" || op == ">" || op == "<=" || op == ">=")
            return 4;
        if (op == "+" || op == "-")
            return 5;
        if (op == "*" || op == "/")
            return 6;
        return 0;
    }
    Values Expression(int min = 1) {
        auto left = Unary();
        while (Precedence(token) >= min) {
            const auto op = token;
            const int prec = Precedence(op);
            Next();
            auto right = Expression(prec + 1);
            if (left.size() == 1)
                left.resize(right.size(), left[0]);
            if (right.size() == 1)
                right.resize(left.size(), right[0]);
            if (left.size() != right.size())
                Fail("default_expression_shape");
            for (std::size_t i = 0; i < left.size(); ++i) {
                auto a = left[i], b = right[i];
                bool f = a.floating || b.floating;
                double n = 0;
                if (op == "+")
                    n = a.value + b.value;
                else if (op == "-")
                    n = a.value - b.value;
                else if (op == "*")
                    n = a.value * b.value;
                else if (op == "/") {
                    if (b.value == 0)
                        Fail("default_division_by_zero");
                    n = a.value / b.value;
                    if (!f)
                        n = std::trunc(n);
                } else {
                    f = false;
                    if (op == "==")
                        n = a.value == b.value;
                    else if (op == "!=")
                        n = a.value != b.value;
                    else if (op == "<")
                        n = a.value < b.value;
                    else if (op == ">")
                        n = a.value > b.value;
                    else if (op == "<=")
                        n = a.value <= b.value;
                    else if (op == ">=")
                        n = a.value >= b.value;
                    else if (op == "&&")
                        n = a.value != 0 && b.value != 0;
                    else if (op == "||")
                        n = a.value != 0 || b.value != 0;
                }
                left[i] = Checked(n, f);
            }
        }
        return left;
    }
};
std::string String(const std::string& source) {
    std::string value;
    std::size_t at = source.find_first_not_of(" \r\n\t");
    if (at == std::string::npos || source[at++] != '"')
        Fail("default_string");
    bool closed = false;
    while (at < source.size()) {
        char c = source[at++];
        if (c == '"') {
            closed = true;
            break;
        }
        if (c == '\\') {
            if (at == source.size())
                Fail("default_string");
            c = source[at++];
            if (c == 'n')
                c = '\n';
            else if (c == 't')
                c = '\t';
            else if (c == 'r')
                c = '\r';
            else if (c != '\\' && c != '"')
                Fail("default_string_escape");
        }
        value += c;
    }
    if (!closed || source.find_first_not_of(" \r\n\t", at) != std::string::npos)
        Fail("default_string");
    return value;
}
}
bool EvaluateDefaults(const EffectDefinition& def, std::map<std::string, ParameterValue>& output,
                      std::string& reason) {
    std::map<std::string, ParameterValue> next;
    std::map<std::string, Values> constants;
    std::string name;
    try {
        if (def.metadata.variables.size() > 256)
            Fail("parameter_budget");
        for (const auto& v : def.metadata.variables) {
            name = v.name;
            ParameterValue value;
            value.rows = v.rows;
            value.columns = v.columns;
            value.elements = v.elements;
            value.array = v.array;
            if (v.type == "string") {
                if (v.array || v.elements != 1 || v.initializer.size() > 65536)
                    Fail("default_string_shape");
                value.type = ParameterValue::Type::String;
                value.text = v.initializer.empty() ? "" : String(v.initializer);
            } else {
                unsigned count;
                if (!Parser::Type(v.type, value.type, count) || !v.elements || v.elements > 256 ||
                    (!v.array && v.elements != 1) || v.rows != (count == 16 ? 4u : 1u) ||
                    v.columns != (count == 16 ? 4u : count))
                    Fail("parameter_shape");
                Values values(count * v.elements);
                if (!v.initializer.empty()) {
                    Parser parser(v.initializer, constants);
                    values = parser.Expression();
                    if (!parser.token.empty())
                        Fail("default_unsupported_expression");
                    if (values.size() == 1 && v.elements == 1)
                        values.resize(count, values[0]);
                    if (values.size() != count * v.elements)
                        Fail("default_initializer_shape");
                }
                Parser::Convert(values, value.type);
                if (v.immutable && v.constant)
                    constants.emplace(name, values);
                for (const auto& n : values) {
                    std::uint32_t word;
                    if (value.type == ParameterValue::Type::Float) {
                        float f = float(n.value);
                        if (!std::isfinite(f))
                            Fail("default_nonfinite_or_overflow");
                        std::memcpy(&word, &f, 4);
                    } else {
                        auto i = std::int32_t(n.value);
                        std::memcpy(&word, &i, 4);
                    }
                    value.words.push_back(word);
                }
            }
            if (!next.emplace(name, value).second)
                Fail("parameter_duplicate");
        }
        output = std::move(next);
        reason.clear();
        return true;
    } catch (const std::exception& e) {
        reason = "MME_runtime/" + name + "/" + e.what();
        return false;
    }
}
}
