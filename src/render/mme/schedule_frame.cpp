#include "schedule_internal.hpp"
#include <set>
namespace mikudancestudio::mme {
using namespace detail;
namespace {
using namespace rhi;
struct Turn {
    std::uint64_t scope = 0, owner = 0;
    const ScheduleDescription* effect = nullptr;
    unsigned target = 0;
};
struct Builder {
    const ScheduleInput& input;
    TargetRegistry& registry;
    ScheduleState& state;
    std::vector<const ScheduledObject*> objects;
    std::vector<Turn> turns;
    std::set<std::uint64_t> discovered, active;
    std::string reason;
    const ScheduleDescription& Effect(std::uint64_t id) {
        for (const auto& e : input.effects)
            if (e.binding.id == id)
                return e;
        ScheduleError("missing_effect");
    }
    Assignment Resolve(const ScheduledObject& object, int subset, const Turn& turn) {
        Assignment a;
        const std::vector<std::pair<std::string, std::string>> empty;
        if (!ResolveAssignment(object, subset, turn.scope, turn.owner,
                               turn.effect ? turn.effect->targets.at(turn.target).defaults : empty,
                               input.assignments, input.effects, a, reason))
            ScheduleError(reason);
        return a;
    }
    std::uint64_t Target(const ScheduleDescription& effect, const std::string& name) {
        TargetLease lease;
        FrameTarget target;
        if (!registry.Resolve(effect.binding.id, name, lease, target, reason))
            ScheduleError(reason);
        return target.id;
    }
    SemanticContext Context(std::uint64_t object, int subset, std::uint64_t owner) {
        if (!input.context)
            ScheduleError("host_context_missing");
        auto result = input.context(object, subset, owner);
        result.owner = object;
        result.offscreenOwner = owner;
        return result;
    }
    ParameterSnapshot Snapshot(const ScheduleDescription& effect, std::uint64_t object, int subset,
                               std::uint64_t owner, std::uint64_t color) {
        ParameterSnapshot snapshot;
        auto context = Context(object, subset, owner);
        const auto& d = Description(color).description;
        context.standard.viewport = {float(d.width), float(d.height)};
        if (!state.transaction->Prepare(*effect.binding.instance, context, snapshot, reason))
            ScheduleError(reason);
        return snapshot;
    }
    int Technique(const ScheduleDescription& effect, int subset, unsigned flags) {
        auto valid = input.techniqueValidity.find(effect.binding.id);
        if (valid == input.techniqueValidity.end())
            ScheduleError("technique_capabilities_missing");
        int selected = -1;
        if (!SelectTechnique(effect, input.mode, subset, flags, valid->second, selected, reason))
            ScheduleError(reason);
        return selected;
    }
    void Add(PlannedStep step) {
        if (state.steps.size() >= 4096)
            ScheduleError("frame_work_budget");
        step.resources.name = "MME/" + std::to_string(step.scope) + "/" +
                              std::to_string(step.object) + "/" + std::to_string(step.effect) +
                              "/" + std::to_string(step.pass);
        state.dependencies.passes.push_back(step.resources);
        state.steps.push_back(std::move(step));
    }
    void Discover(const Turn& turn, unsigned depth = 0) {
        if (depth > 16 || turns.size() > 32)
            ScheduleError("offscreen_graph_budget");
        for (const auto* object : objects)
            for (unsigned subset = 0; subset < object->flags.size(); ++subset) {
                const auto a = Resolve(*object, int(subset), turn);
                if (a.kind != Assignment::Kind::Effect)
                    continue;
                const auto& e = Effect(a.effect);
                for (unsigned t = 0; t < e.targets.size(); ++t)
                    if (e.targets[t].target.offscreen && e.targets[t].publisher) {
                        const auto scope = TargetScope(e.binding.id, t);
                        if (!scope)
                            ScheduleError("scope_identity");
                        if (active.count(scope))
                            ScheduleError("offscreen_cycle");
                        if (!discovered.insert(scope).second)
                            continue;
                        Turn child{scope, object->id, &e, t};
                        turns.push_back(child);
                        active.insert(scope);
                        Discover(child, depth + 1);
                        active.erase(scope);
                    }
            }
    }
    void Samples(const ScheduleDescription& effect, const MmeLegacyPass& pass,
                 FramePass& resources) {
        const auto& p = effect.binding.instance->Definition().metadata;
        if (!MmeLegacySamplers(p, pass, true).empty())
            ScheduleError("vertex_texture_not_supported");
        for (auto slot : MmeLegacySamplers(p, pass)) {
            const auto& sampler = p.samplers.at(slot);
            auto target = std::find_if(effect.targets.begin(), effect.targets.end(),
                                       [&](const auto& t) { return t.name == sampler.texture; });
            if (target == effect.targets.end())
                continue; // Host uploads are P5.4 bindings.
            auto id = Target(effect, target->name);
            if (target->target.mipmapped && sampler.mipNone)
                ScheduleError("mipped_sampler_none");
            if (std::find(resources.sampled.begin(), resources.sampled.end(), id) ==
                resources.sampled.end())
                resources.sampled.push_back(id);
        }
    }
    const FrameTarget& Description(std::uint64_t id) {
        for (const auto& t : state.dependencies.targets)
            if (t.id == id)
                return t;
        ScheduleError("unknown_graph_target");
    }
    MmePortableEffect Layout(const ScheduleDescription& effect, unsigned technique,
                             const ParameterSnapshot& snapshot) {
        MmePortableEffect layout;
        layout.post = effect.scene;
        layout.script = effect.techniques.at(technique).script;
        for (const auto& t : effect.targets) {
            auto target = t.target;
            target.identity = Target(effect, t.name);
            layout.targets.push_back(target);
        }
        const auto& p = effect.binding.instance->Definition().metadata;
        const auto& t = p.techniques.at(technique);
        for (auto index : t.passes) {
            const auto& pass = p.passes.at(index);
            MmePortablePass v;
            v.name = pass.name;
            v.technique = t.name;
            FramePass sampled;
            Samples(effect, pass, sampled);
            for (const auto& target : effect.targets)
                if (std::find(sampled.sampled.begin(), sampled.sampled.end(),
                              Target(effect, target.name)) != sampled.sampled.end())
                    v.sampled.push_back(target.name);
            layout.passes.push_back(std::move(v));
        }
        for (const auto& [name, v] : snapshot.values)
            if (v.type != ParameterValue::Type::String) {
                MmePortableParameter value;
                value.name = name;
                value.rows = v.rows;
                value.columns = v.columns;
                value.elements = v.elements;
                value.type =
                    v.type == ParameterValue::Type::Float  ? MmePortableParameter::Type::Float
                    : v.type == ParameterValue::Type::Bool ? MmePortableParameter::Type::Bool
                                                           : MmePortableParameter::Type::Int;
                for (const auto& decl : p.variables)
                    if (decl.name == name)
                        value.shared = decl.shared;
                value.value.resize(v.words.size() * 4);
                std::memcpy(value.value.data(), v.words.data(), value.value.size());
                layout.parameters.push_back(std::move(value));
            }
        if (effect.scene) {
            if (!MmePortableSceneLayout(layout, reason))
                ScheduleError(reason);
        } else {
            // Ordinary no-script techniques retain all declared passes.
            if (!MmePortableMaterialLayout(layout, reason))
                ScheduleError(reason);
        }
        return layout;
    }
    void Writes(const ScheduleDescription& effect, const std::vector<MmePortableParameter>& values,
                const std::vector<MmePortableParameter>& written) {
        for (const auto& w : written) {
            const auto found = std::find_if(values.begin(), values.end(),
                                            [&](const auto& v) { return v.name == w.name; });
            if (found == values.end())
                ScheduleError("script_write_missing");
            ParameterValue value;
            if (!effect.binding.instance->Read(w.name, value, reason))
                ScheduleError(reason);
            if (found->value.size() != value.words.size() * 4)
                ScheduleError("script_write_shape");
            std::memcpy(value.words.data(), found->value.data(), found->value.size());
            if (!state.transaction->Write(*effect.binding.instance, w.name, value, reason))
                ScheduleError(reason);
        }
    }
    FramePass Attach(const ScheduleDescription& effect, const MmePortableStep& step,
                     std::uint64_t color, std::uint64_t depth) {
        FramePass pass;
        for (const auto& name : step.colors) {
            const auto id =
                name == "FinalColor" || name == "DrawColor" ? color : Target(effect, name);
            FrameAttachment a{id, step.kind == MmePortableStep::Kind::Clear && step.clearColor
                                      ? AttachmentLoad::Clear
                                      : AttachmentLoad::Load};
            a.clear = {step.color[0], step.color[1], step.color[2], step.color[3]};
            pass.colors.push_back(a);
        }
        if (!step.depth.empty() && (step.kind != MmePortableStep::Kind::Clear || step.clearDepth)) {
            const auto id = step.depth == "FinalDepth" ? depth : Target(effect, step.depth);
            if (id) {
                FrameAttachment a{id, step.kind == MmePortableStep::Kind::Clear && step.clearDepth
                                          ? AttachmentLoad::Clear
                                          : AttachmentLoad::Load};
                a.clearDepth = step.depthValue;
                pass.depthStencil = a;
            }
        }
        return pass;
    }
    void Host(const ScheduledObject& object, int subset, const Turn& turn, std::uint64_t color,
              std::uint64_t depth) {
        PlannedStep s;
        s.kind = PlannedStep::Kind::Host;
        s.scope = turn.scope;
        s.object = object.id;
        s.subset = subset;
        s.resources.colors = {{color, AttachmentLoad::Load}};
        if (depth)
            s.resources.depthStencil = FrameAttachment{depth, AttachmentLoad::Load};
        Add(std::move(s));
    }
    void Materials(const Turn& turn, std::uint64_t color, std::uint64_t depth) {
        for (const auto* object : objects)
            for (unsigned subset = 0; subset < object->flags.size(); ++subset) {
                const auto a = Resolve(*object, int(subset), turn);
                if (a.kind == Assignment::Kind::Hidden)
                    continue;
                if (a.kind == Assignment::Kind::Host) {
                    Host(*object, int(subset), turn, color, depth);
                    continue;
                }
                const auto& e = Effect(a.effect);
                if (e.scene)
                    continue;
                const auto selected = Technique(e, int(subset), object->flags[subset]);
                if (selected < 0) {
                    Host(*object, int(subset), turn, color, depth);
                    continue;
                }
                if (e.binding.instance->Definition()
                        .metadata.techniques.at(selected)
                        .passes.empty())
                    continue;
                auto snapshot = Snapshot(e, object->id, int(subset), turn.owner, color);
                auto layout = Layout(e, unsigned(selected), snapshot);
                for (const auto& step : layout.steps) {
                    PlannedStep s;
                    s.kind = PlannedStep::Kind::Material;
                    s.scope = turn.scope;
                    s.object = object->id;
                    s.subset = int(subset);
                    s.effect = e.binding.id;
                    s.technique = unsigned(selected);
                    s.pass =
                        e.binding.instance->Definition().metadata.techniques.at(selected).passes.at(
                            step.pass);
                    s.parameters = snapshot;
                    s.resources = Attach(e, step, color, depth);
                    if (s.resources.colors.empty())
                        ScheduleError("material_output_missing");
                    if (s.resources.colors.front().target != color)
                        s.parameters = Snapshot(e, object->id, int(subset), turn.owner,
                                                s.resources.colors.front().target);
                    if (depth && !s.resources.depthStencil)
                        s.resources.depthStencil = FrameAttachment{depth, AttachmentLoad::Load};
                    Samples(e, e.binding.instance->Definition().metadata.passes.at(s.pass),
                            s.resources);
                    Add(std::move(s));
                }
            }
    }
    void
    Scene(const Turn& turn,
          const std::vector<std::pair<const ScheduledObject*, const ScheduleDescription*>>& scenes,
          std::size_t count, std::uint64_t color, std::uint64_t depth) {
        if (!count) {
            Materials(turn, color, depth);
            return;
        }
        const auto& item = scenes[count - 1];
        const auto& e = *item.second;
        const auto object = item.first->id;
        auto snapshot = Snapshot(e, object, -1, turn.owner, color);
        const auto selected = Technique(e, 0, item.first->flags.empty() ? 0 : item.first->flags[0]);
        if (selected < 0)
            ScheduleError("scene_technique_missing");
        auto layout = Layout(e, unsigned(selected), snapshot);
        for (const auto& step : layout.steps) {
            auto attachments = Attach(e, step, color, depth);
            if (step.kind == MmePortableStep::Kind::Capture) {
                if (attachments.colors.size() != 1 ||
                    (attachments.depthStencil &&
                     Description(attachments.colors.front().target).description.width !=
                         Description(attachments.depthStencil->target).description.width))
                    ScheduleError("capture_layout");
                const auto id = attachments.colors.front().target;
                const auto& a = Description(id).description;
                const auto& b = Description(color).description;
                if (a.width != b.width || a.height != b.height)
                    ScheduleError("capture_extent_not_supported");
                Scene(turn, scenes, count - 1, id,
                      attachments.depthStencil ? attachments.depthStencil->target : 0);
                continue;
            }
            PlannedStep s;
            s.scope = turn.scope;
            s.object = object;
            s.effect = e.binding.id;
            s.technique = unsigned(selected);
            s.resources = std::move(attachments);
            if (step.kind == MmePortableStep::Kind::Clear)
                s.kind = PlannedStep::Kind::Clear;
            else {
                s.kind = PlannedStep::Kind::Buffer;
                s.pass =
                    e.binding.instance->Definition().metadata.techniques.at(selected).passes.at(
                        step.pass);
                Writes(e, step.parameters, layout.writes);
                s.parameters =
                    Snapshot(e, object, -1, turn.owner, s.resources.colors.front().target);
                Samples(e, e.binding.instance->Definition().metadata.passes.at(s.pass),
                        s.resources);
            }
            Add(std::move(s));
        }
        Writes(e, layout.writes, layout.writes);
    }
    void Scope(const Turn& turn, std::uint64_t color, std::uint64_t depth) {
        std::map<int, std::pair<const ScheduledObject*, const ScheduleDescription*>> unique;
        for (const auto* object : objects) {
            const auto a = Resolve(*object, 0, turn);
            if (a.kind != Assignment::Kind::Effect)
                continue;
            const auto& e = Effect(a.effect);
            if (!e.scene)
                continue;
            // Native scene carrier precedence is accessory-first for duplicate order.
            auto found = unique.find(object->order);
            if (found == unique.end() || (object->accessory && !found->second.first->accessory))
                unique[object->order] = {object, &e};
        }
        if (unique.size() > 8)
            ScheduleError("scene_nesting_budget");
        std::vector<std::pair<const ScheduledObject*, const ScheduleDescription*>> scenes;
        for (const auto& [order, pair] : unique)
            scenes.push_back(pair);
        Scene(turn, scenes, scenes.size(), color, depth);
    }
    void Clear(std::uint64_t scope, std::uint64_t color, std::uint64_t depth,
               std::array<float, 4> rgba = {}, float z = 1) {
        PlannedStep s;
        s.kind = PlannedStep::Kind::Clear;
        s.scope = scope;
        s.resources.colors = {{color}};
        s.resources.colors[0].clear = {rgba[0], rgba[1], rgba[2], rgba[3]};
        if (depth) {
            s.resources.depthStencil = FrameAttachment{depth};
            s.resources.depthStencil->clearDepth = z;
        }
        Add(std::move(s));
    }
    void Run() {
        if (input.mode != 0)
            ScheduleError("main_color_schedule_only");
        for (const auto& object : input.objects) {
            if (!object.id || object.flags.size() > 4096)
                ScheduleError("object_identity_or_subsets");
            objects.push_back(&object);
        }
        std::stable_sort(objects.begin(), objects.end(),
                         [](const auto* a, const auto* b) { return a->order < b->order; });
        std::set<std::uint64_t> ids;
        for (const auto* object : objects)
            if (!ids.insert(object->id).second)
                ScheduleError("duplicate_object");
        std::set<std::uint64_t> targetIds;
        for (const auto& [key, entry] : state.registry->entries)
            if (targetIds.insert(entry.target.id).second)
                state.dependencies.targets.push_back(entry.target);
        RenderTextureDesc color{state.registry->width, state.registry->height, TextureFormat::Rgba8,
                                TextureAccess::ColorAttachment | TextureAccess::Sampled |
                                    TextureAccess::CopySource};
        auto depth = color;
        depth.format = TextureFormat::Depth24Stencil8;
        depth.access = unsigned(TextureAccess::DepthStencilAttachment);
        state.dependencies.targets.push_back({1, color});
        state.dependencies.targets.push_back({2, depth});
        state.dependencies.finalColor = 1;
        Discover({});
        for (auto i = turns.rbegin(); i != turns.rend(); ++i) {
            const auto& t = i->effect->targets.at(i->target);
            const auto id = Target(*i->effect, t.name);
            auto d = Description(id).description;
            d.format = TextureFormat::Depth24Stencil8;
            d.mips = 1;
            d.access = unsigned(TextureAccess::DepthStencilAttachment);
            const auto did = (std::uint64_t(1) << 60) | i->scope;
            state.dependencies.targets.push_back({did, d});
            Clear(i->scope, id, did, t.clearColor, t.clearDepth);
            Scope(*i, id, did);
            if (t.target.mipmapped) {
                PlannedStep m;
                m.kind = PlannedStep::Kind::GenerateMips;
                m.scope = i->scope;
                m.effect = i->effect->binding.id;
                m.resources.generateMips = id;
                Add(std::move(m));
            }
        }
        Clear(0, 1, 2);
        Scope({}, 1, 2);
    }
};
void Validate(const rhi::RenderFrame& graph) {
    rhi::RenderPassState state;
    std::map<std::uint64_t, rhi::Handle> handles;
    unsigned index = 1;
    for (const auto& t : graph.targets) {
        rhi::Handle h{1, index++, 1};
        if (!handles.emplace(t.id, h).second || !state.Register(h, t.description))
            ScheduleError("invalid_graph_target");
    }
    for (const auto& p : graph.passes) {
        if (p.generateMips) {
            if (!state.GenerateMips(handles.at(p.generateMips)))
                ScheduleError(state.Reason());
            continue;
        }
        rhi::RenderPassDesc native;
        const auto convert = [&](const rhi::FrameAttachment& a) {
            rhi::PassAttachment result;
            result.texture = handles.at(a.target);
            result.load = a.load;
            result.store = a.store;
            result.clear = a.clear;
            result.clearDepth = a.clearDepth;
            return result;
        };
        for (const auto& a : p.colors)
            native.colors.push_back(convert(a));
        if (p.depthStencil)
            native.depthStencil = convert(*p.depthStencil);
        for (auto id : p.sampled)
            native.sampled.push_back(handles.at(id));
        if (!state.Begin(native) || !state.End())
            ScheduleError(state.Reason());
    }
    if (!state.CanRead(handles.at(graph.finalColor)))
        ScheduleError("uninitialized_final_color");
}
}
ScheduledFrame::ScheduledFrame() = default;
ScheduledFrame::~ScheduledFrame() = default;
bool ScheduledFrame::Prepare(const ScheduleInput& input, TargetRegistry& registry, EffectPool& pool,
                             FrameClock& clock, std::uint64_t id, std::uint32_t tick, float raw,
                             bool edit, std::unique_ptr<ScheduledFrame>& output,
                             std::string& reason) try {
    auto next = std::unique_ptr<ScheduledFrame>(new ScheduledFrame);
    next->state_ = std::make_unique<ScheduleState>();
    auto& s = *next->state_;
    s.registry = registry.state_;
    s.revision = registry.state_->revision;
    s.effects = s.registry->effects;
    for (const auto& [key, allocation] : s.registry->bindings)
        s.bindings[key] = s.registry->entries.at(allocation).lease;
    if (!s.registry->width || input.effects.size() != s.registry->effects.size())
        ScheduleError("registry_inventory");
    for (const auto& e : input.effects) {
        auto found = s.registry->effects.find(e.binding.id);
        if (found == s.registry->effects.end() || found->second != e.binding.revision)
            ScheduleError("stale_effect_inventory");
    }
    if (!EffectFrame::Begin(pool, clock, id, tick, raw, edit, s.transaction, reason))
        return false;
    Builder build{input, registry, s};
    build.Run();
    Validate(s.dependencies);
    output = std::move(next);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
const std::vector<PlannedStep>& ScheduledFrame::Steps() const {
    return state_->steps;
}
const rhi::RenderFrame& ScheduledFrame::Dependencies() const {
    return state_->dependencies;
}
bool ScheduledFrame::Ready(std::string& reason) const {
    if (state_->revision != state_->registry->revision) {
        reason = "MME_schedule/stale_registry";
        return false;
    }
    return state_->transaction->Ready(reason);
}
bool ScheduledFrame::Commit(std::string& reason) {
    return Ready(reason) && state_->transaction->Commit(reason);
}
}
