#include "mikudancestudio/pmm_key_selection.hpp"
#include "mikudancestudio/pmm_edit.hpp"
namespace mikudancestudio::format {
namespace key_selection_detail {
template <class T> decltype(auto) Mark(T& key) {
    using K = std::remove_const_t<T>;
    if constexpr (std::is_same_v<K, PmmBoneKey> || std::is_same_v<K, PmmMorphKey> ||
                  std::is_same_v<K, PmmDisplayKey>)
        return (key.allocated);
    else
        return (key.selected);
}
// Include detached occupied records; unused slots have no channel/frame address.
// Visitors only stage changes. A later track can still fail validation.
template <class Document, class Used, class Unused>
bool Visit(Document& document, std::uint8_t model, Used&& used, Unused&& unused) {
    const auto track = [&](auto& keys, PmmKeyKind kind, std::uint8_t slot) {
        std::map<std::uint32_t, std::uint32_t> channels;
        if (!PmmRecordChannels(keys, channels))
            return false;
        std::map<unsigned, unsigned> successors;
        std::set<unsigned> linked;
        for (const auto& entry : keys.sparse)
            successors.emplace(unsigned(entry.index), entry.value.next);
        for (unsigned i = 0; i < keys.dense.size(); ++i) {
            linked.insert(i);
            for (auto next = keys.dense[i].next; next; next = successors.at(next))
                linked.insert(next);
        }
        for (std::uint32_t i = 0; i < keys.dense.size(); ++i)
            used(PmmKeyAddress{kind, slot, i, keys.dense[i].frame}, Mark(keys.dense[i]), true);
        for (auto& entry : keys.sparse)
            used(PmmKeyAddress{kind, slot, channels.at(unsigned(entry.index)), entry.value.frame},
                 Mark(entry.value), linked.count(unsigned(entry.index)) != 0);
        for (auto& entry : keys.unused)
            unused(std::make_pair(kind, slot), Mark(entry.second));
        return true;
    };
    if (model != 255) {
        for (auto& value : document.models)
            if (value.slot == model)
                return track(value.boneKeys, PmmKeyKind::Bone, model) &&
                       track(value.morphKeys, PmmKeyKind::Morph, model) &&
                       track(value.displayKeys, PmmKeyKind::Display, model);
        return false;
    }
    if (!track(document.cameraKeys, PmmKeyKind::Camera, 255) ||
        !track(document.lightKeys, PmmKeyKind::Light, 255) ||
        !track(document.selfShadowKeys, PmmKeyKind::Shadow, 255) ||
        !track(document.gravityKeys, PmmKeyKind::Gravity, 255))
        return false;
    for (auto& value : document.accessories)
        if (!track(value.keyRecords, PmmKeyKind::Accessory, value.slot))
            return false;
    return true;
}
}
bool ReadPmmSelectedKeys(const PmmSceneDocument& document, std::uint8_t model,
                         std::set<PmmKeyAddress>& result) {
    std::set<PmmKeyAddress> next;
    if (!key_selection_detail::Visit(
            document, model,
            [&](const auto& key, auto mark, bool) {
                if (mark)
                    next.insert(key);
            },
            [](auto, auto) {}))
        return false;
    result = std::move(next);
    return true;
}
// Selection is metadata, not a topology edit. Validate all addressed tracks and
// live references before changing a byte; preserve other models/global tracks.
// Only native clearing commands clear unused slots. Saving and additive
// selection retain those payloads (including nonzero marks in free records).
bool SetPmmSelectedKeys(PmmSceneDocument& document, std::uint8_t model,
                        const std::set<PmmKeyAddress>& selected, const PmmKeyPools& clearUnused) {
    std::set<PmmKeyAddress> live;
    std::set<PmmKeyAddress> previous;
    if (!ReadPmmSelectedKeys(document, model, previous))
        return false;
    std::vector<std::pair<std::uint8_t*, bool>> changes;
    if (!key_selection_detail::Visit(
            document, model,
            [&](const auto& key, auto& mark, bool) {
                live.insert(key);
                if (selected.count(key) != previous.count(key) ||
                    clearUnused.count({key.track, key.slot}))
                    changes.emplace_back(&mark, selected.count(key) != 0);
            },
            [&](auto kind, auto& mark) {
                if (clearUnused.count(kind))
                    changes.emplace_back(&mark, false);
            }))
        return false;
    for (const auto& key : selected)
        if (PmmActiveKey(model, key) && !live.count(key))
            return false;
    for (const auto& change : changes)
        *change.first = change.second;
    return true;
}
bool SelectPmmKeyRange(PmmSceneDocument& document, std::uint8_t model,
                       const PmmKeyRangeSelection& range) {
    std::vector<std::pair<std::uint8_t*, bool>> changes;
    std::set<PmmKeyAddress> rows;
    PmmKeyPools pools;
    if (!key_selection_detail::Visit(
            document, model,
            [&](const auto& key, auto& mark, bool linked) {
                const bool inside = range.first <= key.frame && key.frame <= range.last;
                auto row = key;
                row.frame = 0;
                rows.insert(row);
                pools.emplace(key.track, key.slot);
                if (range.replace.count({key.track, key.slot}))
                    changes.emplace_back(&mark, inside);
                else if (inside && linked && range.append.count(row))
                    changes.emplace_back(&mark, true);
            },
            [&](auto pool, auto& mark) {
                if (range.replace.count(pool))
                    changes.emplace_back(&mark, false);
            }))
        return false;
    for (const auto& row : range.append)
        if (!rows.count(row))
            return false;
    for (const auto& pool : range.replace)
        if (!pools.count(pool))
            return false;
    for (const auto& change : changes)
        *change.first = change.second;
    return true;
}
}
