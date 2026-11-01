#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/pmm_key_deletion.hpp"
#include "mikudancestudio/pmm_bone_registration.hpp"
#include "mikudancestudio/pmm_bone_clipboard.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/vmd_scene_tracks.hpp"
#include "mikudancestudio/wave_format.hpp"
#include "mikudancestudio/background_placement.hpp"
#include "mikudancestudio/bone_key_interpolation.hpp"
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
    m.selection.comboIndex = slot;
    m.selection.maxFrame = 300;
    m.edgeScale = 1;
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
bool ReadAccessory(const std::filesystem::path& path,
                   std::shared_ptr<const runtime::AccessoryResource>& out, std::string& reason) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    const auto size = f ? f.tellg() : std::streampos(-1);
    if (size < 0 || size > 512 * 1024 * 1024) {
        reason = "accessory-read-failed";
        return false;
    }
    std::vector<char> bytes(std::size_t(size), 0);
    f.seekg(0);
    if (!f.read(bytes.data(), std::streamsize(bytes.size()))) {
        reason = "accessory-read-failed";
        return false;
    }
    return runtime::AccessoryResource::Parse(bytes.data(), bytes.size(), out, reason);
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
void EditorSession::SetPhysicsFloor(bool enabled) {
    runtime_->SetPhysicsFloor(enabled);
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
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    state_ = std::move(next);
    runtime_ = std::move(runtime);
    path_.clear();
    histories_.clear();
    clipboard_.clear();
    interpolationCopied_ = false;
    selection_ = {};
    playing_ = false;
    coordinateSystem_ = 0;
    cursorSeconds_ = 0;
    revision_ = savedRevision_ = nextRevision_++;
    reason.clear();
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
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    RecordHistory(Current(), next, forced);
    runtime_ = std::move(runtime);
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
    resolver.SetPathDecoder([](const std::string& bytes) -> std::optional<std::filesystem::path> {
        std::string utf8;
        if (!text::DecodeCp932(bytes, utf8))
            return {};
        return std::filesystem::u8path(utf8);
    });
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
        if (!resolver.ResolveAccessory(a.slot, a.path, source) ||
            !ReadAccessory(source, next.assets.accessories[a.slot], reason))
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
    selection_ = {};
    coordinateSystem_ = 0;
    selection_.model = Document().models.empty() || Document().envelope.settings.options[0]
                           ? 255
                           : Document().envelope.selectedModelSlot;
    if (!RestoreModelSelection(selection_.model, reason))
        RestoreModelSelection(255, reason);
    histories_.clear();
    clipboard_.clear();
    playing_ = false;
    revision_ = savedRevision_ = nextRevision_++;
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
        return Commit(std::move(next), reason);
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
    if (!ReadAccessory(absolute, resource, reason))
        return false;
    PmmSceneAccessory a;
    a.slot = std::uint8_t(slot);
    a.order = std::uint8_t(next.document.accessories.size());
    std::string name;
    if (!text::EncodeCp932(absolute.stem().u8string(), name) || !Fixed(name, a.name) ||
        !DiskPath(absolute, absolute.parent_path(), a.path, reason))
        return false;
    a.state.visible = 1;
    a.state.opacity = 1;
    a.state.scale = 1;
    a.state.shadowEnabled = 1;
    a.state.parentModel = a.state.parentBone = -1;
    PmmAccessoryKey k;
    k.visible = 1;
    k.opacity = 1;
    k.scale = 1;
    k.shadowEnabled = 1;
    k.parentModel = k.parentBone = -1;
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
bool EditorSession::Register(const std::vector<KeyAddress>& addresses, std::string& reason) {
    EndGesture();
    if (addresses.empty()) {
        reason = "no-selected-keys";
        return false;
    }
    auto next = Current();
    HistoryBatch history;
    // The native register button visits selected bones in index order, clears
    // this model's key marks before capture, and retains marks in other models.
    const std::set<KeyAddress> ordered(addresses.begin(), addresses.end());
    for (const auto& address : addresses) {
        if (address.track == Track::Bone && !history.count(address.slot)) {
            auto* model = Model(next.document, address.slot);
            if (!model) {
                reason = "key-registration-failed";
                return false;
            }
            history.emplace(address.slot,
                            CaptureHistory(*model, HistoryKind::Keys,
                                           std::uint32_t(next.document.timeline.currentFrame)));
            auto& entry = history.at(address.slot);
            entry.partialKeys = true;
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
            const auto clear = [](auto& track) {
                for (auto& key : track.dense)
                    key.allocated = 0;
                for (auto& key : track.sparse)
                    key.value.allocated = 0;
                for (auto& [index, key] : track.unused)
                    key.allocated = 0;
            };
            clear(model->boneKeys);
            clear(model->morphKeys);
            clear(model->displayKeys);
            for (auto it = next.selection.keys.begin(); it != next.selection.keys.end();) {
                if (it->slot == address.slot &&
                    (it->track == Track::Bone || it->track == Track::Morph ||
                     it->track == Track::Display))
                    it = next.selection.keys.erase(it);
                else
                    ++it;
            }
        }
    }
    for (const auto& address : ordered) {
        Key key;
        if (!Pose(next.document, address, key)) {
            reason = "key-registration-failed";
            return false;
        }
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
            next.selection.keys.insert(address);
        } else if (!Put(next.document, address, key)) {
            reason = "key-registration-failed";
            return false;
        }
    }
    for (const auto& address : ordered) {
        next.document.timeline.lastRegisteredFrame =
            std::max(next.document.timeline.lastRegisteredFrame, int(address.frame));
        if (address.track == Track::Bone || address.track == Track::Morph ||
            address.track == Track::Display) {
            auto* model = Model(next.document, address.slot);
            model->selection.maxFrame = std::max(model->selection.maxFrame, address.frame);
        }
    }
    return Commit(std::move(next), reason, history, false);
}

