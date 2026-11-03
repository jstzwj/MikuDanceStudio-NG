#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmm_key_deletion.hpp"
#include "mikudancestudio/pmm_bone_registration.hpp"
#include "mikudancestudio/pmm_morph_registration.hpp"
#include "mikudancestudio/pmm_bone_clipboard.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/vmd_scene_tracks.hpp"
#include "mikudancestudio/wave_format.hpp"
#include "mikudancestudio/vac_format.hpp"
#include "mikudancestudio/background_placement.hpp"
#include "mikudancestudio/bone_key_interpolation.hpp"
#include "mikudancestudio/gravity_edit.hpp"
#include "mikudancestudio/key_reduction.hpp"
#include "mikudancestudio/key_scale.hpp"
#include "mikudancestudio/bone_key_scale.hpp"
#include "mikudancestudio/physics_key_edit.hpp"
#include <fstream>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>

namespace mikudancestudio::editor {
using namespace format;
namespace {
bool ReadPicture(const std::filesystem::path& path, std::shared_ptr<const rhi::RgbaImage>& image,
                 bool& opened, std::string& reason) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    opened = bool(file);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < 0 || size > (256u << 20)) {
        reason = "picture-file-read-failed";
        return false;
    }
    std::vector<std::uint8_t> bytes(std::size_t(size), 0);
    file.seekg(0);
    auto decoded = std::make_shared<rhi::RgbaImage>();
    if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) ||
        !rhi::DecodeTextureRgba(bytes.data(), bytes.size(), *decoded)) {
        reason = "picture-decode-failed";
        return false;
    }
    decoded->pixels.Seal();
    image = std::move(decoded);
    return true;
}
int Read(void* p, void* dst, std::size_t n) {
    auto& f = *static_cast<std::istream*>(p);
    f.read(static_cast<char*>(dst), std::streamsize(n));
    return int(f.gcount());
}
bool Bytes(void* p, const void* bytes, std::size_t n) {
    auto& out = *static_cast<std::vector<std::uint8_t>*>(p);
    auto first = static_cast<const std::uint8_t*>(bytes);
    out.insert(out.end(), first, first + n);
    return true;
}
std::vector<char> Name(const std::string& utf8) {
    std::string encoded;
    if (!text::EncodeCp932(utf8, encoded) || encoded.size() > 255)
        throw std::runtime_error("name-not-representable-in-pmm");
    return {encoded.begin(), encoded.end()};
}
template <std::size_t N> bool Fixed(const std::string& text, std::array<char, N>& output) {
    if (text.size() > N || text.find('\0') != std::string::npos)
        return false;
    output.fill(0);
    std::copy(text.begin(), text.end(), output.begin());
    return true;
}
bool DiskPath(const std::filesystem::path& source, const std::filesystem::path& base,
              std::array<char, 256>& target, std::string& reason) {
    const auto relative = source.lexically_relative(base);
    const auto path = relative.empty() ? source : relative;
    std::string encoded;
    if (!text::EncodeCp932(path.generic_u8string(), encoded) || encoded.size() >= target.size()) {
        reason = "pmm-path-not-representable:" + path.u8string();
        return false;
    }
    std::replace(encoded.begin(), encoded.end(), '/', '\\');
    return Fixed(encoded, target);
}
PmmSceneModel* Model(PmmSceneDocument& d, std::uint8_t slot) {
    auto found = std::find_if(d.models.begin(), d.models.end(),
                              [&](const auto& m) { return m.slot == slot; });
    return found == d.models.end() ? nullptr : &*found;
}
void ClearModelKeyMarks(PmmSceneModel& model, Selection& selection) {
    const auto clear = [](auto& track) {
        for (auto& key : track.dense)
            key.allocated = 0;
        for (auto& key : track.sparse)
            key.value.allocated = 0;
        for (auto& [index, key] : track.unused)
            key.allocated = 0;
    };
    clear(model.boneKeys);
    clear(model.morphKeys);
    clear(model.displayKeys);
    for (auto it = selection.keys.begin(); it != selection.keys.end();)
        if (it->slot == model.slot &&
            (it->track == Track::Bone || it->track == Track::Morph || it->track == Track::Display))
            it = selection.keys.erase(it);
        else
            ++it;
}
void SelectGravityRow(Selection& selection) {
    for (auto it = selection.tracks.begin(); it != selection.tracks.end();)
        if (!PmmModelKey(it->track))
            it = selection.tracks.erase(it);
        else
            ++it;
    selection.tracks.insert({Track::Gravity, 255, 0, 0});
}
template <class F> bool Visit(PmmSceneDocument& d, const KeyAddress& a, F&& f) {
    if (a.track == Track::Camera)
        return f(d.cameraKeys);
    if (a.track == Track::Light)
        return f(d.lightKeys);
    if (a.track == Track::Shadow)
        return f(d.selfShadowKeys);
    if (a.track == Track::Gravity)
        return f(d.gravityKeys);
    if (a.track == Track::Accessory) {
        for (auto& item : d.accessories)
            if (item.slot == a.slot)
                return f(item.keyRecords);
        return false;
    }
    auto* m = Model(d, a.slot);
    if (!m)
        return false;
    if (a.track == Track::Bone)
        return f(m->boneKeys);
    if (a.track == Track::Morph)
        return f(m->morphKeys);
    if (a.track == Track::Display)
        return f(m->displayKeys);
    return false;
}
bool Put(PmmSceneDocument& d, const KeyAddress& a, Key value) {
    return Visit(d, a, [&](auto& track) {
        using T = typename std::decay_t<decltype(track.dense)>::value_type;
        auto* key = std::get_if<T>(&value);
        if (!key)
            return false;
        key->frame = a.frame;
        return RegisterPmmKey(track, a.channel, *key);
    });
}
bool Get(PmmSceneDocument& d, const KeyAddress& a, Key& value) {
    return Visit(d, a, [&](auto& track) {
        using T = typename std::decay_t<decltype(track.dense)>::value_type;
        std::vector<std::vector<T>> chains;
        if (!PmmTrackChains(track, chains) || a.channel >= chains.size())
            return false;
        for (const auto& key : chains[a.channel])
            if (key.frame == a.frame) {
                value = key;
                return true;
            }
        std::map<std::uint32_t, std::uint32_t> channels;
        if (!PmmRecordChannels(track, channels))
            return false;
        for (const auto& record : track.sparse)
            if (record.value.frame == a.frame && channels.at(unsigned(record.index)) == a.channel) {
                value = record.value;
                return true;
            }
        return false;
    });
}
enum class RegistrationSlot {
    Invalid,
    Overwrite,
    Insert,
    Full
};
RegistrationSlot ProbeRegistration(PmmSceneDocument& document, const KeyAddress& address) {
    auto result = RegistrationSlot::Invalid;
    Visit(document, address, [&](const auto& track) {
        using T = typename std::decay_t<decltype(track.dense)>::value_type;
        std::vector<std::vector<T>> chains;
        if (address.frame > 0x7fffffffU || !PmmTrackChains(track, chains) ||
            address.channel >= chains.size())
            return false;
        const auto& chain = chains[address.channel];
        if (std::any_of(chain.begin(), chain.end(),
                        [&](const auto& key) { return key.frame == address.frame; }))
            result = RegistrationSlot::Overwrite;
        else
            result = track.dense.size() + track.sparse.size() >= PmmKeyCapacity<T>
                         ? RegistrationSlot::Full
                         : RegistrationSlot::Insert;
        return true;
    });
    return result;
}
bool Pose(PmmSceneDocument& d, const KeyAddress& a, Key& key) {
    if (a.track == Track::Camera) {
        auto k = d.cameraKeys.dense.at(0);
        for (int i = 0; i < 3; ++i) {
            k.eye[i] = d.cameraState[i];
            k.target[i] = d.cameraState[6 + i];
        }
        k.distance = d.cameraState[5];
        k.fov = std::uint32_t(d.envelope.settings.cameraFov);
        k.perspective = d.cameraPerspective;
        k.parentModel = d.appearance.cameraParentModel;
        k.parentBone = d.appearance.cameraParentBone;
        key = k;
        return true;
    }
    if (a.track == Track::Light) {
        PmmLightKey k;
        std::copy_n(d.lightState.begin(), 3, k.color.begin());
        std::copy_n(d.lightState.begin() + 3, 3, k.direction.begin());
        key = k;
        return true;
    }
    if (a.track == Track::Shadow) {
        PmmSelfShadowKey k;
        k.mode = d.selfShadowMode;
        k.distance = d.physicsInterval;
        key = k;
        return true;
    }
    if (a.track == Track::Gravity) {
        PmmGravityKey k;
        k.acceleration = d.physics.gravityMagnitude;
        k.direction = d.physics.gravityDirection;
        k.noise = d.physics.gravityNoise;
        k.noiseEnabled = d.physics.gravityNoiseEnabled;
        key = k;
        return true;
    }
    if (a.track == Track::Accessory) {
        for (auto& item : d.accessories)
            if (item.slot == a.slot) {
                const auto& s = item.state;
                PmmAccessoryKey k;
                k.position = s.position;
                k.rotation = s.rotation;
                k.scale = s.scale;
                k.opacity = s.opacity;
                k.visible = s.visible;
                k.shadowEnabled = s.shadowEnabled;
                k.parentModel = s.parentModel;
                k.parentBone = s.parentBone;
                key = k;
                return true;
            }
        return false;
    }
    auto* m = Model(d, a.slot);
    if (!m)
        return false;
    if (a.track == Track::Bone && a.channel < m->bonePose.size()) {
        auto k = m->boneKeys.dense[a.channel];
        const auto& pose = m->bonePose[a.channel];
        std::copy_n(pose.components.begin(), 3, k.position.begin());
        std::copy_n(pose.components.begin() + 3, 4, k.rotation.begin());
        k.physicsDisabled = pose.flags[0];
        k.allocated = 1;
        key = k;
        return true;
    }
    if (a.track == Track::Morph && a.channel < m->morphPose.size()) {
        PmmMorphKey k;
        k.value = m->morphPose[a.channel];
        k.allocated = 1;
        key = k;
        return true;
    }
    if (a.track == Track::Display) {
        auto k = m->displayKeys.dense.at(0);
        k.visible = m->selection.loadComplete;
        k.ikEnabled = m->ikPose;
        k.allocated = 1;
        for (std::size_t i = 0; i < k.rigidStates.size(); ++i) {
            k.rigidStates[i].modelIndex = m->rigidPose[i].values[2];
            k.rigidStates[i].boneIndex = m->rigidPose[i].values[3];
        }
        key = k;
        return true;
    }
    return false;
}
PmmSceneModel MakeModel(std::uint8_t slot, const runtime::ModelResource& resource) {
    PmmSceneModel m;
    m.slot = slot;
    m.name = m.sourceName = Name("model");
    for (const auto& n : resource.BoneNames())
        m.boneNames.push_back(Name(n));
    for (const auto& n : resource.MorphNames())
        m.morphNames.push_back(Name(n));
    m.layout.boneCount = std::uint32_t(m.boneNames.size());
    m.layout.morphCount = std::uint32_t(m.morphNames.size());
    m.groupCount = std::uint8_t(resource.DisplayGroups().size());
    m.ikSources = resource.IkBoneIndices();
    std::set<int> selectors;
    for (std::size_t i = 0; i < resource.Document().bones.size(); ++i) {
        const auto& bone = resource.Document().bones[i].fields;
        if ((bone.flags & 0x2000) && selectors.insert(bone.extParent).second)
            m.rigidSources.push_back(int(i));
    }
    m.layout.ikCount = std::uint32_t(m.ikSources.size());
    m.rigidCount = std::uint32_t(m.rigidSources.size());
    m.selection.groupFlags.resize(m.groupCount, 0);
    m.selection.loadComplete = 1;
    m.selection.maxFrame = 300;
    m.edgeScale = 1;
    m.toonFlag = 1;
    m.boneKeys.dense.resize(m.layout.boneCount);
    m.bonePose.resize(m.layout.boneCount);
    for (std::size_t i = 0; i < m.boneKeys.dense.size(); ++i) {
        auto& k = m.boneKeys.dense[i];
        k.rotation[3] = 1;
        k.allocated = 1;
        for (int n = 0; n < 16; ++n)
            k.interpolation[n] = n < 8 ? 20 : 107;
        m.bonePose[i].components[6] = 1;
    }
    m.morphKeys.dense.resize(m.layout.morphCount);
    m.morphPose.resize(m.layout.morphCount);
    for (auto& key : m.morphKeys.dense)
        key.allocated = 1;
    m.ikPose.resize(m.layout.ikCount, 1);
    m.rigidPose.resize(m.rigidCount);
    PmmDisplayKey display;
    display.visible = 1;
    display.allocated = 1;
    display.ikEnabled = m.ikPose;
    display.rigidStates.resize(m.rigidCount);
    for (auto& parent : display.rigidStates)
        parent.modelIndex = parent.boneIndex = -1;
    for (auto& parent : m.rigidPose)
        parent.values = {0, 0, -1, 0};
    m.displayKeys.dense = {display};
    return m;
}
bool AccessoryBytes(const std::filesystem::path& path, std::vector<char>& bytes,
                    std::string& reason, std::size_t limit) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    const auto size = f ? f.tellg() : std::streampos(-1);
    if (size < 0 || std::uint64_t(size) > limit) {
        reason = "accessory-read-failed";
        return false;
    }
    bytes.assign(std::size_t(size), 0);
    f.seekg(0);
    if (!f.read(bytes.data(), std::streamsize(bytes.size()))) {
        reason = "accessory-read-failed";
        return false;
    }
    return true;
}
bool ReadAccessory(const std::filesystem::path& path,
                   std::shared_ptr<const runtime::AccessoryResource>& out, std::string& reason,
                   VacAccessory* placement = nullptr, std::filesystem::path* meshSource = nullptr) {
    auto mesh = path;
    std::vector<char> bytes;
    VacAccessory defaults;
    defaults.shadowEnabled = false;
    // The native loader tests this case-sensitive substring on the complete
    // resolved path, independently of the file-drop classifier.
    if (path.u8string().find(".vac") != std::string::npos) {
        if (!AccessoryBytes(path, bytes, reason, 64u << 10) ||
            !ReadVac({bytes.data(), bytes.size()}, defaults, reason))
            return false;
        auto decoded = defaults.meshPath;
        std::replace(decoded.begin(), decoded.end(), '\\', '/');
        mesh = std::filesystem::u8path(decoded);
        if (mesh.is_relative())
            mesh = path.parent_path() / mesh;
        mesh = mesh.lexically_normal();
    }
    if (!AccessoryBytes(mesh, bytes, reason, 512u << 20) ||
        !runtime::AccessoryResource::Parse(bytes.data(), bytes.size(), out, reason))
        return false;
    if (placement)
        *placement = std::move(defaults);
    if (meshSource)
        *meshSource = std::move(mesh);
    return true;
}
}
EditorSession::EditorSession() {
    std::string reason;
    if (!New(reason))
        throw std::runtime_error(reason);
}
const PmmSceneDocument& EditorSession::Document() const {
    return runtime_->Session()->Document();
}
EditorSession::State EditorSession::Current() const {
    State out = state_;
    out.document = Document();
    out.document.envelope.settings.options[0] = selection_.model == 255;
    if (selection_.model != 255)
        out.document.envelope.selectedModelSlot = selection_.model;
    out.selection = selection_;
    out.revision = revision_;
    return out;
}
void EditorSession::SetPanelExpanded(unsigned option, bool expanded) {
    runtime_->SetPanelExpanded(option, expanded);
}
void EditorSession::SetSidebarWidth(int width) {
    runtime_->SetSidebarWidth(width);
}
bool EditorSession::SetFrameLimit(unsigned limit) {
    return runtime_ && runtime_->SetFrameLimit(limit);
}
bool EditorSession::SetGroundShadowColor(float value) {
    if (!runtime_ || !runtime_->SetGroundShadowColor(value))
        return false;
    ++appearanceRevision_;
    return true;
}
bool EditorSession::SetSelfShadowEnabled(bool enabled) {
    if (!runtime_ || !runtime_->SetSelfShadowEnabled(enabled))
        return false;
    ++appearanceRevision_;
    return true;
}
bool EditorSession::ToggleModelRenderFlag(bool shadow) {
    if (!runtime_ || !runtime_->ToggleModelRenderFlag(selection_.model, shadow))
        return false;
    ++appearanceRevision_;
    return true;
}
bool EditorSession::SetOutlineColor(const std::array<std::uint8_t, 3>& rgb) {
    if (!runtime_ || !runtime_->SetOutlineColor(rgb))
        return false;
    ++appearanceRevision_;
    return true;
}
void EditorSession::SetTransparentModels(bool value) {
    if (display_.transparentModels != value ||
        std::any_of(display_.opaqueModels.begin(), display_.opaqueModels.end(), [](bool v) { return v; })) {
        display_.transparentModels = value;
        display_.opaqueModels.fill(false);
        ++appearanceRevision_;
    }
}
void EditorSession::SetHiddenModels(bool value) {
    if (display_.hideModels != value) {
        display_.hideModels = value;
        ++appearanceRevision_;
    }
}
void EditorSession::SetMipmaps(bool value) {
    if (display_.mipmaps != value) {
        display_.mipmaps = value;
        ++appearanceRevision_;
    }
}
void EditorSession::SetWireframe(bool value) {
    if (display_.wireframe != value) {
        display_.wireframe = value;
        ++appearanceRevision_;
    }
}
void EditorSession::SetPhysicsFloor(bool enabled) {
    runtime_->SetPhysicsFloor(enabled);
}
bool EditorSession::SetGravityChannel(unsigned channel, float value, std::string& reason) {
    auto gravity = runtime_->Session()->Pose().gravity;
    if (!EditGravityChannel(gravity, channel, value) ||
        !runtime_->SetGravity(gravity, channel < 4)) {
        reason = "invalid-gravity-channel";
        return false;
    }
    // Native channel edits select the gravity row without changing model mode.
    SelectGravityRow(selection_);
    ++gravityEpoch_;
    reason.clear();
    return true;
}
bool EditorSession::SetGravityNoise(bool enabled, std::string& reason) {
    auto gravity = runtime_->Session()->Pose().gravity;
    gravity.noiseEnabled = enabled;
    if (!runtime_->SetGravity(gravity, false)) {
        reason = "invalid-gravity-noise";
        return false;
    }
    ++gravityEpoch_;
    reason.clear();
    return true;
}
bool EditorSession::RestoreGravity(std::string& reason) {
    format::PmmEvaluatedGravity value;
    if (!format::EvaluatePmmGravityTrack(Document().gravityKeys, Document().timeline.currentFrame,
                                         value, true) ||
        !runtime_->SetGravity(value, false)) {
        reason = "gravity-track-evaluation-failed";
        return false;
    }
    ++gravityEpoch_;
    reason.clear();
    return true;
}
bool EditorSession::RegisterGravity(std::string& reason) {
    const bool registered = runtime_->RegisterGravity(reason);
    if (!registered && reason != "gravity-key-capacity")
        return false;
    for (auto it = selection_.keys.begin(); it != selection_.keys.end();)
        if (!format::PmmModelKey(it->track))
            it = selection_.keys.erase(it);
        else
            ++it;
    if (registered)
        selection_.keys.insert(
            {Track::Gravity, 255, 0, unsigned(Document().timeline.currentFrame)});
    SelectGravityRow(selection_);
    revision_ = nextRevision_++;
    ++gravityEpoch_;
    return registered;
}
void EditorSession::SetGroupExpanded(std::uint8_t slot, unsigned group, bool expanded) {
    runtime_->SetGroupExpanded(slot, group, expanded);
}
bool EditorSession::Prepare(State& state, std::unique_ptr<runtime::SceneRuntime>& out,
                            std::string& reason) const try {
    // UI display groups come from the model, including synthetic Root/Disp/IK
    // rows. Older portable projects did not save that metadata for PMD.
    for (auto& model : state.document.models) {
        const auto found = state.assets.models.find(model.slot);
        if (found != state.assets.models.end()) {
            model.groupCount = std::uint8_t(found->second->DisplayGroups().size());
            model.selection.groupFlags.resize(model.groupCount, 0);
        }
    }
    SceneSession session;
    if (!SceneSession::Create(state.document, session, reason))
        return false;
    auto next = std::make_unique<runtime::SceneRuntime>();
    if (!next->LoadResources(std::move(session), state.assets.models, reason, true))
        return false;
    for (const auto& a : state.assets.accessories)
        if (!next->AttachAccessory(a.first, a.second, reason))
            return false;
    state.document = next->Session()->Document();
    out = std::move(next);
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
bool EditorSession::New(std::string& reason) {
    EndGesture();
    State next;
    next.document = NewPmmScene();
    // Native ResetAppState leaves the modeless ground-shadow color and its
    // current value intact. A newly constructed editor uses the PMM default.
    if (runtime_) {
        next.document.physics.projectedShadowAmbient = Document().physics.projectedShadowAmbient;
        next.document.selfShadowEnabled = Document().selfShadowEnabled;
    }
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    state_ = std::move(next);
    runtime_ = std::move(runtime);
    path_.clear();
    histories_.clear();
    for (auto& clipboard : clipboards_)
        clipboard.clear();
    interpolationCopied_ = false;
    selection_ = {};
    playing_ = false;
    coordinateSystem_ = 0;
    cursorSeconds_ = 0;
    // ResetAppState clears transparency but preserves the non-display switch.
    SetTransparentModels(false);
    revision_ = savedRevision_ = nextRevision_++;
    ++gravityEpoch_;
    reason.clear();
    return true;
}
bool EditorSession::ApplyRuntimeState(State& next, std::string& reason) {
    if (next.assets.models == state_.assets.models &&
        next.assets.accessories == state_.assets.accessories &&
        runtime_->CanApplyEditorDocument(next.document)) {
        for (auto& model : next.document.models) {
            const auto resource = next.assets.models.find(model.slot);
            if (resource != next.assets.models.end()) {
                model.groupCount = std::uint8_t(resource->second->DisplayGroups().size());
                model.selection.groupFlags.resize(model.groupCount, 0);
            }
        }
        SceneSession session;
        if (!SceneSession::Create(next.document, session, reason) ||
            !runtime_->ApplyEditorDocument(std::move(session), reason))
            return false;
        next.document = runtime_->Session()->Document();
    } else {
        std::unique_ptr<runtime::SceneRuntime> runtime;
        if (!Prepare(next, runtime, reason))
            return false;
        runtime_ = std::move(runtime);
    }
    return true;
}
bool EditorSession::Commit(State next, std::string& reason, const HistoryBatch& forced,
                           bool updateFrameBounds) {
    if (updateFrameBounds) {
        const auto last = [](const auto& track) {
            std::uint32_t result = 0;
            for (const auto& k : track.dense)
                result = std::max(result, k.frame);
            for (const auto& k : track.sparse)
                result = std::max(result, k.value.frame);
            return result;
        };
        auto& d = next.document;
        std::uint32_t end = std::max(
            {last(d.cameraKeys), last(d.lightKeys), last(d.selfShadowKeys), last(d.gravityKeys)});
        for (auto& m : d.models) {
            m.selection.maxFrame =
                std::max({last(m.boneKeys), last(m.morphKeys), last(m.displayKeys)});
            end = std::max(end, m.selection.maxFrame);
        }
        for (const auto& a : d.accessories)
            end = std::max(end, last(a.keyRecords));
        if (end > 0x7fffffffu) {
            reason = "frame-out-of-range";
            return false;
        }
        d.timeline.lastRegisteredFrame = int(end);
    }
    const auto before = Current();
    if (!ApplyRuntimeState(next, reason))
        return false;
    RecordHistory(before, next, forced);
    runtime_->Pause();
    state_ = std::move(next);
    selection_ = state_.selection;
    if (selection_.model != 255 && coordinateSystem_ == 2)
        coordinateSystem_ = 0;
    revision_ = nextRevision_++;
    playing_ = false;
    cursorSeconds_ = Document().timeline.currentFrame / 30.;
    reason.clear();
    return true;
}
bool EditorSession::Edit(const std::string&,
                         const std::function<bool(PmmSceneDocument&, std::string&)>& change,
                         std::string& reason) {
    auto next = Current();
    if (!change || !change(next.document, reason))
        return false;
    return Commit(std::move(next), reason);
}
bool EditorSession::Open(const std::filesystem::path& path, std::string& reason) {
    EndGesture();
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    SceneResourceResolver resolver(absolute);
    std::ifstream f(absolute, std::ios::binary);
    SceneSession session;
    if (!f || !SceneSession::Load(Read, &f, SceneResourceResolver::Resolve, &resolver, session)) {
        reason = "scene-open-or-resource-resolution-failed";
        return false;
    }
    State next;
    next.document = session.Document();
    if (next.document.envelope.header.version == PmmVersion::V1 && next.document.v1TailStage < 11)
        next.document.appearance.floorVisible = Document().appearance.floorVisible;
    next.assets.modelPaths = resolver.Resolved();
    for (std::size_t i = 0; i < session.Models().size(); ++i) {
        auto slot = next.document.models[i].slot;
        if (!session.Models()[i] ||
            !runtime::ModelResource::Create(*session.Models()[i], next.assets.models[slot], reason))
            return false;
        if (next.document.envelope.header.version == PmmVersion::V1) {
            auto defaults = MakeModel(slot, *next.assets.models[slot]);
            auto& m = next.document.models[i];
            m.boneNames = std::move(defaults.boneNames);
            m.morphNames = std::move(defaults.morphNames);
            m.name.assign(m.v1Name.begin(), std::find(m.v1Name.begin(), m.v1Name.end(), 0));
            m.sourceName = m.name;
            m.groupCount = std::uint8_t(m.selection.groupFlags.size());
            m.ikSources = std::move(defaults.ikSources);
            m.edgeScale = 1;
        }
    }
    if (next.document.envelope.header.version == PmmVersion::V1) {
        const auto convert = [&](PmmCameraKey& key, int index) {
            key.parentModel = key.parentBone = -1;
            for (const auto& parent : next.document.v1CameraParents)
                if (parent.keyIndex == index) {
                    key.parentModel = parent.model;
                    key.parentBone = parent.bone;
                }
        };
        for (std::size_t i = 0; i < next.document.cameraKeys.dense.size(); ++i)
            convert(next.document.cameraKeys.dense[i], int(i));
        for (auto& key : next.document.cameraKeys.sparse)
            convert(key.value, key.index);
        next.document.v1CameraParents.clear();
    }
    next.document.envelope.header.version = PmmVersion::V2;
    if (next.document.gravityKeys.dense.empty()) {
        PmmGravityKey k;
        k.acceleration = next.document.physics.gravityMagnitude;
        k.direction = next.document.physics.gravityDirection;
        k.noise = next.document.physics.gravityNoise;
        k.noiseEnabled = next.document.physics.gravityNoiseEnabled;
        next.document.gravityKeys.dense = {k};
    }
    if (next.document.selfShadowKeys.dense.empty()) {
        PmmSelfShadowKey k;
        k.mode = next.document.selfShadowMode;
        k.distance = next.document.physicsInterval;
        next.document.selfShadowKeys.dense = {k};
    }
    for (const auto& a : next.document.accessories) {
        std::filesystem::path source;
        if (!resolver.ResolveAccessory(a.slot, a.path, source)) {
            reason = "accessory-resource-resolution-failed:" + std::to_string(a.slot);
            return false;
        }
        if (!ReadAccessory(source, next.assets.accessories[a.slot], reason))
            return false;
        next.assets.accessoryPaths[a.slot] = source;
    }
    if (next.document.wave.enabled) {
        std::string decoded;
        auto end =
            std::find(next.document.wave.pathBytes.begin(), next.document.wave.pathBytes.end(), 0);
        if (!text::DecodeCp932({next.document.wave.pathBytes.data(),
                                std::size_t(end - next.document.wave.pathBytes.begin())},
                               decoded)) {
            reason = "invalid-wave-path";
            return false;
        }
        std::replace(decoded.begin(), decoded.end(), '\\', '/');
        next.assets.wave =
            (absolute.parent_path() / std::filesystem::u8path(decoded)).lexically_normal();
    }
    const auto& picturePath = next.document.picture.pathBytes;
    if (picturePath[0]) {
        std::string decoded;
        const auto end = std::find(picturePath.begin(), picturePath.end(), 0);
        if (!text::DecodeCp932({picturePath.data(), std::size_t(end - picturePath.begin())},
                               decoded)) {
            reason = "invalid-picture-path";
            return false;
        }
        std::replace(decoded.begin(), decoded.end(), '\\', '/');
        next.assets.picturePath =
            (absolute.parent_path() / std::filesystem::u8path(decoded)).lexically_normal();
        bool opened = false;
        if (!ReadPicture(next.assets.picturePath, next.assets.picture, opened, reason))
            return false;
    }
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    runtime_ = std::move(runtime);
    state_ = std::move(next);
    path_ = absolute;
    display_.opaqueModels.fill(false);
    for (const auto& model : Document().models)
        display_.opaqueModels[model.slot] = true;
    selection_ = {};
    coordinateSystem_ = 0;
    selection_.model = Document().models.empty() || Document().envelope.settings.options[0]
                           ? 255
                           : Document().envelope.selectedModelSlot;
    if (!RestoreModelSelection(selection_.model, reason))
        RestoreModelSelection(255, reason);
    histories_.clear();
    for (auto& clipboard : clipboards_)
        clipboard.clear();
    playing_ = false;
    revision_ = savedRevision_ = nextRevision_++;
    ++gravityEpoch_;
    cursorSeconds_ = Document().timeline.currentFrame / 30.;
    return true;
}
bool EditorSession::Save(const std::filesystem::path& path, const AtomicWrite& write,
                         std::string& reason) {
    if (!write) {
        reason = "missing-atomic-writer";
        return false;
    }
    auto next = Current();
    if (!SetPmmSelectedKeys(next.document, selection_.model, selection_.keys)) {
        reason = "key-selection-expired-or-invalid-tracks";
        return false;
    }
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    for (auto& m : next.document.models)
        if (!DiskPath(next.assets.modelPaths.at(m.slot), absolute.parent_path(), m.path, reason))
            return false;
    for (auto& a : next.document.accessories)
        if (!DiskPath(next.assets.accessoryPaths.at(a.slot), absolute.parent_path(), a.path,
                      reason))
            return false;
    if (next.document.wave.enabled &&
        !DiskPath(next.assets.wave, absolute.parent_path(), next.document.wave.pathBytes, reason))
        return false;
    if (!next.assets.picturePath.empty() &&
        !DiskPath(next.assets.picturePath, absolute.parent_path(), next.document.picture.pathBytes,
                  reason))
        return false;
    SceneSession session;
    std::vector<std::uint8_t> bytes;
    if (!SceneSession::Create(next.document, session, reason) || !session.Save(Bytes, &bytes))
        return false;
    if (!write(absolute, bytes, reason))
        return false;
    // Paths are rewritten again on every Save As, using the retained absolute
    // references. Saving never needs to re-evaluate the scene or reset physics.
    path_ = absolute;
    savedRevision_ = revision_;
    reason.clear();
    return true;
}
bool EditorSession::ImportModel(const std::filesystem::path& path, std::string& reason) {
    try {
        auto next = Current();
        unsigned slot = 0;
        while (slot < 255 && next.assets.models.count(std::uint8_t(slot)))
            ++slot;
        if (slot == 255) {
            reason = "model-slot-limit";
            return false;
        }
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        std::ifstream file(absolute, std::ios::binary);
        ModelDocument doc;
        std::shared_ptr<const runtime::ModelResource> resource;
        if (!file || !ReadModelDocument(Read, &file, doc)) {
            reason = "model-read-failed";
            return false;
        }
        if (!runtime::ModelResource::Create(doc, resource, reason))
            return false;
        auto model = MakeModel(std::uint8_t(slot), *resource);
        model.selection.comboIndex = std::uint8_t(next.document.models.size() + 1);
        model.previousDisplayOrder = std::uint8_t(next.document.models.size());
        model.name = model.sourceName = Name(absolute.stem().u8string());
        if (!DiskPath(absolute, absolute.parent_path(), model.path, reason))
            return false;
        next.document.models.push_back(std::move(model));
        next.document.envelope.selectedModelSlot = std::uint8_t(slot);
        next.document.modelRegistrations.clear();
        next.assets.models[std::uint8_t(slot)] = resource;
        next.assets.modelPaths[std::uint8_t(slot)] = absolute;
        next.selection = {};
        next.selection.model = std::uint8_t(slot);
        if (!ReadPmmSelectedKeys(next.document, next.selection.model, next.selection.keys)) {
            reason = "key-selection-invalid-tracks";
            return false;
        }
        if (!Commit(std::move(next), reason))
            return false;
        display_.opaqueModels[slot] = false;
        return true;
    } catch (const std::exception& e) {
        reason = e.what();
        return false;
    }
}
bool EditorSession::ImportAccessory(const std::filesystem::path& path, std::string& reason) {
    auto next = Current();
    unsigned slot = 0;
    while (slot < 255 && next.assets.accessories.count(std::uint8_t(slot)))
        ++slot;
    if (slot == 255) {
        reason = "accessory-slot-limit";
        return false;
    }
    auto absolute = std::filesystem::absolute(path).lexically_normal();
    std::shared_ptr<const runtime::AccessoryResource> resource;
    VacAccessory placement;
    std::filesystem::path mesh;
    if (!ReadAccessory(absolute, resource, reason, &placement, &mesh))
        return false;
    PmmSceneAccessory a;
    a.slot = std::uint8_t(slot);
    a.order = std::uint8_t(next.document.accessories.size());
    std::string name;
    const auto& displayName =
        placement.meshPath.empty() ? mesh.filename().u8string() : placement.meshPath;
    if (!text::EncodeCp932(displayName, name) || !Fixed(name, a.name) ||
        !DiskPath(absolute, absolute.parent_path(), a.path, reason))
        return false;
    a.state.visible = 1;
    a.state.opacity = 1;
    a.state.scale = placement.scale;
    a.state.position = placement.position;
    a.state.rotation = placement.rotation;
    a.state.shadowEnabled = placement.shadowEnabled;
    a.state.parentModel = -1;
    a.state.parentBone = 0;
    PmmAccessoryKey k;
    k.visible = 1;
    k.opacity = 1;
    k.scale = a.state.scale;
    k.position = a.state.position;
    k.rotation = a.state.rotation;
    k.shadowEnabled = a.state.shadowEnabled;
    k.parentModel = -1;
    k.parentBone = 0;
    a.keyRecords.dense = {k};
    next.document.accessories.push_back(a);
    next.document.accessoryListNames.push_back(a.name);
    next.assets.accessories[a.slot] = resource;
    next.assets.accessoryPaths[a.slot] = absolute;
    next.selection.accessory = a.slot;
    return Commit(std::move(next), reason);
}
bool EditorSession::ImportWave(const std::filesystem::path& path, std::string& reason) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    std::ifstream file(absolute, std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < 0 || size > (256u << 20)) {
        reason = "wave-file-read-failed";
        return false;
    }
    std::vector<std::uint8_t> bytes(std::size_t(size), 0);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))) {
        reason = "wave-file-read-failed";
        return false;
    }
    format::WaveAudio parsed;
    if (!format::ReadWave(bytes, parsed, reason))
        return false;
    auto next = Current();
    next.assets.wave = absolute;
    next.document.wave.enabled = 1;
    if (!DiskPath(absolute, absolute.parent_path(), next.document.wave.pathBytes, reason))
        return false;
    return Commit(std::move(next), reason);
}
bool EditorSession::ImportPicture(const std::filesystem::path& path, unsigned viewWidth,
                                  unsigned viewHeight, std::string& reason) {
    if (!viewWidth || !viewHeight || viewWidth > 16384 || viewHeight > 16384) {
        reason = "invalid-picture-viewport";
        return false;
    }
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    auto next = Current();
    bool opened = false;
    std::shared_ptr<const rhi::RgbaImage> image;
    if (!ReadPicture(absolute, image, opened, reason)) {
        const auto failure = reason;
        // The native loader clears the stored path on either failure. An open
        // failure retains the old texture; a decode failure releases it first.
        next.document.picture.pathBytes.fill(0);
        next.assets.picturePath.clear();
        if (opened)
            next.assets.picture.reset();
        if (!Commit(std::move(next), reason))
            return false;
        reason = failure;
        return false;
    }
    if (!DiskPath(absolute, absolute.parent_path(), next.document.picture.pathBytes, reason))
        return false;
    render::BackgroundView view;
    view.width = int(viewWidth);
    view.height = int(viewHeight);
    view.scale = render::BackgroundViewScale(view.width);
    const auto fit = render::FitBackground(int(image->width), int(image->height), view);
    next.document.picture.offsetX = fit.x;
    next.document.picture.offsetY = fit.y;
    next.document.picture.scale = fit.scale;
    next.document.picture.enabled = 1;
    next.assets.picturePath = absolute;
    next.assets.picture = std::move(image);
    return Commit(std::move(next), reason);
}
bool EditorSession::ShowPicture(bool visible, std::string& reason) {
    if (visible && !state_.assets.picture) {
        reason = "picture-not-loaded";
        return false;
    }
    return Edit(
        "background-picture-visible",
        [&](auto& doc, auto&) {
            doc.picture.enabled = visible ? 1 : 0;
            return true;
        },
        reason);
}
bool EditorSession::SetBone(std::uint8_t slot, std::uint32_t bone,
                            const std::array<float, 3>& position,
                            const std::array<float, 4>& rotation, std::string& reason) {
    auto next = Current();
    auto* model = Model(next.document, slot);
    if (!model || bone >= model->bonePose.size()) {
        reason = "bone-selection-expired";
        return false;
    }
    float length = 0;
    for (float value : rotation)
        length += value * value;
    if (!std::isfinite(length) || std::abs(length - 1) > 1e-3f) {
        reason = "bone-rotation-not-unit";
        return false;
    }
    HistoryBatch history{
        {slot, CaptureHistory(*model, HistoryKind::Pose,
                              std::uint32_t(next.document.timeline.currentFrame), {bone})}};
    auto& pose = model->bonePose[bone];
    std::copy(position.begin(), position.end(), pose.components.begin());
    std::copy(rotation.begin(), rotation.end(), pose.components.begin() + 3);
    pose.flags[1] = 1;
    return Commit(std::move(next), reason, history);
}

