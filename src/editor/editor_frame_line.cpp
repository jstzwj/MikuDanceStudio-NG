#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmm_frame_line.hpp"
#include <algorithm>

namespace mikudancestudio::editor {
bool EditorSession::InsertFrameLine(Track track, std::string& reason, bool fromMenu) {
    return ChangeFrameLine(track, true, reason, fromMenu);
}
bool EditorSession::DeleteFrameLine(Track track, std::string& reason, bool fromMenu) {
    return ChangeFrameLine(track, false, reason, fromMenu);
}
bool EditorSession::ChangeFrameLine(Track track, bool insertion, std::string& reason,
                                    bool fromMenu) {
    const bool modelTrack = track == Track::Bone || track == Track::Morph;
    if (playing_ || (!modelTrack && track != Track::Camera && track != Track::Light) ||
        modelTrack != (selection_.model != 255)) {
        reason = "frame-line-unavailable";
        return false;
    }
    auto next = Current();
    auto& doc = next.document;
    const auto frame = std::uint32_t(doc.timeline.currentFrame);
    unsigned affected = 0;
    std::uint32_t maximum = 0;
    HistoryBatch history;
    // Seeking the edited model also changes its evaluated bone poses. Only
    // the native bone command records history; morph/camera/light do not.
    for (const auto& model : doc.models)
        history.emplace(model.slot, CaptureHistory(model, HistoryKind::None, frame));
    auto model = std::find_if(doc.models.begin(), doc.models.end(),
                              [&](const auto& value) { return value.slot == selection_.model; });
    bool ok;
    if (modelTrack) {
        if (model == doc.models.end()) {
            reason = "frame-line-model-expired";
            return false;
        }
        if (track == Track::Bone) {
            history[model->slot] = CaptureHistory(*model, HistoryKind::Keys, frame);
            history[model->slot].insertedFrameLine = insertion;
            for (const auto& address : selection_.keys)
                if (address.track == Track::Bone && address.slot == model->slot && address.frame &&
                    address.frame >= frame)
                    history[model->slot].frameLineSelection.insert(address);
        }
    }
    const auto edit = [&](auto& keys) {
        if (insertion) {
            if (!format::InsertPmmFrameLine(keys, frame, affected))
                return false;
            for (const auto& key : keys.sparse)
                if (key.value.frame > frame)
                    maximum = std::max(maximum, key.value.frame);
            return true;
        }
        using T = typename std::decay_t<decltype(keys.dense)>::value_type;
        std::map<std::uint32_t, std::uint32_t> channels;
        if (!format::PmmRecordChannels(keys, channels))
            return false;
        // Selection is represented by addresses in the Qt frontend, but native
        // key mutation/history operate on each record's selection byte.
        const auto sync = [&](std::uint32_t index, auto& key) {
            const bool selected =
                next.selection.keys.count({track, selection_.model, channels.at(index), key.frame});
            if constexpr (std::is_same_v<T, format::PmmBoneKey> ||
                          std::is_same_v<T, format::PmmMorphKey>)
                key.allocated = selected;
            else
                key.selected = selected;
        };
        for (unsigned i = 0; i < keys.dense.size(); ++i)
            sync(i, keys.dense[i]);
        for (auto& key : keys.sparse)
            sync(unsigned(key.index), key.value);
        format::PmmFrameLineDeletion<T> result;
        if (!format::DeletePmmFrameLine(keys, frame, autoInterpolation_, result))
            return false;
        affected = result.affected;
        if constexpr (std::is_same_v<T, format::PmmBoneKey>) {
            auto& entry = history.at(model->slot);
            entry.partialKeys = true;
            entry.keyRecords = std::move(result.captured);
            entry.keys = {};
            for (const auto& [index, key] : entry.keyRecords)
                if (key.allocated)
                    entry.selectedKeyRecords.insert(index);
        }
        for (auto it = next.selection.keys.begin(); it != next.selection.keys.end();) {
            if (it->track == track && it->slot == selection_.model)
                it = next.selection.keys.erase(it);
            else
                ++it;
        }
        const auto select = [&](std::uint32_t index, const auto& key) {
            bool selected;
            if constexpr (std::is_same_v<T, format::PmmBoneKey> ||
                          std::is_same_v<T, format::PmmMorphKey>)
                selected = key.allocated != 0;
            else
                selected = key.selected != 0;
            if (selected)
                next.selection.keys.insert(
                    {track, selection_.model, channels.at(index), key.frame});
        };
        for (unsigned i = 0; i < keys.dense.size(); ++i)
            select(i, keys.dense[i]);
        for (const auto& key : keys.sparse)
            select(unsigned(key.index), key.value);
        return true;
    };
    if (modelTrack)
        ok = track == Track::Bone ? edit(model->boneKeys) : edit(model->morphKeys);
    else
        ok = track == Track::Camera ? edit(doc.cameraKeys) : edit(doc.lightKeys);
    if (!ok) {
        reason = "frame-line-invalid-track-or-overflow";
        return false;
    }
    if (track == Track::Bone && !affected) {
        // The keyboard helper returns immediately. Its menu dispatcher still
        // sets the dirty bit, without evaluating or adding an undo record.
        if (fromMenu)
            revision_ = nextRevision_++;
        reason.clear();
        return true;
    }
    if (modelTrack)
        model->selection.maxFrame = std::max(model->selection.maxFrame, maximum);
    else
        doc.timeline.lastRegisteredFrame = std::max(doc.timeline.lastRegisteredFrame, int(maximum));

    format::SceneSession evaluated;
    if (!format::SceneSession::Create(doc, evaluated, reason) || !evaluated.Seek(frame))
        return false;
    const auto& value = evaluated.Document();
    if (modelTrack) {
        *model = *std::find_if(value.models.begin(), value.models.end(),
                               [&](const auto& m) { return m.slot == selection_.model; });
        for (auto& pose : model->bonePose)
            pose.flags[1] = 0;
    } else if (track == Track::Camera) {
        doc.cameraState = value.cameraState;
        doc.cameraState[3] = doc.cameraState[4] = 0;
        doc.cameraPerspective = value.cameraPerspective;
        doc.envelope.settings.cameraFov = value.envelope.settings.cameraFov;
        doc.appearance.cameraParentModel = value.appearance.cameraParentModel;
        doc.appearance.cameraParentBone = value.appearance.cameraParentBone;
    } else {
        doc.lightState = value.lightState;
    }
    std::set<KeyAddress> selected;
    for (auto address : next.selection.keys) {
        if (insertion && address.track == track && address.slot == selection_.model &&
            address.frame && address.frame >= frame)
            ++address.frame;
        selected.insert(address);
    }
    next.selection.keys = std::move(selected);
    EndGesture();
    return Commit(std::move(next), reason, history, false);
}
}
