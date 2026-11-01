#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/vmd_scene_tracks.hpp"
#include "mikudancestudio/wave_format.hpp"
#include <fstream>
#include <cmath>
#include <cstring>
#include <limits>

namespace mikudancestudio::editor {
using namespace format;
namespace {
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
    m.groupCount = std::uint8_t(std::min<std::size_t>(255, resource.Document().frames.size()));
    m.ikSources = resource.IkBoneIndices();
    std::set<int> selectors;
    for (std::size_t i = 0; i < resource.Document().bones.size(); ++i) {
        const auto& bone = resource.Document().bones[i].fields;
        if ((bone.flags & 0x2000) && selectors.insert(bone.extParent).second)
            m.rigidSources.push_back(int(i));
    }
    m.layout.ikCount = std::uint32_t(m.ikSources.size());
    m.rigidCount = std::uint32_t(m.rigidSources.size());
    m.selection.groupFlags.resize(m.groupCount, 1);
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
    out.selection = selection_;
    out.revision = revision_;
    return out;
}
void EditorSession::SetPanelExpanded(unsigned option, bool expanded) {
    runtime_->SetPanelExpanded(option, expanded);
    // Panel layout is not a pose edit and must survive undo/redo unchanged.
    for (auto* history : {&undo_, &redo_})
        for (auto& state : *history)
            if (option > 0 && option < state.document.envelope.settings.options.size())
                state.document.envelope.settings.options[option] = expanded;
}
bool EditorSession::Prepare(State& state, std::unique_ptr<runtime::SceneRuntime>& out,
                            std::string& reason) const try {
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
    State next;
    next.document = NewPmmScene();
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    state_ = std::move(next);
    runtime_ = std::move(runtime);
    path_.clear();
    undo_.clear();
    redo_.clear();
    clipboard_.clear();
    selection_ = {};
    playing_ = false;
    cursorSeconds_ = 0;
    revision_ = savedRevision_ = nextRevision_++;
    reason.clear();
    return true;
}
bool EditorSession::Commit(State next, std::string& reason) {
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
        m.selection.maxFrame = std::max({last(m.boneKeys), last(m.morphKeys), last(m.displayKeys)});
        end = std::max(end, m.selection.maxFrame);
    }
    for (const auto& a : d.accessories)
        end = std::max(end, last(a.keyRecords));
    if (end > 0x7fffffffu) {
        reason = "frame-out-of-range";
        return false;
    }
    d.timeline.lastRegisteredFrame = int(end);
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    undo_.push_back(Current());
    if (undo_.size() > 100)
        undo_.erase(undo_.begin());
    redo_.clear();
    runtime_ = std::move(runtime);
    state_ = std::move(next);
    selection_ = state_.selection;
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
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    runtime_ = std::move(runtime);
    state_ = std::move(next);
    path_ = absolute;
    selection_ = {};
    selection_.model = Document().models.empty() ? 255 : Document().envelope.selectedModelSlot;
    undo_.clear();
    redo_.clear();
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
bool EditorSession::SetBone(std::uint8_t slot, std::uint32_t bone,
                            const std::array<float, 3>& position,
                            const std::array<float, 4>& rotation, std::string& reason) {
    return Edit(
        "bone-transform",
        [&](auto& d, auto& error) {
            auto* m = Model(d, slot);
            if (!m || bone >= m->bonePose.size()) {
                error = "bone-selection-expired";
                return false;
            }
            float length = 0;
            for (float x : rotation)
                length += x * x;
            if (!std::isfinite(length) || std::abs(length - 1) > 1e-3f) {
                error = "bone-rotation-not-unit";
                return false;
            }
            std::copy(position.begin(), position.end(), m->bonePose[bone].components.begin());
            std::copy(rotation.begin(), rotation.end(), m->bonePose[bone].components.begin() + 3);
            return true;
        },
        reason);
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
bool EditorSession::SetCamera(const PmmEvaluatedCamera& c, std::string& reason) {
    return Edit(
        "camera-transform",
        [&](auto& d, auto& error) {
            if (!std::isfinite(c.fov) || c.fov < 1 || c.fov > 179) {
                error = "camera-fov-out-of-range";
                return false;
            }
            for (int i = 0; i < 3; ++i) {
                d.cameraState[i] = c.position[i];
                d.cameraState[6 + i] = c.rotation[i];
            }
            d.cameraState[5] = c.distance;
            d.envelope.settings.cameraFov = c.fov;
            d.cameraPerspective = c.perspective;
            d.appearance.cameraParentModel = c.parentModel;
            d.appearance.cameraParentBone = c.parentBone;
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
    return Edit(
        "register-keys",
        [&](auto& d, auto& error) {
            for (const auto& a : addresses) {
                Key key;
                if (!Pose(d, a, key) || !Put(d, a, key)) {
                    error = "key-registration-failed";
                    return false;
                }
            }
            return !addresses.empty();
        },
        reason);
}
bool EditorSession::Delete(const std::set<KeyAddress>& addresses, std::string& reason) {
    return Edit(
        "delete-keys",
        [&](auto& d, auto& error) {
            bool changed = false;
            for (const auto& a : addresses)
                if (a.frame) {
                    if (!Visit(d, a, [&](auto& track) {
                            return DeletePmmKey(track, a.channel, a.frame);
                        })) {
                        error = "key-selection-expired";
                        return false;
                    }
                    changed = true;
                }
            if (!changed)
                error = "no-deletable-keys";
            return changed;
        },
        reason);
}
bool EditorSession::Copy(const std::set<KeyAddress>& addresses, std::string& reason) {
    auto d = Document();
    std::vector<ClipboardKey> next;
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
        next.push_back({a, key, std::move(name)});
    }
    if (next.empty()) {
        reason = "no-selected-keys";
        return false;
    }
    clipboard_ = std::move(next);
    reason.clear();
    return true;
}
bool EditorSession::Paste(std::uint32_t frame, bool mirror, std::string& reason) {
    if (clipboard_.empty()) {
        reason = "clipboard-empty";
        return false;
    }
    auto first = clipboard_.front().address.frame;
    for (const auto& item : clipboard_)
        first = std::min(first, item.address.frame);
    return Edit(
        "paste-keys",
        [&](auto& d, auto& error) {
            for (auto item : clipboard_) {
                const auto offset = item.address.frame - first;
                if (frame > 0x7fffffffU - offset) {
                    error = "frame-overflow";
                    return false;
                }
                item.address.frame = frame + offset;
                if (item.address.track == Track::Bone || item.address.track == Track::Morph ||
                    item.address.track == Track::Display)
                    item.address.slot = selection_.model;
                if (item.address.track == Track::Bone || item.address.track == Track::Morph) {
                    auto* model = Model(d, item.address.slot);
                    if (!model) {
                        error = "clipboard-model-unavailable";
                        return false;
                    }
                    const auto& names =
                        item.address.track == Track::Bone ? model->boneNames : model->morphNames;
                    auto found = std::find(names.begin(), names.end(), item.name);
                    if (found == names.end()) {
                        error = "clipboard-name-not-bound";
                        return false;
                    }
                    item.address.channel = std::uint32_t(found - names.begin());
                }
                if (mirror && item.address.track == Track::Bone) {
                    auto* m = Model(d, item.address.slot);
                    if (!m || item.address.channel >= m->boneNames.size())
                        return false;
                    auto name = m->boneNames[item.address.channel];
                    // CP932 left/right prefix: same reflection used by MMD.
                    if (name.size() >= 2 &&
                        ((std::uint8_t(name[0]) == 0x8d && std::uint8_t(name[1]) == 0xb6) ||
                         (std::uint8_t(name[0]) == 0x89 && std::uint8_t(name[1]) == 0x45))) {
                        const bool left = std::uint8_t(name[0]) == 0x8d;
                        name[0] = char(left ? 0x89 : 0x8d);
                        name[1] = char(left ? 0x45 : 0xb6);
                        auto it = std::find(m->boneNames.begin(), m->boneNames.end(), name);
                        if (it != m->boneNames.end())
                            item.address.channel = std::uint32_t(it - m->boneNames.begin());
                    }
                    auto& k = std::get<PmmBoneKey>(item.key);
                    k.position[0] = -k.position[0];
                    k.rotation[1] = -k.rotation[1];
                    k.rotation[2] = -k.rotation[2];
                }
                if (!Put(d, item.address, item.key)) {
                    error = "clipboard-track-mismatch";
                    return false;
                }
            }
            return true;
        },
        reason);
}
bool EditorSession::Undo(std::string& reason) {
    if (undo_.empty()) {
        reason = "undo-empty";
        return false;
    }
    auto next = undo_.back();
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    redo_.push_back(Current());
    undo_.pop_back();
    state_ = std::move(next);
    runtime_ = std::move(runtime);
    selection_ = state_.selection;
    revision_ = state_.revision;
    playing_ = false;
    cursorSeconds_ = Document().timeline.currentFrame / 30.;
    return true;
}
bool EditorSession::Redo(std::string& reason) {
    if (redo_.empty()) {
        reason = "redo-empty";
        return false;
    }
    auto next = redo_.back();
    std::unique_ptr<runtime::SceneRuntime> runtime;
    if (!Prepare(next, runtime, reason))
        return false;
    undo_.push_back(Current());
    redo_.pop_back();
    state_ = std::move(next);
    runtime_ = std::move(runtime);
    selection_ = state_.selection;
    revision_ = state_.revision;
    playing_ = false;
    cursorSeconds_ = Document().timeline.currentFrame / 30.;
    return true;
}
bool EditorSession::Seek(std::uint32_t frame, std::string& reason) {
    if (!runtime_->Seek(frame, reason))
        return false;
    playing_ = false;
    cursorSeconds_ = frame / 30.;
    return true;
}
bool EditorSession::Play(std::string& reason) {
    const auto settings = Document().timeline;
    playOrigin_ = std::uint32_t(settings.currentFrame);
    if (!settings.startAtCurrent && !Seek(std::uint32_t(settings.playStartFrame), reason))
        return false;
    if (!runtime_->StartPlayback(reason))
        return false;
    playing_ = true;
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
                !runtime_->StartPlayback(reason))
                return false;
            playing_ = true;
            return true;
        }
        if (!Seek(std::uint32_t(settings.returnToStart ? playOrigin_ : settings.playStopFrame),
                  reason))
            return false;
        return true;
    }
    if (!runtime_->Advance(float(cursor), float(delta), reason))
        return false;
    cursorSeconds_ = cursor;
    return true;
}
std::vector<KeyAddress> EditorSession::Keys() const {
    std::vector<KeyAddress> out;
    const auto& d = Document();
    const auto append = [&](const auto& track, Track kind, std::uint8_t slot) {
        using T = typename std::decay_t<decltype(track.dense)>::value_type;
        std::vector<std::vector<T>> chains;
        if (PmmTrackChains(track, chains))
            for (std::size_t i = 0; i < chains.size(); ++i)
                for (const auto& k : chains[i])
                    out.push_back({kind, slot, std::uint32_t(i), k.frame});
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
