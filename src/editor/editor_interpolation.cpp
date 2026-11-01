#include "mikudancestudio/editor_session.hpp"

namespace mikudancestudio::editor {
namespace {
InterpolationCurve Read(const format::PmmBoneKey& key, unsigned channel) {
    InterpolationCurve result;
    for (unsigned i = 0; i < 4; ++i)
        result[i] = static_cast<std::int8_t>(key.interpolation[i * 4 + channel]);
    return result;
}
InterpolationCurve Read(const format::PmmCameraKey& key, unsigned channel) {
    InterpolationCurve result;
    for (unsigned i = 0; i < 4; ++i)
        result[i] = static_cast<std::int8_t>(key.interpolation[i][channel]);
    return result;
}
void Write(format::PmmBoneKey& key, unsigned channel, const InterpolationCurve& curve) {
    for (unsigned i = 0; i < 4; ++i)
        key.interpolation[i * 4 + channel] = std::uint8_t(curve[i]);
}
void Write(format::PmmCameraKey& key, unsigned channel, const InterpolationCurve& curve) {
    for (unsigned i = 0; i < 4; ++i)
        key.interpolation[i][channel] = std::uint8_t(curve[i]);
}
template <class Document, class F>
bool SelectedKeys(Document& document, const Selection& selection, F&& visit) {
    const auto scan = [&](auto& track, Track kind, std::uint8_t slot) {
        using K = typename std::decay_t<decltype(track.dense)>::value_type;
        std::vector<std::vector<K>> chains;
        if (!format::PmmTrackChains(track, chains))
            return false;
        for (unsigned i = 0; i < chains.size(); ++i)
            for (auto& key : chains[i]) {
                const KeyAddress address{kind, slot, i, key.frame};
                if (selection.keys.count(address) && !visit(track, address, key))
                    return false;
            }
        return true;
    };
    if (selection.model == 255)
        return scan(document.cameraKeys, Track::Camera, 255);
    for (auto& model : document.models)
        if (model.slot == selection.model)
            return scan(model.boneKeys, Track::Bone, model.slot);
    return false;
}
}
InterpolationSelection EditorSession::Interpolation(unsigned channel) const {
    InterpolationSelection result;
    const unsigned count = selection_.model == 255 ? 6 : 4;
    if (channel > count)
        return result;
    bool perChannel = true;
    SelectedKeys(Document(), selection_, [&](const auto&, const auto&, const auto& key) {
        for (unsigned c = channel < count ? channel : 0;
             c < (channel < count ? channel + 1 : count); ++c) {
            const auto curve = Read(key, c);
            if (!result.found)
                result.channels[c] = curve;
            else if (curve != result.channels[c])
                perChannel = false;
            if (std::find(result.curves.begin(), result.curves.end(), curve) == result.curves.end())
                result.curves.push_back(curve);
        }
        result.found = true;
        return true;
    });
    result.uniform = result.curves.size() == 1;
    result.canCopy = result.found && perChannel;
    return result;
}
bool EditorSession::CopyInterpolation(unsigned channel, std::string& reason) {
    const auto selected = Interpolation(channel);
    if (!selected.canCopy) {
        reason = "interpolation-selection-not-uniform";
        return false;
    }
    const unsigned count = selection_.model == 255 ? 6 : 4;
    if (channel < count)
        interpolationClipboard_.fill(selected.channels[channel]);
    else
        std::copy_n(selected.channels.begin(), count, interpolationClipboard_.begin());
    interpolationCopied_ = true;
    reason.clear();
    return true;
}
bool EditorSession::ChangeInterpolation(
    unsigned channel, const std::function<void(unsigned, InterpolationCurve&)>& change,
    std::string& reason) {
    const unsigned count = selection_.model == 255 ? 6 : 4;
    if (playing_ || channel > count) {
        reason = "interpolation-edit-unavailable";
        return false;
    }
    auto next = Current();
    bool found = false, modified = false;
    if (!SelectedKeys(next.document, selection_,
                      [&](auto& track, const auto& address, auto& key) {
                          found = true;
                          bool dirty = false;
                          for (unsigned c = channel < count ? channel : 0;
                               c < (channel < count ? channel + 1 : count); ++c) {
                              const auto before = Read(key, c);
                              auto after = before;
                              change(c, after);
                              if (before != after) {
                                  Write(key, c, after);
                                  dirty = true;
                              }
                          }
                          modified |= dirty;
                          return !dirty || format::RegisterPmmKey(track, address.channel, key);
                      }) ||
        !found) {
        reason = "interpolation-selection-empty";
        return false;
    }
    if (!modified) {
        reason.clear();
        return true;
    }
    return Commit(std::move(next), reason);
}
bool EditorSession::PasteInterpolation(unsigned channel, std::string& reason) {
    if (!interpolationCopied_) {
        reason = "interpolation-clipboard-empty";
        return false;
    }
    return ChangeInterpolation(
        channel, [&](unsigned c, auto& curve) { curve = interpolationClipboard_[c]; }, reason);
}
bool EditorSession::LinearInterpolation(unsigned channel, std::string& reason) {
    return ChangeInterpolation(
        channel, [](unsigned, auto& curve) { curve = {20, 20, 107, 107}; }, reason);
}
bool EditorSession::MoveInterpolationPoint(unsigned channel, unsigned point, int x, int y,
                                           std::string& reason) {
    if (point > 1 || !Interpolation(channel).uniform) {
        reason = "interpolation-drag-unavailable";
        return false;
    }
    return ChangeInterpolation(
        channel,
        [&](unsigned, auto& curve) {
            curve[point * 2] = std::clamp(x, 0, 127);
            curve[point * 2 + 1] = std::clamp(y, 0, 127);
        },
        reason);
}
bool EditorSession::BeginGesture() {
    if (playing_ || gesture_)
        return false;
    gesture_ = true;
    gestureModels_.clear();
    return true;
}
void EditorSession::EndGesture() {
    if (boneDragMode_ && Document().timeline.editMode == 5) {
        if (boneDragMode_ <= 3)
            runtime_->SetEditMode(3);
        else if (boneDragMode_ >= 7 && boneDragMode_ <= 9)
            runtime_->SetEditMode(4);
    }
    boneDragMode_ = 0;
    gesture_ = false;
    gestureModels_.clear();
}
}
