#include "editor_window.hpp"
#include "combo_items.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>
#include <QSlider>
#include <cmath>

namespace mikudancestudio::qt {
using namespace panel;
namespace {
QString Label(const std::vector<char>& bytes) {
    std::string value;
    text::DecodeCp932({bytes.data(), bytes.size()}, value);
    return QString::fromUtf8(value.c_str());
}
constexpr double kPi = 3.141592025756836;
}
void EditorWindow::RefreshScopes() {
    using namespace editor;
    auto* combo = static_cast<QComboBox*>(Control(kRegisterScopeCombo));
    std::vector<std::pair<QString, QVariant>> items;
    scopes_.clear();
    const auto add = [&](const QString& label, KeyScope scope) {
        items.emplace_back(label, QVariant{});
        scopes_.push_back(scope);
    };
    const auto kind = [&](const char* id, KeyScopeKind scope) {
        add(language_.Text(id), {scope, {}});
    };
    const auto model = session_.Selected().model;
    if (model == 255) {
        const std::pair<Track, const char*> names[]{{Track::Camera, "camera"},
                                                    {Track::Light, "light"},
                                                    {Track::Shadow, "shadow"},
                                                    {Track::Gravity, "gravity"}};
        for (const auto& name : names)
            add(language_.Text(name.second), {KeyScopeKind::Track, {name.first, 255, 0, 0}});
        std::vector<const format::PmmSceneAccessory*> ordered;
        for (const auto& a : session_.Document().accessories)
            ordered.push_back(&a);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](auto* a, auto* b) { return a->order < b->order; });
        for (const auto* a : ordered)
            add(Label({a->name.begin(), std::find(a->name.begin(), a->name.end(), 0)}),
                {KeyScopeKind::Track, {Track::Accessory, a->slot, 0, 0}});
    } else if (const auto resource = session_.Runtime().Resource(model)) {
        if (scopeResources_[model].lock() != resource) {
            scopeSelections_[model] = 3;
            scopeResources_[model] = resource;
        }
        const auto entry = [&](bool morph, unsigned index) {
            const auto& names = morph ? resource->Document().morphs[index].names
                                      : resource->Document().bones[index].names;
            const auto& bytes = names[language_.Code() == "en" && !names[1].empty() ? 1 : 0];
            std::string text;
            text::DecodeUtf16Le(bytes.data(), bytes.size(), text);
            add(QString::fromUtf8(text.c_str()),
                {KeyScopeKind::Track, {morph ? Track::Morph : Track::Bone, model, index, 0}});
        };
        kind("scope.all", KeyScopeKind::All);
        if (!resource->BoneNames().empty())
            entry(false, resource->DisplayRootBone());
        add(language_.Text("display"), {KeyScopeKind::Track, {Track::Display, model, 0, 0}});
        kind("scope.selected_bones", KeyScopeKind::SelectedBones);
        kind("scope.selected_morphs", KeyScopeKind::SelectedMorphs);
        kind("scope.morphs", KeyScopeKind::Morphs);
        for (const auto& group : resource->DisplayGroups())
            if (group.role == 2)
                for (const auto& child : group.entries)
                    entry(true, child.index);
        kind("scope.bones", KeyScopeKind::Bones);
        for (const auto& group : resource->DisplayGroups())
            if (group.role == 3)
                for (const auto& child : group.entries)
                    entry(false, child.index);
    }
    SyncComboItems(combo, [&](auto append) {
        for (const auto& item : items)
            append(item.first, item.second);
    });
    const auto [selection, inserted] = scopeSelections_.emplace(model, model == 255 ? 0 : 3);
    combo->setCurrentIndex(std::clamp(selection->second, 0, std::max(0, combo->count() - 1)));
}
void EditorWindow::RefreshPanels() {
    const auto& d = session_.Document();
    const auto edit = [&](int id, double value) {
        auto* e = static_cast<QLineEdit*>(Control(id));
        if (!e->hasFocus())
            e->setText(QString::number(value, 'g', 7));
    };
    const auto check = [&](int id, bool value) {
        static_cast<QAbstractButton*>(Control(id))->setChecked(value);
    };
    const auto slider = [&](int id, int minimum, int maximum, int value) {
        auto* s = static_cast<QSlider*>(Control(id));
        s->setRange(minimum, maximum);
        s->setValue(value);
    };
    for (int i = 0; i < 3; ++i) {
        edit(kLightColorEditR + i, int(double(d.lightState[i]) * 256));
        slider(kLightColorSliderR + i, 0, 255, int(double(d.lightState[i]) * 256));
        edit(kLightDirEditX + i, d.lightState[i + 3]);
        slider(kLightDirSliderX + i, -100, 100, int(double(d.lightState[i + 3]) * 100));
    }
    const int shadowDistance = int(10000. - double(d.physicsInterval) * 100000. + .5);
    edit(kSelfShadowRangeEdit, shadowDistance);
    slider(kSelfShadowRangeSlider, 0, 9999, shadowDistance);
    for (int i = 0; i < 3; ++i)
        check(kEditOffCheckbox + i, d.selfShadowMode == i);
    check(kCoordAxisCheckbox, d.view.groundGrid);
    check(kInfoCheckbox, d.view.fpsOverlay);
    check(kFollowCameraCheckbox, d.appearance.followCamera);
    check(kCameraRefModelCheckbox, d.timeline.cameraReferenceMode == 1);
    check(kCameraRefBoneCheckbox, d.timeline.cameraReferenceMode == 2);
    slider(kFrameVolumeSlider, 0, 100, 100 - frameNormalization_);
    const bool model = session_.Selected().model != 255;
    Control(kMainComboModel)->setEnabled(!session_.Playing());
    Control(kModelEditToggle)->setEnabled(!session_.Playing());
    static_cast<QAbstractButton*>(Control(kModelEditToggle))
        ->setText(language_.Text(model ? "mode.to_camera" : "mode.to_model"));
    for (int id = kDeleteModelButton; id <= kIkOffRadio; ++id)
        Control(id)->setEnabled(model);
    const int editModes[]{0, 1, 4, 3};
    for (int i = 0; i < 4; ++i)
        check(kBoneSelectRadio + i, d.timeline.editMode == editModes[i]);
    for (const auto& m : d.models)
        if (m.slot == session_.Selected().model) {
            auto* c = static_cast<QComboBox*>(Control(kIkChainCombo));
            const auto old = c->currentData();
            const auto resource = session_.Runtime().Resource(m.slot);
            SyncComboItems(c, [&](auto add) {
                for (std::size_t i = 0; i < resource->IkBoneIndices().size(); ++i)
                    add(Label(m.boneNames.at(resource->IkBoneIndices()[i])), unsigned(i));
            });
            c->setCurrentIndex(std::max(0, c->findData(old)));
            const auto ik = c->currentData().toUInt();
            check(kIkOnRadio, !m.ikPose.empty() && m.ikPose.at(ik));
            check(kIkOffRadio, !m.ikPose.empty() && !m.ikPose.at(ik));
            Control(kIkOnRadio)->setEnabled(!m.ikPose.empty());
            Control(kIkOffRadio)->setEnabled(!m.ikPose.empty());
        }
    const auto parentModels = [&](int id, int slot, const char* none) {
        auto* c = static_cast<QComboBox*>(Control(id));
        SyncComboItems(c, [&](auto add) {
            add(language_.Text(none), -1);
            for (const auto& m : d.models)
                add(Label(m.name), int(m.slot));
        });
        c->setCurrentIndex(c->findData(slot));
    };
    const auto parentBones = [&](int id, int slot, int bone) {
        auto* c = static_cast<QComboBox*>(Control(id));
        SyncComboItems(c, [&](auto add) {
            for (const auto& m : d.models)
                if (m.slot == slot)
                    for (std::size_t i = 0; i < m.boneNames.size(); ++i)
                        add(Label(m.boneNames[i]), int(i));
        });
        c->setCurrentIndex(c->findData(bone));
        c->setEnabled(c->count() != 0);
    };
    parentModels(kMainComboNormal, d.appearance.cameraParentModel, "parent.none");
    parentBones(kBoneRegisterCombo, d.appearance.cameraParentModel, d.appearance.cameraParentBone);
    bool accessory = false;
    for (const auto& a : d.accessories)
        if (a.slot == session_.Selected().accessory) {
            accessory = true;
            const auto& p = a.state;
            for (int i = 0; i < 3; ++i) {
                edit(kAccPosXEdit + i, p.position[i]);
                edit(kAccRotXEdit + i, double(p.rotation[i]) / kPi * 180);
            }
            edit(kAccScaleEdit, p.scale);
            edit(kAccOpacityEdit, p.opacity);
            check(kAccessoryVisibleCheckbox, p.visible);
            check(kAccessoryShadowCheckbox, p.shadowEnabled);
            check(kAccessoryAddBlendCheckbox, p.additiveBlend);
            parentModels(kMainComboGround, p.parentModel, "parent.ground");
            parentBones(kAttachBoneCombo, p.parentModel, p.parentBone);
        }
    for (int id = kDeleteAccessoryButton; id <= kRegisterAccessoryButton; ++id)
        Control(id)->setEnabled(accessory && (id != kAttachBoneCombo ||
                                              static_cast<QComboBox*>(Control(id))->count() != 0));
}
bool EditorWindow::PanelCommand(int id) {
    std::string reason;
    const struct {
        int collapse, expand, option;
    } panels[]{{kCollapseCameraPanelButton, kExpandCameraPanelButton, 1},
               {kCollapseLightPanelButton, kExpandLightPanelButton, 2},
               {kCollapseAccessoryPanelButton, kExpandAccessoryPanelButton, 3},
               {kCollapseBonePanelButton, kExpandBonePanelButton, 4},
               {kCollapseMorphPanelButton, kExpandMorphPanelButton, 5},
               {kCollapseSelfShadowPanelButton, kExpandSelfShadowPanelButton, 6}};
    for (const auto& p : panels)
        if (id == p.collapse || id == p.expand) {
            session_.SetPanelExpanded(p.option, id == p.expand);
            return true;
        }
    if (id == kModelEditToggle) {
        auto target = session_.Selected().model;
        if (session_.Selected().model != 255) {
            target = 255;
        } else {
            for (const auto& m : session_.Document().models)
                if (m.slot == session_.Document().envelope.selectedModelSlot)
                    target = m.slot;
            if (target == 255 && !session_.Document().models.empty())
                target = session_.Document().models.front().slot;
        }
        std::string reason;
        Result(session_.SelectModel(target, reason), reason);
        return true;
    }
    if (id >= kResetPositionXButton && id <= kCameraDistanceButton) {
        const int field = id == kCameraDistanceButton
                              ? kReadoutDistEdit
                              : kReadoutPosXEdit + id - kResetPositionXButton;
        static_cast<QLineEdit*>(Control(field))->setText("0");
        EditField(field);
        return true;
    }
    if (id >= kBoneSelectRadio && id <= kBoneRotateRadio) {
        session_.SetEditMode(static_cast<QAbstractButton*>(Control(id))->isChecked()
                                 ? (id == kBoneMoveRadio ? 4 : id - kBoneSelectRadio)
                                 : 2);
        return true;
    }
    if (id == kCameraRefBoneCheckbox || id == kCameraRefModelCheckbox) {
        const unsigned mode = static_cast<QAbstractButton*>(Control(id))->isChecked()
                                  ? (id == kCameraRefBoneCheckbox ? 2u : 1u)
                                  : 0u;
        Result(session_.SetCameraReference(mode, reason), reason);
        return true;
    }
    if (id == kFollowCameraCheckbox) {
        Result(session_.SetFollowCamera(static_cast<QAbstractButton*>(Control(id))->isChecked(),
                                        reason),
               reason);
        return true;
    }
    if (id == kResetLightButton || (id >= kEditOffCheckbox && id <= kEditMode2Checkbox) ||
        id == kCoordAxisCheckbox || id == kInfoCheckbox || id == kIkOnRadio || id == kIkOffRadio) {
        Result(session_.Edit(
                   "panel-state",
                   [&](auto& d, auto&) {
                       if (id == kResetLightButton)
                           d.lightState = {.602f, .602f, .602f, -.5f, -1.f, .5f};
                       else if (id >= kEditOffCheckbox && id <= kEditMode2Checkbox)
                           d.selfShadowMode = std::uint8_t(id - kEditOffCheckbox);
                       else if (id == kIkOnRadio || id == kIkOffRadio) {
                           auto* combo = static_cast<QComboBox*>(Control(kIkChainCombo));
                           if (!combo->count())
                               return false;
                           for (auto& m : d.models)
                               if (m.slot == session_.Selected().model)
                                   m.ikPose.at(combo->currentData().toUInt()) = id == kIkOnRadio;
                       } else {
                           const bool value =
                               static_cast<QAbstractButton*>(Control(id))->isChecked();
                           if (id == kCoordAxisCheckbox)
                               d.view.groundGrid = value;
                           if (id == kInfoCheckbox)
                               d.view.fpsOverlay = value;
                       }
                       return true;
                   },
                   reason),
               reason);
        return true;
    }
    if (id == kAccessoryVisibleCheckbox || id == kAccessoryShadowCheckbox ||
        id == kAccessoryAddBlendCheckbox) {
        const bool value = static_cast<QAbstractButton*>(Control(id))->isChecked();
        for (const auto& a : session_.Document().accessories)
            if (a.slot == session_.Selected().accessory) {
                auto state = a.state;
                if (id == kAccessoryVisibleCheckbox)
                    state.visible = value;
                if (id == kAccessoryShadowCheckbox)
                    state.shadowEnabled = value;
                if (id == kAccessoryAddBlendCheckbox)
                    state.additiveBlend = value;
                Result(session_.SetAccessory(a.slot, state, reason), reason);
                break;
            }
        return true;
    }
    // Previous/next selects a morph, rather than changing its weight.
    for (int lane = 0; lane < 4; ++lane)
        if (id == kMorphPreviousButton0 + lane * 5 || id == kMorphNextButton0 + lane * 5) {
            auto* c = static_cast<QComboBox*>(Control(kMorphCombo0 + lane * 5));
            if (c->count())
                c->setCurrentIndex((c->currentIndex() +
                                    (id == kMorphNextButton0 + lane * 5 ? 1 : c->count() - 1)) %
                                   c->count());
            return true;
        }
    return false;
}
bool EditorWindow::PanelEdit(int id, double value) {
    std::string reason;
    if (id >= kLightColorEditR && id <= kLightDirEditZ) {
        Result(session_.Edit(
                   "light-value",
                   [&](auto& d, auto&) {
                       d.lightState[id - kLightColorEditR] = float(
                           id <= kLightColorEditB ? std::clamp(value, 0., 255.) / 256. : value);
                       return true;
                   },
                   reason),
               reason);
        return true;
    }
    if (id == kSelfShadowRangeEdit) {
        Result(session_.Edit(
                   "shadow-distance",
                   [&](auto& d, auto&) {
                       d.physicsInterval = float((10000. - std::clamp(value, 0., 9999.)) / 100000.);
                       return true;
                   },
                   reason),
               reason);
        return true;
    }
    if (id >= kAccPosXEdit && id <= kAccOpacityEdit) {
        for (const auto& a : session_.Document().accessories)
            if (a.slot == session_.Selected().accessory) {
                auto state = a.state;
                if (id <= kAccPosZEdit)
                    state.position[id - kAccPosXEdit] = float(value);
                else if (id <= kAccRotZEdit)
                    state.rotation[id - kAccRotXEdit] = float(value / 180. * kPi);
                else if (id == kAccScaleEdit)
                    state.scale = float(value);
                else
                    state.opacity = float(std::clamp(value, 0., 1.));
                Result(session_.SetAccessory(a.slot, state, reason), reason);
                break;
            }
        return true;
    }
    for (int lane = 0; lane < 4; ++lane)
        if (id == kMorphEdit0 + lane * 5) {
            const auto* c = static_cast<QComboBox*>(Control(kMorphCombo0 + lane * 5));
            if (c->count())
                Result(session_.SetMorph(session_.Selected().model, c->currentData().toUInt(),
                                         float(std::clamp(value, 0., 1.)), reason),
                       reason);
            return true;
        }
    return false;
}
bool EditorWindow::PanelSlider(int id, int value) {
    if (id >= kLightColorSliderR && id <= kLightColorSliderB)
        return PanelEdit(kLightColorEditR + id - kLightColorSliderR, value);
    if (id >= kLightDirSliderX && id <= kLightDirSliderZ)
        return PanelEdit(kLightDirEditX + id - kLightDirSliderX, value / 100.);
    if (id == kSelfShadowRangeSlider)
        return PanelEdit(kSelfShadowRangeEdit, value);
    return false;
}
bool EditorWindow::PanelCombo(int id, int index) {
    const auto* combo = static_cast<QComboBox*>(Control(id));
    const int value = combo->itemData(index).toInt();
    std::string reason;
    if (id == kMainComboNormal || id == kBoneRegisterCombo) {
        auto camera = session_.Runtime().Session()->Pose().camera;
        if (id == kMainComboNormal) {
            camera.parentModel = value;
            camera.parentBone = 0;
        } else
            camera.parentBone = value;
        Result(session_.SetCamera(camera, reason), reason);
        return true;
    }
    if (id == kMainComboGround || id == kAttachBoneCombo) {
        for (const auto& a : session_.Document().accessories)
            if (a.slot == session_.Selected().accessory) {
                auto state = a.state;
                if (id == kMainComboGround) {
                    state.parentModel = value;
                    state.parentBone = 0;
                } else
                    state.parentBone = value;
                Result(session_.SetAccessory(a.slot, state, reason), reason);
                break;
            }
        return true;
    }
    return false;
}
}
