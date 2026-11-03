#include "mikudancestudio/pmm_edit.hpp"
#include "mikudancestudio/bone_key_interpolation.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace mikudancestudio::format {
bool RebuildPmmBoneInterpolation(PmmKeyTrack<PmmBoneKey>& track, std::size_t channel,
                                 std::uint32_t frame) {
    std::vector<std::vector<PmmBoneKey>> checked;
    if (!PmmTrackChains(track, checked) || channel >= track.dense.size())
        return false;
    std::map<std::uint32_t, PmmBoneKey*> keys;
    for (std::uint32_t i = 0; i < track.dense.size(); ++i)
        keys.emplace(i, &track.dense[i]);
    for (auto& key : track.sparse)
        keys.emplace(std::uint32_t(key.index), &key.value);
    auto index = std::uint32_t(channel);
    while (keys.at(index)->frame < frame && keys.at(index)->next)
        index = keys.at(index)->next;
    if (keys.at(index)->frame != frame)
        return false;
    for (unsigned lane = 0; lane < 4; ++lane)
        keyframe_edit::RebuildBoneCurve([&](auto i) -> auto& { return *keys.at(i); },
                                        std::uint32_t(track.dense.size()), index, lane);
    return true;
}
namespace {
struct Writer {
    std::vector<std::uint8_t> bytes;
    void u8(unsigned n) {
        if (n > 255)
            throw std::runtime_error("pmm-byte-range");
        bytes.push_back(std::uint8_t(n));
    }
    void u32(std::uint32_t n) {
        for (unsigned i = 0; i < 4; ++i)
            bytes.push_back(std::uint8_t(n >> (8 * i)));
    }
    void f(float n) {
        if (!std::isfinite(n))
            throw std::runtime_error("pmm-nonfinite-value");
        std::uint32_t bits;
        std::memcpy(&bits, &n, 4);
        u32(bits);
    }
    void flag(unsigned n) {
        if (n > 1)
            throw std::runtime_error("pmm-invalid-flag");
        u8(n);
    }
    template <class T, std::size_t N> void raw(const std::array<T, N>& a) {
        static_assert(sizeof(T) == 1);
        bytes.insert(bytes.end(), a.begin(), a.end());
    }
    template <std::size_t N> void floats(const std::array<float, N>& a) {
        for (auto n : a)
            f(n);
    }
    void name(const std::vector<char>& n) {
        u8(unsigned(n.size()));
        bytes.insert(bytes.end(), n.begin(), n.end());
    }
    template <class T> void link(const T& k) {
        u32(k.frame);
        u32(k.previous);
        u32(k.next);
    }
    void key(const PmmBoneKey& k) {
        link(k);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                u8(k.interpolation[i + j * 4]);
        floats(k.position);
        floats(k.rotation);
        flag(k.allocated);
        flag(k.physicsDisabled);
    }
    void key(const PmmMorphKey& k) {
        link(k);
        f(k.value);
        flag(k.allocated);
    }
    void key(const PmmDisplayKey& k) {
        link(k);
        flag(k.visible);
        for (auto n : k.ikEnabled)
            flag(n);
        for (auto p : k.rigidStates) {
            u32(p.modelIndex);
            u32(p.boneIndex);
        }
        flag(k.allocated);
    }
    void key(const PmmCameraKey& k) {
        link(k);
        f(k.distance);
        floats(k.eye);
        floats(k.target);
        u32(k.parentModel);
        u32(k.parentBone);
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 4; ++j)
                u8(k.interpolation[j][i]);
        flag(k.perspective);
        u32(k.fov);
        flag(k.selected);
    }
    void key(const PmmLightKey& k) {
        link(k);
        floats(k.color);
        floats(k.direction);
        flag(k.selected);
    }
    void key(const PmmGravityKey& k) {
        link(k);
        flag(k.noiseEnabled);
        u32(k.noise);
        f(k.acceleration);
        floats(k.direction);
        flag(k.selected);
    }
    void key(const PmmSelfShadowKey& k) {
        link(k);
        if (k.mode > 2)
            throw std::runtime_error("pmm-shadow-mode");
        u8(k.mode);
        f(k.distance);
        flag(k.selected);
    }
    void opacity(unsigned visible, float value) {
        if (visible > 1 || !std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("pmm-opacity");
        const auto n = std::lround(value * 100);
        if (std::abs(value - float(n) / 100) > 1e-5f)
            throw std::runtime_error("pmm-opacity-precision");
        u8(unsigned((100 - n) * 2 + visible));
    }
    void key(const PmmAccessoryKey& k) {
        link(k);
        opacity(k.visible, k.opacity);
        u32(k.parentModel);
        u32(k.parentBone);
        floats(k.position);
        floats(k.rotation);
        f(k.scale);
        flag(k.shadowEnabled);
        flag(k.selected);
    }
    template <class T> void track(const PmmKeyTrack<T>& t, std::size_t dense) {
        std::vector<std::vector<T>> checked;
        if (t.dense.size() != dense || !PmmTrackChains(t, checked))
            throw std::runtime_error("pmm-invalid-track-links");
        for (const auto& k : t.dense)
            key(k);
        u32(std::uint32_t(t.sparse.size()));
        for (const auto& k : t.sparse) {
            u32(k.index);
            key(k.value);
        }
        if (bytes.size() > (512u << 20))
            throw std::runtime_error("pmm-output-limit");
    }
};
struct Reader {
    const std::vector<std::uint8_t>& data;
    std::size_t at = 0;
    static int Read(void* p, void* out, std::size_t size) {
        auto& r = *static_cast<Reader*>(p);
        size = std::min(size, r.data.size() - r.at);
        if (size)
            std::memcpy(out, r.data.data() + r.at, size);
        r.at += size;
        return int(size);
    }
};
void Model(Writer& w, const PmmSceneModel& m) {
    const auto bones = m.boneNames.size(), morphs = m.morphNames.size(), ik = m.ikSources.size(),
               rigid = m.rigidSources.size();
    if (m.slot == 255 || bones > 100000 || morphs > 100000 || ik > 100000 || rigid > 100000 ||
        bones != m.layout.boneCount || morphs != m.layout.morphCount || ik != m.layout.ikCount ||
        rigid != m.rigidCount || m.bonePose.size() != bones || m.morphPose.size() != morphs ||
        m.ikPose.size() != ik || m.rigidPose.size() != rigid ||
        m.selection.groupFlags.size() != m.groupCount)
        throw std::runtime_error("pmm-model-layout");
    w.u8(m.slot);
    w.name(m.name);
    w.name(m.sourceName);
    w.raw(m.path);
    w.u8(m.groupCount);
    w.u32(std::uint32_t(bones));
    for (const auto& n : m.boneNames)
        w.name(n);
    w.u32(std::uint32_t(morphs));
    for (const auto& n : m.morphNames)
        w.name(n);
    w.u32(std::uint32_t(ik));
    for (auto n : m.ikSources)
        w.u32(n);
    w.u32(std::uint32_t(rigid));
    for (auto n : m.rigidSources)
        w.u32(n);
    const auto& s = m.selection;
    w.u8(s.comboIndex);
    w.flag(s.loadComplete);
    w.u32(s.selectedBone);
    for (auto n : s.selectedMorphs)
        w.u32(n);
    w.u8(m.groupCount);
    for (auto f : s.groupFlags)
        w.flag(f);
    w.u32(s.boneListPos);
    w.u32(s.maxFrame);
    w.track(m.boneKeys, bones);
    w.track(m.morphKeys, morphs);
    const auto check = [&](const PmmDisplayKey& k) {
        if (k.ikEnabled.size() != ik || k.rigidStates.size() != rigid)
            throw std::runtime_error("pmm-display-layout");
    };
    for (const auto& k : m.displayKeys.dense)
        check(k);
    for (const auto& k : m.displayKeys.sparse)
        check(k.value);
    for (const auto& k : m.displayKeys.sparse)
        if (k.index >= 1000)
            throw std::runtime_error("pmm-display-capacity");
    w.track(m.displayKeys, 1);
    for (const auto& pose : m.bonePose) {
        for (unsigned i = 0; i < 7; ++i)
            w.f(pose.components[i]);
        w.raw(pose.flags);
    }
    for (auto f : m.morphPose)
        w.f(f);
    for (auto f : m.ikPose)
        w.flag(f);
    for (const auto& pose : m.rigidPose)
        for (auto n : pose.values)
            w.u32(n);
    w.u8(m.postLoadFlag);
    w.f(m.edgeScale);
    w.u8(m.toonFlag);
    w.u8(m.previousDisplayOrder);
}
void Scene(Writer& w, const PmmSceneDocument& d) {
    if (d.envelope.header.version != PmmVersion::V2)
        throw std::runtime_error("editor-requires-pmm-v2-conversion");
    if (d.models.size() > 254 || d.accessories.size() > 254 ||
        d.accessoryListNames.size() != d.accessories.size())
        throw std::runtime_error("pmm-slot-count");
    std::array<char, 30> signature{};
    std::memcpy(signature.data(), "Polygon Movie maker 0002", 24);
    w.raw(signature);
    const auto& s = d.envelope.settings;
    w.u32(s.renderWidth);
    w.u32(s.renderHeight);
    w.u32(s.editPanelWidth);
    w.f(s.cameraFov);
    for (auto n : s.options)
        w.flag(n);
    w.u8(d.envelope.selectedModelSlot);
    w.u8(unsigned(d.models.size()));
    for (const auto& m : d.models)
        Model(w, m);
    w.track(d.cameraKeys, 1);
    w.floats(d.cameraState);
    w.flag(d.cameraPerspective);
    w.track(d.lightKeys, 1);
    w.floats(d.lightState);
    w.u8(d.selectedAccessorySlot);
    w.u32(d.displayListScroll);
    w.u8(unsigned(d.accessories.size()));
    for (const auto& n : d.accessoryListNames)
        w.raw(n);
    for (const auto& a : d.accessories) {
        w.u8(a.slot);
        w.raw(a.name);
        w.raw(a.path);
        w.u8(a.order);
        w.track(a.keyRecords, 1);
        const auto& v = a.state;
        w.opacity(v.visible, v.opacity);
        w.u32(v.parentModel);
        w.u32(v.parentBone);
        w.floats(v.rotation);
        w.f(v.scale);
        w.floats(v.position);
        w.flag(v.shadowEnabled);
        w.flag(v.additiveBlend);
    }
    const auto& t = d.timeline;
    if (t.currentFrame < 0 || t.startFrame < 0 || t.playStartFrame < 0 || t.playStopFrame < 0)
        throw std::runtime_error("pmm-negative-frame");
    w.u32(t.currentFrame);
    w.u32(t.startFrame);
    w.u32(t.lastRegisteredFrame);
    w.u32(t.editMode);
    w.u8(t.cameraReferenceMode);
    w.flag(t.playbackLoop);
    w.flag(t.returnToStart);
    w.flag(t.startAtCurrent);
    w.u32(t.playStartFrame);
    w.u32(t.playStopFrame);
    w.flag(d.wave.enabled);
    w.raw(d.wave.pathBytes);
    for (const auto* b : {&d.avi, &d.picture}) {
        w.u32(b->offsetX);
        w.u32(b->offsetY);
        w.f(b->scale);
        w.raw(b->pathBytes);
        if (b == &d.avi)
            w.u32(b->enabled);
        else
            w.flag(b->enabled);
    }
    const auto& v = d.view;
    w.flag(v.fpsOverlay);
    w.flag(v.groundGrid);
    w.flag(v.groundShadow);
    w.f(v.fpsLimit);
    w.u32(v.captureMode);
    const auto& p = d.physics;
    w.u32(p.accessoryRenderOrder);
    w.f(p.projectedShadowAmbient);
    w.flag(p.projectedShadowBlend);
    if (p.playbackMode > 3)
        throw std::runtime_error("pmm-physics-mode");
    w.u8(p.playbackMode);
    w.f(p.gravityMagnitude);
    w.u32(p.gravityNoise);
    w.floats(p.gravityDirection);
    w.flag(p.gravityNoiseEnabled);
    w.track(d.gravityKeys, 1);
    w.u8(d.selfShadowEnabled);
    w.f(d.physicsInterval);
    w.track(d.selfShadowKeys, 1);
    const auto& a = d.appearance;
    for (auto n : a.outlineColor)
        w.u32(n);
    w.flag(a.blackBackground);
    w.u32(a.cameraParentModel);
    w.u32(a.cameraParentBone);
    w.floats(a.cameraAttachmentBasis);
    w.flag(a.followCamera);
    w.flag(a.suppressCameraTransform);
    w.flag(a.floorVisible);
    w.u32(a.maxFrame);
    if (!d.modelRegistrations.empty()) {
        if (d.modelRegistrations.size() != d.models.size())
            throw std::runtime_error("pmm-registration-count");
        w.flag(1);
        for (auto r : d.modelRegistrations) {
            w.u8(r.slot);
            w.u32(r.selection);
        }
    }
}
}
bool PreservePmmSessionState(const PmmSceneDocument& source, PmmSceneDocument& target) {
    target.selfShadowMode = source.selfShadowMode;
    const auto copy = [](const auto& from, auto& to) {
        // The preservation writer also accepts legacy tracks outside the
        // topology editor's stricter contract. Validate only this transient
        // cache here; do not impose PmmTrackChains on unchanged disk records.
        using T = typename std::decay_t<decltype(from.dense)>::value_type;
        if (to.dense.size() > PmmKeyCapacity<T> ||
            from.unused.size() > PmmKeyCapacity<T> - to.dense.size())
            return false;
        if (!from.unused.empty()) {
            std::set<std::uint32_t> occupied;
            for (const auto& record : to.sparse)
                occupied.insert(std::uint32_t(record.index));
            for (const auto& [index, key] : from.unused)
                if (index < to.dense.size() || index >= PmmKeyCapacity<T> || key.frame ||
                    occupied.count(index))
                    return false;
        }
        to.unused = from.unused;
        return true;
    };
    if (!copy(source.cameraKeys, target.cameraKeys) || !copy(source.lightKeys, target.lightKeys) ||
        !copy(source.selfShadowKeys, target.selfShadowKeys) ||
        !copy(source.gravityKeys, target.gravityKeys))
        return false;
    for (const auto& from : source.models) {
        const auto to = std::find_if(target.models.begin(), target.models.end(),
                                     [&](const auto& model) { return model.slot == from.slot; });
        if (to == target.models.end() || !copy(from.boneKeys, to->boneKeys) ||
            !copy(from.morphKeys, to->morphKeys) || !copy(from.displayKeys, to->displayKeys))
            return false;
        to->pmdEdgeDiffuse = from.pmdEdgeDiffuse;
    }
    for (const auto& from : source.accessories) {
        const auto to = std::find_if(target.accessories.begin(), target.accessories.end(),
                                     [&](const auto& item) { return item.slot == from.slot; });
        if (to == target.accessories.end() || !copy(from.keyRecords, to->keyRecords))
            return false;
    }
    return true;
}
bool RebuildPmmV2(const PmmSceneDocument& source, PmmSceneDocument& output, std::string& reason) {
    try {
        Writer w;
        Scene(w, source);
        Reader read{w.bytes};
        PmmSceneDocument checked;
        if (!ReadPmmSceneDocument(Reader::Read, &read, nullptr, nullptr, checked)) {
            reason = "pmm-rebuilt-document-rejected";
            return false;
        }
        if (!PreservePmmSessionState(source, checked)) {
            reason = "pmm-unused-records-invalid";
            return false;
        }
        output = std::move(checked);
        reason.clear();
        return true;
    } catch (const std::runtime_error& e) {
        reason = e.what();
        return false;
    }
}
PmmSceneDocument NewPmmScene() {
    PmmSceneDocument d;
    d.envelope.header.version = PmmVersion::V2;
    d.envelope.settings = {640, 480, 250, 45, {1, 1, 1, 1, 1, 1, 1}};
    PmmCameraKey camera;
    camera.distance = -45;
    camera.eye = {0, 10, 0};
    camera.parentModel = camera.parentBone = -1;
    camera.fov = 45;
    for (unsigned i = 0; i < 4; ++i)
        camera.interpolation[i].fill(i < 2 ? 20 : 107);
    d.cameraKeys.dense = {camera};
    d.cameraState = {0, 10, 0, 0, 0, -45, 0, 0, 0};
    PmmLightKey light;
    light.color = {.602f, .602f, .602f};
    light.direction = {-.5f, -1, .5f};
    d.lightKeys.dense = {light};
    d.lightState = {.602f, .602f, .602f, -.5f, -1, .5f};
    PmmGravityKey gravity;
    gravity.acceleration = 9.8000002f;
    gravity.noise = 10;
    gravity.direction = {0, -1, 0};
    d.gravityKeys.dense = {gravity};
    d.physics.gravityMagnitude = gravity.acceleration;
    d.physics.gravityNoise = 10;
    d.physics.gravityDirection = gravity.direction;
    d.physics.playbackMode = 2;
    PmmSelfShadowKey shadow;
    shadow.mode = 1;
    shadow.distance = .01125f;
    d.selfShadowKeys.dense = {shadow};
    d.selfShadowEnabled = d.selfShadowMode = 1;
    d.physicsInterval = shadow.distance;
    d.timeline.playStopFrame = 300;
    d.view.groundGrid = 1;
    d.view.fpsLimit = 60;
    d.avi.scale = d.picture.scale = 1;
    d.appearance.cameraParentModel = d.appearance.cameraParentBone = -1;
    d.appearance.maxFrame = 300;
    d.appearance.floorVisible = 1;
    for (unsigned i = 0; i < 4; ++i)
        d.appearance.cameraAttachmentBasis[i * 5] = 1;
    return d;
}
}
