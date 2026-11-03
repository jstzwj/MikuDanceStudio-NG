#include "editor_window.hpp"
#include "mikudancestudio/mmd_editor_menu.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QAbstractButton>
#include <QMenuBar>
#include <QLineEdit>
#include <QMessageBox>
#include <limits>

namespace mikudancestudio::qt {
namespace {
const char* FileCommand(int command) {
    switch (command) {
    case MDS_CMD_EXIT:
        return "file.exit";
    case MDS_CMD_NEW_PROJECT:
        return "file.new";
    case MDS_CMD_OPEN_PROJECT:
        return "file.open";
    case MDS_CMD_SAVE_PROJECT:
        return "file.save";
    case MDS_CMD_SAVE_PROJECT_AS:
        return "file.save_as";
    case MDS_CMD_LOAD_POSE:
        return "file.pose_open";
    case MDS_CMD_SAVE_POSE:
        return "file.pose_save";
    case MDS_CMD_LOAD_MOTION:
        return "file.motion_open";
    case MDS_CMD_SAVE_MOTION:
        return "file.motion_save";
    case MDS_CMD_LOAD_WAVE:
        return "file.wave";
    case MDS_CMD_RENDER_IMAGE:
        return "file.image";
    case MDS_CMD_NUMERIC_INPUT:
        return "edit.numeric";
    case MDS_CMD_SCREEN_SIZE:
        return "view.screen_size";
    case MDS_CMD_LOAD_BACKGROUND_IMAGE:
        return "background.load";
    case MDS_CMD_TOGGLE_BACKGROUND_IMAGE:
        return "background.show";
    case MDS_CMD_TOGGLE_BLACK_BACKGROUND:
        return "background.black";
    case MDS_CMD_SCREEN_CAPTURE_OFF:
        return "background.capture.0";
    case MDS_CMD_SCREEN_CAPTURE_MODE_1:
        return "background.capture.1";
    case MDS_CMD_SCREEN_CAPTURE_MODE_2:
        return "background.capture.2";
    case MDS_CMD_SCREEN_CAPTURE_MODE_3:
        return "background.capture.3";
    default:
        return nullptr;
    }
}
int PhysicsMode(int command) {
    switch (command) {
    case MDS_CMD_PHYSICS_DISABLED:
        return 0;
    case MDS_CMD_PHYSICS_ALWAYS:
        return 1;
    case MDS_CMD_PHYSICS_ON_OFF:
        return 2;
    case MDS_CMD_PHYSICS_TRACE:
        return 3;
    default:
        return -1;
    }
}
bool ModelCommand(int command) {
    switch (command) {
    case MDS_CMD_LOAD_POSE:
    case MDS_CMD_SAVE_POSE:
    case MDS_CMD_SELECT_ALL_BONE_FRAMES:
    case MDS_CMD_SELECT_ALL_DISPLAY_FRAMES:
    case MDS_CMD_SELECT_ALL_MORPH_FRAMES:
    case MDS_CMD_REGISTER_ALL_MORPH_FRAMES:
    case MDS_CMD_RESET_MORPHS:
    case MDS_CMD_SELECT_PHYSICS_BONES:
    case MDS_CMD_SELECT_PHYSICS_ON_FRAMES:
    case MDS_CMD_DELETE_UNUSED_FRAMES:
    case MDS_CMD_MULTIPLY_MORPH_FRAMES:
    case MDS_CMD_MULTIPLY_BONE_FRAMES:
    case MDS_CMD_TOGGLE_FRAME_PHYSICS:
    case MDS_CMD_CENTER_POSITION_BIAS:
    case MDS_CMD_DELETE_MOUTH_FRAMES:
    case MDS_CMD_SHIFT_MOUTH_FRAMES:
    case MDS_CMD_REGISTER_BLINKING:
    case MDS_CMD_DELETE_EYE_FRAMES:
    case MDS_CMD_DELETE_EYEBROW_FRAMES:
        return true;
    default:
        return false;
    }
}
bool Implemented(int command) {
    if (FileCommand(command) || PhysicsMode(command) >= 0 || ModelCommand(command) ||
        (command >= MDS_CMD_SELECT_ALL_CAMERA_FRAMES &&
         command <= MDS_CMD_SELECT_ALL_ACCESSORY_FRAMES))
        return true;
    switch (command) {
    case MDS_CMD_TOGGLE_INFORMATION:
    case MDS_CMD_TOGGLE_COORDINATE_AXIS:
    case MDS_CMD_TOGGLE_CAMERA_TRACKING:
    case MDS_CMD_TOGGLE_GROUND_SHADOW:
    case MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY:
    case MDS_CMD_TOGGLE_PHYSICS_FLOOR:
    case MDS_CMD_INSERT_BONE_CAMERA_FRAME_LINE:
    case MDS_CMD_INSERT_MORPH_LIGHT_FRAME_LINE:
    case MDS_CMD_DELETE_BONE_CAMERA_FRAME_LINE:
    case MDS_CMD_DELETE_MORPH_LIGHT_FRAME_LINE:
    case MDS_CMD_TOGGLE_UI_LANGUAGE:
    case MDS_CMD_TOGGLE_SEPARATE_WINDOW:
    case MDS_CMD_TOGGLE_ALWAYS_ON_TOP:
    case MDS_CMD_TOGGLE_FULL_SCREEN:
    case MDS_CMD_RESET_BONE_CAMERA_ROTATION:
    case MDS_CMD_RESET_PHYSICS_BODIES:
    case MDS_CMD_EDGE_THICKNESS:
    case MDS_CMD_GRAVITY_SETTINGS:
    case MDS_CMD_MULTIPLY_CAMERA_FRAMES:
    case MDS_CMD_MODEL_DRAW_ORDER:
    case MDS_CMD_MODEL_CALCULATION_ORDER:
    case MDS_CMD_PASTE_AT_DIFFERENT_FRAME:
    case MDS_CMD_FPS_UNLIMITED:
    case MDS_CMD_FPS_30:
    case MDS_CMD_FPS_60:
    case MDS_CMD_GROUND_SHADOW_COLOR:
    case MDS_CMD_EDGE_COLOR:
    case MDS_CMD_TOGGLE_SELF_SHADOW:
    case MDS_CMD_ACCESSORY_SETTINGS:
    case MDS_CMD_TOGGLE_WAVE_DISABLED:
    case MDS_CMD_TOGGLE_MODEL_TRANSPARENCY:
    case MDS_CMD_TOGGLE_MODEL_NON_DISPLAY:
    case MDS_CMD_TOGGLE_PLAYBACK_PHYSICS_ON_OFF:
    case MDS_CMD_TOGGLE_LOW_POWER:
    case MDS_CMD_TOGGLE_PREVIOUS_FOLDER:
    case MDS_CMD_TOGGLE_MIPMAP:
    case MDS_CMD_TOGGLE_WIREFRAME:
    case MDS_CMD_ABOUT:
    case MDS_CMD_ABOUT_PHYSICS:
    case MDS_CMD_RESTORE_TEXTURES:
    case MDS_CMD_TOGGLE_WAVE_ON_FRAME:
        return true;
    default:
        return false;
    }
}
}

void EditorWindow::BuildMenus() {
    // clear() only removes QMenuBar entries; release the complete former tree.
    for (auto* entry : menuBar()->actions())
        delete entry->menu();
    menuBar()->clear();
    actions_.clear();
    menuActions_.clear();
    std::vector<QMenu*> parents;
    for (const auto& entry : ui::kMmdMenu) {
        parents.resize(entry.depth);
        if (entry.kind == ui::MenuKind::Popup) {
            auto* menu = entry.depth ? parents.back()->addMenu(language_.Text(entry.message))
                                     : menuBar()->addMenu(language_.Text(entry.message));
            menu->setObjectName(entry.message);
            menu->setToolTipsVisible(true);
            menu->menuAction()->setMenuRole(QAction::NoRole);
            parents.push_back(menu);
        } else if (entry.kind == ui::MenuKind::Separator) {
            parents.back()->addSeparator();
        } else {
            auto* action = parents.back()->addAction(language_.Text(entry.message));
            action->setMenuRole(QAction::NoRole);
            action->setProperty("mmdCommandId", entry.command);
            action->setProperty("mmdImplemented", Implemented(entry.command));
            action->setObjectName(entry.message);
            menuActions_[entry.command] = action;
            if (const auto* alias = FileCommand(entry.command)) {
                actions_[alias] = action;
                action->setObjectName(alias);
            }
            if (!Implemented(entry.command))
                action->setToolTip(language_.Text("menu.unavailable"));
            connect(action, &QAction::triggered, this,
                    [this, command = entry.command] { MenuCommand(command); });
        }
    }
    // NG additions do not change the order/nesting of the original eight menus.
    auto* extensions = menuBar()->addMenu(language_.Text("menu.application"));
    extensions->setObjectName("menu.application");
    const auto add = [&](const char* name, const QString& label, const QKeySequence& shortcut) {
        auto* action = extensions->addAction(label);
        action->setObjectName(name);
        action->setMenuRole(QAction::NoRole);
        action->setShortcut(shortcut);
        actions_[name] = action;
        return action;
    };
    connect(add("undo", language_.Text("control.400"), QKeySequence::Undo), &QAction::triggered,
            this, [this] { Dispatch(panel::kUndoButton); });
    connect(add("redo", language_.Text("control.401"), QKeySequence::Redo), &QAction::triggered,
            this, [this] { Dispatch(panel::kRedoButton); });
    extensions->addSeparator();
    connect(add("file.sequence", language_.Text("file.sequence"), {}), &QAction::triggered, this,
            [this] { FileAction("file.sequence"); });
    extensions->addSeparator();
    auto* renderer = extensions->addMenu(language_.Text("menu.renderer"));
    renderer->setObjectName("menu.renderer");
    for (auto pair : {std::pair{rhi::Backend::D3D11, "DirectX 11"},
                      {rhi::Backend::D3D12, "DirectX 12"},
                      {rhi::Backend::Vulkan, "Vulkan"},
                      {rhi::Backend::Metal, "Metal"}}) {
        auto* a = renderer->addAction(pair.second);
        a->setProperty("rhiBackend", int(pair.first));
        a->setEnabled(rhi::BackendAvailable(pair.first));
        a->setCheckable(true);
        a->setChecked(viewport_->Options().backend == pair.first);
        connect(a, &QAction::triggered, this, [this, b = pair.first] {
            auto options = viewport_->Options();
            options.backend = b;
            options.surface.reset();
            CreateViewport(options);
            BuildMenus();
            Refresh();
        });
    }
    auto* language = extensions->addMenu(language_.Text("menu.language"));
    language->setObjectName("menu.language");
    for (const char* code : {"en", "ja", "zh_CN"}) {
        auto* a = language->addAction(language_.Text(QString("language.") + code));
        a->setCheckable(true);
        a->setChecked(language_.Code() == code);
        connect(a, &QAction::triggered, this, [this, code] { SetLanguage(code); });
    }
    if (separate_) {
        separate_->addAction(actions_.at("undo"));
        separate_->addAction(actions_.at("redo"));
    }
}

void EditorWindow::RefreshMenus() {
    const auto& doc = session_.Document();
    const bool model = session_.Selected().model != 255;
    for (const auto& [command, action] : menuActions_) {
        bool enabled = Implemented(command);
        if (ModelCommand(command))
            enabled &= model && !session_.Playing();
        if (command == MDS_CMD_EDGE_THICKNESS)
            enabled &= model;
        if (command == MDS_CMD_ACCESSORY_SETTINGS)
            enabled &= !session_.Playing() && !doc.accessories.empty();
        if (command == MDS_CMD_RESET_BONE_CAMERA_ROTATION)
            enabled &= !session_.Playing();
        if (command == MDS_CMD_TOGGLE_SEPARATE_WINDOW)
            enabled &= !fullScreen_;
        if (command == MDS_CMD_INSERT_BONE_CAMERA_FRAME_LINE ||
            command == MDS_CMD_INSERT_MORPH_LIGHT_FRAME_LINE ||
            command == MDS_CMD_DELETE_BONE_CAMERA_FRAME_LINE ||
            command == MDS_CMD_DELETE_MORPH_LIGHT_FRAME_LINE)
            enabled &= !session_.Playing();
        if (command >= MDS_CMD_SELECT_ALL_CAMERA_FRAMES &&
            command <= MDS_CMD_SELECT_ALL_ACCESSORY_FRAMES)
            enabled &= !model && !session_.Playing();
        if (command == MDS_CMD_MULTIPLY_CAMERA_FRAMES)
            enabled &= !model && !session_.Playing();
        if (command == MDS_CMD_MODEL_DRAW_ORDER || command == MDS_CMD_MODEL_CALCULATION_ORDER)
            enabled &= !session_.Playing();
        if (command == MDS_CMD_PASTE_AT_DIFFERENT_FRAME)
            enabled &= session_.CanPasteToSelectedTrack();
        action->setEnabled(enabled);
    }
    const auto check = [&](int command, bool value) {
        auto* action = menuActions_.at(command);
        action->setCheckable(true);
        action->setChecked(value);
    };
    for (int command : {MDS_CMD_PHYSICS_DISABLED, MDS_CMD_PHYSICS_ALWAYS, MDS_CMD_PHYSICS_ON_OFF,
                        MDS_CMD_PHYSICS_TRACE})
        check(command, doc.physics.playbackMode == PhysicsMode(command));
    check(MDS_CMD_TOGGLE_BLACK_BACKGROUND, doc.appearance.blackBackground);
    check(MDS_CMD_TOGGLE_BACKGROUND_IMAGE, doc.picture.enabled);
    for (int i = 0; i < 4; ++i)
        check(MDS_CMD_SCREEN_CAPTURE_OFF + i, doc.view.captureMode == i);
    check(MDS_CMD_TOGGLE_INFORMATION, doc.view.fpsOverlay);
    check(MDS_CMD_TOGGLE_COORDINATE_AXIS, doc.view.groundGrid);
    check(MDS_CMD_TOGGLE_CAMERA_TRACKING, doc.appearance.followCamera);
    check(MDS_CMD_TOGGLE_GROUND_SHADOW, doc.view.groundShadow);
    check(MDS_CMD_TOGGLE_SELF_SHADOW, doc.selfShadowEnabled != 0);
    check(MDS_CMD_TOGGLE_WAVE_DISABLED, session_.PlaybackAudioDisabled());
    check(MDS_CMD_TOGGLE_WAVE_ON_FRAME, session_.FrameAudioPreview());
    check(MDS_CMD_TOGGLE_LOW_POWER, lowPower_);
    check(MDS_CMD_TOGGLE_PREVIOUS_FOLDER, fileHistory_.enabled);
    check(MDS_CMD_TOGGLE_MIPMAP, session_.DisplayOptions().mipmaps);
    check(MDS_CMD_TOGGLE_WIREFRAME, session_.DisplayOptions().wireframe);
    check(MDS_CMD_TOGGLE_PLAYBACK_PHYSICS_ON_OFF, session_.PlaybackPhysicsOnOff());
    check(MDS_CMD_TOGGLE_MODEL_TRANSPARENCY, session_.DisplayOptions().transparentModels);
    check(MDS_CMD_TOGGLE_MODEL_NON_DISPLAY, session_.DisplayOptions().hideModels);
    check(MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY, doc.physics.projectedShadowBlend);
    check(MDS_CMD_TOGGLE_PHYSICS_FLOOR, doc.appearance.floorVisible);
    check(MDS_CMD_TOGGLE_SEPARATE_WINDOW, separate_ != nullptr);
    check(MDS_CMD_TOGGLE_ALWAYS_ON_TOP, separateOnTop_);
    check(MDS_CMD_FPS_UNLIMITED, doc.view.fpsLimit == 1000);
    check(MDS_CMD_FPS_30, doc.view.fpsLimit == 30);
    check(MDS_CMD_FPS_60, doc.view.fpsLimit != 30 && doc.view.fpsLimit != 1000);
}

void EditorWindow::MenuCommand(int command) {
    if (command == MDS_CMD_GRAVITY_SETTINGS) {
        GravitySettings();
        return;
    }
    const auto found = menuActions_.find(command);
    if (found == menuActions_.end() || !found->second->isEnabled())
        return;
    idleRenderState_ = 1;
    if (command == MDS_CMD_RESTORE_TEXTURES) {
        std::string reason;
        if (Result(viewport_->ReloadTextures(reason), reason))
            UpdateFrame();
        return;
    }
    if (command == MDS_CMD_ABOUT || command == MDS_CMD_ABOUT_PHYSICS) {
        const bool physics = command == MDS_CMD_ABOUT_PHYSICS;
        const auto title =
            language_.Text(physics ? "dialog.about_physics.title" : "dialog.about.title");
        const auto text =
            physics ? QString("Bullet Physics Library Ver.2.75\n\nhttp://www.bulletphysics.com/")
                    : language_.Text("dialog.about.body").arg(QStringLiteral(MDS_PRODUCT_VERSION));
        QMessageBox dialog(QMessageBox::NoIcon, title, text, QMessageBox::Ok,
                           !physics && separate_ ? separate_ : this);
        dialog.setObjectName(physics ? "mmd.aboutPhysics" : "mmd.about");
        dialog.setTextFormat(Qt::PlainText);
        dialog.setOption(QMessageBox::Option::DontUseNativeDialog);
        // QMessageBox intentionally ignores titles on macOS. The original
        // MMD dialog contract requires one on every platform.
        dialog.QDialog::setWindowTitle(title);
        if (physics)
            dialog.setWindowFlag(Qt::WindowStaysOnTopHint);
        const bool running = timer_.isActive();
        timer_.stop();
        dialog.exec();
        clock_.restart();
        if (running) {
            RefreshFrameTimer();
            timer_.start();
        }
        return;
    }
    if (command == MDS_CMD_TOGGLE_WIREFRAME) {
        session_.SetWireframe(!session_.DisplayOptions().wireframe);
        RefreshMenus();
        UpdateFrame();
        return;
    }
    if (command == MDS_CMD_TOGGLE_MIPMAP) {
        session_.SetMipmaps(!session_.DisplayOptions().mipmaps);
        RefreshMenus();
        UpdateFrame();
        return;
    }
    if (command == MDS_CMD_TOGGLE_PREVIOUS_FOLDER) {
        fileHistory_.enabled = !fileHistory_.enabled;
        RefreshMenus();
        return;
    }
    if (const auto* name = FileCommand(command)) {
        FileAction(name);
        return;
    }
    if (command == MDS_CMD_FPS_UNLIMITED || command == MDS_CMD_FPS_30 ||
        command == MDS_CMD_FPS_60) {
        session_.SetFrameLimit(command == MDS_CMD_FPS_30   ? 30
                               : command == MDS_CMD_FPS_60 ? 60
                                                           : 1000);
        RefreshFrameTimer();
        RefreshMenus();
        return;
    }
    if (command == MDS_CMD_TOGGLE_WAVE_DISABLED) {
        // Native 297 only changes the startup/loop gate. Current PCM playback
        // continues; the switch does not mute, stop or seek the active device.
        session_.SetPlaybackAudioDisabled(!session_.PlaybackAudioDisabled());
        RefreshMenus();
        return;
    }
    if (command == MDS_CMD_TOGGLE_WAVE_ON_FRAME) {
        session_.SetFrameAudioPreview(!session_.FrameAudioPreview());
        RefreshMenus();
        return;
    }
    if (command == MDS_CMD_TOGGLE_LOW_POWER) {
        lowPower_ = !lowPower_;
        idleRenderState_ = 1;
        static_cast<QAbstractButton*>(Control(panel::kFrameVolumeCheckbox))->setChecked(lowPower_);
        RefreshMenus();
        return;
    }
    if (command == MDS_CMD_TOGGLE_MODEL_TRANSPARENCY ||
        command == MDS_CMD_TOGGLE_MODEL_NON_DISPLAY) {
        if (command == MDS_CMD_TOGGLE_MODEL_TRANSPARENCY)
            session_.SetTransparentModels(!session_.DisplayOptions().transparentModels);
        else
            session_.SetHiddenModels(!session_.DisplayOptions().hideModels);
        RefreshMenus();
        UpdateFrame();
        return;
    }
    if (command == MDS_CMD_PASTE_AT_DIFFERENT_FRAME) {
        const bool model = session_.Selected().model != 255;
        if (!testing_) {
            QMessageBox confirm(
                QMessageBox::NoIcon, language_.Text("dialog.track_paste.title"),
                language_.Text(model ? "dialog.track_paste.bone" : "dialog.track_paste.accessory"),
                QMessageBox::Ok | QMessageBox::Cancel, this);
            if (separate_)
                confirm.setWindowFlag(Qt::WindowStaysOnTopHint);
            if (confirm.exec() != QMessageBox::Ok)
                return;
        }
        std::string reason;
        const bool ok = session_.PasteToSelectedTrack(
            unsigned(session_.Document().timeline.currentFrame), reason);
        if (!ok && (reason == "clipboard-select-bone" || reason == "clipboard-select-accessory")) {
            lastError_ = language_.Text(model ? "dialog.track_paste.select_bone"
                                              : "dialog.track_paste.select_accessory");
            if (!testing_)
                QMessageBox::information(this, language_.Text("dialog.track_paste.title"),
                                         lastError_);
        } else
            Result(ok, reason);
        Refresh();
        return;
    }
    if (command == MDS_CMD_EDGE_THICKNESS) {
        EdgeThickness();
        return;
    }
    if (command == MDS_CMD_GROUND_SHADOW_COLOR) {
        GroundShadowColor();
        return;
    }
    if (command == MDS_CMD_CENTER_POSITION_BIAS) {
        CenterBias();
        return;
    }
    if (command == MDS_CMD_EDGE_COLOR) {
        OutlineColor();
        return;
    }
    if (command == MDS_CMD_ACCESSORY_SETTINGS) {
        AccessoryOrder();
        return;
    }
    if (command == MDS_CMD_DELETE_UNUSED_FRAMES) {
        std::string reason;
        unsigned count;
        const bool ok = Result(session_.DeleteUnusedFrames(count, reason), reason);
        Refresh();
        if (ok && !testing_) {
            QMessageBox message(QMessageBox::NoIcon, language_.Text("dialog.reduction.title"),
                                language_.Text("dialog.reduction.message").arg(count),
                                QMessageBox::Ok, this);
            message.setWindowFlag(Qt::WindowStaysOnTopHint);
            message.exec();
        }
        return;
    }
    if (command == MDS_CMD_MULTIPLY_MORPH_FRAMES) {
        MorphScale();
        return;
    }
    if (command == MDS_CMD_SHIFT_MOUTH_FRAMES) {
        ShiftMouthFrames();
        return;
    }
    if (command == MDS_CMD_REGISTER_BLINKING) {
        RegisterBlinks();
        return;
    }
    if (command == MDS_CMD_MULTIPLY_CAMERA_FRAMES) {
        CameraScale();
        return;
    }
    if (command == MDS_CMD_MULTIPLY_BONE_FRAMES) {
        BoneScale();
        return;
    }
    if (command == MDS_CMD_TOGGLE_FRAME_PHYSICS) {
        PhysicsKeys();
        return;
    }
    if (command == MDS_CMD_MODEL_DRAW_ORDER || command == MDS_CMD_MODEL_CALCULATION_ORDER) {
        ModelOrder(command == MDS_CMD_MODEL_CALCULATION_ORDER);
        return;
    }
    if (command == MDS_CMD_TOGGLE_UI_LANGUAGE) {
        SetLanguage(language_.Code() == "en" ? "ja" : "en");
        return;
    }
    if (command == MDS_CMD_TOGGLE_PHYSICS_FLOOR) {
        session_.SetPhysicsFloor(!session_.Document().appearance.floorVisible);
        Refresh();
        return;
    }
    if (command == MDS_CMD_TOGGLE_PLAYBACK_PHYSICS_ON_OFF) {
        session_.SetPlaybackPhysicsOnOff(!session_.PlaybackPhysicsOnOff());
        Refresh();
        return;
    }
    if (command == MDS_CMD_TOGGLE_SEPARATE_WINDOW) {
        SetSeparateWindow(!separate_);
        return;
    }
    if (command == MDS_CMD_TOGGLE_ALWAYS_ON_TOP) {
        ToggleSeparateOnTop();
        return;
    }
    if (command == MDS_CMD_TOGGLE_FULL_SCREEN) {
        SetFullScreenViewport(true);
        return;
    }
    if (command == MDS_CMD_INSERT_BONE_CAMERA_FRAME_LINE ||
        command == MDS_CMD_INSERT_MORPH_LIGHT_FRAME_LINE ||
        command == MDS_CMD_DELETE_BONE_CAMERA_FRAME_LINE ||
        command == MDS_CMD_DELETE_MORPH_LIGHT_FRAME_LINE) {
        const bool primary = command == MDS_CMD_INSERT_BONE_CAMERA_FRAME_LINE ||
                             command == MDS_CMD_DELETE_BONE_CAMERA_FRAME_LINE;
        const bool insertion = command == MDS_CMD_INSERT_BONE_CAMERA_FRAME_LINE ||
                               command == MDS_CMD_INSERT_MORPH_LIGHT_FRAME_LINE;
        const bool model = session_.Selected().model != 255;
        const auto track = model ? (primary ? editor::Track::Bone : editor::Track::Morph)
                                 : (primary ? editor::Track::Camera : editor::Track::Light);
        std::string reason;
        Result(insertion ? session_.InsertFrameLine(track, reason, true)
                         : session_.DeleteFrameLine(track, reason, true),
               reason);
        Refresh();
        return;
    }
    int control = 0;
    if (command == MDS_CMD_SELECT_PHYSICS_ON_FRAMES) {
        std::string reason;
        Result(session_.SelectPhysicsOnKeys(reason), reason);
        Refresh();
        return;
    }
    if (command == MDS_CMD_RESET_BONE_CAMERA_ROTATION || command == MDS_CMD_SELECT_PHYSICS_BONES) {
        std::string reason;
        Result(command == MDS_CMD_RESET_BONE_CAMERA_ROTATION ? session_.ResetRotation(reason)
                                                             : session_.SelectPhysicsBones(reason),
               reason);
        Refresh();
        return;
    }
    if (command == MDS_CMD_RESET_PHYSICS_BODIES) {
        session_.ResetPhysicsBodies();
        return;
    }
    if (command == MDS_CMD_REGISTER_ALL_MORPH_FRAMES || command == MDS_CMD_RESET_MORPHS) {
        std::string reason;
        unsigned failed = 0;
        const bool ok =
            Result(command == MDS_CMD_REGISTER_ALL_MORPH_FRAMES
                       ? session_.RegisterAllMorphs(session_.Selected().model, reason, &failed)
                       : session_.ResetMorphs(session_.Selected().model, reason),
                   reason);
        if (ok)
            MorphOverflow(failed);
        Refresh();
        if (command == MDS_CMD_RESET_MORPHS)
            for (int id :
                 {panel::kMorphEdit0, panel::kMorphEdit1, panel::kMorphEdit2, panel::kMorphEdit3})
                static_cast<QLineEdit*>(Control(id))->setText("0.00000");
        return;
    }
    if (command == MDS_CMD_DELETE_MOUTH_FRAMES || command == MDS_CMD_DELETE_EYE_FRAMES ||
        command == MDS_CMD_DELETE_EYEBROW_FRAMES) {
        const unsigned panel = command == MDS_CMD_DELETE_MOUTH_FRAMES ? 3
                               : command == MDS_CMD_DELETE_EYE_FRAMES ? 2
                                                                      : 1;
        if (!testing_) {
            QMessageBox confirm(QMessageBox::NoIcon,
                                language_.Text(QString("dialog.morph_purge.title.%1").arg(panel)),
                                language_.Text(QString("dialog.morph_purge.text.%1").arg(panel)),
                                QMessageBox::Ok | QMessageBox::Cancel, this);
            confirm.setWindowFlag(Qt::WindowStaysOnTopHint);
            if (confirm.exec() != QMessageBox::Ok)
                return;
        }
        std::string reason;
        Result(session_.PurgeMorphKeys(panel, reason), reason);
        Refresh();
        return;
    }
    if (command == MDS_CMD_TOGGLE_INFORMATION)
        control = panel::kInfoCheckbox;
    if (command == MDS_CMD_TOGGLE_COORDINATE_AXIS)
        control = panel::kCoordAxisCheckbox;
    if (command == MDS_CMD_TOGGLE_CAMERA_TRACKING)
        control = panel::kFollowCameraCheckbox;
    if (control) {
        static_cast<QAbstractButton*>(Control(control))->click();
        return;
    }
    using namespace editor;
    const auto select = [&](Track kind, std::uint8_t slot) {
        const auto scope = kind == Track::Bone    ? KeyScopeKind::Bones
                           : kind == Track::Morph ? KeyScopeKind::Morphs
                                                  : KeyScopeKind::Track;
        session_.SelectKeys(0, std::numeric_limits<std::uint32_t>::max(), {scope, {kind, slot}});
    };
    switch (command) {
    case MDS_CMD_SELECT_ALL_BONE_FRAMES:
        select(Track::Bone, session_.Selected().model);
        break;
    case MDS_CMD_SELECT_ALL_MORPH_FRAMES:
        select(Track::Morph, session_.Selected().model);
        break;
    case MDS_CMD_SELECT_ALL_DISPLAY_FRAMES:
        select(Track::Display, session_.Selected().model);
        break;
    case MDS_CMD_SELECT_ALL_CAMERA_FRAMES:
        select(Track::Camera, 255);
        break;
    case MDS_CMD_SELECT_ALL_LIGHT_FRAMES:
        select(Track::Light, 255);
        break;
    case MDS_CMD_SELECT_ALL_SELF_SHADOW_FRAMES:
        select(Track::Shadow, 255);
        break;
    case MDS_CMD_SELECT_ALL_GRAVITY_FRAMES:
        select(Track::Gravity, 255);
        break;
    case MDS_CMD_SELECT_ALL_ACCESSORY_FRAMES:
        for (const auto& accessory : session_.Document().accessories)
            select(Track::Accessory, accessory.slot);
        break;
    case MDS_CMD_TOGGLE_SELF_SHADOW:
        session_.SetSelfShadowEnabled(!session_.Document().selfShadowEnabled);
        break;
    default: {
        std::string reason;
        Result(session_.Edit(
                   "menu-setting",
                   [&](auto& doc, auto&) {
                       if (PhysicsMode(command) >= 0)
                           doc.physics.playbackMode = std::uint8_t(PhysicsMode(command));
                       else if (command == MDS_CMD_TOGGLE_GROUND_SHADOW)
                           doc.view.groundShadow = !doc.view.groundShadow;
                       else if (command == MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY)
                           doc.physics.projectedShadowBlend = !doc.physics.projectedShadowBlend;
                       else
                           return false;
                       return true;
                   },
                   reason),
               reason);
        break;
    }
    }
    Refresh();
}
}
