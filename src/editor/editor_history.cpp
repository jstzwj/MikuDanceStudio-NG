#include "mikudancestudio/editor_session.hpp"
#include <algorithm>

namespace mikudancestudio::editor {
namespace {
constexpr std::size_t HistoryDepth = 30; // MMD's per-model undo/redo rings.
template <class Document> auto Model(Document& d, std::uint8_t slot) {
    return std::find_if(d.models.begin(), d.models.end(),
                        [&](const auto& m) { return m.slot == slot; });
}
bool Equal(const format::PmmBoneKey& a, const format::PmmBoneKey& b) {
    return a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
           a.interpolation == b.interpolation && a.position == b.position &&
           a.rotation == b.rotation && a.allocated == b.allocated &&
           a.physicsDisabled == b.physicsDisabled;
}
bool Equal(const format::PmmKeyTrack<format::PmmBoneKey>& a,
           const format::PmmKeyTrack<format::PmmBoneKey>& b) {
    if (a.dense.size() != b.dense.size() || a.sparse.size() != b.sparse.size())
        return false;
    for (std::size_t i = 0; i < a.dense.size(); ++i)
        if (!Equal(a.dense[i], b.dense[i]))
            return false;
    for (std::size_t i = 0; i < a.sparse.size(); ++i)
        if (a.sparse[i].index != b.sparse[i].index || !Equal(a.sparse[i].value, b.sparse[i].value))
            return false;
    return true;
}
}

bool EditorSession::CanUndo() const {
    const auto it = histories_.find(selection_.model);
    return !playing_ && selection_.model != 255 && it != histories_.end() &&
           !it->second.undo.empty();
}
bool EditorSession::CanRedo() const {
    const auto it = histories_.find(selection_.model);
    return !playing_ && selection_.model != 255 && it != histories_.end() &&
           !it->second.redo.empty();
}
EditorSession::HistoryBatch EditorSession::FrameHistory() const {
    HistoryBatch result;
    for (const auto& model : Document().models)
        if (std::any_of(model.bonePose.begin(), model.bonePose.end(),
                        [](const auto& p) { return p.flags[1] != 0; }))
            result.emplace(model.slot,
                           CaptureHistory(model, HistoryKind::PoseWithFrame,
                                          std::uint32_t(Document().timeline.currentFrame)));
    return result;
}
EditorSession::HistoryEntry
EditorSession::CaptureHistory(const format::PmmSceneModel& model, HistoryKind kind,
                              std::uint32_t frame, const std::vector<std::uint32_t>& indices) {
    HistoryEntry entry;
    entry.kind = kind;
    entry.frame = frame;
    if (kind == HistoryKind::None)
        return entry;
    const auto capture = [&](std::uint32_t i) {
        HistoryPose pose;
        pose.index = i;
        std::copy_n(model.bonePose[i].components.begin(), 7, pose.components.begin());
        pose.edited = model.bonePose[i].flags[1];
        entry.poses.push_back(pose);
    };
    if (kind == HistoryKind::Pose) {
        for (auto i : indices)
            capture(i);
    } else {
        for (std::uint32_t i = 0; i < model.bonePose.size(); ++i)
            capture(i);
    }
    if (kind == HistoryKind::Keys)
        entry.keys = model.boneKeys;
    return entry;
}
void EditorSession::PushHistory(std::uint8_t slot, HistoryEntry entry) {
    auto& history = histories_[slot];
    if (gesture_ && gestureModels_.count(slot) && !history.undo.empty()) {
        auto& original = history.undo.back();
        // A gesture retains the first displaced value for every touched bone.
        for (const auto& pose : entry.poses)
            if (std::none_of(original.poses.begin(), original.poses.end(),
                             [&](const auto& p) { return p.index == pose.index; }))
                original.poses.push_back(pose);
    } else {
        history.undo.push_back(std::move(entry));
        if (history.undo.size() > HistoryDepth)
            history.undo.erase(history.undo.begin());
        if (gesture_)
            gestureModels_.insert(slot);
    }
    history.redo.clear();
}
void EditorSession::RecordHistory(const State& before, const State& after,
                                  const HistoryBatch& forced) {
    for (auto it = histories_.begin(); it != histories_.end();) {
        const auto a = before.assets.models.find(it->first),
                   b = after.assets.models.find(it->first);
        if (a == before.assets.models.end() || b == after.assets.models.end() ||
            a->second != b->second)
            it = histories_.erase(it);
        else
            ++it;
    }
    for (const auto& old : before.document.models) {
        const auto next = Model(after.document, old.slot);
        const auto a = before.assets.models.find(old.slot), b = after.assets.models.find(old.slot);
        if (next == after.document.models.end() || a == before.assets.models.end() ||
            b == after.assets.models.end() || a->second != b->second)
            continue;
        if (const auto requested = forced.find(old.slot); requested != forced.end()) {
            if (requested->second.kind != HistoryKind::None)
                PushHistory(old.slot, requested->second);
        } else if (!Equal(old.boneKeys, next->boneKeys)) {
            PushHistory(old.slot,
                        CaptureHistory(old, HistoryKind::Keys,
                                       std::uint32_t(before.document.timeline.currentFrame)));
        } else {
            std::vector<std::uint32_t> changed;
            for (std::uint32_t i = 0; i < old.bonePose.size(); ++i)
                if (!std::equal(old.bonePose[i].components.begin(),
                                old.bonePose[i].components.begin() + 7,
                                next->bonePose[i].components.begin()) ||
                    old.bonePose[i].flags[1] != next->bonePose[i].flags[1])
                    changed.push_back(i);
            if (!changed.empty())
                PushHistory(old.slot,
                            CaptureHistory(old, HistoryKind::Pose,
                                           std::uint32_t(before.document.timeline.currentFrame),
                                           changed));
        }
    }
}
bool EditorSession::ApplyHistory(bool redo, std::string& reason) {
    EndGesture();
    if (!(redo ? CanRedo() : CanUndo())) {
        reason = redo ? "redo-unavailable" : "undo-unavailable";
        return false;
    }
    auto& history = histories_.at(selection_.model);
    auto& source = redo ? history.redo : history.undo;
    auto& destination = redo ? history.undo : history.redo;
    const auto& entry = source.back();
    auto next = Current();
    auto model = Model(next.document, selection_.model);
    if (model == next.document.models.end()) {
        reason = "history-model-expired";
        return false;
    }
    std::vector<std::uint32_t> indices;
    for (const auto& pose : entry.poses)
        indices.push_back(pose.index);
    auto displaced = CaptureHistory(*model, entry.kind,
                                    std::uint32_t(next.document.timeline.currentFrame), indices);
    if (!redo && entry.kind == HistoryKind::PoseWithFrame)
        displaced.kind = HistoryKind::Pose;
    if (!redo)
        displaced.originalUndo = std::make_shared<const HistoryEntry>(entry);
    if (entry.kind == HistoryKind::Keys || (!redo && entry.kind == HistoryKind::PoseWithFrame))
        next.document.timeline.currentFrame = int(entry.frame);
    if (entry.kind == HistoryKind::Keys) {
        if (entry.partialKeys) {
            std::map<std::uint32_t, std::uint32_t> channels;
            if (!format::PmmRecordChannels(model->boneKeys, channels)) {
                reason = "history-key-links";
                return false;
            }
            std::set<std::uint32_t> selected;
            const auto remember = [&](std::uint32_t index, const auto& key) {
                if (next.selection.keys.count(
                        {Track::Bone, selection_.model, channels.at(index), key.frame}))
                    selected.insert(index);
            };
            for (unsigned i = 0; i < model->boneKeys.dense.size(); ++i)
                remember(i, model->boneKeys.dense[i]);
            for (const auto& key : model->boneKeys.sparse)
                remember(unsigned(key.index), key.value);
            displaced.partialKeys = true;
            displaced.keys = {};
            for (const auto& [index, key] : entry.keyRecords) {
                if (selected.erase(index))
                    displaced.selectedKeyRecords.insert(index);
            }
            selected.insert(entry.selectedKeyRecords.begin(), entry.selectedKeyRecords.end());
            if (!format::RestorePmmKeyRecords(model->boneKeys, entry.keyRecords,
                                              displaced.keyRecords) ||
                !format::PmmRecordChannels(model->boneKeys, channels)) {
                reason = "history-key-restore";
                return false;
            }
            for (auto& [index, key] : displaced.keyRecords)
                key.allocated = displaced.selectedKeyRecords.count(index) != 0;
            for (auto it = next.selection.keys.begin(); it != next.selection.keys.end();) {
                if (it->track == Track::Bone && it->slot == selection_.model)
                    it = next.selection.keys.erase(it);
                else
                    ++it;
            }
            const auto restoreSelection = [&](std::uint32_t index, auto& key) {
                key.allocated = selected.count(index) != 0;
                if (selected.count(index))
                    next.selection.keys.insert(
                        {Track::Bone, selection_.model, channels.at(index), key.frame});
            };
            for (unsigned i = 0; i < model->boneKeys.dense.size(); ++i)
                restoreSelection(i, model->boneKeys.dense[i]);
            for (auto& key : model->boneKeys.sparse)
                restoreSelection(unsigned(key.index), key.value);
        } else {
            model->boneKeys = entry.keys;
        }
        displaced.frame = entry.frame;
        displaced.insertedFrameLine = entry.insertedFrameLine;
        if (entry.insertedFrameLine) {
            std::set<KeyAddress> keys;
            for (const auto& address : next.selection.keys) {
                if (address.track == Track::Bone && address.slot == selection_.model &&
                    address.frame &&
                    (redo ? address.frame >= entry.frame : address.frame > entry.frame)) {
                    displaced.frameLineSelection.insert(address);
                } else {
                    keys.insert(address);
                }
            }
            keys.insert(entry.frameLineSelection.begin(), entry.frameLineSelection.end());
            next.selection.keys = std::move(keys);
        }
    }
    displaced.poses.clear();
    for (const auto& pose : entry.poses) {
        if (redo && entry.kind == HistoryKind::Keys)
            break; // Native key redo evaluates the frame instead of restoring poses.
        auto& target = model->bonePose.at(pose.index);
        HistoryPose capture;
        capture.index = pose.index;
        std::copy_n(target.components.begin(), 7, capture.components.begin());
        capture.edited = target.flags[1];
        displaced.poses.push_back(capture);
        std::copy(pose.components.begin(), pose.components.end(), target.components.begin());
        target.flags[1] = pose.edited;
    }
    if (redo && entry.kind == HistoryKind::Keys) {
        format::SceneSession evaluated;
        if (!format::SceneSession::Create(next.document, evaluated, reason) ||
            !evaluated.Seek(entry.frame))
            return false;
        const auto value = Model(evaluated.Document(), selection_.model);
        for (std::size_t i = 0; i < model->bonePose.size(); ++i) {
            std::copy_n(value->bonePose[i].components.begin(), 7,
                        model->bonePose[i].components.begin());
            model->bonePose[i].flags[0] = value->bonePose[i].flags[0];
        }
    }
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    if (redo)
        destination.push_back(*entry.originalUndo);
    else
        destination.push_back(std::move(displaced));
    source.pop_back();
    selection_ = next.selection;
    state_ = std::move(next);
    runtime_ = std::move(runtime);
    // Undo does not restore selection, UI state, other models or asset loading.
    revision_ = nextRevision_++;
    cursorSeconds_ = Document().timeline.currentFrame / 30.;
    reason.clear();
    return true;
}
}
