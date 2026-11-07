#include "schedule_internal.hpp"
namespace mikudancestudio::mme {
using namespace detail;
TargetRegistry::TargetRegistry() : state_(std::make_shared<TargetRegistryState>()) {
}
bool TargetRegistry::Reconcile(const std::vector<ScheduleDescription>& effects, unsigned width,
                               unsigned height, std::uint64_t device, std::string& reason) try {
    if (!device || effects.size() > 256 || !width || !height || width > 4096 || height > 4096)
        ScheduleError("registry_extent_or_inventory");
    auto next = *state_;
    next.entries.clear();
    next.bindings.clear();
    next.effects.clear();
    const bool rebuild =
        state_->width != width || state_->height != height || state_->device != device;
    if (rebuild)
        ++next.epoch;
    next.width = width;
    next.height = height;
    next.device = device;
    std::map<std::string, std::string> publishers;
    const auto sharedKey = [](const auto& d) {
        return d.name + "/" + Upper(d.semantic) + "/" + d.type;
    };
    for (const auto& e : effects) {
        if (!e.binding.id || !e.binding.revision || !e.binding.instance ||
            !next.effects.emplace(e.binding.id, e.binding.revision).second)
            ScheduleError("duplicate_effect_identity");
        for (const auto& d : e.targets)
            if (d.publisher) {
                const auto key = std::to_string(e.binding.id) + "/" +
                                 std::to_string(e.binding.revision) + "/" + d.name;
                rhi::RenderTextureDesc desc;
                std::string why;
                if (!MmePortableExtent(d.target, width, height, desc.width, desc.height, why))
                    ScheduleError(why);
                desc.format = d.target.depth ? rhi::TextureFormat::Depth24Stencil8
                              : d.target.format == MmePortableFormat::Rgba16Float
                                  ? rhi::TextureFormat::Rgba16Float
                                  : rhi::TextureFormat::Rgba8;
                desc.access = d.target.depth ? unsigned(rhi::TextureAccess::DepthStencilAttachment)
                                             : rhi::TextureAccess::ColorAttachment |
                                                   rhi::TextureAccess::Sampled |
                                                   rhi::TextureAccess::CopySource;
                if (d.target.mipmapped)
                    for (unsigned size = std::max(desc.width, desc.height); size > 1; size >>= 1)
                        ++desc.mips;
                if (!rhi::ValidRenderTextureDesc(desc))
                    ScheduleError("invalid_target_description");
                TargetRegistryState::Entry entry;
                entry.key = key;
                entry.target.description = desc;
                const auto old = state_->entries.find(key);
                if (!rebuild && old != state_->entries.end() &&
                    rhi::EqualRenderTextureDesc(old->second.target.description, desc))
                    entry = old->second;
                else {
                    entry.lease = {next.nextId++, e.binding.revision, next.epoch};
                    entry.target.id = entry.lease.id;
                }
                if (!next.entries.emplace(key, entry).second)
                    ScheduleError("duplicate_target_declaration");
                if (d.shared) {
                    const auto shared = sharedKey(d);
                    const auto found = publishers.find(shared);
                    if (found != publishers.end() &&
                        !rhi::EqualRenderTextureDesc(
                            next.entries.at(found->second).target.description, desc))
                        ScheduleError("shared_target_conflict/" + d.name);
                    // Native SetTexture publishes in effect-loading order. Last live
                    // publisher wins, while the replaced allocation retains identity.
                    publishers[shared] = key;
                }
            }
    }
    for (const auto& e : effects)
        for (const auto& d : e.targets) {
            const auto key =
                d.shared ? (publishers.count(sharedKey(d)) ? publishers.at(sharedKey(d)) : "")
                         : std::to_string(e.binding.id) + "/" + std::to_string(e.binding.revision) +
                               "/" + d.name;
            if (key.empty() || !next.entries.count(key))
                ScheduleError("shared_target_unbound/" + d.name);
            if (!next.bindings.emplace(std::make_pair(e.binding.id, d.name), key).second)
                ScheduleError("duplicate_target_binding");
        }
    bool changed = rebuild || next.bindings != state_->bindings ||
                   next.effects != state_->effects || next.entries.size() != state_->entries.size();
    for (const auto& [key, entry] : next.entries) {
        auto old = state_->entries.find(key);
        if (old == state_->entries.end() || old->second.lease.id != entry.lease.id)
            changed = true;
    }
    if (changed)
        ++next.revision;
    *state_ = std::move(next);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    reason = e.what();
    return false;
}
bool TargetRegistry::Resolve(std::uint64_t effect, const std::string& name, TargetLease& lease,
                             rhi::FrameTarget& target, std::string& reason) const {
    auto binding = state_->bindings.find({effect, name});
    if (binding == state_->bindings.end()) {
        reason = "MME_schedule/unbound_target/" + name;
        return false;
    }
    const auto& entry = state_->entries.at(binding->second);
    lease = entry.lease;
    target = entry.target;
    reason.clear();
    return true;
}
bool TargetRegistry::Validate(TargetLease lease, std::string& reason) const {
    for (const auto& [key, e] : state_->entries)
        if (e.lease.id == lease.id && e.lease.generation == lease.generation &&
            e.lease.epoch == lease.epoch) {
            reason.clear();
            return true;
        }
    reason = "MME_schedule/stale_target";
    return false;
}
}
