#include "mme_schedule_fixture.hpp"
using namespace mme_test;
using namespace mikudancestudio::rhi;
int main() try {
    EffectPool pool;
    auto input = ScheduleFixture(pool);
    std::string reason;
    TargetRegistry registry;
    auto publisher = input.effects[2], reader = input.effects[4];
    Check(registry.Reconcile({reader, publisher}, 200, 100, 1, reason), reason);
    TargetLease first, alias;
    FrameTarget target, other;
    Check(registry.Resolve(12, "ParentMap", first, target, reason) &&
              registry.Resolve(14, "ParentMap", alias, other, reason),
          reason);
    Check(first.id == alias.id && target.description.mips == 8,
          "reader before publisher shares complete mip chain");
    auto wrongType = reader;
    wrongType.targets[0].type = "texture2D";
    Check(!registry.Reconcile({publisher, wrongType}, 200, 100, 1, reason) &&
              registry.Validate(first, reason),
          "generic texture and texture2D pool cells do not alias");
    Check(registry.Reconcile({reader, publisher}, 200, 100, 1, reason) &&
              registry.Validate(first, reason),
          "unchanged inventory retains lease");
    auto second = publisher;
    second.binding.id = 15;
    Check(registry.Reconcile({publisher, reader, second}, 200, 100, 1, reason) &&
              registry.Resolve(12, "ParentMap", alias, other, reason),
          reason);
    Check(alias.id != first.id && registry.Validate(first, reason),
          "last publisher wins but earlier owned allocation remains live");
    Check(registry.Resolve(15, "ParentMap", first, target, reason) && first.id == alias.id,
          "publisher itself sees shared pool winner");
    second.targets[0].target.relative = true;
    second.targets[0].target.viewportRatio = {.5f, .5f};
    Check(!registry.Reconcile({publisher, reader, second}, 200, 100, 1, reason) &&
              registry.Validate(first, reason),
          "incompatible publisher atomic rejection");
    second = publisher;
    second.binding.id = 15;
    second.targets[0].target.format = MmePortableFormat::Rgba16Float;
    Check(!registry.Reconcile({publisher, reader, second}, 200, 100, 1, reason) &&
              registry.Validate(first, reason),
          "format conflicts reject atomically");
    Check(!registry.Reconcile({reader}, 200, 100, 1, reason) && registry.Validate(first, reason),
          "reader cannot fabricate allocation on publisher unload");
    Check(registry.Reconcile({publisher, reader}, 200, 100, 1, reason) &&
              !registry.Validate(first, reason),
          "unload expires removed publisher lease");
    Check(registry.Resolve(12, "ParentMap", first, target, reason), reason);
    publisher.binding.revision = 2;
    Check(registry.Reconcile({publisher, reader}, 200, 100, 1, reason) &&
              !registry.Validate(first, reason),
          "source revision rebuild invalidates old lease");
    Check(registry.Resolve(12, "ParentMap", first, target, reason), reason);
    Check(registry.Reconcile({publisher, reader}, 201, 101, 1, reason) &&
              !registry.Validate(first, reason),
          "viewport rebuild invalidates fixed and relative targets");
    Check(registry.Resolve(12, "ParentMap", first, target, reason), reason);
    Check(registry.Reconcile({publisher, reader}, 201, 101, 2, reason) &&
              !registry.Validate(first, reason),
          "device replacement expires identities");
    auto privateCopy = publisher;
    privateCopy.binding.id = 16;
    privateCopy.targets[0].shared = false;
    Check(registry.Reconcile({publisher, reader, privateCopy}, 201, 101, 2, reason), reason);
    Check(registry.Resolve(12, "ParentMap", first, target, reason) &&
              registry.Resolve(16, "ParentMap", alias, other, reason) && first.id != alias.id,
          "private same-name target never aliases");
    Check(!registry.Reconcile({publisher, publisher}, 201, 101, 2, reason),
          "duplicate effect identity rejected");
    Check(registry.Reconcile({}, 201, 101, 2, reason) && !registry.Validate(first, reason),
          "complete unload releases all allocations");
    Check(registry.Reconcile({publisher, reader}, 201, 101, 2, reason) &&
              registry.Resolve(12, "ParentMap", alias, other, reason) && alias.id != first.id,
          "reload cannot resurrect zombie lease");
    std::cout
        << "Shared target publishers, conflicts, aliases, private scopes and lifetime invalidation passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