bool EditorSession::SetMorph(std::uint8_t slot, std::uint32_t morph, float value,
                             std::string& reason) {
    return Edit(
        "morph-weight",
        [&](auto& d, auto& error) {
            auto* m = Model(d, slot);
            if (!m || morph >= m->morphPose.size()) {
                error = "morph-selection-expired";
                return false;
            }
            if (!std::isfinite(value) || value < 0 || value > 1) {
                error = "morph-weight-out-of-range";
                return false;
            }
            m->morphPose[morph] = value;
            return true;
        },
        reason);
}
bool EditorSession::SetAccessory(std::uint8_t slot, const PmmAccessoryState& pose,
                                 std::string& reason) {
    return Edit(
        "accessory-transform",
        [&](auto& d, auto& error) {
            for (auto& a : d.accessories)
                if (a.slot == slot) {
                    a.state = pose;
                    return true;
                }
            error = "accessory-selection-expired";
            return false;
        },
        reason);
}
bool EditorSession::RegisterMorph(std::uint8_t slot, std::optional<std::uint32_t> channel,
                                  std::string& reason, unsigned* failedKeys) {
    return RegisterMorphChannels(
        slot, channel ? std::vector<std::uint32_t>{*channel} : std::vector<std::uint32_t>{}, false,
        reason, failedKeys);
}
bool EditorSession::RegisterAllMorphs(std::uint8_t slot, std::string& reason,
                                      unsigned* failedKeys) {
    std::vector<std::uint32_t> channels;
    for (const auto& model : Document().models)
        if (model.slot == slot)
            for (std::uint32_t i = 0; i < model.morphPose.size(); ++i)
                channels.push_back(i);
    return RegisterMorphChannels(slot, channels, true, reason, failedKeys);
}
bool EditorSession::RegisterMorphChannels(std::uint8_t slot,
                                          const std::vector<std::uint32_t>& channels,
                                          bool seekModel, std::string& reason,
                                          unsigned* failedKeys) {
    EndGesture();
    auto next = Current();
    auto* model = Model(next.document, slot);
    const auto resource = runtime_->Resource(slot);
    if (playing_ || !model || !resource) {
        reason = "morph-registration-unavailable";
        return false;
    }
    const auto frame = unsigned(next.document.timeline.currentFrame);
    for (auto channel : channels)
        if (channel >= model->morphPose.size()) {
            reason = "morph-selection-expired";
            return false;
        }
    ClearModelKeyMarks(*model, next.selection);
    PmmMorphRegistration pool;
    if (!pool.Load(model->morphKeys)) {
        reason = "morph-registration-pool-invalid";
        return false;
    }
    unsigned failed = 0;
    // Button commands ignore PMD's base Morph. Raw track insertion remains
    // available to import/paste, which use a different native registrar.
    for (auto channel : channels) {
        int resourceMorph;
        if (!runtime_->DocumentMorphIndex(slot, int(channel), resourceMorph, reason))
            return false;
        if (resource->SourceKind() == ModelKind::Pmd && resourceMorph == 0)
            continue;
        if (!pool.Register(unsigned(model->morphKeys.dense.size()), channel, frame,
                           model->morphPose[channel], model->selection.maxFrame)) {
            ++failed;
            continue;
        }
        next.selection.keys.insert({Track::Morph, slot, channel, frame});
    }
    pool.Store(model->morphKeys);
    if (!seekModel || !model->morphPose.empty())
        next.document.timeline.lastRegisteredFrame =
            std::max(next.document.timeline.lastRegisteredFrame, int(model->selection.maxFrame));
    if (seekModel) {
        // The all-Morph command seeks only this model after registration,
        // replacing its pending bone/display pose without appending bone Undo.
        SceneSession evaluated;
        if (!SceneSession::Create(next.document, evaluated, reason) ||
            !evaluated.Seek(frame, {false, false}))
            return false;
        auto document = evaluated.Document();
        const auto* sought = Model(document, slot);
        if (!sought) {
            reason = "morph-registration-model-expired";
            return false;
        }
        *model = *sought;
    }
    HistoryBatch history;
    history.emplace(slot, CaptureHistory(*model, HistoryKind::None, frame));
    if (!Commit(std::move(next), reason, history, false))
        return false;
    if (failedKeys)
        *failedKeys = failed;
    return true;
}
bool EditorSession::ResetMorphs(std::uint8_t slot, std::string& reason) {
    EndGesture();
    auto next = Current();
    auto* model = Model(next.document, slot);
    if (playing_ || !model) {
        reason = "morph-reset-unavailable";
        return false;
    }
    std::fill(model->morphPose.begin(), model->morphPose.end(), 0.f);
    HistoryBatch history;
    history.emplace(slot, CaptureHistory(*model, HistoryKind::None, 0));
    const bool clean = !Dirty();
    if (!Commit(std::move(next), reason, history, false))
        return false;
    // Native reset changes live weights without setting the modified flag.
    // Revision still advances so cached CPU/GPU geometry is refreshed.
    if (clean)
        savedRevision_ = revision_;
    return true;
}
bool EditorSession::ScaleMorphKeys(float scale, float add, std::string& reason) {
    if (playing_ || selection_.model == 255 || !std::isfinite(scale) || !std::isfinite(add)) {
        reason = "morph-key-scale-unavailable";
        return false;
    }
    auto next = Current();
    auto* model = Model(next.document, selection_.model);
    if (!model || !SetPmmSelectedKeys(next.document, selection_.model, selection_.keys)) {
        reason = "morph-key-scale-selection-expired";
        return false;
    }
    const auto apply = [&](auto& key) {
        keyframe_edit::ScaleMorphKey(key, scale, add);
        return !key.allocated || std::isfinite(key.value);
    };
    bool valid = true;
    for (auto& key : model->morphKeys.dense)
        valid &= apply(key);
    for (auto& key : model->morphKeys.sparse)
        valid &= apply(key.value);
    for (auto& [index, key] : model->morphKeys.unused)
        valid &= apply(key);
    if (!valid) {
        reason = "morph-key-scale-not-finite";
        return false;
    }
    // The native apply tail seeks this model, replacing its pending bone and
    // display poses too. Other models and the bone undo/redo stacks survive.
    SceneSession evaluated;
    if (!SceneSession::Create(next.document, evaluated, reason) ||
        !evaluated.Seek(unsigned(next.document.timeline.currentFrame))) {
        if (reason.empty())
            reason = "morph-key-scale-evaluation-failed";
        return false;
    }
    for (const auto& value : evaluated.Document().models)
        if (value.slot == selection_.model)
            *model = value;
    HistoryBatch history;
    for (const auto& value : next.document.models)
        history.emplace(value.slot, CaptureHistory(value, HistoryKind::None, 0));
    EndGesture();
    next.document.timeline.editMode = Document().timeline.editMode;
    // Even accepting identity or an empty selection dirties the native scene.
    return Commit(std::move(next), reason, history, false);
}
bool EditorSession::ScaleCameraKeys(const std::array<float, 16>& values, std::string& reason) {
    if (playing_ || selection_.model != 255 ||
        !std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); })) {
        reason = "camera-key-scale-unavailable";
        return false;
    }
    auto next = Current();
    auto& d = next.document;
    if (d.cameraKeys.dense.size() != 1 ||
        std::any_of(d.cameraKeys.sparse.begin(), d.cameraKeys.sparse.end(),
                    [](const auto& k) { return k.index < 1 || k.index >= 10000; }) ||
        std::any_of(d.cameraKeys.unused.begin(), d.cameraKeys.unused.end(),
                    [](const auto& k) { return k.first < 1 || k.first >= 10000; })) {
        reason = "camera-key-scale-pool-index";
        return false;
    }
    if (!SetPmmSelectedKeys(d, 255, selection_.keys)) {
        reason = "camera-key-scale-selection-expired";
        return false;
    }
    const auto apply = [&](auto& key) {
        keyframe_edit::ScaleCameraKey(key, values);
        return !key.selected || (std::isfinite(key.distance) &&
                                 std::all_of(key.eye.begin(), key.eye.end(),
                                             [](float v) { return std::isfinite(v); }) &&
                                 std::all_of(key.target.begin(), key.target.end(),
                                             [](float v) { return std::isfinite(v); }));
    };
    bool valid = true;
    for (auto& key : d.cameraKeys.dense)
        valid &= apply(key);
    for (auto& key : d.cameraKeys.sparse)
        valid &= apply(key.value);
    for (auto& [index, key] : d.cameraKeys.unused)
        valid &= apply(key);
    if (!valid) {
        reason = "camera-key-scale-not-finite";
        return false;
    }
    PmmEvaluatedCamera camera;
    if (!EvaluatePmmCameraTrack(d.cameraKeys, d.timeline.currentFrame, PmmCameraInterpolation::Seek,
                                camera)) {
        reason = "camera-key-scale-evaluation-failed";
        return false;
    }
    // Native ReloadModels here reloads only the camera. Pending model poses,
    // other global values and all bone history survive; view offsets reset.
    std::copy(camera.position.begin(), camera.position.end(), d.cameraState.begin());
    std::copy(camera.rotation.begin(), camera.rotation.end(), d.cameraState.begin() + 6);
    d.cameraState[3] = d.cameraState[4] = 0;
    d.cameraState[5] = camera.distance;
    d.envelope.settings.cameraFov = camera.fov;
    d.cameraPerspective = camera.perspective;
    d.appearance.cameraParentModel = camera.parentModel;
    d.appearance.cameraParentBone = camera.parentBone;
    HistoryBatch history;
    for (const auto& model : d.models)
        history.emplace(model.slot, CaptureHistory(model, HistoryKind::None, 0));
    return Commit(std::move(next), reason, history, false);
}
bool EditorSession::ScaleBoneKeys(const std::array<float, 12>& values, std::string& reason) {
    if (!std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); })) {
        reason = "bone-key-scale-unavailable";
        return false;
    }
    return TransformSelectedBoneKeys(
        [&](unsigned, auto& key) {
            keyframe_edit::ScaleBoneKey(key, values);
            return true;
        },
        reason);
}
bool EditorSession::OffsetRootBoneKeys(const std::array<float, 3>& offset, std::string& reason) {
    const auto resource = runtime_->Resource(selection_.model);
    if (!resource ||
        !std::all_of(offset.begin(), offset.end(), [](float v) { return std::isfinite(v); })) {
        reason = "root-bone-offset-unavailable";
        return false;
    }
    const auto& models = Document().models;
    const auto source = std::find_if(models.begin(), models.end(), [&](const auto& model) {
        return model.slot == selection_.model;
    });
    if (source == models.end()) {
        reason = "root-bone-offset-selection-expired";
        return false;
    }
    std::vector<bool> root(source->boneKeys.dense.size());
    for (unsigned i = 0; i < root.size(); ++i) {
        int documentIndex;
        if (!runtime_->DocumentBoneIndex(selection_.model, int(i), documentIndex, reason))
            return false;
        root[i] = resource->Document().bones[std::size_t(documentIndex)].fields.parent == -1;
    }
    std::map<unsigned, unsigned> previous;
    for (const auto& key : source->boneKeys.sparse)
        previous.emplace(unsigned(key.index), key.value.previous);
    for (const auto& [index, key] : source->boneKeys.unused)
        previous.emplace(index, key.previous);
    return TransformSelectedBoneKeys(
        [&](unsigned index, auto& key) {
            // Native selection is per physical record, including detached records
            // and retained free slots. Resolve the bone through predecessor links.
            auto bone = index;
            for (std::size_t depth = 0; bone >= root.size(); ++depth) {
                const auto found = previous.find(bone);
                if (depth >= previous.size() || found == previous.end())
                    return false;
                bone = found->second;
            }
            if (root[bone])
                for (unsigned axis = 0; axis < 3; ++axis)
                    key.position[axis] = offset[axis] + key.position[axis];
            return true;
        },
        reason);
}
bool EditorSession::TransformSelectedBoneKeys(
    const std::function<bool(unsigned, PmmBoneKey&)>& transform, std::string& reason) {
    if (playing_ || selection_.model == 255) {
        reason = "bone-key-transform-unavailable";
        return false;
    }
    auto next = Current();
    auto* model = Model(next.document, selection_.model);
    if (!model || !SetPmmSelectedKeys(next.document, selection_.model, selection_.keys)) {
        reason = "bone-key-scale-selection-expired";
        return false;
    }
    auto entry =
        CaptureHistory(*model, HistoryKind::Keys, unsigned(next.document.timeline.currentFrame));
    entry.partialKeys = true;
    entry.keys = {};
    const auto apply = [&](unsigned index, auto& key) {
        if (!key.allocated)
            return true;
        entry.keyRecords.emplace(index, key);
        entry.selectedKeyRecords.insert(index);
        if (!transform(index, key))
            return false;
        return std::all_of(key.position.begin(), key.position.end(),
                           [](float v) { return std::isfinite(v); }) &&
               std::all_of(key.rotation.begin(), key.rotation.end(),
                           [](float v) { return std::isfinite(v); });
    };
    bool valid = true;
    for (unsigned i = 0; i < model->boneKeys.dense.size(); ++i)
        valid &= apply(i, model->boneKeys.dense[i]);
    for (auto& key : model->boneKeys.sparse)
        valid &= apply(unsigned(key.index), key.value);
    for (auto& [index, key] : model->boneKeys.unused)
        valid &= apply(index, key);
    if (!valid) {
        reason = "bone-key-scale-not-finite";
        return false;
    }
    // SnapshotSelectedKeysForUndo is called even on identity, but returns
    // without touching either history stack when the pool has no marks.
    if (entry.keyRecords.empty())
        entry.kind = HistoryKind::None;
    else
        for (auto& saved : entry.poses) {
            PmmEvaluatedBone live;
            bool physics;
            if (!runtime_->BoneRegistrationPose(selection_.model, saved.index, 0, live, physics,
                                                reason))
                return false;
            std::copy(live.translation.begin(), live.translation.end(), saved.components.begin());
            std::copy(live.rotation.begin(), live.rotation.end(), saved.components.begin() + 3);
        }
    SceneSession evaluated;
    if (!SceneSession::Create(next.document, evaluated, reason) ||
        !evaluated.Seek(unsigned(next.document.timeline.currentFrame))) {
        if (reason.empty())
            reason = "bone-key-scale-evaluation-failed";
        return false;
    }
    for (const auto& value : evaluated.Document().models)
        if (value.slot == selection_.model)
            *model = value;
    HistoryBatch history;
    for (const auto& value : next.document.models)
        history.emplace(value.slot, value.slot == selection_.model
                                        ? entry
                                        : CaptureHistory(value, HistoryKind::None, 0));
    EndGesture();
    next.document.timeline.editMode = Document().timeline.editMode;
    return Commit(std::move(next), reason, history, false);
}
bool EditorSession::SetSelectedKeyPhysics(bool enabled, std::string& reason) {
    if (playing_ || selection_.model == 255) {
        reason = "physics-key-edit-unavailable";
        return false;
    }
    auto next = Current();
    auto* model = Model(next.document, selection_.model);
    const auto resource = runtime_->Resource(selection_.model);
    if (!model || !resource ||
        !SetPmmSelectedKeys(next.document, selection_.model, selection_.keys)) {
        reason = "physics-key-edit-selection-expired";
        return false;
    }
    const auto roots = unsigned(model->boneKeys.dense.size());
    std::vector<std::uint8_t> physical(roots);
    for (unsigned bone = 0; bone < roots; ++bone) {
        int index;
        if (!runtime_->DocumentBoneIndex(selection_.model, int(bone), index, reason))
            return false;
        physical[bone] = resource->EditingBones()[std::size_t(index)].hasRigidBody;
    }
    auto entry =
        CaptureHistory(*model, HistoryKind::Keys, unsigned(next.document.timeline.currentFrame));
    entry.partialKeys = true;
    entry.keys = {};
    std::map<unsigned, PmmBoneKey*> records;
    for (unsigned i = 0; i < roots; ++i)
        records.emplace(i, &model->boneKeys.dense[i]);
    for (auto& key : model->boneKeys.sparse)
        records.emplace(unsigned(key.index), &key.value);
    keyframe_edit::VisitSelectedPhysicsKeys(
        roots, [&](unsigned i) { return physical[i] != 0; },
        [&](unsigned i) -> PmmBoneKey& { return *records.at(i); },
        [&](unsigned i, auto& key) {
            entry.keyRecords.emplace(i, key);
            entry.selectedKeyRecords.insert(i);
            key.physicsDisabled = !enabled;
        });
    if (entry.keyRecords.empty()) {
        reason.clear();
        return true;
    }
    for (auto& saved : entry.poses) {
        PmmEvaluatedBone live;
        bool physics;
        if (!runtime_->BoneRegistrationPose(selection_.model, saved.index, 0, live, physics,
                                            reason))
            return false;
        std::copy(live.translation.begin(), live.translation.end(), saved.components.begin());
        std::copy(live.rotation.begin(), live.rotation.end(), saved.components.begin() + 3);
    }
    SceneSession evaluated;
    if (!SceneSession::Create(next.document, evaluated, reason) ||
        !evaluated.Seek(unsigned(next.document.timeline.currentFrame))) {
        if (reason.empty())
            reason = "physics-key-edit-evaluation-failed";
        return false;
    }
    for (const auto& value : evaluated.Document().models)
        if (value.slot == selection_.model)
            *model = value;
    HistoryBatch history;
    for (const auto& value : next.document.models)
        history.emplace(value.slot, value.slot == selection_.model
                                        ? entry
                                        : CaptureHistory(value, HistoryKind::None, 0));
    EndGesture();
    next.document.timeline.editMode = Document().timeline.editMode;
    const bool clean = !Dirty();
    if (!Commit(std::move(next), reason, history, false))
        return false;
    // Native apply changes keys and history without writing SceneModified.
    if (clean)
        savedRevision_ = revision_;
    return true;
}
bool EditorSession::ResetRotation(std::string& reason) {
    if (playing_) {
        reason = "rotation-reset-during-playback";
        return false;
    }
    EndGesture();
    if (selection_.model == 255) {
        auto camera = runtime_->Session()->Pose().camera;
        camera.rotation = {};
        if (!SetCamera(camera, reason))
            return false;
        // Native command marks even an already-zero camera as modified.
        revision_ = nextRevision_++;
        return true;
    }
    const auto bone = ActiveBone();
    if (bone < 0) {
        reason.clear();
        return true;
    }
    auto next = Current();
    auto* model = Model(next.document, selection_.model);
    if (!model || std::size_t(bone) >= model->bonePose.size()) {
        reason = "bone-selection-expired";
        return false;
    }
    HistoryBatch history{{model->slot, CaptureHistory(*model, HistoryKind::Pose,
                                                      unsigned(next.document.timeline.currentFrame),
                                                      {unsigned(bone)})}};
    auto& pose = model->bonePose[std::size_t(bone)];
    std::fill_n(pose.components.begin() + 3, 3, 0.f);
    pose.components[6] = 1;
    pose.flags[1] = 1;
    return Commit(std::move(next), reason, history, false);
}
void EditorSession::ResetPhysicsBodies() {
    runtime_->RequestPhysicsReset();
}
bool EditorSession::SetEdgeThickness(float value, std::string& reason) {
    reason.clear();
    if (!std::isfinite(value)) {
        reason = "edge-thickness-not-finite";
        return false;
    }
    // The original open dialog remains usable after model/camera selection changes.
    if (selection_.model == 255)
        return true;
    if (!runtime_->SetModelEdgeScale(selection_.model, value)) {
        reason = "edge-model-unavailable";
        return false;
    }
    // Even an unchanged accepted value marks the native document. Do not seek,
    // pause, recalculate retained key watermarks or consume bone undo/redo.
    revision_ = nextRevision_++;
    return true;
}
bool EditorSession::SetRenderSize(unsigned width, unsigned height, std::string& reason) {
    if (!width || !height || width > 16384 || height > 16384) {
        reason = "render-size-out-of-range";
        return false;
    }
    auto next = Current();
    next.document.envelope.settings.renderWidth = width;
    next.document.envelope.settings.renderHeight = height;
    // Changing an output property does not recompute retained registration
    // watermarks or create bone history. The host validates device limits.
    HistoryBatch history;
    for (const auto& model : next.document.models)
        history.emplace(model.slot, CaptureHistory(model, HistoryKind::None, 0));
    return Commit(std::move(next), reason, history, false);
}
bool EditorSession::Register(const std::vector<KeyAddress>& addresses, std::string& reason,
                             unsigned* failedKeys) {
    EndGesture();
    if (addresses.empty()) {
        reason = "no-selected-keys";
        return false;
    }
    auto next = Current();
    HistoryBatch history;
    // Every model registration button clears this model's key marks. Only
    // bone registration appends history; auxiliary commands must also suppress
    // automatic bone history caused by clearing those selection bytes.
    const std::set<KeyAddress> ordered(addresses.begin(), addresses.end());
    if (std::any_of(ordered.begin(), ordered.end(),
                    [](const auto& address) { return !PmmModelKey(address.track); })) {
        PmmKeyPools pools{
            {Track::Camera, 255}, {Track::Light, 255}, {Track::Shadow, 255}, {Track::Gravity, 255}};
        for (const auto& accessory : next.document.accessories)
            pools.emplace(Track::Accessory, accessory.slot);
        if (!SetPmmSelectedKeys(next.document, 255, {}, pools)) {
            reason = "key-registration-invalid-tracks";
            return false;
        }
        for (auto it = next.selection.keys.begin(); it != next.selection.keys.end();)
            if (!PmmModelKey(it->track))
                it = next.selection.keys.erase(it);
            else
                ++it;
    }
    for (const auto& address : ordered) {
        if ((address.track == Track::Bone || address.track == Track::Morph ||
             address.track == Track::Display) &&
            !history.count(address.slot)) {
            auto* model = Model(next.document, address.slot);
            if (!model) {
                reason = "key-registration-failed";
                return false;
            }
            const bool bone = std::any_of(ordered.begin(), ordered.end(), [&](const auto& a) {
                return a.slot == address.slot && a.track == Track::Bone;
            });
            history.emplace(address.slot,
                            CaptureHistory(*model, bone ? HistoryKind::Keys : HistoryKind::None,
                                           std::uint32_t(next.document.timeline.currentFrame)));
            auto& entry = history.at(address.slot);
            entry.partialKeys = bone;
            entry.keys = {};
            // Native SnapshotPose reads the live editable bone transforms,
            // including the last completed IK solve, before choosing backups.
            for (auto& saved : entry.poses) {
                PmmEvaluatedBone live;
                bool physics;
                if (!runtime_->BoneRegistrationPose(address.slot, saved.index, 0, live, physics,
                                                    reason))
                    return false;
                std::copy(live.translation.begin(), live.translation.end(),
                          saved.components.begin());
                std::copy(live.rotation.begin(), live.rotation.end(), saved.components.begin() + 3);
            }
            ClearModelKeyMarks(*model, next.selection);
        }
    }
    unsigned failed = 0;
    for (const auto& address : ordered) {
        Key key;
        if (!Pose(next.document, address, key)) {
            reason = "key-registration-failed";
            return false;
        }
        // A full native pool still permits overwrites. Overflow is a command
        // result, not a transaction failure: previous insertions, cleared marks
        // and bone history remain in effect even when later channels are full.
        const auto slot = ProbeRegistration(next.document, address);
        if (slot == RegistrationSlot::Invalid) {
            reason = "key-registration-invalid-tracks";
            return false;
        }
        // The validation above records capacity failures before any payload or
        // interpolation mutation. A failed bone still loses its edit marker.
        if (!PmmModelKey(address.track)) {
            for (auto it = next.selection.tracks.begin(); it != next.selection.tracks.end();)
                if (!PmmModelKey(it->track))
                    it = next.selection.tracks.erase(it);
                else
                    ++it;
            next.selection.tracks.insert({address.track, address.slot, 0, 0});
        }
        if (slot == RegistrationSlot::Full) {
            ++failed;
            if (address.track == Track::Bone)
                Model(next.document, address.slot)->bonePose.at(address.channel).flags[1] = 0;
            if (PmmModelKey(address.track))
                next.document.timeline.lastRegisteredFrame =
                    std::max(next.document.timeline.lastRegisteredFrame,
                             int(Model(next.document, address.slot)->selection.maxFrame));
            continue;
        }
        const bool inserted = slot == RegistrationSlot::Insert;
        if (address.track == Track::Bone) {
            auto* model = Model(next.document, address.slot);
            PmmEvaluatedBone live;
            bool physics;
            if (!runtime_->BoneRegistrationPose(address.slot, address.channel,
                                                next.document.physics.playbackMode, live, physics,
                                                reason))
                return false;
            PmmBoneRegistrationPose pose{live.translation, live.rotation, {}};
            if (physics)
                pose.physicsDisabled = !registrationPhysics_;
            if (!RegisterPmmBonePose(model->boneKeys, address.channel, address.frame, pose,
                                     autoInterpolation_, history.at(address.slot).keyRecords)) {
                reason = "key-registration-failed";
                return false;
            }
            auto& current = model->bonePose.at(address.channel);
            std::copy(pose.position.begin(), pose.position.end(), current.components.begin());
            std::copy(pose.rotation.begin(), pose.rotation.end(), current.components.begin() + 3);
            if (physics)
                current.flags[0] = *pose.physicsDisabled;
            current.flags[1] = 0;
        } else {
            std::visit(
                [](auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, PmmMorphKey> ||
                                  std::is_same_v<T, PmmDisplayKey>)
                        value.allocated = 1;
                    else if constexpr (!std::is_same_v<T, PmmBoneKey>)
                        value.selected = 1;
                },
                key);
            if (!Put(next.document, address, key)) {
                reason = "key-registration-failed";
                return false;
            }
        }
        next.selection.keys.insert(address);
        if (inserted || address.track == Track::Bone)
            next.document.timeline.lastRegisteredFrame =
                std::max(next.document.timeline.lastRegisteredFrame, int(address.frame));
        if (PmmModelKey(address.track)) {
            auto* model = Model(next.document, address.slot);
            if (inserted || address.track == Track::Bone)
                model->selection.maxFrame = std::max(model->selection.maxFrame, address.frame);
            next.document.timeline.lastRegisteredFrame = std::max(
                next.document.timeline.lastRegisteredFrame, int(model->selection.maxFrame));
        }
    }
    if (!Commit(std::move(next), reason, history, false))
        return false;
    if (failedKeys)
        *failedKeys = failed;
    return true;
}

