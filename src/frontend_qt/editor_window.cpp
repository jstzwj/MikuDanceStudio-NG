#include "editor_window.hpp"
#include "export.hpp"
#include "mikudancestudio/mmd_control_specs.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/editor_rotation.hpp"
#include "mikudancestudio/mmd_editor_labels.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QScrollBar>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QPainter>
#include <QSettings>
#include <QProgressDialog>
#include <cmath>
#include <fstream>

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
bool AtomicSave(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
                std::string& error) {
    QSaveFile file(Path(path));
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) !=
            qint64(bytes.size()) ||
        !file.commit()) {
        error = file.errorString().toStdString();
        return false;
    }
    return true;
}
QString Name(const std::vector<char>& bytes) {
    std::string name;
    text::DecodeCp932({bytes.data(), bytes.size()}, name);
    return QString::fromUtf8(name.c_str());
}
class Canvas final : public QWidget {
public:
    Canvas(Language& language, editor::EditorSession& session, QWidget* p)
        : QWidget(p), language_(language), session_(session) {
        setAutoFillBackground(true);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const auto color = [](std::uint32_t value) {
            return QColor(value & 255, (value >> 8) & 255, (value >> 16) & 255);
        };
        const auto fill = [&](const ui::PanelFill& f) {
            const auto a = color(f.color), b = color(f.color2);
            QLinearGradient gradient(f.left, f.top, f.vertical ? f.left : f.right,
                                     f.vertical ? f.bottom : f.top);
            gradient.setColorAt(0, a);
            gradient.setColorAt(1, b);
            p.fillRect(QRect(f.left, f.top, f.right - f.left, f.bottom - f.top), gradient);
            p.setPen(Qt::black);
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRect(f.left, f.top, f.right - f.left, f.bottom - f.top));
        };
        const auto& settings = session_.Document().envelope.settings;
        const int side = std::max(250, settings.editPanelWidth), bottom = height();
        const auto& o = settings.options;
        const ui::EditorLayoutOptions options{session_.Selected().model == 255,
                                              bool(o[1]),
                                              bool(o[2]),
                                              bool(o[3]),
                                              bool(o[4]),
                                              bool(o[5]),
                                              bool(o[6])};
        const auto viewport =
            ui::MmdViewport(width(), height(), side, settings.renderWidth, settings.renderHeight);
        p.fillRect(rect(), color(ui::kDefaultThemeColors[0]));
        p.fillRect(side + 9, 0, width() - side - 9, height() - 160, Qt::black);
        fill({0, bottom - 160, width(), bottom, ui::kDefaultThemeColors[0],
              ui::kDefaultThemeColors[1], true});
        for (const auto& f : ui::MmdPanelFills(side, bottom, options))
            fill(f);
        p.fillRect(side, 0, 6, bottom - 159, QColor(220, 220, 220));
        fill({side + 9, viewport.y - 25, width(), viewport.y - 1, 0x969696, 0x646464, true});
        fill({side + 9, viewport.y + viewport.height, width(), viewport.y + viewport.height + 32,
              0x787878, 0xc8c8c8, true});
        p.setPen(Qt::white);
        for (const auto& label :
             ui::MmdPanelLabels(side, bottom, options, language_.Code() == "en")) {
            QFont font = this->font();
            font.setPixelSize(label.size);
            p.setFont(font);
            p.drawText(QRect(label.x, label.y, 250, label.size + 3), Qt::AlignTop | Qt::AlignLeft,
                       language_.Text(label.id));
        }
    }

