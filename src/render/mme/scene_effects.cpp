#include "mikudancestudio/scene_effects.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/cpu_math.hpp"
#include "scene_effect_controls.hpp"
#include <chrono>
#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>
namespace mikudancestudio::render {
using namespace mme;
using namespace rhi;
namespace {
[[noreturn]] void Fail(const std::string& s) {
    throw std::runtime_error(s);
}
SemanticContext Context(const ModelReplayDraw& draw, unsigned width, unsigned height) {
    SemanticContext context;
    auto& i = context.standard;
    const auto& s = draw.pipeline.shader;
    i.world = cpu_math::Identity();
    i.beginWorld = s.world;
    i.view = cpu_math::Multiply(s.world, s.view);
    i.projection = s.projection;
    i.material = {s.nativeDiffuse, s.materialAmbient, s.materialSpecular, s.emissive};
    i.power = s.shininess;
    i.viewport = {float(width), float(height)};
    i.lightColor = {s.lights[0].diffuse, s.lights[0].ambient, s.lights[0].specular};
    i.lightDirection = s.lights[0].direction;
    for (const auto& t : s.layout.stages) {
        i.flags[0] |= t.role == MaterialTextureRole::Base;
        i.flags[1] |= t.role == MaterialTextureRole::Sphere;
        i.flags[2] |=
            t.role == MaterialTextureRole::Sphere && t.coordinates == MaterialCoordinates::MeshUv1;
        i.flags[3] |= t.role == MaterialTextureRole::Toon;
    }
    return context;
}
}
struct SceneEffects::Impl {
    EffectPool pool;
    FrameClock clock;
    TargetRegistry targets;
    std::unique_ptr<EffectCompiler> compiler;
    ScheduleInput input;
    CompiledEffects compiled;
    std::map<editor::EffectAddress, std::shared_ptr<const EffectDefinition>> definitions;
    std::map<editor::EffectAddress, std::shared_ptr<const EffectDefinition>> inspected;
    std::uint64_t inspectedRevision = 0;
    std::unique_ptr<ScheduledFrame> pending;
    std::string report, backend, failed;
    std::uint64_t revision = 0, nextFrame = 1, deviceRevision = 1;
    std::map<std::filesystem::path, std::uint64_t> ids;
    SourceOptions source;
};
SceneEffects::SceneEffects() : impl_(new Impl) {
    impl_->source.preprocessor = DefaultSpirvCompiler()->Executable();
}
SceneEffects::~SceneEffects() = default;
void SceneEffects::SetSourceOptions(SourceOptions options) {
    impl_->source = std::move(options);
    impl_->revision = 0;
    impl_->inspectedRevision = 0;
}
void SceneEffects::Reset() {
    auto source = impl_->source;
    impl_.reset(new Impl);
    impl_->source = std::move(source);
}
void SceneEffects::Abandon() {
    impl_->pending.reset();
}
void SceneEffects::InvalidateTargets() {
    Abandon();
    ++impl_->deviceRevision;
}
const std::string& SceneEffects::Report() const {
    return impl_->report;
}
std::shared_ptr<const EffectDefinition> SceneEffects::Definition(editor::EffectAddress a) const {
    const auto it = impl_->definitions.find(a);
    return it == impl_->definitions.end() ? nullptr : it->second;
}
bool SceneEffects::Commit(std::string& reason) {
    if (!impl_->pending) {
        reason.clear();
        return true;
    }
    if (!impl_->pending->Commit(reason))
        return false;
    impl_->pending.reset();
    ++impl_->nextFrame;
    impl_->report = "submitted-and-committed\n" + impl_->report;
    return true;
}
std::shared_ptr<const EffectDefinition>
SceneEffects::Inspect(editor::EffectAddress address, const editor::EffectDocument& document,
                      std::uint64_t revision) {
    auto& s = *impl_;
    if (s.revision == revision)
        if (auto definition = Definition(address))
            return definition;
    const auto assignment = document.assignments.find(address);
    if (assignment == document.assignments.end())
        return {};
    if (s.inspectedRevision != revision) {
        s.inspected.clear();
        s.inspectedRevision = revision;
    }
    auto found = s.inspected.find(address);
    if (found != s.inspected.end())
        return found->second;
    std::shared_ptr<const EffectDefinition> definition;
    std::string reason;
    if (!LoadEffectDefinition(assignment->second.source, s.source, definition, reason))
        s.report = reason;
    s.inspected.emplace(address, definition);
    return definition;
}
bool SceneEffects::Build(const editor::EffectDocument& document, std::uint64_t revision,
                         const runtime::SceneRuntime& scene, const SceneRenderSources& sources,
                         const SceneRenderSettings& settings, const BackendOptions& options,
                         const RenderFrame& base, RenderFrame& output, std::string& reason) try {
    auto& s = *impl_;
    s.pending.reset();
    const auto backend = std::to_string(unsigned(options.backend));
    if (document.basicMaterials ||
        std::none_of(document.assignments.begin(), document.assignments.end(),
                     [](const auto& row) { return row.second.enabled; })) {
        s.report = document.basicMaterials ? "basic-materials-explicitly-selected" : "no-effects";
        output = base;
        reason.clear();
        return true;
    }
    if (!s.compiler || s.backend != backend) {
        s.compiler = std::make_unique<EffectCompiler>(
            EffectCompilerOptions{DefaultSpirvCompiler(), backend});
        s.backend = backend;
        s.revision = 0;
        ++s.deviceRevision;
    }
    if (s.revision != revision) {
        s.revision = revision;
        s.failed.clear();
        s.definitions.clear();
        s.input = {};
        s.compiled.clear();
        s.ids.clear();
        s.pool = EffectPool{};
        s.clock = FrameClock{};
        s.nextFrame = 1;
        std::set<std::filesystem::path> loading;
        std::function<std::uint64_t(const std::filesystem::path&, std::uint64_t,
                                    const std::map<std::string, ParameterValue>*)>
            load;
        load = [&](const auto& path, std::uint64_t reload, const auto* overrides) -> std::uint64_t {
            const auto canonical = std::filesystem::absolute(path).lexically_normal();
            if (loading.count(canonical))
                Fail("effect-dependency-cycle");
            auto found = s.ids.find(canonical);
            if (!overrides && found != s.ids.end())
                return found->second;
            if (s.input.effects.size() >= 64 || !loading.insert(canonical).second)
                Fail("effect-dependency-budget-or-cycle");
            std::shared_ptr<const EffectDefinition> def;
            if (!LoadEffectDefinition(canonical, s.source, def, reason))
                Fail(canonical.u8string() + ":" + reason);
            std::shared_ptr<EffectInstance> instance;
            if (!EffectInstance::Create(def, s.pool, instance, reason))
                Fail(reason);
            if (overrides)
                for (const auto& [name, value] : *overrides)
                    if (!instance->Write(name, value, reason))
                        Fail(name + ":" + reason);
            const auto id = std::uint64_t(s.input.effects.size() + 1);
            s.ids.emplace(canonical, id);
            ScheduleDescription description;
            if (!DescribeSchedule({id, reload, canonical, instance}, description, reason))
                Fail(reason);
            std::shared_ptr<const CompiledEffect> compiled;
            if (!s.compiler->Compile(description, compiled, reason))
                Fail(reason);
            s.input.techniqueValidity[id] = compiled->techniques;
            s.compiled[id] = compiled;
            s.input.effects.push_back(description);
            for (const auto& target : description.targets)
                for (const auto& entry : target.defaults) {
                    if (entry.second == "hide" || entry.second == "none" ||
                        entry.second == "main_default")
                        continue;
                    load(canonical.parent_path() / std::filesystem::u8path(entry.second), reload,
                         nullptr);
                }
            loading.erase(canonical);
            return id;
        };
        try {
            for (const auto& [address, e] : document.assignments) {
                if (!e.enabled)
                    continue;
                const auto id = load(e.source, e.reload, &e.parameters);
                const auto object = editor::EffectObjectId(address.accessory, address.slot);
                s.input.assignments.effects[{0, object, address.subset}] = {
                    Assignment::Kind::Effect, id};
                s.definitions[address] = s.compiled.at(id)->definition;
            }
        } catch (const std::exception& e) {
            s.failed = e.what();
        }
    }
    if (!s.failed.empty())
        Fail(s.failed);
    if (!scene.Session())
        Fail("effect-scene-session-required");
    RenderGeometry geometry;
    geometry.resources = base.resources;
    const FramePass* main = nullptr;
    for (const auto& p : base.passes) {
        if (p.name == "scene")
            main = &p;
        else if (p.name == "self_shadow")
            geometry.prepasses.push_back(p);
        else if (p.name != "post_process")
            Fail("effect-host-pass-not-supported:" + p.name);
    }
    if (!main)
        Fail("effect-main-scene-missing");
    for (const auto& t : base.targets)
        if (t.id != 1 && t.id != 2 && t.id != base.finalColor)
            geometry.externalTargets.push_back(t);
    bool after = false;
    for (const auto& draw : main->draws) {
        const bool material =
            draw.sceneObject && (draw.pipeline.shadow.mode == BuiltinShadowMode::None ||
                                 draw.pipeline.shadow.mode == BuiltinShadowMode::Material);
        if (material) {
            after = true;
            geometry.draws.emplace(std::make_pair(draw.sceneObject, draw.sceneSubset), draw);
            geometry.hostDraws[{draw.sceneObject, draw.sceneSubset}].push_back(draw);
        } else
            (after ? geometry.suffix : geometry.prefix).push_back(draw);
    }
    geometry.hostSampled = main->sampled;
    s.input.objects.clear();
    std::set<std::string> controlItems;
    for (const auto& [id, effect] : s.compiled)
        for (const auto& variable : effect->definition->metadata.variables)
            if (variable.semantic == "CONTROLOBJECT") {
                const auto item = variable.annotations.find("ITEM");
                if (item != variable.annotations.end())
                    controlItems.insert(item->second);
            }
    const auto controls = SceneControlObjects(scene, sources, controlItems);
    const auto addObject = [&](bool accessory, std::uint8_t slot, const std::filesystem::path& path,
                               int order, unsigned subsets) {
        ScheduledObject object;
        object.id = editor::EffectObjectId(accessory, slot);
        object.path = path;
        object.order = order;
        object.accessory = accessory;
        object.flags.resize(subsets);
        object.visible = false;
        for (const auto& control : controls)
            if (control.id == object.id)
                object.visible = control.visible;
        for (unsigned n = 0; n < subsets; ++n) {
            s.input.assignments.shown.erase({0, object.id, int(n)});
            const auto draw = geometry.draws.find({object.id, int(n)});
            if (draw != geometry.draws.end()) {
                auto context = Context(draw->second, settings.width, settings.height);
                object.flags[n] = (context.standard.flags[0] ? 1 : 0) |
                                  (context.standard.flags[1] ? 2 : 0) |
                                  (context.standard.flags[3] ? 4 : 0);
            } else
                s.input.assignments.shown[{0, object.id, int(n)}] = false;
        }
        for (const auto& [key, a] : s.input.assignments.effects)
            if (key.object == object.id)
                for (const auto& effect : s.input.effects)
                    if (effect.binding.id == a.effect && effect.scene) {
                        object.sceneCarrier = true;
                        for (unsigned n = 0; n < subsets; ++n)
                            s.input.assignments.shown.erase({0, object.id, int(n)});
                    }
        s.input.objects.push_back(object);
    };
    if (!scene.Session())
        Fail("effect-scene-session-required");
    const auto& doc = scene.Session()->Document();
    for (const auto& m : doc.models)
        if (scene.Resource(m.slot))
            addObject(false, m.slot, sources.models.at(m.slot), m.previousDisplayOrder,
                      unsigned(scene.Resource(m.slot)->Document().materials.size()));
    for (const auto& a : doc.accessories)
        if (scene.Accessory(a.slot))
            addObject(true, a.slot, sources.accessories.at(a.slot), a.order,
                      unsigned(scene.Accessory(a.slot)->Subsets().size()));
    ModelFrameInputs cameraInputs;
    ModelPipeline camera;
    if (!SceneViewInputs(scene, settings, cameraInputs, reason) ||
        !BuildModelInputs(cameraInputs, {}, {}, camera))
        Fail("effect-view-context");
    ModelReplayDraw cameraDraw;
    cameraDraw.pipeline = camera;
    s.input.context = [&](auto object, int subset, auto offscreenOwner) {
        auto found = geometry.draws.find({object, subset});
        if (found == geometry.draws.end())
            found = geometry.draws.lower_bound({object, 0});
        auto c =
            Context(found != geometry.draws.end() && found->first.first == object ? found->second
                                                                                  : cameraDraw,
                    settings.width, settings.height);
        c.standard.beginWorld = camera.shader.world;
        c.standard.view = cpu_math::Multiply(camera.shader.world, camera.shader.view);
        for (const auto& control : controls)
            if (control.id == object)
                c.standard.world = control.world;
        c.owner = object;
        c.offscreenOwner = offscreenOwner;
        c.objects = controls;
        if (object > 0 && object < 256) {
            const auto model = scene.Resource(std::uint8_t(object - 1));
            if (model)
                c.standard.counts = {int(model->Document().vertices.size()),
                                     int(model->Document().materials.size())};
        } else if (object >= 257 && object < 512) {
            const auto accessory = scene.Accessory(std::uint8_t(object - 257));
            if (accessory)
                c.standard.counts = {int(accessory->Vertices().size()),
                                     int(accessory->Subsets().size())};
        }
        if (object > 0 && object < 256)
            c.standard.flags[4] = settings.display.Transparent(std::uint8_t(object - 1));
        return c;
    };
    s.input.hostSceneDraws = true;
    s.input.hostDepthFormat = TextureFormat::DepthStencil;
    s.input.clearColor = main->color ? main->color->clear : ClearColor{};
    if (!s.targets.Reconcile(s.input.effects, settings.width, settings.height, s.deviceRevision,
                             reason))
        Fail(reason);
    const auto tick = std::uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
    if (!ScheduledFrame::Prepare(s.input, s.targets, s.pool, s.clock, s.nextFrame, tick,
                                 scene.Simulation().cursorSeconds, !scene.Playing(), s.pending,
                                 reason)) {
        std::string diagnostics;
        for (const auto& [id, e] : s.compiled)
            for (const auto& p : e->passes)
                if (!p.diagnostic.empty())
                    diagnostics += p.diagnostic + "\n";
        Fail(reason + "\n" + diagnostics);
    }
    RenderFrame next;
    if (!AssembleRenderFrame(*s.pending, s.compiled, s.targets, geometry, next, reason))
        Fail(reason);
    next.source = base.source;
    output = std::move(next);
    s.report = "executed-plan-ready\n" + s.pending->Report();
    reason.clear();
    return true;
} catch (const std::exception& e) {
    impl_->pending.reset();
    reason = e.what();
    impl_->report = reason;
    return false;
}
}