bool EditorSession::DeleteUnusedFrames(unsigned& deleted, std::string& reason) {
    if (playing_) {
        reason = "key-reduction-during-playback";
        return false;
    }
    if (selection_.model == 255) {
        deleted = 0;
        reason.clear();
        return true;
    }
    EndGesture();
    auto next = Current();
    auto* model = Model(next.document, selection_.model);
    const auto resource = runtime_->Resource(selection_.model);
    if (!model || !resource) {
        reason = "key-reduction-model-expired";
        return false;
    }
    std::vector<cpu_bone::BoneType> types;
    for (unsigned i = 0; i < model->boneKeys.dense.size(); ++i) {
        int mapped;
        if (!runtime_->DocumentBoneIndex(selection_.model, int(i), mapped, reason))
            return false;
        types.push_back(resource->EditingBones().at(std::size_t(mapped)).type);
    }
    ClearModelKeyMarks(*model, next.selection);
    unsigned count = 0;
    const auto reduce = [&](auto& track, auto&& included, auto&& same) {
        std::map<std::uint32_t, std::uint32_t> channels;
        if (!PmmRecordChannels(track, channels))
            return false;
        using Key = typename std::decay_t<decltype(track.dense)>::value_type;
        std::map<std::uint32_t, Key*> records;
        for (unsigned i = 0; i < track.dense.size(); ++i)
            records.emplace(i, &track.dense[i]);
        for (auto& key : track.sparse)
            records.emplace(unsigned(key.index), &key.value);
        for (unsigned i = 0; i < track.dense.size(); ++i)
            if (included(i))
                count += keyframe_edit::MarkRedundantInterior(
                    i, [&](unsigned index) -> auto& { return *records.at(index); }, same,
                    [&](unsigned index) { records.at(index)->allocated = 1; });
        return true;
    };
    const auto all = [](unsigned) { return true; };
    if (!reduce(
            model->boneKeys,
            [&](unsigned i) {
                return types[i] != cpu_bone::BoneType::InertTip &&
                       types[i] != cpu_bone::BoneType::Effector;
            },
            [](const auto& a, const auto& b) { return keyframe_edit::SameBonePose(a, b); }) ||
        !reduce(model->morphKeys, all,
                [](const auto& a, const auto& b) { return a.value == b.value; }) ||
        !reduce(model->displayKeys, all, [](const auto& a, const auto& b) {
            return a.visible == b.visible && a.ikEnabled == b.ikEnabled &&
                   a.rigidStates.size() == b.rigidStates.size() &&
                   std::equal(a.rigidStates.begin(), a.rigidStates.end(), b.rigidStates.begin(),
                              [](const auto& x, const auto& y) {
                                  return x.modelIndex == y.modelIndex && x.boneIndex == y.boneIndex;
                              });
        })) {
        reason = "key-reduction-invalid-tracks";
        return false;
    }
    std::set<KeyAddress> marked;
    if (!ReadPmmSelectedKeys(next.document, selection_.model, marked)) {
        reason = "key-reduction-invalid-selection";
        return false;
    }
    if (!DeleteKeys(std::move(next), marked, reason, true))
        return false;
    deleted = count;
    return true;
}
bool EditorSession::Delete(const std::set<KeyAddress>& addresses, std::string& reason) {
    EndGesture();
    return DeleteKeys(Current(), addresses, reason, false);
}
bool EditorSession::DeleteKeys(State next, const std::set<KeyAddress>& addresses,
                               std::string& reason, bool markedModelRecords) {
    if (addresses.empty() && !markedModelRecords) {
        reason = "no-selected-keys";
        return false;
    }
    auto& d = next.document;
    HistoryBatch history;
    for (const auto& model : d.models) {
        const bool boneEdit = std::any_of(addresses.begin(), addresses.end(), [&](const auto& a) {
            return a.track == Track::Bone && a.slot == model.slot;
        });
        history.emplace(model.slot,
                        CaptureHistory(model, boneEdit ? HistoryKind::Keys : HistoryKind::None,
                                       std::uint32_t(d.timeline.currentFrame)));
    }

    for (const auto& a : addresses) {
        Key key;
        if (!Get(d, a, key)) {
            reason = "key-selection-expired";
            return false;
        }
    }
    // Group the requested addresses into physical tables. Each table is edited
    // once in native slot order, including roots before later records.
    std::set<std::pair<Track, std::uint8_t>> tables;
    for (const auto& address : addresses)
        tables.emplace(address.track, address.slot);
    if (markedModelRecords)
        for (const auto kind : {Track::Bone, Track::Morph, Track::Display})
            tables.emplace(kind, next.selection.model);
    for (const auto& [kind, slot] : tables) {
        const KeyAddress table{kind, slot, 0, 0};
        if (!Visit(d, table, [&](auto& track) {
                using T = typename std::decay_t<decltype(track.dense)>::value_type;
                std::map<std::uint32_t, std::uint32_t> channels;
                if (!PmmRecordChannels(track, channels))
                    return false;
                std::set<std::uint32_t> selected;
                const auto mark = [&](std::uint32_t index, auto& key) {
                    const KeyAddress address{kind, slot, channels.at(index), key.frame};
                    const bool erase = [&] {
                        if constexpr (std::is_same_v<T, PmmBoneKey> ||
                                      std::is_same_v<T, PmmMorphKey> ||
                                      std::is_same_v<T, PmmDisplayKey>)
                            if (markedModelRecords)
                                return key.allocated != 0;
                        return addresses.count(address) != 0;
                    }();
                    if (erase)
                        selected.insert(index);
                    const bool marked =
                        erase || (!markedModelRecords && next.selection.keys.count(address));
                    if constexpr (std::is_same_v<T, PmmBoneKey> || std::is_same_v<T, PmmMorphKey> ||
                                  std::is_same_v<T, PmmDisplayKey>)
                        key.allocated = marked;
                    else
                        key.selected = marked;
                };
                for (unsigned i = 0; i < track.dense.size(); ++i)
                    mark(i, track.dense[i]);
                for (auto& key : track.sparse)
                    mark(unsigned(key.index), key.value);
                PmmKeyDeletionOptions options;
                options.autoInterpolation = autoInterpolation_;
                if constexpr (std::is_same_v<T, PmmBoneKey>) {
                    for (const auto& body : next.assets.models.at(slot)->Document().rigids)
                        if (body.fields.mode > 0 && body.fields.boneIndex >= 0)
                            options.physicsBones.insert(unsigned(body.fields.boneIndex));
                }
                PmmFrameLineDeletion<T> result;
                if (!DeletePmmKeyRecords(track, selected, options, result))
                    return false;
                if constexpr (std::is_same_v<T, PmmBoneKey>) {
                    auto& entry = history.at(slot);
                    entry.partialKeys = true;
                    entry.keys = {};
                    entry.keyRecords = std::move(result.captured);
                    for (const auto& [index, key] : entry.keyRecords)
                        if (key.allocated)
                            entry.selectedKeyRecords.insert(index);
                }
                return true;
            })) {
            reason = "key-delete-failed";
            return false;
        }
    }
    // Deletion immediately evaluates the edited tracks, including frame zero.
    SceneSession evaluated;
    if (!SceneSession::Create(d, evaluated, reason) ||
        !evaluated.Seek(std::uint32_t(d.timeline.currentFrame))) {
        if (reason.empty())
            reason = "key-delete-evaluation-failed";
        return false;
    }
    bool global = false;
    std::set<std::uint8_t> models;
    if (markedModelRecords)
        models.insert(next.selection.model);
    for (const auto& a : addresses) {
        if (a.track == Track::Bone || a.track == Track::Morph || a.track == Track::Display)
            models.insert(a.slot);
        else
            global = true;
        next.selection.keys.erase(a);
    }
    if (global) {
        // Native global deletion refreshes every global track, without seeking
        // model poses. Preserve pending edits in all unrelated models.
        const auto& value = evaluated.Document();
        d.cameraState = value.cameraState;
        d.cameraState[3] = d.cameraState[4] = 0;
        d.cameraPerspective = value.cameraPerspective;
        d.envelope.settings.cameraFov = value.envelope.settings.cameraFov;
        d.appearance.cameraParentModel = value.appearance.cameraParentModel;
        d.appearance.cameraParentBone = value.appearance.cameraParentBone;
        d.lightState = value.lightState;
        d.selfShadowMode = value.selfShadowMode;
        d.physicsInterval = value.physicsInterval;
        d.physics.gravityNoiseEnabled = value.physics.gravityNoiseEnabled;
        d.physics.gravityNoise = value.physics.gravityNoise;
        d.physics.gravityMagnitude = value.physics.gravityMagnitude;
        d.physics.gravityDirection = value.physics.gravityDirection;
        for (std::size_t i = 0; i < d.accessories.size(); ++i)
            d.accessories[i].state = value.accessories[i].state;
    }
    for (const auto& model : evaluated.Document().models)
        if (models.count(model.slot))
            *Model(d, model.slot) = model;
    return Commit(std::move(next), reason, history, false);
}
bool EditorSession::Copy(const std::set<KeyAddress>& addresses, std::string& reason) {
    if (playing_) {
        reason = "clipboard-copy-during-playback";
        return false;
    }
    const bool cameraMode = selection_.model == 255;
    auto d = Document();
    std::vector<std::pair<std::uint32_t, ClipboardKey>> next;
    for (const auto& a : addresses) {
        const bool modelTrack =
            a.track == Track::Bone || a.track == Track::Morph || a.track == Track::Display;
        if (cameraMode ? modelTrack : (!modelTrack || a.slot != selection_.model))
            continue;
        Key key;
        if (!Get(d, a, key)) {
            reason = "key-selection-expired";
            return false;
        }
        std::vector<char> name;
        if (a.track == Track::Bone || a.track == Track::Morph) {
            auto* model = Model(d, a.slot);
            name = a.track == Track::Bone ? model->boneNames.at(a.channel)
                                          : model->morphNames.at(a.channel);
        }
        std::uint32_t record = a.channel;
        if (!Visit(d, a, [&](auto& track) {
                if (!a.frame)
                    return true;
                std::map<std::uint32_t, std::uint32_t> channels;
                if (!PmmRecordChannels(track, channels))
                    return false;
                for (const auto& entry : track.sparse)
                    if (entry.value.frame == a.frame &&
                        channels.at(unsigned(entry.index)) == a.channel) {
                        record = unsigned(entry.index);
                        return true;
                    }
                return false;
            })) {
            reason = "key-selection-expired";
            return false;
        }
        next.push_back({record, {a, key, std::move(name)}});
    }
    std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b) {
        // Native accessory copy visits table index before accessory slot.
        return std::tie(a.second.address.track, a.first, a.second.address.slot) <
               std::tie(b.second.address.track, b.first, b.second.address.slot);
    });
    auto& clipboard = clipboards_[cameraMode];
    clipboard.clear(); // An empty selection clears only this mode's clipboard.
    for (auto& item : next)
        clipboard.push_back(std::move(item.second));
    reason.clear();
    return true;
}
bool EditorSession::CanPasteFrames(bool mirror) const {
    if (playing_)
        return false;
    const bool cameraMode = selection_.model == 255;
    const auto& clipboard = clipboards_[cameraMode];
    if (!mirror)
        return !clipboard.empty();
    return !cameraMode && std::any_of(clipboard.begin(), clipboard.end(), [](const auto& item) {
        return item.address.track == Track::Bone;
    });
}
bool EditorSession::Paste(std::uint32_t frame, bool mirror, std::string& reason,
                          std::vector<Track>* failedPools) {
    return PasteFrames(frame, mirror, false, reason, failedPools);
}
bool EditorSession::CanPasteToSelectedTrack() const {
    if (playing_)
        return false;
    const bool camera = selection_.model == 255;
    const auto track = camera ? Track::Accessory : Track::Bone;
    const auto& clipboard = clipboards_[camera];
    return std::any_of(clipboard.begin(), clipboard.end(),
                       [&](const auto& item) { return item.address.track == track; });
}
bool EditorSession::PasteToSelectedTrack(std::uint32_t frame, std::string& reason,
                                         std::vector<Track>* failedPools) {
    return PasteFrames(frame, false, true, reason, failedPools);
}
bool EditorSession::PasteFrames(std::uint32_t frame, bool mirror, bool selectedTrack,
                                std::string& reason, std::vector<Track>* failedPools) {
    if (playing_) {
        reason = "clipboard-paste-during-playback";
        return false;
    }
    if (!(selectedTrack ? CanPasteToSelectedTrack() : CanPasteFrames(mirror))) {
        if (failedPools)
            failedPools->clear();
        reason.clear();
        return true; // Original empty/unsupported-mode paste is a no-op.
    }
    EndGesture();
    const auto& clipboard = clipboards_[selection_.model == 255];
    auto first = clipboard.front().address.frame;
    for (const auto& item : clipboard)
        first = std::min(first, item.address.frame);
    auto next = Current();
    auto& d = next.document;
    auto* model = Model(d, selection_.model);
    const auto selectedBone = ActiveBone();
    if (selectedTrack &&
        (model ? selectedBone < 0
               : std::none_of(
                     d.accessories.begin(), d.accessories.end(),
                     [&](const auto& a) { return a.slot == selection_.accessory; }))) {
        reason = model ? "clipboard-select-bone" : "clipboard-select-accessory";
        return false;
    }
    const bool boneClipboard =
        std::any_of(clipboard.begin(), clipboard.end(),
                    [](const auto& item) { return item.address.track == Track::Bone; });
    const bool modelClipboard =
        boneClipboard || std::any_of(clipboard.begin(), clipboard.end(), [](const auto& item) {
            return item.address.track == Track::Morph || item.address.track == Track::Display;
        });
    HistoryBatch history;
    if (modelClipboard) {
        if (!model) {
            reason = "clipboard-model-unavailable";
            return false;
        }
        ClearModelKeyMarks(*model, next.selection);
        if (boneClipboard) {
            auto entry =
                CaptureHistory(*model, HistoryKind::Keys, unsigned(d.timeline.currentFrame));
            entry.partialKeys = true;
            entry.keys = {};
            for (auto& pose : entry.poses) {
                PmmEvaluatedBone live;
                bool controlled;
                if (!runtime_->BoneRegistrationPose(model->slot, pose.index, 0, live, controlled,
                                                    reason))
                    return false;
                std::copy(live.translation.begin(), live.translation.end(),
                          pose.components.begin());
                std::copy(live.rotation.begin(), live.rotation.end(), pose.components.begin() + 3);
            }
            history.emplace(model->slot, std::move(entry));
        } else {
            // Clearing bone selection bytes for an auxiliary-only paste must
            // not become an inferred bone-key Undo operation.
            history.emplace(model->slot, CaptureHistory(*model, HistoryKind::None, 0));
        }
    }
    if (!modelClipboard) {
        const auto clear = [](auto& track) {
            for (auto& key : track.dense)
                key.selected = 0;
            for (auto& key : track.sparse)
                key.value.selected = 0;
            for (auto& [index, key] : track.unused)
                key.selected = 0;
        };
        clear(d.cameraKeys);
        clear(d.lightKeys);
        clear(d.selfShadowKeys);
        clear(d.gravityKeys);
        for (auto& accessory : d.accessories)
            clear(accessory.keyRecords);
        for (auto it = next.selection.keys.begin(); it != next.selection.keys.end();)
            if (it->track != Track::Bone && it->track != Track::Morph &&
                it->track != Track::Display)
                it = next.selection.keys.erase(it);
            else
                ++it;
    }
    std::set<Track> stopped;
    std::vector<Track> rejected;
    for (auto item : clipboard) {
        // Native paste has a separate loop for each record kind. A full pool
        // stops that loop, including any later overwrites, then proceeds to the
        // next kind. All accessory slots belong to the same clipboard loop.
        if (stopped.count(item.address.track))
            continue;
        if (selectedTrack && item.address.track != (model ? Track::Bone : Track::Accessory))
            continue;
        if (mirror && item.address.track != Track::Bone)
            continue;
        const auto offset = item.address.frame - first;
        if (frame > 0x7fffffffU - offset) {
            reason = "frame-overflow";
            return false;
        }
        item.address.frame = frame + offset;
        if (item.address.track == Track::Bone || item.address.track == Track::Morph ||
            item.address.track == Track::Display)
            item.address.slot = selection_.model;
        if (selectedTrack) {
            if (model)
                item.address.channel = unsigned(selectedBone);
            else
                item.address.slot = selection_.accessory;
        } else if (item.address.track == Track::Bone || item.address.track == Track::Morph) {
            const auto& names =
                item.address.track == Track::Bone ? model->boneNames : model->morphNames;
            const int index = FindPmmClipboardBone(
                {item.name.data(), item.name.size()}, names.size(),
                [&](std::size_t i) { return std::string_view(names[i].data(), names[i].size()); },
                mirror);
            if (index < 0)
                continue; // Native name misses are successful no-ops.
            item.address.channel = unsigned(index);
        }
        const auto slot = ProbeRegistration(d, item.address);
        if (slot == RegistrationSlot::Invalid) {
            reason = "clipboard-track-mismatch";
            return false;
        }
        if (slot == RegistrationSlot::Full) {
            stopped.insert(item.address.track);
            rejected.push_back(item.address.track);
            continue;
        }
        if (item.address.track == Track::Bone) {
            int resourceBone;
            if (!runtime_->DocumentBoneIndex(model->slot, int(item.address.channel), resourceBone,
                                             reason))
                return false;
            const auto resource = runtime_->Resource(model->slot);
            const auto& info = resource->EditingBones().at(unsigned(resourceBone));
            auto key = std::get<PmmBoneKey>(item.key);
            key.frame = offset;
            const bool fixed = info.type == cpu_bone::BoneType::FixedAxis ||
                               (info.type == cpu_bone::BoneType::UnderIk &&
                                (info.flags & cpu_bone::kBoneFlagFixedAxis));
            if (fixed) {
                const auto& bone = resource->Document().bones.at(unsigned(resourceBone)).fields;
                float axis[3];
                if (resource->SourceKind() == ModelKind::Pmd) {
                    const auto& tail =
                        resource->Document().bones.at(unsigned(std::max(info.tailBone, 0))).fields;
                    float direction[3];
                    for (unsigned i = 0; i < 3; ++i)
                        direction[i] = tail.position[i] - bone.position[i];
                    cpu_model::Math math;
                    math.normalize(axis, direction);
                } else
                    std::copy_n(bone.axis, 3, axis);
                if (!ConstrainPmmClipboardRotation(key.rotation, axis, mirror)) {
                    reason = "clipboard-fixed-axis-invalid";
                    return false;
                }
            }
            const bool movable = info.type == cpu_bone::BoneType::RotateMove ||
                                 info.type == cpu_bone::BoneType::Move ||
                                 info.type == cpu_bone::BoneType::Ik;
            if (!RegisterPmmBoneClipboard(model->boneKeys, item.address.channel, key, frame,
                                          movable, mirror, history.at(model->slot).keyRecords,
                                          model->selection.maxFrame)) {
                reason = "clipboard-bone-registration-failed";
                return false;
            }
        } else {
            std::visit(
                [](auto& key) {
                    using T = std::decay_t<decltype(key)>;
                    if constexpr (std::is_same_v<T, PmmMorphKey> ||
                                  std::is_same_v<T, PmmDisplayKey>)
                        key.allocated = 1;
                    else if constexpr (!std::is_same_v<T, PmmBoneKey>)
                        key.selected = 1;
                },
                item.key);
            if (!Put(d, item.address, item.key)) {
                reason = "clipboard-track-mismatch";
                return false;
            }
            // Auxiliary/global overwrite returns before updating native watermarks.
            if (slot == RegistrationSlot::Insert) {
                if (model && PmmModelKey(item.address.track))
                    model->selection.maxFrame =
                        std::max(model->selection.maxFrame, item.address.frame);
                else
                    d.timeline.lastRegisteredFrame =
                        std::max(d.timeline.lastRegisteredFrame, int(item.address.frame));
            }
        }
        next.selection.keys.insert(item.address);
    }
    if (modelClipboard) {
        // Native paste seeks this model at the current cursor after inserting
        // all keys. Other models' pending edits remain untouched.
        SceneSession evaluated;
        if (!SceneSession::Create(d, evaluated, reason) ||
            !evaluated.Seek(unsigned(d.timeline.currentFrame), {false, false}))
            return false;
        const auto& result = evaluated.Document().models;
        const auto found = std::find_if(result.begin(), result.end(),
                                        [&](const auto& m) { return m.slot == model->slot; });
        if (found == result.end())
            return false;
        *model = *found;
    } else {
        SceneSession evaluated;
        if (!SceneSession::Create(d, evaluated, reason) ||
            !evaluated.Seek(unsigned(d.timeline.currentFrame)) ||
            !evaluated.SetViewCamera(evaluated.Pose().camera, {})) {
            if (reason.empty())
                reason = "clipboard-scene-evaluation-failed";
            return false;
        }
        auto result = evaluated.Document();
        // Ordinary camera-mode paste also seeks the remembered selected model.
        // Command 250 only refreshes global/accessory tracks, preserving every
        // model's pending pose and the existing physics reset state.
        // ReloadModels is the legacy name of camera-track evaluation.
        for (auto& entry : result.models)
            if (selectedTrack || entry.slot != d.envelope.selectedModelSlot)
                entry = *Model(d, entry.slot);
        d = std::move(result);
    }
    if (!Commit(std::move(next), reason, history, false))
        return false;
    if (modelClipboard || !selectedTrack)
        runtime_->RequestPhysicsReset();
    if (failedPools)
        *failedPools = std::move(rejected);
    return true;
}

