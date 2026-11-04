#include "editor_window.hpp"
#include "model_physics_dialog.hpp"
#include "edit_composition.hpp"
#include "edit_navigation.hpp"
#include "mikudancestudio/editor_numeric.hpp"
#include "mikudancestudio/editor_rotation.hpp"
#include "mikudancestudio/sidebar_navigation.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMimeData>
#include <QTimer>
#include <cmath>

namespace mikudancestudio::qt {
using namespace editor;
using namespace panel;
void EditorWindow::Dispatch(int id) {
    idleRenderState_ = 1;
    if (PanelCommand(id)) {
        Refresh();
        return;
    }
    std::string r;
    bool ok = true;
    const auto current = unsigned(session_.Document().timeline.currentFrame);
    const auto registerKeys = [&](const std::vector<KeyAddress>& keys, unsigned capacity) {
        unsigned failed = 0;
        const bool registered = session_.Register(keys, r, &failed);
        if (registered)
            KeyOverflow(failed, capacity, !keys.empty() && format::PmmModelKey(keys[0].track));
        return registered;
    };
    switch (id) {
    case kFrameVolumeCheckbox:
        MenuCommand(MDS_CMD_TOGGLE_LOW_POWER);
        return;
    case kPhysicsCheckbox:
        session_.SetRegistrationPhysics(static_cast<QAbstractButton*>(Control(id))->isChecked());
        break;
    case kPhysicsFrameCheckbox:
        session_.SetAutoInterpolation(static_cast<QAbstractButton*>(Control(id))->isChecked());
        break;
    case kCurveCopyButton:
        ok = session_.CopyInterpolation(interpolation_->Channel(), r);
        break;
    case kCurvePasteButton:
        ok = session_.PasteInterpolation(interpolation_->Channel(), r);
        break;
    case kCurveLinearButton:
        ok = session_.LinearInterpolation(interpolation_->Channel(), r);
        break;
    case kUndoButton:
        ok = session_.Undo(r);
        break;
    case kRedoButton:
        ok = session_.Redo(r);
        break;
    case kPreviousFrameButton:
        ok = SeekFrame(current ? current - 1 : 0, r);
        break;
    case kNextFrameButton:
        ok = SeekFrame(current + 1, r);
        break;
    case kFirstFrameButton:
        ok = SeekFrame(0, r);
        break;
    case kLastFrameButton:
        ok = SeekFrame(unsigned(std::max(0, session_.Document().timeline.lastRegisteredFrame)), r);
        break;
    case kPreviousKeyframeButton:
    case kNextKeyframeButton: {
        auto target = id == kPreviousKeyframeButton ? 0u : 0x7fffffffu;
        for (const auto& k : session_.Keys())
            if (id == kPreviousKeyframeButton ? k.frame < current && k.frame > target
                                              : k.frame > current && k.frame < target)
                target = k.frame;
        if (target == 0x7fffffffu)
            target = current;
        ok = SeekFrame(target, r);
        break;
    }
    case kPlayButton:
        if (physicsDialog_ && physicsDialog_->Active())
            return;
        if (session_.Playing()) {
            session_.Pause();
            audio_.Stop();
        } else {
            clock_.restart();
            ok = session_.Play(r);
            if (ok && !session_.PlaybackAudioDisabled() && session_.Document().wave.enabled &&
                !session_.Assets().wave.empty()) {
                if (audio_.Path() != session_.Assets().wave)
                    ok = audio_.Load(session_.Assets().wave, r);
                if (ok)
                    ok = audio_.Play(session_.CursorSeconds(), r);
                if (!ok)
                    session_.Pause();
            }
        }
        break;
    case kLoadModelButton:
    case kLoadAccessoryButton: {
        const auto p = ChooseFile(id == kLoadModelButton ? FileKind::Model : FileKind::Accessory,
                                  false, language_.Text("control.437"),
                                  id == kLoadModelButton ? "MMD Model (*.pmd *.pmx)"
                                                         : "Accessory (*.x *.vac)");
        if (p.isEmpty())
            return;
        ok = id == kLoadModelButton ? session_.ImportModel(Path(p), r)
                                    : session_.ImportAccessory(Path(p), r);
        break;
    }
    case kDeleteModelButton:
        ok = session_.UnloadModel(session_.Selected().model, r);
        break;
    case kDeleteAccessoryButton:
        ok = session_.UnloadAccessory(session_.Selected().accessory, r);
        break;
    case kCopyFramesButton:
        ok = session_.Copy(session_.Selected().keys, r);
        break;
    case kPasteButton:
    case kReversePasteButton:
        ok = PasteKeys(id == kReversePasteButton, false, r);
        break;
    case kDeleteFramesButton:
        ok = session_.Delete(session_.Selected().keys, r);
        break;
    case kSelectColumnButton:
        timeline_->SelectColumn();
        break;
    case kSelectRangeButton: {
        auto* a = static_cast<QLineEdit*>(Control(kRangeStartEdit));
        auto* b = static_cast<QLineEdit*>(Control(kRangeStopEdit));
        const auto* scope = static_cast<QComboBox*>(Control(kRegisterScopeCombo));
        const int index = scope->currentIndex();
        if (index >= 0 && unsigned(index) < scopes_.size())
            session_.SelectKeys(a->text().toUInt(), b->text().toUInt(), scopes_[index]);
        break;
    }
    case kShowCurrentFrameButton:
        timeline_->SetFrameScroll(std::max(0, int(current) - (side_ - 84) / 26));
        break;
    case kGoToFrameButton:
        ok = SeekFrame(static_cast<QLineEdit*>(Control(kGotoFrameEdit))->text().toUInt(), r);
        break;
    case kSetGoToFrameButton:
        static_cast<QLineEdit*>(Control(kGotoFrameEdit))->setText(QString::number(current));
        break;
    case kSelectAllBonesButton:
    case kSelectUnregisteredBonesButton: {
        ok = session_.SelectAllBones(id == kSelectUnregisteredBonesButton, r);
        break;
    }
    case kRegisterFrameButton: {
        std::vector<KeyAddress> addresses;
        for (auto bone : session_.Selected().bones)
            addresses.push_back({Track::Bone, session_.Selected().model, bone, current});
        ok = registerKeys(addresses, format::PmmKeyCapacity<format::PmmBoneKey>);
        break;
    }
    case kRegisterCameraButton:
        // Native camera registration advertises 600000 although its pool is 10000.
        ok = registerKeys({{Track::Camera, 255, 0, current}}, 600000);
        break;
    case kRegisterLightButton:
        ok = registerKeys({{Track::Light, 255, 0, current}}, 10000);
        break;
    case kRegisterSelfShadowButton:
        ok = registerKeys({{Track::Shadow, 255, 0, current}}, 10000);
        break;
    case kRegisterModelDisplayButton:
        ok = registerKeys({{Track::Display, session_.Selected().model, 0, current}}, 1000);
        break;
    case kRegisterAccessoryButton:
        ok = registerKeys({{Track::Accessory, session_.Selected().accessory, 0, current}}, 10000);
        break;
    case kRegisterMorphButton0:
    case kRegisterMorphButton1:
    case kRegisterMorphButton2:
    case kRegisterMorphButton3: {
        unsigned failed = 0;
        const int registers[]{kRegisterMorphButton0, kRegisterMorphButton1, kRegisterMorphButton2,
                              kRegisterMorphButton3};
        const int index =
            int(std::find(std::begin(registers), std::end(registers), id) - std::begin(registers));
        const int combos[]{kMorphCombo0, kMorphCombo1, kMorphCombo2, kMorphCombo3};
        auto* c = static_cast<QComboBox*>(Control(combos[index]));
        ok = session_.RegisterMorph(
            session_.Selected().model,
            c->count() ? std::optional<std::uint32_t>{c->currentData().toUInt()} : std::nullopt, r,
            &failed);
        if (ok)
            MorphOverflow(failed);
        break;
    }
    case kCopyBonePoseButton:
        ok = session_.CopyBonePose(r);
        break;
    case kBonePasteButton:
    case kBoneReversePasteButton:
        ok = session_.PasteBonePose(id == kBoneReversePasteButton, r);
        break;
    case kResetBonePoseButton:
        ok = session_.ResetSelectedBonePose(r);
        break;
    case kPerspectiveCheckbox: {
        auto c = session_.Runtime().Session()->Pose().camera;
        c.perspective = static_cast<QAbstractButton*>(Control(id))->isChecked() ? 0 : 1;
        ok = session_.SetCamera(c, r);
        break;
    }
    case kResetCameraButton: {
        auto c = session_.Runtime().Session()->Pose().camera;
        c.position = {0, c.parentModel < 0 ? 10.f : 0.f, 0};
        c.distance = c.parentModel < 0 ? -45.f : 0.f;
        c.rotation = {};
        ok = session_.SetCamera(c, r);
        break;
    }
    case kPlaybackLoopCheckbox:
    case kPlaybackReturnCheckbox:
    case kPlaybackFromCurrentCheckbox: {
        bool value = static_cast<QAbstractButton*>(Control(id))->isChecked();
        ok = session_.Edit(
            "playback-setting",
            [&](auto& d, auto&) {
                if (id == kPlaybackLoopCheckbox)
                    d.timeline.playbackLoop = value;
                else if (id == kPlaybackReturnCheckbox)
                    d.timeline.returnToStart = value;
                else
                    d.timeline.startAtCurrent = value;
                return true;
            },
            r);
        break;
    }
    case kModelVisibleCheckbox: {
        const bool value = static_cast<QAbstractButton*>(Control(id))->isChecked();
        ok = session_.Edit(
            "model-visibility",
            [&](auto& d, auto&) {
                for (auto& m : d.models)
                    if (m.slot == session_.Selected().model)
                        m.selection.loadComplete = value;
                return true;
            },
            r);
        break;
    }
    default:
        break;
    }
    Result(ok, r);
    Refresh();
}
void EditorWindow::EditField(int id) {
    auto* field = qobject_cast<QLineEdit*>(Control(id));
    if (!field)
        return;
    bool valid = false;
    const auto value = field->text().toDouble(&valid);
    if (!valid || !std::isfinite(value))
        return;
    if (PanelEdit(id, value)) {
        Refresh();
        return;
    }
    std::string r;
    bool ok = true;
    if (id == kCurrentFrameEdit)
        ok = value >= 0 && value <= 0x7fffffff && SeekFrame(unsigned(value), r);
    else if (id == kPlayStartFrameEdit || id == kPlayStopFrameEdit)
        ok = session_.Edit(
            "playback-range",
            [&](auto& d, auto&) {
                if (value < 0 || value > 0x7fffffff)
                    return false;
                if (id == kPlayStartFrameEdit)
                    d.timeline.playStartFrame = int(value);
                else
                    d.timeline.playStopFrame = int(value);
                return d.timeline.playStopFrame >= d.timeline.playStartFrame;
            },
            r);
    else if (id == kFovEdit) {
        auto c = session_.Runtime().Session()->Pose().camera;
        c.fov = float(std::clamp(value, 1., 125.));
        ok = session_.SetCamera(c, r);
    } else if (id >= kReadoutPosXEdit && id <= kReadoutDistEdit) {
        const int pos[]{kReadoutPosXEdit, kReadoutPosYEdit, kReadoutPosZEdit},
            rot[]{kReadoutRotXEdit, kReadoutRotYEdit, kReadoutRotZEdit};
        if (session_.Selected().model == 255) {
            auto c = session_.Runtime().Session()->Pose().camera;
            for (int i = 0; i < 3; ++i) {
                if (id == pos[i])
                    c.position[i] = float(i == 2 && c.parentModel >= 0 ? -value : value);
                if (id == rot[i])
                    c.rotation[i] =
                        float(value * (i == 0 ? -3.141592025756836 : 3.141592025756836) / 180);
            }
            if (id == kReadoutDistEdit)
                c.distance = float(-value);
            ok = session_.SetCamera(c, r);
        } else if (session_.ActiveBone() >= 0 && id != kReadoutDistEdit) {
            for (const auto& model : session_.Document().models)
                if (model.slot == session_.Selected().model) {
                    const auto bone = std::size_t(session_.ActiveBone());
                    if (bone >= model.bonePose.size()) {
                        ok = false;
                        break;
                    }
                    const auto p = model.bonePose[bone].components;
                    cpu_math::Vec3 position{p[0], p[1], p[2]};
                    cpu_math::Quaternion rotation{p[3], p[4], p[5], p[6]};
                    auto degrees = BoneRotationDegrees(rotation);
                    for (int i = 0; i < 3; ++i) {
                        if (id == pos[i])
                            position[i] = float(value);
                        if (id == rot[i]) {
                            degrees[i] = float(value);
                            rotation = BoneRotationFromDegrees(degrees);
                        }
                    }
                    ok = session_.SetBone(model.slot, bone, position, rotation, r);
                    break;
                }
        }
    }
    Result(ok, r);
    Refresh();
}
void EditorWindow::Slider(int id, int value) {
    if (PanelSlider(id, value)) {
        Refresh();
        return;
    }
    if (id == kTimelineHScroll) {
        timeline_->SetFrameScroll(value);
        return;
    }
    if (id == kTimelineVScroll) {
        timeline_->SetRowScroll(value);
        return;
    }
    std::string r;
    if (id == kFrameVolumeSlider) {
        frameNormalization_ = 100 - value;
        return;
    }
    if (id == kFovSlider) {
        auto c = session_.Runtime().Session()->Pose().camera;
        c.fov = float(value);
        Result(session_.SetCamera(c, r), r);
    }
    const int sliders[]{kMorphSlider0, kMorphSlider1, kMorphSlider2, kMorphSlider3},
        combos[]{kMorphCombo0, kMorphCombo1, kMorphCombo2, kMorphCombo3};
    for (int i = 0; i < 4; ++i)
        if (id == sliders[i]) {
            auto* c = static_cast<QComboBox*>(Control(combos[i]));
            if (c->count())
                Result(session_.SetMorph(session_.Selected().model, c->currentData().toUInt(),
                                         value / 100.f, r),
                       r);
        }
    Refresh();
}
void EditorWindow::Combo(int id, int index) {
    auto* c = static_cast<QComboBox*>(Control(id));
    if (index < 0)
        return;
    if (PanelCombo(id, index)) {
        Refresh();
        return;
    }
    if (id == kMainComboModel) {
        if (!accessoryScopeResource_.expired()) {
            scopeSelections_[accessoryScopeModel_] = 3;
            accessoryScopeResource_.reset();
        }
        std::string reason;
        if (Result(session_.SelectModel(std::uint8_t(c->itemData(index).toUInt()), reason), reason))
            edgeResource_.reset(); // Native combo activation refills even the same model.
    }
    if (id == kAccessoryCombo)
        session_.Selected().accessory = std::uint8_t(c->itemData(index).toUInt());
    if (id == kRegisterScopeCombo)
        scopeSelections_[session_.Selected().model] = index;
    Refresh();
}
void EditorWindow::FinishSidebarDrag() {
    if (!sidebarDragging_)
        return;
    sidebarDragging_ = false;
    canvas_->setCursor(sidebarCursor_);
}
bool EditorWindow::eventFilter(QObject* object, QEvent* event) {
    if (HandleEditComposition(object, event) || HandleEditDeleteTail(object, event)) {
        idleRenderState_ = 1;
        return true;
    }
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
        event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        if (drop->mimeData()->hasUrls()) {
            const auto urls = drop->mimeData()->urls();
            const bool local = std::any_of(urls.begin(), urls.end(),
                                           [](const auto& url) { return url.isLocalFile(); });
            if (local) {
                drop->setDropAction(Qt::CopyAction);
                drop->accept();
                if (event->type() == QEvent::Drop)
                    DropFiles(urls);
                return true;
            }
        }
    }
    // Timer/paint/update events must not wake a sleeping scene themselves.
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::InputMethod:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
    case QEvent::Resize:
    case QEvent::Expose:
    case QEvent::Show:
    case QEvent::Hide:
        idleRenderState_ = 1;
        break;
    default:
        break;
    }
    if (separate_ && object == separate_) {
        if (event->type() == QEvent::Close) {
            static_cast<QCloseEvent*>(event)->ignore();
            SetSeparateWindow(false);
            return true;
        }
        if (event->type() == QEvent::Resize) {
            Layout();
            UpdateFrame();
        }
    }
    auto* viewCanvas = separate_ ? separate_ : canvas_;
    if (viewport_ && ((object == this && event->type() == QEvent::WindowDeactivate) ||
                      (object == separate_ && event->type() == QEvent::WindowDeactivate) ||
                      (object == viewCanvas &&
                       (event->type() == QEvent::Hide || event->type() == QEvent::UngrabMouse)))) {
        FinishSidebarDrag();
        viewport_->CancelInput();
    }
    if (!fullScreen_ && viewport_ && object == viewCanvas &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonRelease)) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const auto point = mouse->position().toPoint();
        if (sidebarDragging_) {
            if (event->type() == QEvent::MouseMove && (mouse->buttons() & Qt::LeftButton)) {
                const auto placement = ui::DragSidebar(point.x(), canvas_->width());
                session_.SetSidebarWidth(placement.width);
                sidebarStoredWidth_ = placement.width;
                sidebarClientWidth_ = canvas_->width();
                sidebarRatio_ = placement.ratio;
                Layout();
                UpdateFrame();
            } else if (event->type() == QEvent::MouseButtonRelease ||
                       !(mouse->buttons() & Qt::LeftButton))
                FinishSidebarDrag();
            return true;
        }
        const bool divider =
            !separate_ && ui::SidebarHit(point.x(), point.y(), side_, canvas_->height());
        if (event->type() == QEvent::MouseMove && !viewport_->HeaderDragging()) {
            if (divider)
                canvas_->setCursor(Qt::SizeHorCursor);
            else
                viewCanvas->unsetCursor();
        }
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton &&
            divider && canvas_->width() >= 255 && !viewport_->HeaderDragging()) {
            sidebarDragging_ = true;
            sidebarCursor_ = canvas_->cursor();
            canvas_->setCursor(Qt::SizeHorCursor);
            return true;
        }
        const auto origin = viewportContainer_->pos();
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
            const auto point = mouse->position().toPoint();
            const auto operation =
                render::ViewportHeaderToolAt(point.x(), point.y(), viewCanvas->width(), origin.y());
            if (operation) {
                const auto restore =
                    render::ViewportHeaderRestorePoint(operation, viewCanvas->width(), origin.y());
                return viewport_->BeginHeaderDrag(unsigned(operation), point - origin,
                                                  QPoint(restore[0], restore[1]) - origin,
                                                  viewCanvas->window()->windowHandle());
            }
        } else if (viewport_->HeaderDragging()) {
            const QPointF point = mouse->position() - origin;
            QMouseEvent forwarded(event->type(), point, point, mouse->globalPosition(),
                                  mouse->button(), mouse->buttons(), mouse->modifiers(),
                                  mouse->pointingDevice());
            QCoreApplication::sendEvent(viewport_, &forwarded);
            return true;
        }
    }
    // The original Ctrl+S path also works when a panel edit has focus. Keep
    // the shortcut out of the menu label, which has no accelerator suffix.
    if (event->type() == QEvent::KeyPress && qobject_cast<QLineEdit*>(object)) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            (key->modifiers() & Qt::AltModifier))
            return true; // Prevent ignored edit input from bubbling to the viewport canvas.
    }
    if (event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->matches(QKeySequence::Save)) {
        if (!static_cast<QKeyEvent*>(event)->isAutoRepeat())
            MenuCommand(MDS_CMD_SAVE_PROJECT);
        return true;
    }
    if (event->type() == QEvent::KeyPress && !qobject_cast<QLineEdit*>(object)) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool viewportInput =
            object == viewport_ || object == viewCanvas || (fullScreen_ && object == this);
        const bool toggle = (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
                            (key->modifiers() & Qt::AltModifier);
        if (viewportInput && (toggle || (fullScreen_ && key->key() == Qt::Key_Escape))) {
            if (!key->isAutoRepeat()) {
                const bool enabled = toggle && !fullScreen_;
                // Docking may delete the input QWindow. Finish delivering its
                // event before changing window ownership.
                QTimer::singleShot(0, this, [this, enabled] { SetFullScreenViewport(enabled); });
            }
            return true;
        }
        if (key->key() == Qt::Key_I || key->key() == Qt::Key_U || key->key() == Qt::Key_K ||
            key->key() == Qt::Key_J) {
            if (!key->isAutoRepeat() && !session_.Playing()) {
                const bool primary = key->key() == Qt::Key_I || key->key() == Qt::Key_K;
                const bool insertion = key->key() == Qt::Key_I || key->key() == Qt::Key_U;
                const bool model = session_.Selected().model != 255;
                const auto track = model ? (primary ? editor::Track::Bone : editor::Track::Morph)
                                         : (primary ? editor::Track::Camera : editor::Track::Light);
                std::string reason;
                Result(insertion ? session_.InsertFrameLine(track, reason)
                                 : session_.DeleteFrameLine(track, reason),
                       reason);
                Refresh();
            }
            return true;
        }
        if ((key->modifiers() & Qt::KeypadModifier) && key->key() >= Qt::Key_0 &&
            key->key() <= Qt::Key_9) {
            if (!key->isAutoRepeat()) {
                std::string reason;
                Result(session_.ViewPreset(unsigned(key->key() - Qt::Key_0), reason), reason);
                Refresh();
            }
            return true;
        }
        if (key->key() == Qt::Key_Space) {
            Dispatch(kPlayButton);
            return true;
        }
        if (key->key() == Qt::Key_H &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            if (!key->isAutoRepeat())
                MenuCommand(MDS_CMD_TOGGLE_ANTIALIASING);
            return true;
        }
        if (key->key() == Qt::Key_D &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            if (!key->isAutoRepeat())
                MenuCommand(MDS_CMD_TOGGLE_PHYSICS_BODIES);
            return true;
        }
        if (key->key() == Qt::Key_P &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            if (!key->isAutoRepeat())
                MenuCommand(MDS_CMD_TOGGLE_SELF_SHADOW);
            return true;
        }
        if (key->key() == Qt::Key_V) {
            if (!key->isAutoRepeat()) {
                if ((key->modifiers() & Qt::ControlModifier) && !session_.Playing())
                    Dispatch(kPasteButton);
                else
                    MenuCommand(key->modifiers() & Qt::ShiftModifier
                                    ? MDS_CMD_TOGGLE_MODEL_NON_DISPLAY
                                    : MDS_CMD_TOGGLE_MODEL_TRANSPARENCY);
            }
            return true;
        }
        if (key->key() == Qt::Key_L && !key->isAutoRepeat() &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            session_.CycleCoordinateSystem();
            Refresh();
            return true;
        }
        if (key->key() == Qt::Key_Delete) {
            Dispatch(kDeleteFramesButton);
            return true;
        }
        if (key->key() == Qt::Key_F &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            if (!key->isAutoRepeat())
                MenuCommand(MDS_CMD_PASTE_AT_DIFFERENT_FRAME);
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}
}
