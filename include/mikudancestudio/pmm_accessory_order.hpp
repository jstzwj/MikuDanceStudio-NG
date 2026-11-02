#pragma once
#include "mikudancestudio/pmm_scene_document.hpp"
#include <algorithm>

namespace mikudancestudio::format {
struct PmmAccessoryOrderEntry {
    std::uint8_t slot;
    std::array<char, 100> name;
};
inline std::vector<PmmAccessoryOrderEntry> PmmAccessoryOrder(const PmmSceneDocument& document) {
    std::vector<const PmmSceneAccessory*> rows;
    for (const auto& item : document.accessories)
        rows.push_back(&item);
    std::sort(rows.begin(), rows.end(), [](auto* a, auto* b) {
        return a->order < b->order || (a->order == b->order && a->slot < b->slot);
    });
    std::vector<PmmAccessoryOrderEntry> result;
    for (auto* row : rows)
        result.push_back({row->slot, row->name});
    return result;
}
inline bool SetPmmAccessoryOrder(PmmSceneDocument& document,
                                 const std::vector<PmmAccessoryOrderEntry>& rows, int split) {
    if (rows.empty() || rows.size() > 255 || rows.size() != document.accessories.size() ||
        split < 0 || unsigned(split) > rows.size())
        return false;
    std::array<PmmSceneAccessory*, 255> bySlot{};
    std::array<bool, 255> seen{};
    for (auto& item : document.accessories) {
        if (item.slot == 255 || bySlot[item.slot])
            return false;
        bySlot[item.slot] = &item;
    }
    for (const auto& row : rows) {
        if (row.slot == 255 || !bySlot[row.slot] || seen[row.slot] ||
            std::find(row.name.begin(), row.name.end(), 0) == row.name.end())
            return false;
        seen[row.slot] = true;
    }
    std::vector<std::array<char, 100>> names;
    for (const auto& row : rows)
        names.push_back(row.name);
    for (unsigned i = 0; i < rows.size(); ++i) {
        auto& item = *bySlot[rows[i].slot];
        item.order = std::uint8_t(i);
        item.name = rows[i].name;
    }
    document.accessoryListNames = std::move(names);
    document.selectedAccessorySlot = rows.front().slot;
    document.physics.accessoryRenderOrder = split;
    return true;
}
}
