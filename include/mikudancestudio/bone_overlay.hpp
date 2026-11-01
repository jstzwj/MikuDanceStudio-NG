#pragma once
#include "cpu_bone_types.hpp"
#include <cstdint>

namespace mikudancestudio::cpu_bone {
// Storage-independent native overlay rules. Hosts retain projection, vertex
// storage and architecture-specific coordinate arithmetic. An enabled IK link
// can deliberately emit more than one V for the same bone.
template <class BoneAt, class RelationAt, class ActiveAt, class SecondaryAt, class EmitV,
          class EmitLine>
std::uint32_t OverlayLines(int boneCount, BoneAt boneAt, int relationshipCount,
                           RelationAt relationAt, ActiveAt active, SecondaryAt secondary,
                           int selected, int cursorKind, int physMode, bool pmx, float scale,
                           EmitV emitV, EmitLine emitLine) {
    std::uint32_t primitives = 0;
    const auto filtered = [&](const auto* bone) -> bool {
        return (physMode == 1 && bone->hasRigidBody) ||
               (physMode == 2 && bone->hasRigidBody && bone->physicsDisabled == 0);
    };
    const auto endpoint = [&](const auto* bone, int* x, int* y) -> bool {
        const std::uint16_t flags = bone->flags;
        if (pmx && (flags & kBoneFlagTailIsBone) == 0) {
            *x = bone->tailScreenX;
            *y = bone->tailScreenY;
            return true;
        }
        const int linked = bone->tailBone;
        if (linked <= 0 || linked >= boneCount)
            return false;
        // The tail bone's projected screen coordinates (original x64 0x7FF7CB4E56xx reads
        // [linked]+460/+464 = screenX/screenY); typed member access
        // selects the correct offsets on both architectures. The previous hardcoded x86 byte offsets 452/456
        // read ikWorkingQuat floating-point bit patterns on x64, scattering the bone lines.
        *x = boneAt(linked).screenX;
        *y = boneAt(linked).screenY;
        return true;
    };
    const auto emitBone = [&](const auto* bone, std::uint32_t color) -> bool {
        int endX = 393939;
        int endY = 393939;
        if (bone->screenX == 393939 || bone->screenY == 393939 || !endpoint(bone, &endX, &endY) ||
            endX == 393939 || endY == 393939)
            return false;
        return emitV(bone->screenX, bone->screenY, endX, endY, scale, color);
    };

    if (cursorKind < 2) {
        for (int index = 0; index < boneCount; ++index) {
            auto* bone = &boneAt(index);
            const std::uint16_t flags = bone->flags;
            const BoneType type = bone->type;
            if ((flags & kBoneFlagVisible) == 0 || type >= BoneType::FixedAxis || filtered(bone) ||
                (bone->tailBone <= 0 && !(pmx && (flags & kBoneFlagTailIsBone) == 0)))
                continue;

            bool inActiveRelationship = false;
            for (int relation = 0; relation < relationshipCount; ++relation) {
                const auto& record = relationAt(relation);
                if (record.enabled == 0)
                    continue;
                const std::uint8_t memberCount = record.linkCount;
                const std::uint16_t* members = record.links;
                for (int member = 0; members != nullptr && member < memberCount; ++member) {
                    if (members[member] != index)
                        continue;
                    inActiveRelationship = true;
                    const std::uint32_t color =
                        active(index) ? (index == selected ? 0xFFFF0000u : 0xFFC80000u)
                                      : (secondary(index) ? 0xFF64FF64u : 0xFFFF9B00u);
                    if (emitBone(bone, color))
                        primitives += 2;
                }
            }
            if (!inActiveRelationship) {
                const std::uint32_t color = active(index)
                                                ? (index == selected ? 0xFFFF0000u : 0xFFC80000u)
                                                : (secondary(index) ? 0xFF64FF64u : 0xFF6464E6u);
                if (emitBone(bone, color))
                    primitives += 2;
            }
        }

        // 0x49A9F9..0x49AB2B: one black centre line per
        // active relationship record, between +0 and +4.
        for (int relation = 0; relation < relationshipCount; ++relation) {
            const auto& record = relationAt(relation);
            if (record.enabled == 0)
                continue;
            const int first = record.boneIndex;
            const int second = record.targetBone;
            if (first < 0 || first >= boneCount || second < 0 || second >= boneCount)
                continue;
            auto* a = &boneAt(first);
            auto* b = &boneAt(second);
            if ((a->flags & kBoneFlagVisible) == 0 || (b->flags & kBoneFlagVisible) == 0 ||
                a->screenX == 393939 || a->screenY == 393939 || b->screenX == 393939 ||
                b->screenY == 393939)
                continue;
            emitLine(a->screenX, a->screenY, b->screenX, b->screenY, 0xFF000000u);
            ++primitives;
        }
    }

    // 0x49AB4A..0x49AE33: the selected bone gets a second V
    // in every cursor mode except 2.
    if (cursorKind != 2 && selected >= 0 && selected < boneCount) {
        auto* bone = &boneAt(selected);
        const std::uint16_t flags = bone->flags;
        const int linked = bone->tailBone;
        if ((flags & kBoneFlagVisible) != 0 && bone->type < BoneType::FixedAxis &&
            !filtered(bone) && linked > 0 && linked < boneCount && bone->screenX != 393939 &&
            bone->screenY != 393939 && boneAt(linked).screenX != 393939 &&
            boneAt(linked).screenY != 393939 &&
            emitV(bone->screenX, bone->screenY, boneAt(linked).screenX, boneAt(linked).screenY,
                  scale, 0xFFFF3232u)) {
            primitives += 2;
        }
    }
    return primitives;
}
template <class BoneAt, class ActiveAt, class SecondaryAt, class Emit>
void OverlayMarkers(int boneCount, BoneAt boneAt, ActiveAt active, SecondaryAt secondary,
                    int selected, int cursorKind, int physMode, Emit emitMarker) {
    for (int index = 0; index < boneCount; ++index) {
        auto* bone = &boneAt(index);
        const std::uint16_t flags = bone->flags;
        const BoneType type = bone->type;
        if ((flags & kBoneFlagVisible) == 0)
            continue;

        float v0 = 0.0f;
        float v1 = 0.0f;
        const bool isActive = active(index);
        const bool isSecondary = secondary(index);
        bool emit = false;
        if (cursorKind > 2) {
            if (index != selected && isActive && (type == BoneType::Move || type == BoneType::Ik)) {
                v0 = type == BoneType::Move ? 0.034456f : 0.104768f;
                v1 = type == BoneType::Move ? 0.069058999f : 0.13937099f;
                emit = true;
            }
        } else if (cursorKind < 2 &&
                   !((physMode == 1 && bone->hasRigidBody) ||
                     (physMode == 2 && bone->hasRigidBody && bone->physicsDisabled == 0))) {
            const int state = isActive ? 0 : (isSecondary ? 1 : 2);
            switch (type) {
            case BoneType::RotateMove:
            case BoneType::RotateGrant:
            case BoneType::Effector: {
                const float starts[] = {0.0f, 0.17508f, 0.52664f};
                const float ends[] = {0.033902999f, 0.209683f, 0.561243f};
                v0 = starts[state];
                v1 = ends[state];
                emit = true;
                break;
            }
            case BoneType::Move: {
                const float starts[] = {0.034456f, 0.210236f, 0.56179601f};
                const float ends[] = {0.069058999f, 0.244839f, 0.59639901f};
                v0 = starts[state];
                v1 = ends[state];
                emit = true;
                break;
            }
            case BoneType::Ik: {
                const float starts[] = {0.104768f, 0.28054801f, 0.63210797f};
                const float ends[] = {0.13937099f, 0.31515101f, 0.66671097f};
                v0 = starts[state];
                v1 = ends[state];
                emit = true;
                break;
            }
            case BoneType::UnderIk: {
                const bool ik = (flags & kBoneFlagFixedAxis) != 0;
                const float startsIk[] = {0.77328497f, 0.73812902f, 0.70297301f};
                const float endsIk[] = {0.80788797f, 0.77273202f, 0.73757601f};
                const float starts[] = {0.069611996f, 0.24539199f, 0.59695202f};
                const float ends[] = {0.104215f, 0.27999499f, 0.63155502f};
                v0 = ik ? startsIk[state] : starts[state];
                v1 = ik ? endsIk[state] : ends[state];
                emit = true;
                break;
            }
            case BoneType::FixedAxis: {
                const float starts[] = {0.139924f, 0.31570399f, 0.66726398f};
                const float ends[] = {0.174527f, 0.35030699f, 0.70186698f};
                v0 = starts[state];
                v1 = ends[state];
                emit = true;
                break;
            }
            default:
                break;
            }
        }
        if (emit) {
            emitMarker(index, v0, v1);
        }
    }

    // 0x49C1ED..0x49C80C: every mode except 2 appends one final
    // selected marker from the dedicated atlas family.
    if (cursorKind != 2 && selected >= 0 && selected < boneCount) {
        auto* bone = &boneAt(selected);
        const std::uint16_t flags = bone->flags;
        const bool filteredInMode2 =
            physMode == 2 && bone->hasRigidBody && bone->physicsDisabled == 0;
        float v0 = 0.0f;
        float v1 = 0.0f;
        bool emit = false;
        if ((flags & kBoneFlagVisible) != 0 && !filteredInMode2) {
            switch (bone->type) {
            case BoneType::RotateMove:
                v0 = 0.35086f;
                v1 = 0.385463f;
                emit = true;
                break;
            case BoneType::Move:
                v0 = 0.38601601f;
                v1 = 0.42061901f;
                emit = true;
                break;
            case BoneType::Ik:
            case BoneType::Unused3:
                v0 = 0.456328f;
                v1 = 0.490931f;
                emit = true;
                break;
            case BoneType::UnderIk:
                if ((flags & kBoneFlagFixedAxis) != 0) {
                    v0 = 0.77328497f;
                    v1 = 0.80788797f;
                } else {
                    v0 = 0.42117199f;
                    v1 = 0.45877501f;
                }
                emit = true;
                break;
            case BoneType::RotateGrant:
            case BoneType::Effector:
                v0 = 0.0f;
                v1 = 0.033902999f;
                emit = true;
                break;
            case BoneType::FixedAxis:
                v0 = 0.49148399f;
                v1 = 0.52608699f;
                emit = true;
                break;
            default:
                break;
            }
        }
        if (emit) {
            emitMarker(selected, v0, v1);
        }
    }
}
} // namespace mikudancestudio::cpu_bone
