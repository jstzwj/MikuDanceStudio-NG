#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/morph_key_purge.hpp"
#include "mikudancestudio/pmm_key_selection.hpp"
#include "mikudancestudio/blink_registration.hpp"
#include "mikudancestudio/pmm_morph_registration.hpp"
#include <cstring>

namespace mikudancestudio::editor {
namespace {
bool BlinkName(const std::vector<char>& name) {
    static constexpr char expected[]{char(0x82), char(0xdc), char(0x82), char(0xce),
                                     char(0x82), char(0xbd), char(0x82), char(0xab)};
    return name.size() == sizeof expected && std::equal(name.begin(), name.end(), expected);
}
}
bool EditorSession::HasBlinkMorph() const {
    for (const auto& model : Document().models)
        if (model.slot == selection_.model)
            return std::any_of(model.morphNames.begin(), model.morphNames.end(), BlinkName);
    return false;
}
bool EditorSession::RegisterBlinks(std::int32_t start, std::int32_t end, unsigned& count,
                                   std::string& reason, const std::function<unsigned()>& random15,
                                   unsigned* failedKeys) {
    if (playing_ || !HasBlinkMorph()) {
        reason = "blink-registration-unavailable";
        return false;
    }
    auto next = Current();
    auto model = std::find_if(next.document.models.begin(), next.document.models.end(),
                              [&](const auto& m) { return m.slot == selection_.model; });
    const auto channel =
        unsigned(std::find_if(model->morphNames.begin(), model->morphNames.end(), BlinkName) -
                 model->morphNames.begin());
    auto& track = model->morphKeys;
    format::PmmMorphRegistration pool;
    if (!format::SetPmmSelectedKeys(next.document, selection_.model, selection_.keys) ||
        !pool.Load(track)) {
        reason = "blink-track-invalid";
        return false;
    }
    auto phase = blinkPhase_;
    auto randomState = blinkRandomState_;
    unsigned added = 0, failed = 0;
    auto frame = std::uint32_t(start);
    while (frame < std::uint32_t(end)) {
        const auto random = random15 ? random15() : keyframe_edit::BlinkRandom15(randomState);
        if (random > 32767u) {
            reason = "blink-random-out-of-range";
            return false;
        }
        if (!keyframe_edit::AdvanceBlink(frame, phase, random))
            continue;
        if (frame > 0x7fffffffU - 6) {
            reason = "blink-frame-not-representable";
            return false;
        }
        // Stage the complete operation before publishing the document.
        for (unsigned i = 0; i < 4; ++i) {
            format::PmmMorphKey key;
            key.frame = frame + (i == 0 ? 0 : i == 1 ? 2 : i == 2 ? 3 : 6);
            key.value = i == 1 || i == 2 ? 1.f : 0.f;
            key.allocated = 1;
            if (!pool.Register(unsigned(track.dense.size()), channel, key.frame, key.value,
                               model->selection.maxFrame))
                ++failed;
        }
        model->morphPose[channel] = 0;
        ++added;
    }
    pool.Store(track);
    next.document.timeline.lastRegisteredFrame =
        std::max(next.document.timeline.lastRegisteredFrame, int(model->selection.maxFrame));
    if (!format::ReadPmmSelectedKeys(next.document, selection_.model, next.selection.keys)) {
        reason = "blink-selection-invalid";
        return false;
    }
    HistoryBatch history;
    for (const auto& value : next.document.models)
        history.emplace(value.slot, CaptureHistory(value, HistoryKind::None, 0));
    EndGesture();
    next.document.timeline.editMode = Document().timeline.editMode;
    if (!Commit(std::move(next), reason, history, false))
        return false;
    blinkPhase_ = phase;
    blinkRandomState_ = randomState;
    count = added;
    if (failedKeys)
        *failedKeys = failed;
    return true;
}
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
