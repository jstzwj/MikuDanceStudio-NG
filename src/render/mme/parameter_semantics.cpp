#include "parameter_runtime_internal.hpp"
#include "mikudancestudio/cpu_math.hpp"
#include "../../../third_party/mmeffect/src/mmeffect/model_name_registry.h"
#include <cmath>
#include <cstring>
#include <set>

namespace mikudancestudio::mme {
namespace {
bool Fail(std::string& reason, const std::string& code) {
    reason = "MME_runtime/" + code;
    return false;
}
const ControlObject* ById(const SemanticContext& c, std::uint64_t id) {
    if (!id)
        return nullptr;
    for (const auto& object : c.objects)
        if (object.id == id)
            return &object;
    return nullptr;
}
ControlSample Resolve(const MmeLegacyVariable& v, const SemanticContext& c) {
    const auto name = v.annotations.at("NAME");
    auto item = v.annotations.find("ITEM");
    const std::string itemName = item == v.annotations.end() ? "" : item->second;
    const auto* owner = ById(c, c.owner);
    ::mme::ModelNameRegistry<const ControlObject> registry;
    for (const auto& object : c.objects)
        registry.Add(object.name, object.order, &object);
    const auto* target = owner ? registry.Find(name, owner->order) : nullptr;
    if (!target) {
        if (name == "(self)")
            target = owner;
        else if (name == "(OffscreenOwner)")
            target = ById(c, c.offscreenOwner);
        else if (name == "(AttachedModel)" && owner && owner->accessory)
            target = ById(c, owner->attachedModel);
    }
    ControlSample s;
    s.hasItem = !itemName.empty();
    const auto* first = registry.First(name);
    s.accessory = first ? first->accessory
                        : (name.size() >= 2 && name[name.size() - 2] == '.' &&
                           (name.back() == 'x' || name.back() == 'X'));
    if (!target)
        return s;
    s.found = true;
    s.visible = target->visible;
    s.transform = cpu_math::Identity();
    if (!s.hasItem) {
        s.matrix = true;
        s.transform = target->world;
    } else {
        const ControlItem* resolved = nullptr;
        if (owner && owner->accessory && itemName == "(AttachedBone)") {
            if (owner->attachedModel != target->id)
                return s;
            const auto found = target->items.find(owner->attachedBone);
            if (found != target->items.end())
                resolved = &found->second;
        } else {
            const auto found = target->items.find(itemName);
            if (found != target->items.end())
                resolved = &found->second;
        }
        if (resolved) {
            s.matrix = resolved->matrix;
            if (s.matrix)
                s.transform = resolved->transform;
            else
                for (unsigned i = 0; i < 3; ++i)
                    s.transform[12 + i] = resolved->value[i];
        }
    }
    return s;
}
}
bool EvaluateControl(const MmeLegacyVariable& v, const ControlSample& s, ParameterValue& output,
                     std::string& reason) {
    ParameterValue next;
    next.rows = v.rows;
    next.columns = v.columns;
    next.elements = v.elements;
    if (v.array || v.elements != 1 ||
        !((v.type == "bool" && v.rows == 1 && v.columns == 1) ||
          (v.type == "float" && v.rows == 1 && v.columns == 1) ||
          (v.rows == 1 && v.columns >= 2 && v.columns <= 4 &&
           v.type == "float" + std::to_string(v.columns)) ||
          (v.type == "float4x4" && v.rows == 4 && v.columns == 4)))
        return Fail(reason, v.name + "/control_shape");
    if (v.type == "bool") {
        next.type = ParameterValue::Type::Bool;
        next.words = {s.found && s.visible ? 1u : 0u};
    } else {
        auto matrix = s.found ? s.transform : cpu_math::Identity();
        if (!s.found && s.accessory)
            matrix[0] = matrix[5] = matrix[10] = 10;
        std::vector<float> values;
        if (v.type == "float")
            values = {s.found ? (s.matrix ? std::sqrt(matrix[0] * matrix[0] +
                                                      matrix[1] * matrix[1] + matrix[2] * matrix[2])
                                          : matrix[12])
                              : (s.hasItem ? 0.f : (s.accessory ? 10.f : 1.f))};
        else if (v.rows == 4)
            values.assign(matrix.begin(), matrix.end());
        else
            values.assign(matrix.begin() + 12, matrix.begin() + 12 + v.columns);
        for (float value : values) {
            if (!std::isfinite(value))
                return Fail(reason, v.name + "/control_nonfinite");
            std::uint32_t word;
            std::memcpy(&word, &value, 4);
            next.words.push_back(word);
        }
    }
    output = std::move(next);
    reason.clear();
    return true;
}
bool detail::EvaluateSemanticsForFrame(const EffectDefinition& definition,
                                       const SemanticContext& context,
                                       const MmeStandardInputs& standard,
                                       std::map<std::string, ParameterValue>& output,
                                       std::string& reason) {
    std::set<std::uint64_t> ids;
    for (const auto& object : context.objects)
        if (!object.id || !ids.insert(object.id).second)
            return Fail(reason, "control_object_identity");
    MmeStandardMath math;
    math.multiply = cpu_math::Multiply;
    math.inverse = cpu_math::Inverse;
    math.normalize = [](auto& v) {
        std::array<float, 3> result{};
        if (cpu_math::Normalize(v, result))
            v = result;
        else
            v = {};
    };
    std::map<std::string, ParameterValue> next;
    for (const auto& v : definition.metadata.variables) {
        if (v.immutable || v.semantic == "STANDARDSGLOBAL")
            continue;
        ParameterValue value;
        value.rows = v.rows;
        value.columns = v.columns;
        value.elements = v.elements;
        value.array = v.array;
        if (v.semantic == "CONTROLOBJECT") {
            if (!v.annotations.count("NAME")) {
                // The native shared-without-Name declaration is silently not
                // registered as a control binding; its ordinary value survives.
                if (v.shared)
                    continue;
                return Fail(reason, v.name + "/control_name");
            }
            if (!EvaluateControl(v, Resolve(v, context), value, reason))
                return false;
        } else {
            MmeStandardBinding binding;
            if (!MmeStandardClassify(v, binding, reason)) {
                reason = "MME_runtime/" + v.name + "/" + reason;
                return false;
            }
            if (binding.kind == MmeStandardBinding::Kind::Value)
                continue;
            std::vector<std::uint8_t> bytes;
            if (!MmeStandardEvaluate(binding, standard, math, bytes, reason)) {
                reason = "MME_runtime/" + v.name + "/" + reason;
                return false;
            }
            value.type = v.type == "bool"  ? ParameterValue::Type::Bool
                         : v.type == "int" ? ParameterValue::Type::Int
                                           : ParameterValue::Type::Float;
            value.words.resize(bytes.size() / 4);
            std::memcpy(value.words.data(), bytes.data(), bytes.size());
            for (auto word : value.words)
                if (value.type == ParameterValue::Type::Float) {
                    float f;
                    std::memcpy(&f, &word, 4);
                    if (!std::isfinite(f))
                        return Fail(reason, v.name + "/semantic_nonfinite");
                }
        }
        next.emplace(v.name, std::move(value));
    }
    output = std::move(next);
    reason.clear();
    return true;
}
bool EvaluateSemantics(const EffectDefinition& definition, const SemanticContext& context,
                       std::map<std::string, ParameterValue>& output, std::string& reason) {
    return detail::EvaluateSemanticsForFrame(definition, context, context.standard, output, reason);
}
}
