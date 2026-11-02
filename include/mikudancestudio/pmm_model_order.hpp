#pragma once
#include "mikudancestudio/pmm_scene_document.hpp"
#include <algorithm>
#include <array>

namespace mikudancestudio::format {
enum class ModelOrderKind {
    Draw,
    Calculation
};

inline std::vector<std::uint8_t> PmmModelOrder(const PmmSceneDocument& document,
                                               ModelOrderKind kind) {
    std::vector<const PmmSceneModel*> models;
    for (const auto& model : document.models)
        models.push_back(&model);
    const auto rank = [&](const auto* model) {
        return kind == ModelOrderKind::Draw || document.envelope.header.version == PmmVersion::V1
                   ? model->selection.comboIndex
                   : model->previousDisplayOrder;
    };
    std::sort(models.begin(), models.end(), [&](auto a, auto b) {
        return rank(a) < rank(b) || (rank(a) == rank(b) && a->slot < b->slot);
    });
    std::vector<std::uint8_t> orderedSlots;
    for (const auto* model : models)
        orderedSlots.push_back(model->slot);
    return orderedSlots;
}

// Orders address stable model orderedSlots, not document vector positions. Validate the
// complete permutation before writing either byte. Draw ranks reserve zero for
// camera mode; calculation ranks start at zero.
inline bool SetPmmModelOrder(PmmSceneDocument& document, ModelOrderKind kind,
                             const std::vector<std::uint8_t>& orderedSlots) {
    if (orderedSlots.size() != document.models.size() || orderedSlots.size() > 255 ||
        (kind != ModelOrderKind::Draw && kind != ModelOrderKind::Calculation))
        return false;
    std::array<PmmSceneModel*, 255> models{};
    for (auto& model : document.models) {
        if (model.slot == 255 || models[model.slot])
            return false;
        models[model.slot] = &model;
    }
    std::array<bool, 255> seen{};
    for (auto slot : orderedSlots) {
        if (slot == 255 || !models[slot] || seen[slot])
            return false;
        seen[slot] = true;
    }
    for (std::size_t row = 0; row < orderedSlots.size(); ++row) {
        auto& model = *models[orderedSlots[row]];
        if (kind == ModelOrderKind::Draw)
            model.selection.comboIndex = std::uint8_t(row + 1);
        else
            model.previousDisplayOrder = std::uint8_t(row);
    }
    return true;
}
}
