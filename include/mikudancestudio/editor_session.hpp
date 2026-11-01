#pragma once
#include "scene_runtime.hpp"
#include "pmm_edit.hpp"
#include <filesystem>
#include <variant>

namespace mikudancestudio::editor {
enum class Track {
    Bone,
    Morph,
    Display,
    Camera,
    Light,
    Shadow,
    Gravity,
    Accessory
};
struct KeyAddress {
    Track track = Track::Camera;
    std::uint8_t slot = 255;
    std::uint32_t channel = 0, frame = 0;
    bool operator<(const KeyAddress& b) const {
        return std::tie(track, slot, channel, frame) <
               std::tie(b.track, b.slot, b.channel, b.frame);
    }
};
struct Selection {
    std::uint8_t model = 255, accessory = 255;
    std::vector<std::uint32_t> bones;
    std::set<KeyAddress> keys;
    std::set<KeyAddress> tracks; // frame-independent row selections use frame zero
};
enum class KeyScopeKind {
    All,
    Bones,
    Morphs,
    SelectedBones,
    SelectedMorphs,
    Track
};
struct KeyScope {
    KeyScopeKind kind = KeyScopeKind::All;
    KeyAddress track;
};
using Key = std::variant<format::PmmBoneKey, format::PmmMorphKey, format::PmmDisplayKey,
                         format::PmmCameraKey, format::PmmLightKey, format::PmmSelfShadowKey,
                         format::PmmGravityKey, format::PmmAccessoryKey>;
struct ClipboardKey {
    KeyAddress address;
    Key key;
    std::vector<char> name;
};
using InterpolationCurve = std::array<int, 4>; // x1, y1, x2, y2; signed FX9 bytes
struct InterpolationSelection {
    std::vector<InterpolationCurve> curves;
    std::array<InterpolationCurve, 6> channels{};
    bool found = false, uniform = false, canCopy = false;
};
struct EditorAssets {
    std::map<std::uint8_t, std::shared_ptr<const runtime::ModelResource>> models;
    std::map<std::uint8_t, std::shared_ptr<const runtime::AccessoryResource>> accessories;
    std::map<std::uint8_t, std::filesystem::path> modelPaths, accessoryPaths;
    std::filesystem::path wave;
};
// No window/GPU objects in documents, commands, selection or undo history.
// Hosts supply the atomic disk replacement policy (QSaveFile in the Qt host).
class EditorSession final {
public:
    using AtomicWrite = std::function<bool(const std::filesystem::path&,
                                           const std::vector<std::uint8_t>&, std::string&)>;
    EditorSession();
    bool New(std::string&);
    bool Open(const std::filesystem::path&, std::string&);
    bool Save(const std::filesystem::path&, const AtomicWrite&, std::string&);
    bool ImportModel(const std::filesystem::path&, std::string&);
    bool ImportAccessory(const std::filesystem::path&, std::string&);
    bool ImportWave(const std::filesystem::path&, std::string&);
    bool UnloadModel(std::uint8_t, std::string&);
    bool UnloadAccessory(std::uint8_t, std::string&);
    bool ImportMotion(const format::VmdDocument&, std::uint8_t slot, std::string&);
    bool ImportPose(const format::VpdDocument&, std::uint8_t slot, std::string&);
    bool ExportMotion(std::uint8_t slot, format::VmdDocument&, std::string&) const;
    bool ExportPose(std::uint8_t slot, format::VpdDocument&, std::string&) const;
    bool SetBone(std::uint8_t, std::uint32_t, const std::array<float, 3>&,
                 const std::array<float, 4>&, std::string&);
    bool SetMorph(std::uint8_t, std::uint32_t, float, std::string&);
    bool SetAccessory(std::uint8_t, const format::PmmAccessoryState&, std::string&);
    bool SetCamera(const format::PmmEvaluatedCamera&, std::string&);
    bool Register(const std::vector<KeyAddress>&, std::string&);
    bool Delete(const std::set<KeyAddress>&, std::string&);
    bool Copy(const std::set<KeyAddress>&, std::string&);
    bool Paste(std::uint32_t frame, bool mirror, std::string&);
    InterpolationSelection Interpolation(unsigned channel) const;
    bool CopyInterpolation(unsigned channel, std::string&);
    bool PasteInterpolation(unsigned channel, std::string&);
    bool LinearInterpolation(unsigned channel, std::string&);
    bool MoveInterpolationPoint(unsigned channel, unsigned point, int x, int y, std::string&);
    bool HasInterpolationClipboard() const { return interpolationCopied_; }
    // A continuous gesture keeps its live edits but creates only one undo entry.
    bool BeginGesture();
    void EndGesture();
    bool Edit(const std::string& label,
              const std::function<bool(format::PmmSceneDocument&, std::string&)>&, std::string&);
    bool Undo(std::string&);
    bool Redo(std::string&);
    bool Seek(std::uint32_t, std::string&);
    void SetPanelExpanded(unsigned option, bool expanded);
    void SetGroupExpanded(std::uint8_t slot, unsigned group, bool expanded);
    bool Play(std::string&);
    void Pause();
    bool Advance(double deltaSeconds, std::string&);
    bool Playing() const { return playing_; }
    double CursorSeconds() const { return cursorSeconds_; }
    std::uint64_t PlaybackEpoch() const { return playbackEpoch_; }
    bool Dirty() const { return revision_ != savedRevision_; }
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    const format::PmmSceneDocument& Document() const;
    const runtime::SceneRuntime& Runtime() const { return *runtime_; }
    const EditorAssets& Assets() const { return state_.assets; }
    const std::filesystem::path& Path() const { return path_; }
    std::uint64_t Revision() const { return revision_; }
    Selection& Selected() { return selection_; }
    const Selection& Selected() const { return selection_; }
    std::vector<KeyAddress> Keys() const;
    void SelectKeys(std::uint32_t first, std::uint32_t last, const KeyScope&);
    void ExtendSelectedColumns();

private:
    struct State {
        format::PmmSceneDocument document;
        EditorAssets assets;
        Selection selection;
        std::uint64_t revision = 0;
    };
    State state_;
    std::unique_ptr<runtime::SceneRuntime> runtime_;
    std::vector<State> undo_, redo_;
    std::vector<ClipboardKey> clipboard_;
    std::array<InterpolationCurve, 6> interpolationClipboard_{};
    bool interpolationCopied_ = false, gesture_ = false, gestureCommitted_ = false;
    Selection selection_;
    std::filesystem::path path_;
    std::uint64_t revision_ = 0, savedRevision_ = 0, nextRevision_ = 1;
    bool playing_ = false;
    double cursorSeconds_ = 0;
    std::uint32_t playOrigin_ = 0;
    std::uint64_t playbackEpoch_ = 0;
    State Current() const;
    bool Prepare(State&, std::unique_ptr<runtime::SceneRuntime>&, std::string&) const;
    bool Commit(State, std::string&);
    bool ChangeInterpolation(unsigned channel,
                             const std::function<void(unsigned, InterpolationCurve&)>&,
                             std::string&);
};
}
