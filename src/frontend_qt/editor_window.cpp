#include "editor_window.hpp"
#include "editor_canvas.hpp"
#include "combo_items.hpp"
#include "numeric_dialog.hpp"
#include "english_name_dialog.hpp"
#include "model_physics_dialog.hpp"
#include "preferences_dialog.hpp"
#include "effect_dialog.hpp"
#include <QStatusBar>
#include "model_labels.hpp"
#include "editor_font.hpp"
#include "editor_style.hpp"
#include "mikudancestudio/idle_render_gate.hpp"
#include "mikudancestudio/editor_rotation.hpp"
#include "mikudancestudio/mmd_control_specs.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <cmath>

namespace mikudancestudio::qt {
using namespace editor;
using namespace panel;
std::filesystem::path Path(const QString& text) {
    return std::filesystem::u8path(text.toUtf8().toStdString());
}
QString Path(const std::filesystem::path& path) {
    return QString::fromUtf8(path.u8string().c_str());
}
namespace {
QString Name(const std::vector<char>& bytes) {
    std::string name;
    text::DecodeCp932({bytes.data(), bytes.size()}, name);
    return QString::fromUtf8(name.c_str());
}

}
EditorWindow::EditorWindow(std::filesystem::path assets, bool testing,
                           std::unique_ptr<AudioOutput> audio)
    : assets_(std::move(assets)), testing_(testing), audio_(std::move(audio), testing) {
    InstallEditorStyle(*qApp);
    setObjectName("mmd.editor");
    setAcceptDrops(true);
    installEventFilter(this);
#ifdef __ANDROID__
    setMinimumSize(320, 160);
#else
    setMinimumSize(640, 480);
#endif
    resize(1024, 768);
#ifdef __ANDROID__
    // Android owns the Activity bounds. Follow its available geometry on
    // keyboard resizing and rotation instead of retaining a desktop window size.
    setWindowState(Qt::WindowMaximized);
#endif
    menuBar()->setNativeMenuBar(false);
    setFont(EditorFont(language_.Code()));
    // The original menu retains the system non-client font, independently
    // of the language-specific font applied to panel controls.
    menuBar()->setFont(SystemMenuFont());
    canvas_ = new Canvas(language_, session_, assets_, this);
    canvas_->setAcceptDrops(true);
    canvas_->installEventFilter(this);
    setCentralWidget(canvas_);
    timeline_ = new Timeline(session_, language_, assets_, canvas_);
    timeline_->installEventFilter(this);
    timeline_->changed = [this] { Refresh(); };
    timeline_->seek = [this](unsigned frame, std::string& reason) {
        return SeekFrame(frame, reason);
    };
    interpolation_ = new Interpolation(session_, canvas_);
    interpolation_->installEventFilter(this);
    interpolation_->changed = [this](bool ok, const std::string& reason) {
        Result(ok, reason);
        Refresh();
    };
    for (const auto& spec : ui::kControls) {
        QWidget* widget = nullptr;
        const auto cls =
            spec.wcls ? QString::fromWCharArray(spec.wcls) : QString::fromLatin1(spec.acls);
        if (cls == "BUTTON") {
            const auto style = spec.style & 15;
            if ((spec.style & 0x1000u) && (style == 2 || style == 3 || style == 4 || style == 9)) {
                // BS_PUSHLIKE is still a toggle, painted as a push button.
                auto* button = new QPushButton(canvas_);
                button->setCheckable(true);
                widget = button;
            } else if (style == 2 || style == 3)
                widget = new QCheckBox(canvas_);
            else if (style == 4 || style == 9) {
                auto* b = new QRadioButton(canvas_);
                b->setAutoExclusive(false);
                widget = b;
            } else
                widget = new QPushButton(canvas_);
            connect(static_cast<QAbstractButton*>(widget), &QAbstractButton::clicked, this,
                    [this, id = spec.id] { Dispatch(id); });
        } else if (cls == "EDIT") {
            auto* edit = new QLineEdit(canvas_);
            edit->setFrame((spec.style & 0x00800000u) != 0);
            edit->setProperty("mmdNativeWindowBorder",
                              (spec.style & 0x00800000u) && !(spec.ex & 0x00000200u));
            edit->setAlignment(Qt::AlignCenter);
            widget = edit;
            connect(edit, &QLineEdit::editingFinished, this, [this, edit, id = spec.id] {
                // Focus changes/reparenting also finish untouched fields. Never
                // write rounded display text back into the live scene.
                if (!refreshing_ && edit->isModified()) {
                    edit->setModified(false);
                    EditField(id);
                }
            });
        } else if (cls == "COMBOBOX") {
            auto* combo = new QComboBox(canvas_);
            widget = combo;
            connect(combo, qOverload<int>(&QComboBox::activated), this,
                    [this, id = spec.id](int i) { Combo(id, i); });
        } else if (cls.contains("SCROLLBAR", Qt::CaseInsensitive)) {
            auto* scroll = new QScrollBar(
                spec.id == kTimelineVScroll ? Qt::Vertical : Qt::Horizontal, canvas_);
            widget = scroll;
            connect(scroll, &QScrollBar::valueChanged, this,
                    [this, id = spec.id](int v) { Slider(id, v); });
        } else {
            auto* slider =
                new QSlider(spec.id == kFrameVolumeSlider ? Qt::Vertical : Qt::Horizontal, canvas_);
            widget = slider;
            slider->setProperty("mmdNativeTrackbar", true);
            slider->setTickPosition(spec.style & 0x10u ? QSlider::NoTicks : QSlider::TicksBelow);
            // Original WM_CREATE suppresses intermediate ticks, retaining endpoints.
            slider->setTickInterval(1000);
            slider->setRange(0, 100);
            connect(slider, &QSlider::valueChanged, this, [this, id = spec.id](int v) {
                if (!refreshing_)
                    Slider(id, v);
            });
        }
        widget->setObjectName(QString("control.%1").arg(spec.id));
        widget->setProperty("mmdControlId", spec.id);
        widget->installEventFilter(this);
        controls_[spec.id] = widget;
    }
    rhi::BackendOptions options;
    options.backend = rhi::DefaultBackend();
    options.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    CreateViewport(options);
    if (!testing_) {
        QSettings settings;
        lowPower_ = settings.value("editor/lowPower", false).toBool();
        session_.SetMipmaps(settings.value("renderer/mipmaps", true).toBool());
        session_.SetAntialiasing(settings.value("renderer/antialiasing", true).toBool());
        session_.SetWireframe(settings.value("renderer/wireframe", false).toBool());
        fileHistory_.enabled = settings.value("files/rememberFolders", true).toBool();
        fileHistory_.Restore(settings.value("files/directories").toStringList());
        language_.Set(settings.value("language", language_.Selection()).toString());
        separateGeometry_ = settings.value("viewport/separateGeometry").toByteArray();
        separateSize_ = settings.value("viewport/separateSize", separateSize_).toSize();
        if (!separateSize_.isValid())
            separateSize_ = QSize(800, 657);
        separateOnTop_ = settings.value("viewport/alwaysOnTop", false).toBool();
    }
    BuildMenus();
    Texts();
    Refresh();
    if (!testing_ && QSettings().value("viewport/separate", false).toBool())
        SetSeparateWindow(true);
    timer_.setParent(this);
    timer_.setObjectName("editor.frameTimer");
    timer_.setTimerType(Qt::PreciseTimer);
    RefreshFrameTimer();
    connect(&timer_, &QChronoTimer::timeout, this, [this] {
        const auto nanoseconds = clock_.nsecsElapsed();
        clock_.restart();
        Tick(double(nanoseconds) / 1000000000.);
    });
    clock_.start();
    if (!testing_)
        timer_.start();
    TranslateStandardDialogs();
}
void EditorWindow::Tick(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        return;
    AdvanceIdleRenderGate(idleRenderState_, session_.Playing(), lowPower_,
                          (englishDialog_ && englishDialog_->Active()) ||
                              (physicsDialog_ && physicsDialog_->Active()));
    if (!idleRenderState_)
        return;
    if (session_.Playing()) {
        const auto audioError = audio_.Error();
        if (!audioError.empty()) {
            session_.Pause();
            audio_.Stop();
            Result(false, audioError);
            Refresh();
            return;
        }
        if (const auto position = audio_.ClockPosition())
            seconds = std::max(0., *position - session_.CursorSeconds());
        std::string reason;
        const auto epoch = session_.PlaybackEpoch();
        if (seconds > 0 && !Result(session_.Advance(std::min(1., seconds), reason), reason))
            session_.Pause();
        if (session_.Playing() && session_.PlaybackEpoch() != epoch &&
            session_.Document().wave.enabled && !session_.Assets().wave.empty()) {
            if (session_.PlaybackAudioDisabled())
                audio_.ReleaseClock();
            else if (!Result(audio_.Play(session_.CursorSeconds(), reason), reason))
                session_.Pause();
        }
        Refresh();
    } else {
        std::string reason;
        if (!session_.TickIdle(std::min(1., seconds), reason)) {
            // As with a failed render, retain the diagnostic without reopening
            // a modal dialog on every timer tick of a failed runtime.
            lastError_ = QString::fromUtf8(reason.c_str());
            return;
        }
        UpdateFrame();
    }
}
EditorWindow::~EditorWindow() {
    timer_.stop();
    audio_.Stop();
    delete edgeDialog_.data();
    delete groundShadowDialog_.data();
    delete gravityDialog_.data();
    if (englishDialog_)
        disconnect(englishDialog_, nullptr, this, nullptr);
    delete englishDialog_.data();
    if (physicsDialog_)
        disconnect(physicsDialog_, nullptr, this, nullptr);
    delete physicsDialog_.data();
    delete effectDialog_.data();
    if (!testing_) {
        QSettings settings;
        settings.setValue("editor/lowPower", lowPower_);
        settings.setValue("renderer/mipmaps", session_.DisplayOptions().mipmaps);
        settings.setValue("renderer/antialiasing", session_.DisplayOptions().antialiasing);
        settings.setValue("renderer/wireframe", session_.DisplayOptions().wireframe);
        settings.setValue("files/rememberFolders", fileHistory_.enabled);
        settings.setValue("files/directories", fileHistory_.Save());
        settings.setValue("viewport/separate", separate_ != nullptr || restoreSeparate_);
        settings.setValue("viewport/alwaysOnTop", separateOnTop_);
        settings.setValue("viewport/separateGeometry",
                          separate_ ? separate_->saveGeometry() : separateGeometry_);
        settings.setValue("viewport/separateSize", separate_ && !separate_->isMaximized()
                                                       ? separate_->size()
                                                       : separateSize_);
    }
    if (viewportContainer_)
        delete viewportContainer_;
    viewport_ = nullptr;
    viewportContainer_ = nullptr;
    delete separate_;
    separate_ = nullptr;
}
QWidget* EditorWindow::Control(int id) const {
    auto it = controls_.find(id);
    return it == controls_.end() ? nullptr : it->second;
}
bool EditorWindow::Result(bool ok, const std::string& reason) {
    if (ok) {
        lastError_.clear();
        return true;
    }
    lastError_ = QString::fromUtf8(reason.c_str());
    if (!testing_) {
        if (reason == "gravity-key-capacity") {
            QMessageBox message(QMessageBox::NoIcon, language_.Text("dialog.key_capacity.title"),
                                language_.Text("dialog.key_capacity.message").arg(10000),
                                QMessageBox::Ok, this);
            message.exec();
        } else
            QMessageBox::warning(this, language_.Text("dialog.error"), lastError_);
    }
    return false;
}
bool EditorWindow::SetLanguage(const QString& language) {
    if (!language_.Set(language))
        return false;
    TranslateStandardDialogs();
    if (!testing_) {
        QSettings settings;
        settings.setValue("language", language_.Selection());
    }
    RetranslateMenus();
    Texts();
    Refresh();
    if (preferencesDialog_) {
        preferencesDialog_->Retranslate();
        preferencesDialog_->Reset();
    }
    if (effectDialog_)
        effectDialog_->Retranslate();
    return true;
}
void EditorWindow::Effects() {
    if (!effectDialog_)
        effectDialog_ = new EffectDialog(
            session_, language_,
            [this](auto address) { return viewport_->EffectDefinition(address); },
            [this] { return viewport_->EffectReport(); }, [this] { Refresh(); }, this);
    effectDialog_->Refresh();
    effectDialog_->show();
    effectDialog_->raise();
}
void EditorWindow::TranslateStandardDialogs() {
    language_.Activate();
}
void EditorWindow::Preferences() {
    if (!preferencesDialog_)
        preferencesDialog_ = new PreferencesDialog(
            language_, [this](const QString& code) { return SetLanguage(code); }, this);
    preferencesDialog_->Reset();
    if (!testing_ && isVisible()) {
        preferencesDialog_->show();
        preferencesDialog_->raise();
        preferencesDialog_->activateWindow();
    }
}
void EditorWindow::Texts() {
    setFont(EditorFont(language_.Code()));
    for (const int id :
         {panel::kCurrentFrameEdit, panel::kPreviousFrameButton, panel::kNextFrameButton,
          panel::kNextKeyframeButton, panel::kPreviousKeyframeButton, panel::kFirstFrameButton,
          panel::kLastFrameButton})
        if (auto* control = Control(id))
            control->setFont(FrameNavigationFont());
    for (const auto& pair : controls_)
        if (auto* button = qobject_cast<QAbstractButton*>(pair.second)) {
            button->setText(language_.Text(QString("control.%1").arg(pair.first)));
            button->setProperty("mmdAdaptiveCaption", language_.AdaptiveCaptions());
            button->setToolTip(language_.AdaptiveCaptions() ? button->text() : QString());
        }
}
void EditorWindow::RefreshFrameTimer() {
    const double requested = session_.Document().view.fpsLimit;
    const double rate =
        std::isfinite(requested) && requested >= 1 && requested <= 1000 ? requested : 60.;
    const auto interval = std::chrono::nanoseconds(qint64(1000000000. / rate));
    if (timer_.interval() != interval)
        timer_.setInterval(interval);
}
void EditorWindow::Refresh() {
    idleRenderState_ = 1;
    refreshing_ = true;
    RefreshFrameTimer();
    if (!session_.Playing() && (!audio_.Previewing() || audio_.Path() != session_.Assets().wave ||
                                !session_.Document().wave.enabled))
        audio_.Stop();
    const auto& doc = session_.Document();
    if (physicsDialog_ && !physicsDialog_->Valid())
        physicsDialog_->Invalidate();
    if (englishDialog_ && !englishDialog_->Valid())
        englishDialog_->Invalidate();
    if (edgeDialog_) {
        const auto slot = session_.Selected().model;
        const auto resource = session_.Runtime().Resource(slot);
        if (slot == 255 || !resource)
            edgeDialog_->close();
        else if (slot != edgeModel_ || resource != edgeResource_.lock()) {
            // Native model-combo refresh replaces pending text for the new
            // model. Camera mode destroys this modeless window instead.
            for (const auto& model : doc.models)
                if (model.slot == slot)
                    edgeDialog_->Reset(model.edgeScale);
            edgeModel_ = slot;
            edgeResource_ = resource;
        }
    }
    if (gravityDialog_ && gravityEpoch_ != session_.GravityEpoch()) {
        gravityDialog_->Mirror([this] { return session_.Runtime().Session()->Pose().gravity; });
        gravityEpoch_ = session_.GravityEpoch();
    }
    setWindowTitle(
        language_.Text("app.title") + " - " +
        (session_.Path().empty() ? QString("untitled.pmm") : Path(session_.Path().filename())) +
        (session_.Dirty() ? " *" : ""));
    const auto edit = [&](int id, auto value) {
        if (auto* e = qobject_cast<QLineEdit*>(Control(id)))
            if (!e->hasFocus())
                e->setText(QString::number(value));
    };
    const auto check = [&](int id, bool value) {
        if (auto* c = qobject_cast<QAbstractButton*>(Control(id)))
            c->setChecked(value);
    };
    Control(kUndoButton)->setEnabled(session_.CanUndo());
    Control(kRedoButton)->setEnabled(session_.CanRedo());
    Control(kCopyFramesButton)->setEnabled(!session_.Playing());
    Control(kPasteButton)->setEnabled(session_.CanPasteFrames());
    Control(kReversePasteButton)->setEnabled(session_.CanPasteFrames(true));
    actions_["undo"]->setEnabled(session_.CanUndo());
    actions_["redo"]->setEnabled(session_.CanRedo());
    const bool posePaste = session_.Selected().model != 255 && session_.HasBonePoseClipboard();
    Control(kBonePasteButton)->setEnabled(posePaste);
    Control(kBoneReversePasteButton)->setEnabled(posePaste);
    edit(kCurrentFrameEdit, doc.timeline.currentFrame);
    edit(kGotoFrameEdit, doc.timeline.currentFrame);
    edit(kPlayStartFrameEdit, doc.timeline.playStartFrame);
    edit(kPlayStopFrameEdit, doc.timeline.playStopFrame);
    check(kPlaybackLoopCheckbox, doc.timeline.playbackLoop);
    check(kPlaybackReturnCheckbox, doc.timeline.returnToStart);
    check(kPhysicsFrameCheckbox, session_.AutoInterpolation());
    check(kPhysicsCheckbox, session_.RegistrationPhysics());
    check(kPlaybackFromCurrentCheckbox, doc.timeline.startAtCurrent);
    Control(kPlayButton)->setEnabled(!(physicsDialog_ && physicsDialog_->Active()));
    if (auto* b = qobject_cast<QAbstractButton*>(Control(kPlayButton)))
        b->setText(language_.Text(session_.Playing() ? "stop" : "control.408"));
    auto* models = qobject_cast<QComboBox*>(Control(kMainComboModel));
    SyncComboItems(models, [&](auto add) {
        add(language_.Text("camera_mode"), 255);
        for (auto slot : format::PmmModelOrder(doc, format::ModelOrderKind::Draw))
            add(ModelLabel(slot), slot);
    });
    models->setCurrentIndex(models->findData(session_.Selected().model));
    const auto camera = session_.Runtime().Session()->Pose().camera;
    edit(kFovEdit, camera.fov);
    check(kPerspectiveCheckbox, !camera.perspective);
    if (auto* s = qobject_cast<QSlider*>(Control(kFovSlider))) {
        s->setRange(1, 125);
        s->setValue(int(camera.fov));
    }
    const int pos[]{kReadoutPosXEdit, kReadoutPosYEdit, kReadoutPosZEdit};
    const int rot[]{kReadoutRotXEdit, kReadoutRotYEdit, kReadoutRotZEdit};
    const auto readout = [&](int id, float value, bool angle = false) {
        if (auto* field = qobject_cast<QLineEdit*>(Control(id)); field && !field->hasFocus())
            field->setText(QString::asprintf(angle ? "%1.1f" : "%1.2f", double(value)));
    };
    for (int i = 0; i < 3; ++i) {
        readout(pos[i],
                i == 2 && camera.parentModel >= 0 ? -camera.position[i] : camera.position[i]);
        float degrees = float(camera.rotation[i] / (i == 0 ? -3.141592f : 3.141592f)) * 180.f;
        if (i == 0 && std::abs(degrees) < 1e-7f)
            degrees = 0;
        readout(rot[i], degrees, true);
    }
    float displayedDistance = camera.parentModel < 0 ? -camera.distance : 0;
    if (std::abs(displayedDistance) < 1e-7f)
        displayedDistance = 0;
    readout(kReadoutDistEdit, displayedDistance);
    if (session_.Selected().model != 255)
        for (int id = kReadoutPosXEdit; id <= kReadoutDistEdit; ++id)
            if (auto* e = qobject_cast<QLineEdit*>(Control(id)); e && !e->hasFocus())
                e->setText("-----");
    for (const auto& m : doc.models)
        if (m.slot == session_.Selected().model) {
            check(kModelVisibleCheckbox, m.selection.loadComplete);
            check(kShadowCheckbox, m.toonFlag != 0);
            check(kAddBlendCheckbox, m.postLoadFlag != 0);
            for (int panel = 0; panel < 4; ++panel) {
                const int combos[]{kMorphCombo0, kMorphCombo1, kMorphCombo2, kMorphCombo3};
                const int edits[]{kMorphEdit0, kMorphEdit1, kMorphEdit2, kMorphEdit3};
                const int sliders[]{kMorphSlider0, kMorphSlider1, kMorphSlider2, kMorphSlider3};
                auto* c = qobject_cast<QComboBox*>(Control(combos[panel]));
                const auto old = c->currentData();
                const auto resource = session_.Runtime().Resource(m.slot);
                SyncComboItems(c, [&](auto add) {
                    for (std::size_t i = 0; i < resource->Document().morphs.size(); ++i)
                        if (resource->Document().morphs[i].header.panel == panel + 1) {
                            int channel;
                            std::string reason;
                            if (session_.Runtime().ResourceMorphIndex(m.slot, int(i), channel,
                                                                      reason))
                                add(ModelTrackLabel(session_, m.slot, unsigned(i), true,
                                                    language_.EnglishLayout()),
                                    unsigned(channel));
                        }
                });
                int index = c->findData(old);
                c->setCurrentIndex(index < 0 ? 0 : index);
                const auto morph = c->currentData().toUInt();
                const float value = c->count() ? m.morphPose.at(morph) : 0;
                edit(edits[panel], value);
                static_cast<QSlider*>(Control(sliders[panel]))->setValue(qRound(value * 100));
            }
            if (session_.ActiveBone() >= 0 &&
                std::size_t(session_.ActiveBone()) < m.bonePose.size()) {
                const auto& p = m.bonePose[std::size_t(session_.ActiveBone())];
                const auto degrees = BoneRotationDegrees(
                    {p.components[3], p.components[4], p.components[5], p.components[6]});
                for (int i = 0; i < 3; ++i)
                    readout(pos[i], p.components[i]);
                for (int i = 0; i < 3; ++i)
                    readout(rot[i], std::abs(degrees[i]) < 1e-7f ? 0 : degrees[i], true);
            }
        }
    RefreshMenus();
    auto* accessories = qobject_cast<QComboBox*>(Control(kAccessoryCombo));
    SyncComboItems(accessories, [&](auto add) {
        for (const auto& a : format::PmmAccessoryOrder(doc)) {
            std::vector<char> name(a.name.begin(), std::find(a.name.begin(), a.name.end(), 0));
            add(Name(name), a.slot);
        }
    });
    if (session_.Selected().accessory == 255 && !doc.accessories.empty())
        session_.Selected().accessory = doc.accessories[0].slot;
    accessories->setCurrentIndex(accessories->findData(session_.Selected().accessory));
    RefreshPanels();
    RefreshScopes();
    timeline_->Refresh();
    auto* curves = static_cast<QComboBox*>(Control(kInterpCurveCombo));
    const unsigned curveCount = session_.Selected().model == 255 ? 6 : 4;
    const int curveChannel = curves->count() == int(curveCount + 1) ? curves->currentIndex() : 3;
    SyncComboItems(curves, [&](auto add) {
        for (unsigned i = 0; i < curveCount; ++i)
            add(language_.Text(QString("interpolation.%1").arg(i)));
        add(language_.Text("interpolation.all"));
    });
    curves->setCurrentIndex(curveChannel);
    interpolation_->SetChannel(unsigned(curveChannel));
    const auto selectedCurves = session_.Interpolation(unsigned(curveChannel));
    Control(kCurveCopyButton)->setEnabled(selectedCurves.canCopy);
    Control(kCurvePasteButton)
        ->setEnabled(selectedCurves.found && session_.HasInterpolationClipboard() &&
                     !session_.Playing());
    Control(kCurveLinearButton)->setEnabled(selectedCurves.found && !session_.Playing());
    auto* hs = qobject_cast<QScrollBar*>(Control(kTimelineHScroll));
    hs->setRange(0, std::max(doc.appearance.maxFrame, doc.timeline.currentFrame) + 100);
    hs->setPageStep(std::max(1, (side_ - 100) / 13));
    auto* vs = qobject_cast<QScrollBar*>(Control(kTimelineVScroll));
    vs->setRange(0, std::max(0, timeline_->RowCount() - 1));
    Layout();
    refreshing_ = false;
    UpdateFrame();
}
void EditorWindow::UpdateFrame() {
    if (!testing_ && !windowTransition_) {
        std::string reason;
        if (!viewport_->Render(reason)) {
            lastError_ = QString::fromStdString(reason);
            statusBar()->showMessage(lastError_);
            statusBar()->show();
        } else if (auto* bar = findChild<QStatusBar*>()) {
            bar->clearMessage();
            bar->hide();
        }
        RefreshMenus();
    }
}
bool EditorWindow::SeekFrame(unsigned frame, std::string& reason) {
    if (!session_.Seek(frame, reason))
        return false;
    if (!session_.Playing() && session_.FrameAudioPreview() && !session_.PlaybackAudioDisabled() &&
        session_.Document().wave.enabled && !session_.Assets().wave.empty()) {
        if (audio_.Path() != session_.Assets().wave && !audio_.Load(session_.Assets().wave, reason))
            return false;
        return audio_.PreviewFrame(frame, reason);
    }
    return true;
}

}