private:
    Language& language_;
    editor::EditorSession& session_;
};
std::array<float, 4> Quaternion(float x, float y, float z) {
    return BoneRotationFromDegrees({x, y, z});
}
}
EditorWindow::EditorWindow(std::filesystem::path assets, bool testing)
    : assets_(std::move(assets)), testing_(testing) {
    setObjectName("mmd.editor");
    setMinimumSize(640, 480);
    resize(1024, 768);
    menuBar()->setNativeMenuBar(false);
    QFont font;
    font.setFamilies({"MS UI Gothic", "Noto Sans CJK SC", "Microsoft YaHei", "Hiragino Sans",
                      "PingFang SC", "DejaVu Sans"});
    font.setPixelSize(12);
    setFont(font);
    canvas_ = new Canvas(language_, session_, this);
    setCentralWidget(canvas_);
    timeline_ = new Timeline(session_, language_, canvas_);
    timeline_->changed = [this] { Refresh(); };
    for (const auto& spec : ui::kControls) {
        QWidget* widget = nullptr;
        const auto cls =
            spec.wcls ? QString::fromWCharArray(spec.wcls) : QString::fromLatin1(spec.acls);
        if (cls == "BUTTON") {
            const auto style = spec.style & 15;
            if (style == 2 || style == 3)
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
            edit->setAlignment(Qt::AlignCenter);
            widget = edit;
            connect(edit, &QLineEdit::editingFinished, this, [this, id = spec.id] {
                if (!refreshing_)
                    EditField(id);
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
    options.backend =
        rhi::BackendAvailable(rhi::Backend::Vulkan) ? rhi::Backend::Vulkan : rhi::Backend::D3D11;
    options.vulkanLibrary = qEnvironmentVariable("MDS_TEST_VULKAN_LIBRARY").toStdString();
    if (qEnvironmentVariable("MDS_GPU_DRIVER") == "software")
        options.driver = rhi::BackendDriver::Software;
    CreateViewport(options);
    if (!testing_) {
        QSettings settings;
        language_.Set(settings.value("language", language_.Code()).toString());
    }
    BuildMenus();
    Texts();
    Refresh();
    connect(&timer_, &QTimer::timeout, this, [this] {
        auto seconds = clock_.restart() / 1000.;
        if (audio_.Running())
            seconds = std::max(0., audio_.Position() -
                                       double(session_.Runtime().Simulation().cursorSeconds));
        if (session_.Playing()) {
            std::string reason;
            if (!Result(session_.Advance(std::min(1., seconds), reason), reason))
                session_.Pause();
            Refresh();
        } else
            UpdateFrame();
    });
    clock_.start();
    if (!testing_)
        timer_.start(16);
}
EditorWindow::~EditorWindow() {
    timer_.stop();
    audio_.Stop();
    if (viewportContainer_)
        delete viewportContainer_;
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
    if (!testing_)
        QMessageBox::warning(this, language_.Text("dialog.error"), lastError_);
    return false;
}
void EditorWindow::CreateViewport(const rhi::BackendOptions& options) {
    // A different API receives a fresh native window. CPU state remains owned
    // by EditorSession; retiring the old controller never evaluates a frame.
    auto* next = new Viewport(session_, assets_);
    next->SetOptions(options);
    auto* container = QWidget::createWindowContainer(next, canvas_);
    auto* old = viewportContainer_;
    viewport_ = next;
    viewportContainer_ = container;
    if (old)
        delete old;
    viewport_->zoom = [this](int delta) {
        auto camera = session_.Runtime().Session()->Pose().camera;
        if (session_.Selected().model == 255 && camera.parentModel >= 0)
            camera.position[2] = float(delta) * .05f + camera.position[2];
        else
            camera.distance = float(delta) * .05f + camera.distance;
        std::string r;
        Result(session_.SetCamera(camera, r), r);
        Refresh();
    };
    viewport_->drag = [this](QPoint delta, Qt::MouseButtons buttons,
                             Qt::KeyboardModifiers modifiers) {
        if (!(buttons & Qt::RightButton) && !(buttons & Qt::MiddleButton))
            return;
        auto camera = session_.Runtime().Session()->Pose().camera;
        if ((buttons & Qt::MiddleButton) || (modifiers & Qt::ShiftModifier)) {
            camera.position[0] -= delta.x() * .1f;
            camera.position[1] += delta.y() * .1f;
        } else {
            camera.rotation[0] += delta.y() * .01f;
            camera.rotation[1] += delta.x() * .01f;
        }
        std::string r;
        Result(session_.SetCamera(camera, r), r);
        Refresh();
    };
    Layout();
}
void EditorWindow::BuildMenus() {
    menuBar()->clear();
    actions_.clear();
    const auto action = [&](QMenu* menu, const QString& id, const QKeySequence& shortcut = {}) {
        auto* a = menu->addAction(language_.Text(id));
        a->setObjectName(id);
        a->setShortcut(shortcut);
        actions_[id] = a;
        connect(a, &QAction::triggered, this, [this, id] { FileAction(id); });
        return a;
    };
    auto* file = menuBar()->addMenu(language_.Text("menu.file"));
    action(file, "file.new", QKeySequence::New);
    action(file, "file.open", QKeySequence::Open);
    action(file, "file.save", QKeySequence::Save);
    action(file, "file.save_as", QKeySequence::SaveAs);
    file->addSeparator();
    action(file, "file.image");
    action(file, "file.sequence");
    file->addSeparator();
    action(file, "file.pose_open");
    action(file, "file.pose_save");
    action(file, "file.motion_open");
    action(file, "file.motion_save");
    file->addSeparator();
    action(file, "file.wave");
    file->addSeparator();
    action(file, "file.exit");
    auto* edit = menuBar()->addMenu(language_.Text("menu.edit"));
    action(edit, "edit.numeric");
    auto* undo = edit->addAction(language_.Text("control.400"));
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, this, [this] { Dispatch(kUndoButton); });
    actions_["undo"] = undo;
    auto* redo = edit->addAction(language_.Text("control.401"));
    redo->setShortcut(QKeySequence::Redo);
    connect(redo, &QAction::triggered, this, [this] { Dispatch(kRedoButton); });
    actions_["redo"] = redo;
    auto* physics = menuBar()->addMenu(language_.Text("menu.physics"));
    for (unsigned i = 0; i < 4; ++i) {
        const char* names[]{"physics.off", "physics.always", "physics.play", "physics.trace"};
        auto* a = physics->addAction(language_.Text(names[i]));
        a->setCheckable(true);
        actions_[QString("physics.%1").arg(i)] = a;
        connect(a, &QAction::triggered, this, [this, i] {
            std::string r;
            Result(session_.Edit(
                       "physics-mode",
                       [&](auto& d, auto&) {
                           d.physics.playbackMode = std::uint8_t(i);
                           return true;
                       },
                       r),
                   r);
            Refresh();
        });
    }
    auto* view = menuBar()->addMenu(language_.Text("menu.view"));
    auto* renderer = view->addMenu(language_.Text("menu.renderer"));
    for (auto pair : {std::pair{rhi::Backend::D3D11, "DirectX 11"},
                      {rhi::Backend::D3D12, "DirectX 12"},
                      {rhi::Backend::Vulkan, "Vulkan"}}) {
        auto* a = renderer->addAction(pair.second);
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
    auto* language = view->addMenu(language_.Text("menu.language"));
    for (const char* code : {"en", "ja", "zh_CN"}) {
        auto* a = language->addAction(language_.Text(QString("language.") + code));
        a->setCheckable(true);
        a->setChecked(language_.Code() == code);
        connect(a, &QAction::triggered, this, [this, code] { SetLanguage(code); });
    }
}
bool EditorWindow::SetLanguage(const QString& language) {
    if (!language_.Set(language))
        return false;
    if (!testing_) {
        QSettings settings;
        settings.setValue("language", language);
    }
    BuildMenus();
    Texts();
    Refresh();
    return true;
}
void EditorWindow::Texts() {
    for (const auto& pair : controls_)
        if (auto* button = qobject_cast<QAbstractButton*>(pair.second))
            button->setText(language_.Text(QString("control.%1").arg(pair.first)));
}
void EditorWindow::Layout() {
    if (!canvas_)
        return;
    side_ = std::max(250, session_.Document().envelope.settings.editPanelWidth);
    layout_.cameraMode = session_.Selected().model == 255;
    const auto& options = session_.Document().envelope.settings.options;
    layout_.cameraPanelExpanded = options[1];
    layout_.lightPanelExpanded = options[2];
    layout_.accessoryPanelExpanded = options[3];
    layout_.bonePanelExpanded = options[4];
    layout_.morphPanelExpanded = options[5];
    layout_.selfShadowPanelExpanded = options[6];
    const auto viewport = ui::MmdViewport(canvas_->width(), canvas_->height(), side_,
                                          int(session_.Document().envelope.settings.renderWidth),
                                          int(session_.Document().envelope.settings.renderHeight));
    if (viewportContainer_)
        viewportContainer_->setGeometry(viewport.x, viewport.y, std::max(1, viewport.width),
                                        std::max(1, viewport.height));
    const auto place = [&](const auto& rect) {
        auto* w = Control(rect.id);
        if (!w)
            return;
        w->setGeometry(rect.x, rect.y, std::max(1, rect.width),
                       qobject_cast<QComboBox*>(w) ? 20 : std::max(1, rect.height));
        w->raise();
    };
    for (const auto& rect : ui::MmdEditorControls(side_, canvas_->height(), layout_))
        place(rect);
    for (const auto& rect : ui::MmdViewportControls(side_, canvas_->width(), viewport))
        place(rect);
    timeline_->setGeometry(6, 145, std::max(1, side_ - 25), std::max(1, canvas_->height() - 393));
    for (const auto& pair : controls_) {
        const auto id = pair.first;
        bool show = true;
        const struct {
            int first, collapse, expand, option;
            bool camera;
        } groups[]{
            {kPerspectiveCheckbox, kCollapseCameraPanelButton, kExpandCameraPanelButton, 1, true},
            {kLightColorSliderR, kCollapseLightPanelButton, kExpandLightPanelButton, 2, true},
            {kAccessoryCombo, kCollapseAccessoryPanelButton, kExpandAccessoryPanelButton, 3, true},
            {kBoneSelectRadio, kCollapseBonePanelButton, kExpandBonePanelButton, 4, false},
            {kMorphCombo0, kCollapseMorphPanelButton, kExpandMorphPanelButton, 5, false},
            {kSelfShadowRangeSlider, kCollapseSelfShadowPanelButton, kExpandSelfShadowPanelButton,
             6, true}};
        for (const auto& group : groups) {
            if (id >= group.first && id <= group.collapse)
                show = layout_.cameraMode == group.camera && options[group.option];
            if (id == group.expand)
                show = layout_.cameraMode == group.camera && !options[group.option];
        }
        pair.second->setVisible(show);
    }
    canvas_->update();
}
void EditorWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    Layout();
}
void EditorWindow::Refresh() {
    refreshing_ = true;
    if (!session_.Playing())
        audio_.Stop();
    const auto& doc = session_.Document();
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
    actions_["undo"]->setEnabled(session_.CanUndo());
    actions_["redo"]->setEnabled(session_.CanRedo());
    edit(kCurrentFrameEdit, doc.timeline.currentFrame);
    edit(kGotoFrameEdit, doc.timeline.currentFrame);
    edit(kPlayStartFrameEdit, doc.timeline.playStartFrame);
    edit(kPlayStopFrameEdit, doc.timeline.playStopFrame);
    check(kPlaybackLoopCheckbox, doc.timeline.playbackLoop);
    check(kPlaybackReturnCheckbox, doc.timeline.returnToStart);
    check(kPlaybackFromCurrentCheckbox, doc.timeline.startAtCurrent);
    if (auto* b = qobject_cast<QAbstractButton*>(Control(kPlayButton)))
        b->setText(language_.Text(session_.Playing() ? "stop" : "control.408"));
    auto* models = qobject_cast<QComboBox*>(Control(kMainComboModel));
    models->clear();
    models->addItem(language_.Text("camera_mode"), 255);
    for (const auto& m : doc.models)
        models->addItem(Name(m.name), m.slot);
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
    for (int i = 0; i < 3; ++i) {
        edit(pos[i], i == 2 && camera.parentModel >= 0 ? -camera.position[i] : camera.position[i]);
        edit(rot[i], float(camera.rotation[i] / (i == 0 ? -3.141592f : 3.141592f)) * 180.f);
    }
    edit(kReadoutDistEdit, camera.parentModel < 0 ? -camera.distance : 0);
    if (session_.Selected().model != 255)
        for (int id = kReadoutPosXEdit; id <= kReadoutDistEdit; ++id)
            if (auto* e = qobject_cast<QLineEdit*>(Control(id)); e && !e->hasFocus())
                e->setText("-----");
    for (const auto& m : doc.models)
        if (m.slot == session_.Selected().model) {
            check(kModelVisibleCheckbox, m.selection.loadComplete);
            for (int panel = 0; panel < 4; ++panel) {
                const int combos[]{kMorphCombo0, kMorphCombo1, kMorphCombo2, kMorphCombo3};
                const int edits[]{kMorphEdit0, kMorphEdit1, kMorphEdit2, kMorphEdit3};
                const int sliders[]{kMorphSlider0, kMorphSlider1, kMorphSlider2, kMorphSlider3};
                auto* c = qobject_cast<QComboBox*>(Control(combos[panel]));
                const auto old = c->currentData();
                c->clear();
                const auto resource = session_.Runtime().Resource(m.slot);
                for (std::size_t i = 0; i < m.morphNames.size(); ++i)
                    if (resource->Document().morphs[i].header.panel == panel + 1)
                        c->addItem(Name(m.morphNames[i]), unsigned(i));
                int index = c->findData(old);
                c->setCurrentIndex(index < 0 ? 0 : index);
                const auto morph = c->currentData().toUInt();
                const float value = c->count() ? m.morphPose.at(morph) : 0;
                edit(edits[panel], value);
                static_cast<QSlider*>(Control(sliders[panel]))->setValue(qRound(value * 100));
            }
            if (!session_.Selected().bones.empty() &&
                session_.Selected().bones[0] < m.bonePose.size()) {
                const auto& p = m.bonePose[session_.Selected().bones[0]];
                const auto degrees = BoneRotationDegrees(
                    {p.components[3], p.components[4], p.components[5], p.components[6]});
                for (int i = 0; i < 3; ++i)
                    edit(pos[i], p.components[i]);
                for (int i = 0; i < 3; ++i)
                    edit(rot[i], degrees[i]);
            }
        }
    for (unsigned i = 0; i < 4; ++i)
        actions_[QString("physics.%1").arg(i)]->setChecked(doc.physics.playbackMode == i);
    auto* accessories = qobject_cast<QComboBox*>(Control(kAccessoryCombo));
    accessories->clear();
    for (const auto& a : doc.accessories) {
        std::vector<char> name(a.name.begin(), std::find(a.name.begin(), a.name.end(), 0));
        accessories->addItem(Name(name), a.slot);
    }
    if (session_.Selected().accessory == 255 && !doc.accessories.empty())
        session_.Selected().accessory = doc.accessories[0].slot;
    accessories->setCurrentIndex(accessories->findData(session_.Selected().accessory));
    RefreshPanels();
    timeline_->Refresh();
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
    if (!testing_) {
        std::string reason;
        if (!viewport_->Render(reason))
            lastError_ = QString::fromStdString(reason);
    }
}
bool EditorWindow::Open(const std::filesystem::path& path) {
    std::string r;
    const bool ok = Result(session_.Open(path, r), r);
    if (ok)
        Refresh();
    return ok;
}
bool EditorWindow::Save(const std::filesystem::path& path) {
    std::string r;
    const bool ok = Result(session_.Save(path, AtomicSave, r), r);
    if (ok)
        Refresh();
    return ok;
}
bool EditorWindow::ConfirmUnsaved() {
    if (!session_.Dirty() || testing_)
        return true;
    const auto answer =
        QMessageBox::question(this, language_.Text("app.title"), language_.Text("dialog.unsaved"),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;
    FileAction("file.save");
    return !session_.Dirty();
}
void EditorWindow::closeEvent(QCloseEvent* e) {
    if (ConfirmUnsaved())
        e->accept();
    else
        e->ignore();
}
void EditorWindow::FileAction(const QString& id) {
    if (id == "file.new") {
        if (ConfirmUnsaved()) {
            std::string r;
            Result(session_.New(r), r);
            Refresh();
        }
        return;
    }
    if (id == "file.exit") {
        close();
        return;
    }
    if (id == "edit.numeric") {
        Numeric();
        return;
    }
    if (id == "file.open") {
        if (!ConfirmUnsaved())
            return;
        auto p = QFileDialog::getOpenFileName(this, language_.Text(id), {}, "MMD Project (*.pmm)");
        if (!p.isEmpty())
            Open(Path(p));
        return;
    }
    if (id == "file.save" || id == "file.save_as") {
        auto p = session_.Path();
        if (p.empty() || id == "file.save_as")
            p = Path(
                QFileDialog::getSaveFileName(this, language_.Text(id), {}, "MMD Project (*.pmm)"));
        if (!p.empty())
            Save(p);
        return;
    }
    if (id == "file.wave") {
        const auto path = QFileDialog::getOpenFileName(this, language_.Text(id), {}, "WAV (*.wav)");
        if (path.isEmpty())
            return;
        std::string r;
        Result(session_.ImportWave(Path(path), r), r);
        Refresh();
        return;
    }
    if (id == "file.sequence") {
        const auto path = QFileDialog::getSaveFileName(this, language_.Text(id), "frames", {},
                                                       nullptr, QFileDialog::DontConfirmOverwrite);
        if (path.isEmpty())
            return;
        session_.Pause();
        audio_.Stop();
        timer_.stop();
        const auto& d = session_.Document();
        const auto start = unsigned(d.timeline.playStartFrame),
                   end = unsigned(d.timeline.playStopFrame);
        QProgressDialog progress(language_.Text(id), language_.Text("dialog.cancel"), 0,
                                 int(end - start + 1), this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        std::string r;
        const bool ok = ExportSequence(
            session_, assets_, viewport_->Options(), Path(path), start, end,
            d.envelope.settings.renderWidth, d.envelope.settings.renderHeight,
            [&](unsigned done, unsigned total) {
                progress.setMaximum(int(total));
                progress.setValue(int(done));
                QApplication::processEvents();
                return !progress.wasCanceled();
            },
            r);
        if (r != "export-cancelled")
            Result(ok, r);
        clock_.restart();
        if (!testing_)
            timer_.start(16);
        Refresh();
        return;
    }
    if (id == "file.image") {
        auto path = QFileDialog::getSaveFileName(this, language_.Text(id), {}, "PNG (*.png)");
        if (path.isEmpty())
            return;
        QImage image;
        std::string r;
        const auto& settings = session_.Document().envelope.settings;
        if (Result(RenderImage(session_.Runtime(), session_.Assets(), assets_, viewport_->Options(),
                               settings.renderWidth, settings.renderHeight, image, r),
                   r)) {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit())
                Result(false, file.errorString().toStdString());
        }
        return;
    }
    const bool pose = id.startsWith("file.pose_"), motion = id.startsWith("file.motion_");
    if (pose || motion) {
        const bool save = id.endsWith("save");
        const auto filter = pose ? "MMD Pose (*.vpd)" : "MMD Motion (*.vmd)";
        const auto path = save ? QFileDialog::getSaveFileName(this, language_.Text(id), {}, filter)
                               : QFileDialog::getOpenFileName(this, language_.Text(id), {}, filter);
        if (path.isEmpty())
            return;
        std::string r;
        // The portable disk parsers accept FILE*. QFile supplies a native open
        // descriptor, keeping Unicode filename policy in this frontend.
        QFile file(path);
        if (!save && !file.open(QIODevice::ReadOnly)) {
            Result(false, file.errorString().toStdString());
            return;
        }
        // Parse/serialize in a temporary anonymous stream: no locale path API.
        auto* stream = std::tmpfile();
        if (!stream) {
            Result(false, "temporary-stream-failed");
            return;
        }
        bool ok = false;
        if (!save) {
            const auto bytes = file.readAll();
            std::fwrite(bytes.data(), 1, std::size_t(bytes.size()), stream);
            std::rewind(stream);
        }
        if (pose) {
            format::VpdDocument d;
            ok = save ? (session_.ExportPose(session_.Selected().model, d, r) &&
                         format::WriteVpd(stream, d))
                      : (format::ReadVpd(stream, d) &&
                         session_.ImportPose(d, session_.Selected().model, r));
        } else {
            format::VmdDocument d;
            ok = save ? (session_.ExportMotion(session_.Selected().model, d, r) &&
                         format::WriteVmd(stream, d))
                      : (format::ReadVmd(stream, d) &&
                         session_.ImportMotion(d, session_.Selected().model, r));
        }
        if (save && ok) {
            std::rewind(stream);
            std::vector<std::uint8_t> bytes;
            char buffer[4096];
            std::size_t n;
            while ((n = std::fread(buffer, 1, sizeof(buffer), stream)))
                bytes.insert(bytes.end(), buffer, buffer + n);
            file.close();
            ok = AtomicSave(Path(path), bytes, r);
        }
        std::fclose(stream);
        Result(ok, r.empty() ? "motion-or-pose-file-invalid" : r);
        Refresh();
    }
}
void EditorWindow::Numeric() {
    QDialog dialog(this);
    dialog.setWindowTitle(language_.Text("edit.numeric"));
    QFormLayout layout(&dialog);
    std::array<QDoubleSpinBox*, 6> values{};
    const auto camera = session_.Runtime().Session()->Pose().camera;
    for (int i = 0; i < 6; ++i) {
        auto* value = new QDoubleSpinBox(&dialog);
        value->setDecimals(4);
        value->setRange(-100000, 100000);
        value->setValue(i < 3 ? camera.position[i]
                              : float(camera.rotation[i - 3] / (i == 3 ? -3.141592f : 3.141592f)) *
                                    180.f);
        values[i] = value;
        layout.addRow(QString(i < 3 ? "%1" : "R%1").arg("XYZ"[i % 3]), value);
    }
    for (const auto& m : session_.Document().models)
        if (m.slot == session_.Selected().model && !session_.Selected().bones.empty()) {
            auto bone = session_.Selected().bones[0];
            if (bone < m.bonePose.size()) {
                const auto& p = m.bonePose[bone].components;
                const auto degrees = BoneRotationDegrees({p[3], p[4], p[5], p[6]});
                for (int i = 0; i < 3; ++i) {
                    values[i]->setValue(p[i]);
                    values[i + 3]->setValue(degrees[i]);
                }
            }
        }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout.addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::string r;
    if (session_.Selected().model == 255) {
        auto next = camera;
        for (int i = 0; i < 3; ++i) {
            next.position[i] = float(values[i]->value());
            next.rotation[i] = float(values[i + 3]->value() *
                                     (i == 0 ? -3.141592025756836 : 3.141592025756836) / 180);
        }
        Result(session_.SetCamera(next, r), r);
    } else if (!session_.Selected().bones.empty())
        Result(session_.SetBone(session_.Selected().model, session_.Selected().bones[0],
                                {float(values[0]->value()), float(values[1]->value()),
                                 float(values[2]->value())},
                                Quaternion(float(values[3]->value()), float(values[4]->value()),
                                           float(values[5]->value())),
                                r),
               r);
    Refresh();
}
void EditorWindow::Dispatch(int id) {
    if (PanelCommand(id)) {
        Refresh();
        return;
    }
    std::string r;
    bool ok = true;
    const auto current = unsigned(session_.Document().timeline.currentFrame);
    switch (id) {
    case kUndoButton:
        ok = session_.Undo(r);
        break;
    case kRedoButton:
        ok = session_.Redo(r);
        break;
    case kPreviousFrameButton:
        ok = session_.Seek(current ? current - 1 : 0, r);
        break;
    case kNextFrameButton:
        ok = session_.Seek(current + 1, r);
        break;
    case kFirstFrameButton:
        ok = session_.Seek(0, r);
        break;
    case kLastFrameButton:
        ok = session_.Seek(unsigned(std::max(0, session_.Document().timeline.lastRegisteredFrame)),
                           r);
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
        ok = session_.Seek(target, r);
        break;
    }
    case kPlayButton:
        if (session_.Playing()) {
            session_.Pause();
            audio_.Stop();
        } else {
            clock_.restart();
            ok = session_.Play(r);
            if (ok && !testing_ && session_.Document().wave.enabled &&
                !session_.Assets().wave.empty()) {
                if (audio_.Path() != session_.Assets().wave)
                    ok = audio_.Load(session_.Assets().wave, r);
                if (ok)
                    ok = audio_.Play(session_.Runtime().Simulation().cursorSeconds, r);
                if (!ok)
                    session_.Pause();
            }
        }
        break;
    case kLoadModelButton:
    case kLoadAccessoryButton: {
        const auto p = QFileDialog::getOpenFileName(
            this, language_.Text("control.437"), {},
            id == kLoadModelButton ? "MMD Model (*.pmd *.pmx)" : "Accessory (*.x)");
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
        ok = session_.Paste(current, id == kReversePasteButton, r);
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
        timeline_->SelectRange(a->text().toUInt(), b->text().toUInt());
        break;
    }
    case kShowCurrentFrameButton:
        timeline_->SetFrameScroll(int(current));
        break;
    case kGoToFrameButton:
        ok = session_.Seek(static_cast<QLineEdit*>(Control(kGotoFrameEdit))->text().toUInt(), r);
        break;
    case kSetGoToFrameButton:
        static_cast<QLineEdit*>(Control(kGotoFrameEdit))->setText(QString::number(current));
        break;
    case kSelectAllBonesButton:
    case kSelectUnregisteredBonesButton: {
        session_.Selected().bones.clear();
        auto resource = session_.Runtime().Resource(session_.Selected().model);
        if (resource)
            for (unsigned i = 0; i < resource->BoneNames().size(); ++i)
                session_.Selected().bones.push_back(i);
        break;
    }
    case kRegisterFrameButton: {
        std::vector<KeyAddress> addresses;
        for (auto bone : session_.Selected().bones)
            addresses.push_back({Track::Bone, session_.Selected().model, bone, current});
        ok = session_.Register(addresses, r);
        break;
    }
    case kRegisterCameraButton:
        ok = session_.Register({{Track::Camera, 255, 0, current}}, r);
        break;
    case kRegisterLightButton:
        ok = session_.Register({{Track::Light, 255, 0, current}}, r);
        break;
    case kRegisterSelfShadowButton:
        ok = session_.Register({{Track::Shadow, 255, 0, current}}, r);
        break;
    case kRegisterModelDisplayButton:
        ok = session_.Register({{Track::Display, session_.Selected().model, 0, current}}, r);
        break;
    case kRegisterAccessoryButton:
        ok = session_.Register({{Track::Accessory, session_.Selected().accessory, 0, current}}, r);
        break;
    case kRegisterMorphButton0:
    case kRegisterMorphButton1:
    case kRegisterMorphButton2:
    case kRegisterMorphButton3: {
        const int registers[]{kRegisterMorphButton0, kRegisterMorphButton1, kRegisterMorphButton2,
                              kRegisterMorphButton3};
        const int index =
            int(std::find(std::begin(registers), std::end(registers), id) - std::begin(registers));
        const int combos[]{kMorphCombo0, kMorphCombo1, kMorphCombo2, kMorphCombo3};
        auto* c = static_cast<QComboBox*>(Control(combos[index]));
        ok = session_.Register(
            {{Track::Morph, session_.Selected().model, c->currentData().toUInt(), current}}, r);
        break;
    }
    case kCopyBonePoseButton:
        ok = session_.ExportPose(session_.Selected().model, poseClipboard_, r);
        break;
    case kBonePasteButton:
        ok = session_.ImportPose(poseClipboard_, session_.Selected().model, r);
        break;
    case kResetBonePoseButton:
        ok = session_.Edit(
            "reset-bones",
            [&](auto& d, auto&) {
                for (auto& m : d.models)
                    if (m.slot == session_.Selected().model)
                        for (auto b : session_.Selected().bones) {
                            m.bonePose.at(b).components = {0, 0, 0, 0, 0, 0, 1, 0};
                        }
                return true;
            },
            r);
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
        ok = value >= 0 && value <= 0x7fffffff && session_.Seek(unsigned(value), r);
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
        } else if (!session_.Selected().bones.empty() && id != kReadoutDistEdit) {
            for (const auto& model : session_.Document().models)
                if (model.slot == session_.Selected().model) {
                    const auto bone = session_.Selected().bones.front();
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
        session_.Selected() = {};
        session_.Selected().model = std::uint8_t(c->itemData(index).toUInt());
    }
    if (id == kAccessoryCombo)
        session_.Selected().accessory = std::uint8_t(c->itemData(index).toUInt());
    Refresh();
}
bool EditorWindow::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::KeyPress && !qobject_cast<QLineEdit*>(object)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Space) {
            Dispatch(kPlayButton);
            return true;
        }
        if (key->key() == Qt::Key_Delete) {
            Dispatch(kDeleteFramesButton);
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}
}
