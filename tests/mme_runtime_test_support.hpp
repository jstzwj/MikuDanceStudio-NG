#pragma once
#include "mikudancestudio/mme_runtime.hpp"
#include "mikudancestudio/cpu_math.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace mme_test {
using namespace mikudancestudio::mme;
inline void Check(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline std::shared_ptr<const EffectDefinition> Definition(const std::string& source) {
    auto d = std::make_shared<EffectDefinition>();
    std::string reason;
    Check(MmeLegacyDescribe(source + "\ntechnique TestTechnique {}", d->metadata, reason), reason);
    return d;
}
inline ParameterValue Float(float f) {
    ParameterValue v;
    v.words.resize(1);
    std::memcpy(v.words.data(), &f, 4);
    return v;
}
inline float Number(const ParameterValue& v, unsigned at = 0) {
    float f;
    Check(v.type == ParameterValue::Type::Float && at < v.words.size(), "float value shape");
    std::memcpy(&f, &v.words[at], 4);
    return f;
}
inline ParameterValue Read(const EffectInstance& i, const std::string& name) {
    ParameterValue v;
    std::string reason;
    Check(i.Read(name, v, reason), reason);
    return v;
}
inline std::shared_ptr<EffectInstance> Instance(EffectPool& p, const std::string& source) {
    std::shared_ptr<EffectInstance> i;
    std::string reason;
    Check(EffectInstance::Create(Definition(source), p, i, reason), reason);
    return i;
}
inline SemanticContext Context() {
    SemanticContext c;
    c.standard.world = c.standard.view = c.standard.projection = c.standard.beginWorld =
        mikudancestudio::cpu_math::Identity();
    c.standard.viewport = {800, 600};
    return c;
}
}
