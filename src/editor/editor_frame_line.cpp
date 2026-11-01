#include "mikudancestudio/editor_session.hpp"
#include <algorithm>

namespace mikudancestudio::editor {
bool EditorSession::InsertFrameLine(Track track, std::string& reason, bool fromMenu) {
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
    const auto insert = [&](auto& keys) {
        if (!format::InsertPmmFrameLine(keys, frame, affected))
            return false;
        for (const auto& key : keys.sparse)
            if (key.value.frame > frame)
                maximum = std::max(maximum, key.value.frame);
        return true;
    };
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
            history[model->slot].insertedFrameLine = true;
            for (const auto& address : selection_.keys)
                if (address.track == Track::Bone && address.slot == model->slot && address.frame &&
                    address.frame >= frame)
                    history[model->slot].frameLineSelection.insert(address);
        }
        ok = track == Track::Bone ? insert(model->boneKeys) : insert(model->morphKeys);
    } else {
        ok = track == Track::Camera ? insert(doc.cameraKeys) : insert(doc.lightKeys);
    }
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
        if (address.track == track && address.slot == selection_.model && address.frame &&
            address.frame >= frame)
            ++address.frame;
        selected.insert(address);
    }
    next.selection.keys = std::move(selected);
    EndGesture();
    return Commit(std::move(next), reason, history, false);
}
}