bool EditorSession::Delete(const std::set<KeyAddress>& addresses, std::string& reason) {
    EndGesture();
    if (addresses.empty()) {
        reason = "no-selected-keys";
        return false;
    }
    auto next = Current();
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
                    const bool erase = addresses.count(address) != 0;
                    if (erase)
                        selected.insert(index);
                    const bool marked = erase || next.selection.keys.count(address);
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
    auto d = Document();
    std::vector<std::pair<std::uint32_t, ClipboardKey>> next;
    for (const auto& a : addresses) {
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
    if (next.empty()) {
        reason = "no-selected-keys";
        return false;
    }
    std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b) {
        return std::tie(a.second.address.track, a.second.address.slot, a.first) <
               std::tie(b.second.address.track, b.second.address.slot, b.first);
    });
    clipboard_.clear();
    for (auto& item : next)
        clipboard_.push_back(std::move(item.second));
    reason.clear();
    return true;
}
bool EditorSession::Paste(std::uint32_t frame, bool mirror, std::string& reason) {
    EndGesture();
    if (clipboard_.empty()) {
        reason = "clipboard-empty";
        return false;
    }
    auto first = clipboard_.front().address.frame;
    for (const auto& item : clipboard_)
        first = std::min(first, item.address.frame);
    auto next = Current();
    auto& d = next.document;
    auto* model = Model(d, selection_.model);
    const bool boneClipboard =
        std::any_of(clipboard_.begin(), clipboard_.end(),
                    [](const auto& item) { return item.address.track == Track::Bone; });
    const bool modelClipboard =
        boneClipboard || std::any_of(clipboard_.begin(), clipboard_.end(), [](const auto& item) {
            return item.address.track == Track::Morph || item.address.track == Track::Display;
        });
    HistoryBatch history;
    if (modelClipboard) {
        if (!model) {
            reason = "clipboard-model-unavailable";
            return false;
        }
        const auto clear = [](auto& track) {
            for (auto& key : track.dense)
                key.allocated = 0;
            for (auto& key : track.sparse)
                key.value.allocated = 0;
            for (auto& [index, key] : track.unused)
                key.allocated = 0;
        };
        clear(model->boneKeys);
        clear(model->morphKeys);
        clear(model->displayKeys);
        for (auto it = next.selection.keys.begin(); it != next.selection.keys.end();)
            if (it->slot == model->slot && (it->track == Track::Bone || it->track == Track::Morph ||
                                            it->track == Track::Display))
                it = next.selection.keys.erase(it);
            else
                ++it;
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
        }
    }
    for (auto item : clipboard_) {
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
        if (item.address.track == Track::Bone || item.address.track == Track::Morph) {
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
            if (model &&
                (item.address.track == Track::Morph || item.address.track == Track::Display))
                model->selection.maxFrame = std::max(model->selection.maxFrame, item.address.frame);
            else
                d.timeline.lastRegisteredFrame =
                    std::max(d.timeline.lastRegisteredFrame, int(item.address.frame));
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
    }
    return Commit(std::move(next), reason, history, false);
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
    if (!runtime_->StartPlayback(reason, PlaybackApplication()))
        return false;
    if (globals && !RefreshTrackedView(false, reason))
        return false;
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
                !runtime_->StartPlayback(reason, PlaybackApplication()))
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
