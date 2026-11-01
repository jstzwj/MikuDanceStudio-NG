#pragma once
#include "scene_runtime.hpp"
#include "pmm_edit.hpp"
#include "rhi_image.hpp"
#include "bone_selection.hpp"
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
    std::filesystem::path picturePath;
    std::shared_ptr<const rhi::RgbaImage> picture;
};
struct BoneDragInput {
    int mode = 0; // 1..6 rotation, 7..9 view plane, 10..12 local/world translation.
    int x = 0, y = 0, previousX = 0, previousY = 0, originX = 0, originY = 0;
    int viewWidth = 0, viewHeight = 0;
    float clipW = 1, viewScale = 1;
    bool shift = false, control = false;
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
    bool ImportPicture(const std::filesystem::path&, unsigned viewWidth, unsigned viewHeight,
                       std::string&);
    bool ShowPicture(bool visible, std::string&);
    bool UnloadModel(std::uint8_t, std::string&);
    bool UnloadAccessory(std::uint8_t, std::string&);
    bool ImportMotion(const format::VmdDocument&, std::uint8_t slot, std::string&);
    bool ImportPose(const format::VpdDocument&, std::uint8_t slot, std::string&);
    bool ExportMotion(std::uint8_t slot, format::VmdDocument&, std::string&) const;
    bool ExportPose(std::uint8_t slot, format::VpdDocument&, std::string&) const;
    bool CopyBonePose(std::string&);
    bool PasteBonePose(bool mirrored, std::string&);
    bool ResetSelectedBonePose(std::string&);
    bool HasBonePoseClipboard() const { return !poseClipboard_.empty(); }
    bool SetBone(std::uint8_t, std::uint32_t, const std::array<float, 3>&,
                 const std::array<float, 4>&, std::string&);
    bool SetMorph(std::uint8_t, std::uint32_t, float, std::string&);
    bool SetAccessory(std::uint8_t, const format::PmmAccessoryState&, std::string&);
    bool SetCamera(const format::PmmEvaluatedCamera&, std::string&);
    bool DragBones(const BoneDragInput&, std::string&);
    bool BeginBoneDrag(unsigned mode, std::string&);
    void EndBoneDrag();
    unsigned BoneDragMode() const { return boneDragMode_; }
    bool DragView(bool middleButton, int dx, int dy, bool shift, bool control, std::string&);
    bool DragSceneAxis(unsigned mode, int dy, bool shift, bool control, std::string&);
    bool DragViewTool(unsigned operation, int dx, int dy, bool shift, bool control, float viewScale,
                      std::string&);
    bool WheelView(int delta, std::string&);
    bool PrepareView(std::string&);
    bool ViewPreset(unsigned numpadKey, std::string&);
    bool SetCameraReference(unsigned mode, std::string&);
    bool SetFollowCamera(bool enabled, std::string&);
    int ActiveBone() const;
    bool Register(const std::vector<KeyAddress>&, std::string&);
    bool Delete(const std::set<KeyAddress>&, std::string&);
    bool InsertFrameLine(Track, std::string&, bool fromMenu = false);
    bool DeleteFrameLine(Track, std::string&, bool fromMenu = false);
    bool AutoInterpolation() const { return autoInterpolation_; }
    void SetAutoInterpolation(bool enabled) { autoInterpolation_ = enabled; }
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
    void SetSidebarWidth(int width);
    void SetPhysicsFloor(bool enabled);
    void SetGroupExpanded(std::uint8_t slot, unsigned group, bool expanded);
    void SetEditMode(std::int32_t mode);
    unsigned CoordinateSystem() const { return coordinateSystem_; }
    void CycleCoordinateSystem();
    bool Play(std::string&);
    void Pause();
    bool Advance(double deltaSeconds, std::string&);
    bool Playing() const { return playing_; }
    double CursorSeconds() const { return cursorSeconds_; }
    std::uint64_t PlaybackEpoch() const { return playbackEpoch_; }
    bool Dirty() const { return revision_ != savedRevision_; }
    bool CanUndo() const;
    bool CanRedo() const;
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
    // These only change selection metadata, never evaluate the scene or create history.
    bool SelectBones(const std::vector<std::uint32_t>&, int active, std::string&);
    bool SelectModel(std::uint8_t, std::string&);
    bool SelectAllBones(bool unregisteredOnly, std::string&);
    bool PickBone(const std::vector<cpu_bone::ScreenPoint>&, int x, int y, bool additive,
                  std::string&);
    bool BoxSelectBones(const std::vector<cpu_bone::ScreenPoint>&, int left, int top, int right,
                        int bottom, bool additive, bool finish, std::string&);

private:
    struct State {
        format::PmmSceneDocument document;
        EditorAssets assets;
        Selection selection;
        std::uint64_t revision = 0;
    };
    State state_;
    std::unique_ptr<runtime::SceneRuntime> runtime_;
    enum class HistoryKind {
        None,
        Pose,
        Keys,
        PoseWithFrame
    };
    struct HistoryPose {
        std::uint32_t index = 0;
        std::array<float, 7> components{};
        std::uint8_t edited = 0;
    };
    struct HistoryEntry {
        HistoryKind kind = HistoryKind::Pose;
        std::uint32_t frame = 0;
        std::shared_ptr<const HistoryEntry> originalUndo;
        std::vector<HistoryPose> poses;
        format::PmmKeyTrack<format::PmmBoneKey> keys;
        // Insertion keeps native record identity; selection follows the same
        // records across undo/redo instead of staying at their former frames.
        bool insertedFrameLine = false;
        std::set<KeyAddress> frameLineSelection;
        bool partialKeys = false;
        std::map<std::uint32_t, format::PmmBoneKey> keyRecords;
        std::set<std::uint32_t> selectedKeyRecords;
    };
    struct ModelHistory {
        std::vector<HistoryEntry> undo, redo;
    };
    std::map<std::uint8_t, ModelHistory> histories_;
    using HistoryBatch = std::map<std::uint8_t, HistoryEntry>;
    std::set<std::uint8_t> gestureModels_;
    std::vector<ClipboardKey> clipboard_;
    struct CopiedBonePose {
        std::vector<char> name;
        std::array<float, 3> position;
        std::array<float, 4> rotation;
    };
    std::vector<CopiedBonePose> poseClipboard_;
    std::array<InterpolationCurve, 6> interpolationClipboard_{};
    bool interpolationCopied_ = false, gesture_ = false;
    Selection selection_;
    std::filesystem::path path_;
    std::uint64_t revision_ = 0, savedRevision_ = 0, nextRevision_ = 1;
    bool playing_ = false;
    bool autoInterpolation_ = true;
    unsigned coordinateSystem_ = 0, boneDragMode_ = 0;
    double cursorSeconds_ = 0;
    std::uint32_t playOrigin_ = 0;
    std::uint64_t playbackEpoch_ = 0;
    State Current() const;
    bool Prepare(State&, std::unique_ptr<runtime::SceneRuntime>&, std::string&) const;
    bool Commit(State, std::string&, const HistoryBatch& forced = {},
                bool updateFrameBounds = true);
    void RecordHistory(const State& before, const State& after, const HistoryBatch& forced);
    void PushHistory(std::uint8_t, HistoryEntry);
    HistoryBatch FrameHistory() const;
    bool ApplyHistory(bool redo, std::string&);
    bool ChangeFrameLine(Track, bool insert, std::string&, bool fromMenu);
    static HistoryEntry CaptureHistory(const format::PmmSceneModel&, HistoryKind,
                                       std::uint32_t frame,
                                       const std::vector<std::uint32_t>& indices = {});
    bool ChangeInterpolation(unsigned channel,
                             const std::function<void(unsigned, InterpolationCurve&)>&,
                             std::string&);
    bool ApplyView(const format::PmmEvaluatedCamera&, const std::array<float, 2>&, std::string&);
    bool RestoreModelSelection(std::uint8_t, std::string&);
    int ActiveBone(std::uint8_t) const;
    bool ReanchorModelView(std::uint8_t, format::PmmEvaluatedCamera&, std::array<float, 2>&,
                           cpu_math::Matrix& basis, std::string&) const;
    bool AppliesGlobalTracks() const;
    format::PmmSceneApplication PlaybackApplication() const;
    bool RefreshTrackedView(bool reanchor, std::string&);
    bool ViewReferencePoint(std::uint8_t, unsigned mode, bool fallbackToRoot, cpu_math::Vec3&,
                            std::string&) const;
};
}
