#pragma once
#include "model_reader.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>

namespace mikudancestudio::editor {
// Clipboard is application state, independent from PMM/model lifetimes.
struct ModelPhysicsClipboard {
    format::RigidData body;
    format::JointData joint;
    ModelPhysicsClipboard() { body.boneIndex = joint.rigidA = joint.rigidB = 0; }
};
// Stable scratch indices and separate combo order mirror MMD's 100000-entry
// staging tables. The collision mask is POSITIVE while editing, inverted only
// at the import/export boundaries. No Bullet or UI objects are retained here.
class ModelPhysicsEdit {
public:
    struct DragEcho {
        int control = 0;
        float value = 0;
        const char* format = "%3.2f";
    };
    // Frame-driver modes 4..6 and 10..12 edit the selected scratch record,
    // including a deleted slot still selected by the native dialog. Keep the
    // x64 float multiply/subtract; modifiers do not scale precision.
    DragEcho Drag(bool jointPage, int index, unsigned mode, int dy, bool shift) {
        if (index < 0 ||
            (jointPage ? std::size_t(index) >= joints.size() : std::size_t(index) >= bodies.size()))
            return {};
        if (mode >= 4 && mode <= 6) {
            const auto axis = mode - 4;
            float& value = jointPage ? joints[index].rotation[axis] : bodies[index].rotation[axis];
            value = value - float(dy) * .01f;
            return {int((jointPage ? 754 : 715) + axis), value / 3.141592f * 180.f};
        }
        if (mode >= 10 && mode <= 12) {
            const auto axis = mode - 10;
            float& value = jointPage ? joints[index].position[axis]
                           : shift   ? bodies[index].size[axis]
                                     : bodies[index].position[axis];
            value = value - float(dy) * .05f;
            if (!jointPage && shift && value < .1f)
                value = .1f;
            return {int((jointPage ? 744
                         : shift   ? 709
                                   : 712) +
                        axis),
                    value, jointPage ? "%5.4f" : "%3.2f"};
        }
        return {};
    }
    static constexpr unsigned Capacity = 100000;
    std::vector<format::RigidData> bodies;
    std::vector<format::JointData> joints;
    std::vector<int> bodyOrder, jointOrder;
    explicit ModelPhysicsEdit(const format::PmdDocument& model = {})
        : bodies(model.rigids), joints(model.joints), bodyOrder(bodies.size()),
          jointOrder(joints.size()), bodyOrdinals_(bodies.size()), jointOrdinals_(joints.size()) {
        std::iota(bodyOrder.begin(), bodyOrder.end(), 0);
        std::iota(jointOrder.begin(), jointOrder.end(), 0);
        bodyOrdinals_ = bodyOrder;
        jointOrdinals_ = jointOrder;
        for (auto& body : bodies)
            body.noCollapse = std::uint16_t(~body.noCollapse);
    }
    int BodyOrdinal(int index) const {
        return index >= 0 && std::size_t(index) < bodyOrdinals_.size() ? bodyOrdinals_[index] : -1;
    }
    int JointOrdinal(int index) const {
        return index >= 0 && std::size_t(index) < jointOrdinals_.size() ? jointOrdinals_[index]
                                                                        : -1;
    }
    bool Body(int index) const { return BodyOrdinal(index) >= 0; }
    bool Joint(int index) const { return JointOrdinal(index) >= 0; }
    int BodyAtRow(int row) const {
        int found = -1;
        for (unsigned i = 0; i < bodyOrdinals_.size(); ++i)
            if (bodyOrdinals_[i] == row)
                found = int(i);
        return found;
    }
    int JointAtRow(int row) const {
        int found = -1;
        for (unsigned i = 0; i < jointOrdinals_.size(); ++i)
            if (jointOrdinals_[i] == row)
                found = int(i);
        return found;
    }
    int AddBody() {
        unsigned index = 0;
        while (index < bodies.size() && Body(int(index)))
            ++index;
        if (index == Capacity)
            return -1;
        if (index == bodies.size()) {
            bodies.emplace_back();
            bodyOrdinals_.push_back(-1);
        }
        const auto mask = bodies[index].noCollapse; // Native reused slots retain this field.
        auto& body = bodies[index];
        body = {};
        body.boneIndex = 0;
        body.noCollapse = mask;
        std::snprintf(body.pmdName, sizeof body.pmdName, "BODY_%u", index);
        body.mass = 1;
        body.linearDamping = body.angularDamping = body.friction = .5f;
        std::fill_n(body.size, 3, 2.f);
        bodyOrdinals_[index] = int(bodyOrder.size());
        bodyOrder.push_back(int(index));
        return int(index);
    }
    int AddJoint() {
        if (bodyOrder.empty())
            return -1;
        unsigned index = 0;
        while (index < joints.size() && Joint(int(index)))
            ++index;
        if (index == Capacity)
            return -1;
        if (index == joints.size()) {
            joints.emplace_back();
            jointOrdinals_.push_back(-1);
        }
        auto& joint = joints[index];
        joint = {};
        // Original initializes scratch slot zero even when that slot is deleted.
        // Export rejects such an unresolved link unless the user selects bodies.
        joint.rigidA = joint.rigidB = 0;
        std::snprintf(joint.pmdName, sizeof joint.pmdName, "JOINT_%u", index);
        jointOrdinals_[index] = int(jointOrder.size());
        jointOrder.push_back(int(index));
        return int(index);
    }
    bool DeleteBody(int index, int row = -1) {
        if (index < 0 || std::size_t(index) >= bodies.size())
            return false;
        if (row < 0)
            row = BodyOrdinal(index);
        if (row < 0 || std::size_t(row) >= bodyOrder.size())
            return false;
        // The selected scratch slot can already be dead after native deletion.
        // The visible row and scratch liveness are deliberately independent.
        bodyOrder.erase(bodyOrder.begin() + row);
        bodyOrdinals_[index] = -1;
        for (auto& ordinal : bodyOrdinals_)
            if (ordinal > row)
                --ordinal;
        for (unsigned j = 0; j < joints.size(); ++j)
            if (Joint(int(j)) && (joints[j].rigidA == index || joints[j].rigidB == index))
                DeleteJoint(int(j));
        return true;
    }
    bool DeleteJoint(int index, int row = -1) {
        if (index < 0 || std::size_t(index) >= joints.size())
            return false;
        if (row < 0)
            row = JointOrdinal(index);
        if (row < 0 || std::size_t(row) >= jointOrder.size())
            return false;
        jointOrder.erase(jointOrder.begin() + row);
        jointOrdinals_[index] = -1;
        for (auto& ordinal : jointOrdinals_)
            if (ordinal > row)
                --ordinal;
        return true;
    }
    bool PasteBody(int index, const ModelPhysicsClipboard& clipboard) {
        if (index < 0 || std::size_t(index) >= bodies.size())
            return false;
        auto& to = bodies[index];
        const auto& from = clipboard.body;
        to.boneIndex = from.boneIndex;
        to.mode = from.mode;
        to.shape = from.shape;
        to.mass = from.mass;
        to.linearDamping = from.linearDamping;
        to.angularDamping = from.angularDamping;
        std::copy_n(from.position, 3, to.position);
        std::copy_n(from.rotation, 3, to.rotation);
        std::copy_n(from.size, 3, to.size);
        // Name, group, collision, restitution and friction belong to destination.
        return true;
    }
    bool PasteJoint(int index, const ModelPhysicsClipboard& clipboard) {
        if (index < 0 || std::size_t(index) >= joints.size())
            return false;
        char name[20];
        std::memcpy(name, joints[index].pmdName, sizeof name);
        joints[index] = clipboard.joint;
        std::memcpy(joints[index].pmdName, name, sizeof name);
        return true;
    }
    bool Export(std::vector<format::RigidData>& rigid, std::vector<format::JointData>& joint,
                std::string& reason) const {
        std::vector<format::RigidData> r;
        std::vector<format::JointData> j;
        std::vector<int> compact(bodies.size(), -1);
        // The native writer compacts by SCRATCH index, not current combo order.
        for (unsigned i = 0; i < bodies.size(); ++i)
            if (Body(int(i))) {
                compact[i] = int(r.size());
                r.push_back(bodies[i]);
                r.back().noCollapse = std::uint16_t(~r.back().noCollapse);
            }
        for (unsigned i = 0; i < joints.size(); ++i)
            if (Joint(int(i))) {
                const auto& source = joints[i];
                if (source.rigidA < 0 || source.rigidB < 0 ||
                    std::size_t(source.rigidA) >= compact.size() ||
                    std::size_t(source.rigidB) >= compact.size() || compact[source.rigidA] < 0 ||
                    compact[source.rigidB] < 0) {
                    reason = "physics-joint-body-missing";
                    return false;
                }
                j.push_back(source);
                j.back().rigidA = compact[source.rigidA];
                j.back().rigidB = compact[source.rigidB];
            }
        rigid = std::move(r);
        joint = std::move(j);
        reason.clear();
        return true;
    }

private:
    std::vector<int> bodyOrdinals_, jointOrdinals_;
};
inline bool SetPhysicsBodyValue(format::RigidData& body, int control, double value, bool enter) {
    if (control >= 709 && control <= 711)
        body.size[control - 709] = float(std::max(.1, value));
    else if (control >= 712 && control <= 714)
        body.position[control - 712] = value;
    else if (control >= 715 && control <= 717)
        body.rotation[control - 715] = float(value / 180.0 * double(3.141592f));
    else if (control == 718)
        body.mass = enter ? float(std::max(.1, value)) : float(value);
    else if (control == 720)
        body.linearDamping = value;
    else if (control == 721)
        body.angularDamping = value;
    else if (control == 722)
        body.restitution = value;
    else if (control == 723)
        body.friction = value;
    else
        return false;
    return true;
}
inline bool SetPhysicsJointValue(format::JointData& joint, int control, double value, bool& clamped,
                                 bool enter = false) {
    clamped = false;
    if (control >= 744 && control <= 746)
        joint.position[control - 744] = value;
    else if (control >= 748 && control <= 753)
        joint.springs[control - 748] = value;
    else if (control >= 754 && control <= 756)
        joint.rotation[control - 754] = float(value / 180.0 * double(3.141592f));
    else {
        constexpr int limits[]{757, 759, 761, 747, 758, 760, 763, 765, 767, 762, 764, 766};
        const auto found = std::find(std::begin(limits), std::end(limits), control);
        if (found == std::end(limits))
            return false;
        const auto index = found - std::begin(limits);
        if (!enter && (index == 7 || index == 10)) {
            value = float(value);
            clamped = value >= 90 || value <= -90;
            if (clamped)
                value = value > 0 ? 80.f : -80.f;
        }
        joint.limits[index] = index < 6 ? float(value)
                              : !enter && (index == 7 || index == 10)
                                  ? float(value) / 180.f * 3.141592f
                                  : float(value / 180.0 * double(3.141592f));
    }
    return true;
}
inline std::uint16_t PhysicsCollisionMask(std::string text) {
    text.resize(std::min<std::size_t>(99, text.size()));
    std::uint16_t mask = 0;
    std::size_t first = 0;
    do {
        const auto end = text.find(' ', first);
        const auto token = text.substr(first, end == std::string::npos ? end : end - first);
        std::size_t i = 0;
        while (i < token.size() && (token[i] == ' ' || (token[i] >= '\t' && token[i] <= '\r')))
            ++i;
        const bool negative = i < token.size() && token[i] == '-';
        if (i < token.size() && (token[i] == '+' || token[i] == '-'))
            ++i;
        std::uint32_t value = 0;
        while (i < token.size() && token[i] >= '0' && token[i] <= '9')
            value = value * 10 + unsigned(token[i++] - '0');
        if (negative)
            value = 0 - value;
        if (value >= 1 && value <= 16)
            mask |= std::uint16_t(1u << (value - 1));
        if (end == std::string::npos)
            break;
        first = end + 1;
    } while (true);
    return mask;
}
}
