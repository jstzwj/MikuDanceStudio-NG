#include "mikudancestudio/editor_session.hpp"
namespace mikudancestudio::editor {
namespace {
bool ModelTrack(Track t) {
    return t == Track::Bone || t == Track::Morph || t == Track::Display;
}
bool Active(const Selection& selected, const KeyAddress& key) {
    return selected.model == 255 ? !ModelTrack(key.track)
                                 : ModelTrack(key.track) && key.slot == selected.model;
}
}
void EditorSession::SelectKeys(std::uint32_t first, std::uint32_t last, const KeyScope& scope) {
    const auto matches = [&](const KeyAddress& key) {
        if (!Active(selection_, key))
            return false;
        switch (scope.kind) {
        case KeyScopeKind::All:
            return true;
        case KeyScopeKind::Bones:
            return key.track == Track::Bone;
        case KeyScopeKind::Morphs:
            return key.track == Track::Morph;
        case KeyScopeKind::SelectedBones:
            return key.track == Track::Bone &&
                   std::find(selection_.bones.begin(), selection_.bones.end(), key.channel) !=
                       selection_.bones.end();
        case KeyScopeKind::SelectedMorphs: {
            auto row = key;
            row.frame = 0;
            return key.track == Track::Morph && selection_.tracks.count(row) != 0;
        }
        case KeyScopeKind::Track:
            return key.track == scope.track.track && key.slot == scope.track.slot &&
                   key.channel == scope.track.channel;
        }
        return false;
    };
    // Native array sweeps replace marks only in their addressed tracks.
    // MarkKeyTrackRangeByName (selected/named model rows) adds marks instead.
    const bool replace = scope.kind == KeyScopeKind::All || scope.kind == KeyScopeKind::Bones ||
                         scope.kind == KeyScopeKind::Morphs ||
                         (scope.kind == KeyScopeKind::Track && !ModelTrack(scope.track.track));
    if (replace)
        for (auto it = selection_.keys.begin(); it != selection_.keys.end();)
            if (matches(*it))
                it = selection_.keys.erase(it);
            else
                ++it;
    for (const auto& key : Keys())
        if (matches(key) && first <= key.frame && key.frame <= last)
            selection_.keys.insert(key);
}
void EditorSession::ExtendSelectedColumns() {
    std::set<std::uint32_t> frames;
    for (const auto& key : selection_.keys)
        if (Active(selection_, key))
            frames.insert(key.frame);
    for (const auto& key : Keys())
        if (Active(selection_, key) && frames.count(key.frame))
            selection_.keys.insert(key);
}
}
