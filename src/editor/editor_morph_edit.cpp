#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/morph_key_purge.hpp"
#include "mikudancestudio/pmm_key_selection.hpp"

namespace mikudancestudio::editor {
bool EditorSession::PurgeMorphKeys(unsigned panel, std::string& reason) {
    return EditMorphKeys(panel, std::nullopt, reason);
}
bool EditorSession::ShiftMouthKeys(std::int32_t shift, std::string& reason) {
    return EditMorphKeys(3, shift, reason);
}
bool EditorSession::EditMorphKeys(unsigned panel, const std::optional<std::int32_t>& shift,
                                  std::string& reason) {
    if (playing_ || selection_.model == 255 || panel < 1 || panel > 3) {
        reason = "morph-key-purge-unavailable";
        return false;
    }
    auto next = Current();
    auto model = std::find_if(next.document.models.begin(), next.document.models.end(),
                              [&](const auto& m) { return m.slot == selection_.model; });
    const auto resource = runtime_->Resource(selection_.model);
    if (model == next.document.models.end() || !resource ||
        !format::SetPmmSelectedKeys(next.document, selection_.model, selection_.keys)) {
        reason = "morph-key-purge-model-expired";
        return false;
    }
    auto& track = model->morphKeys;
    std::vector<std::vector<format::PmmMorphKey>> chains;
    if (!format::PmmTrackChains(track, chains)) {
        reason = "morph-key-purge-invalid-track";
        return false;
    }
    std::map<unsigned, format::PmmMorphKey*> records;
    for (unsigned i = 0; i < track.dense.size(); ++i)
        records.emplace(i, &track.dense[i]);
    for (auto& key : track.sparse)
        records.emplace(unsigned(key.index), &key.value);
    for (unsigned i = 0; i < track.dense.size(); ++i) {
        int mapped;
        if (!runtime_->DocumentMorphIndex(selection_.model, int(i), mapped, reason))
            return false;
        if (resource->Document().morphs.at(std::size_t(mapped)).header.panel == panel) {
            const auto at = [&](unsigned index) -> auto& { return *records.at(index); };
            if (shift)
                keyframe_edit::ShiftMorphChain(i, *shift, at);
            else
                keyframe_edit::PurgeMorphChain(i, at);
        }
    }
    for (const auto& key : track.sparse)
        if (!key.value.frame)
            track.unused[unsigned(key.index)] = key.value;
    track.sparse.erase(std::remove_if(track.sparse.begin(), track.sparse.end(),
                                      [](const auto& k) { return !k.value.frame; }),
                       track.sparse.end());
    // Seek only this model, like the native command tail. Retain unrelated live
    // poses, key watermarks and bone history, including an existing redo entry.
    format::SceneSession evaluated;
    if (!format::SceneSession::Create(next.document, evaluated, reason) ||
        !evaluated.Seek(unsigned(next.document.timeline.currentFrame))) {
        if (reason.empty())
            reason = "morph-key-purge-evaluation-failed";
        return false;
    }
    for (const auto& value : evaluated.Document().models)
        if (value.slot == selection_.model)
            *model = value;
    if (!format::ReadPmmSelectedKeys(next.document, selection_.model, next.selection.keys)) {
        reason = "morph-key-purge-selection-failed";
        return false;
    }
    HistoryBatch history;
    for (const auto& value : next.document.models)
        history.emplace(value.slot, CaptureHistory(value, HistoryKind::None, 0));
    EndGesture();
    next.document.timeline.editMode = Document().timeline.editMode;
    return Commit(std::move(next), reason, history, false);
}
}