bool EditorSession::Undo(std::string& reason) {
    return ApplyHistory(false, reason);
}
bool EditorSession::Redo(std::string& reason) {
    return ApplyHistory(true, reason);
}
bool EditorSession::Seek(std::uint32_t frame, std::string& reason) {
    EndGesture();
    auto history = FrameHistory();
    const bool globals = AppliesGlobalTracks();
    if (!runtime_->Seek(frame, reason, {globals, globals}))
        return false;
    if (globals && !RefreshTrackedView(selection_.model != 255, reason))
        return false;
    if (globals)
        ++gravityEpoch_;
    runtime_->ClearPoseEditMarkers();
    for (auto& item : history)
        PushHistory(item.first, std::move(item.second));
    playing_ = false;
    cursorSeconds_ = frame / 30.;
    return true;
}
bool EditorSession::Play(std::string& reason) {
    EndGesture();
    const auto settings = Document().timeline;
    playOrigin_ = std::uint32_t(settings.currentFrame);
    if (!settings.startAtCurrent && !Seek(std::uint32_t(settings.playStartFrame), reason))
        return false;
    auto history = FrameHistory();
    const bool globals = AppliesGlobalTracks();
    if (!runtime_->StartPlayback(reason, PlaybackApplication(), playbackPhysicsOnOff_))
        return false;
    if (globals && !RefreshTrackedView(false, reason))
        return false;
    if (globals)
        ++gravityEpoch_;
    runtime_->ClearPoseEditMarkers();
    for (auto& item : history)
        PushHistory(item.first, std::move(item.second));
    playing_ = true;
    ++playbackEpoch_;
    return true;
}
void EditorSession::Pause() {
    playing_ = false;
    runtime_->Pause();
}
bool EditorSession::TickIdle(double delta, std::string& reason) {
    if (playing_ || !std::isfinite(delta) || delta < 0 || delta > 1) {
        reason = "invalid-idle-delta-or-playback-active";
        return false;
    }
    if (delta == 0) {
        reason.clear();
        return true;
    }
    return runtime_->Tick(float(delta), reason);
}
bool EditorSession::Advance(double delta, std::string& reason) {
    if (!playing_)
        return true;
    if (!std::isfinite(delta) || delta < 0 || delta > 1) {
        reason = "invalid-playback-delta";
        return false;
    }
    auto settings = Document().timeline;
    auto cursor = cursorSeconds_ + delta;
    if (cursor * 30 > settings.playStopFrame) {
        if (settings.playbackLoop) {
            if (!Seek(std::uint32_t(settings.playStartFrame), reason) ||
                !runtime_->StartPlayback(reason, PlaybackApplication(), playbackPhysicsOnOff_))
                return false;
            if (AppliesGlobalTracks() && !RefreshTrackedView(false, reason))
                return false;
            playing_ = true;
            ++playbackEpoch_;
            return true;
        }
        if (!Seek(std::uint32_t(settings.returnToStart ? playOrigin_ : settings.playStopFrame),
                  reason))
            return false;
        return true;
    }
    const bool globals = AppliesGlobalTracks();
    const bool cameraActive = runtime_->Session()->Playback().camera.active;
    if (!runtime_->Advance(float(cursor), float(delta), reason, PlaybackApplication()))
        return false;
    if (globals && cameraActive && !RefreshTrackedView(false, reason))
        return false;
    if (globals)
        ++gravityEpoch_;
    cursorSeconds_ = cursor;
    return true;
}
std::vector<KeyAddress> EditorSession::Keys() const {
    std::vector<KeyAddress> out;
    const auto& d = Document();
    const auto append = [&](const auto& track, Track kind, std::uint8_t slot) {
        std::map<std::uint32_t, std::uint32_t> channels;
        if (PmmRecordChannels(track, channels)) {
            for (std::uint32_t i = 0; i < track.dense.size(); ++i)
                out.push_back({kind, slot, i, track.dense[i].frame});
            for (const auto& key : track.sparse)
                out.push_back({kind, slot, channels.at(unsigned(key.index)), key.value.frame});
        }
    };
    append(d.cameraKeys, Track::Camera, 255);
    append(d.lightKeys, Track::Light, 255);
    append(d.selfShadowKeys, Track::Shadow, 255);
    append(d.gravityKeys, Track::Gravity, 255);
    for (const auto& m : d.models) {
        append(m.boneKeys, Track::Bone, m.slot);
        append(m.morphKeys, Track::Morph, m.slot);
        append(m.displayKeys, Track::Display, m.slot);
    }
    for (const auto& a : d.accessories)
        append(a.keyRecords, Track::Accessory, a.slot);
    return out;
}
}
