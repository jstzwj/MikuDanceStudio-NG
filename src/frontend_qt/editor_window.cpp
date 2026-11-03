#include "editor_window.hpp"
#include "mikudancestudio/idle_render_gate.hpp"
#include "combo_items.hpp"
#include "export.hpp"
#include "numeric_dialog.hpp"
#include "english_name_dialog.hpp"
#include "model_physics_dialog.hpp"
#include "toon_dialog.hpp"
#include "model_labels.hpp"
#include "mikudancestudio/editor_numeric.hpp"
#include "mikudancestudio/mmd_control_specs.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/editor_rotation.hpp"
#include "mikudancestudio/mmd_editor_labels.hpp"
#include "mikudancestudio/sidebar_navigation.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/file_drop.hpp"
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
#include <QColorDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QSignalBlocker>
#include <QPainter>
#include <QSettings>
#include <QScopedValueRollback>
#include <QProgressDialog>
#include <QLibraryInfo>
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
    Canvas(Language& language, editor::EditorSession& session, const std::filesystem::path& assets,
           QWidget* p, bool viewportOnly = false)
        : QWidget(p), language_(language), session_(session),
          viewTools_(Path(assets / "playing_indicator.bmp")), viewportOnly_(viewportOnly) {
        setAutoFillBackground(true);
        setMouseTracking(true);
    }
    void SetDetached(bool value) {
        detached_ = value;
        update();
    }
    void SetFullScreen(bool value) {
        fullScreen_ = value;
        update();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        if (fullScreen_) {
            p.fillRect(rect(), Qt::black);
            return;
        }
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
        const auto viewport = viewportOnly_
                                  ? ui::MmdSeparateViewport(width(), height(), settings.renderWidth,
                                                            settings.renderHeight)
                                  : ui::MmdViewport(width(), height(), side, settings.renderWidth,
                                                    settings.renderHeight);
        if (viewportOnly_) {
            p.fillRect(rect(), Qt::black);
        } else {
            p.fillRect(rect(), color(ui::kDefaultThemeColors[0]));
            if (!detached_)
                p.fillRect(side + 9, 0, width() - side - 9, height() - 160, Qt::black);
            fill({0, bottom - 160, width(), bottom, ui::kDefaultThemeColors[0],
                  ui::kDefaultThemeColors[1], true});
            for (const auto& f : ui::MmdPanelFills(side, bottom, options))
                fill(f);
            p.fillRect(side, 0, 6, bottom - 159, QColor(220, 220, 220));
        }
        if (viewportOnly_ || !detached_) {
            const int inset = viewportOnly_ ? 0 : side + 9;
            fill({inset, viewport.y - 25, width(), viewport.y - 1, 0x969696, 0x646464, true});
            p.drawImage(QPoint(width() - 58, viewport.y - 24), viewTools_);
            fill({inset, viewport.y + viewport.height, width(), viewport.y + viewport.height + 32,
                  0x787878, 0xc8c8c8, true});
        }
        if (viewportOnly_)
            return;
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
    QImage viewTools_;
    bool viewportOnly_ = false, detached_ = false, fullScreen_ = false;
};
}
EditorWindow::EditorWindow(std::filesystem::path assets, bool testing,
                           std::unique_ptr<AudioOutput> audio)
    : assets_(std::move(assets)), testing_(testing), audio_(std::move(audio), testing) {
    setObjectName("mmd.editor");
    setAcceptDrops(true);
    installEventFilter(this);
    setMinimumSize(640, 480);
    resize(1024, 768);
    menuBar()->setNativeMenuBar(false);
    QFont font;
    font.setFamilies({"MS UI Gothic", "Noto Sans CJK SC", "Microsoft YaHei", "Hiragino Sans",
                      "PingFang SC", "DejaVu Sans"});
    font.setPixelSize(12);
    setFont(font);
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
        language_.Set(settings.value("language", language_.Code()).toString());
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
void EditorWindow::CreateViewport(const rhi::BackendOptions& options) {
    // A different API receives a fresh native window. CPU state remains owned
    // by EditorSession; retiring the old controller never evaluates a frame.
    auto* next = new Viewport(session_, assets_);
    next->SetOptions(options);
    auto* container = QWidget::createWindowContainer(next, separate_ ? separate_ : canvas_);
    auto* old = viewportContainer_;
    viewport_ = next;
    viewport_->installEventFilter(this);
    viewportContainer_ = container;
    if (old)
        delete old;
    viewport_->changed = [this](bool ok, const std::string& reason) {
        Result(ok, reason);
        Refresh();
    };
    viewport_->physicsEdit = [this] {
        render::PhysicsEditView view;
        if (physicsDialog_ && physicsDialog_->Active()) {
            view.edit = &physicsDialog_->State();
            view.slot = session_.Selected().model;
            view.body = physicsDialog_->SelectedBody();
            view.joint = physicsDialog_->SelectedJoint();
            view.jointPage = physicsDialog_->JointPage();
        }
        return view;
    };
    viewport_->physicsDrag = [this](unsigned mode, int dy, bool shift) {
        return physicsDialog_ && physicsDialog_->Drag(mode, dy, shift);
    };
    viewport_->zoom = [this](int delta) {
        std::string r;
        Result(session_.WheelView(delta, r), r);
        Refresh();
    };
    viewport_->drag = [this](QPoint delta, Qt::MouseButtons buttons,
                             Qt::KeyboardModifiers modifiers) {
        if (!(buttons & Qt::RightButton) && !(buttons & Qt::MiddleButton))
            return;
        std::string r;
        bool ok = true;
        if (buttons & Qt::RightButton)
            ok = session_.DragView(false, delta.x(), delta.y(), modifiers & Qt::ShiftModifier,
                                   modifiers & Qt::ControlModifier, r);
        if (ok && (buttons & Qt::MiddleButton))
            ok = session_.DragView(true, delta.x(), delta.y(), modifiers & Qt::ShiftModifier,
                                   modifiers & Qt::ControlModifier, r);
        Result(ok, r);
        Refresh();
    };
    Layout();
    // A replacement child of an already-visible QWidget is not implicitly
    // shown. Ensure Qt attaches/maps the new native child on every platform.
    // A hidden ancestor still keeps hidden-window acceptance off the desktop.
    container->show();
}
void EditorWindow::SetSeparateWindow(bool enabled) {
    if (enabled == (separate_ != nullptr))
        return;
    QScopedValueRollback transition(windowTransition_, true);
    FinishSidebarDrag();
    viewport_->CancelInput();
    auto options = viewport_->Options();
    options.surface.reset();
    QWidget* retired = nullptr;
    if (enabled) {
        dockedSidebarWidth_ = session_.Document().envelope.settings.editPanelWidth;
        auto* window = new Canvas(language_, session_, assets_, nullptr, true);
        window->setObjectName("mmd.separateViewport");
        window->setAcceptDrops(true);
        window->setWindowTitle("MMD");
        window->setFont(font());
        window->setWindowFlag(Qt::WindowStaysOnTopHint, separateOnTop_);
        window->resize(separateSize_);
        if (!separateGeometry_.isEmpty())
            window->restoreGeometry(separateGeometry_);
        // Qt clamps restored geometry to the current screen. MMD preserves the
        // user's normal size, even when it exceeds the available desktop area.
        if (!window->isMaximized())
            window->resize(separateSize_);
        separate_ = window;
        window->installEventFilter(this);
    } else {
        retired = separate_;
        separateGeometry_ = retired->saveGeometry();
        if (!retired->isMaximized())
            separateSize_ = retired->size();
        retired->removeEventFilter(this);
        separate_ = nullptr;
        session_.SetSidebarWidth(dockedSidebarWidth_);
        sidebarClientWidth_ = 0;
    }
    for (int id = kModelEditToggle; id <= kCoordAxisCheckbox; ++id)
        Control(id)->setParent(separate_ ? separate_ : canvas_);
    static_cast<Canvas*>(canvas_)->SetDetached(enabled);
    CreateViewport(options);
    BuildMenus();
    Refresh();
    if (separate_ && !testing_)
        separate_->show();
    if (retired) {
        retired->hide();
        retired->deleteLater(); // Closing the separate window never closes the project.
    }
}
void EditorWindow::ToggleSeparateOnTop() {
    QScopedValueRollback transition(windowTransition_, true);
    separateOnTop_ = !separateOnTop_;
    if (separate_) {
        const bool visible = separate_->isVisible();
        // Changing QWidget flags in place invalidates X11 native ancestry even
        // when the embedded QWindow is replaced. Retire the complete parent
        // hierarchy through the same ownership path as ordinary detach/dock.
        SetSeparateWindow(false);
        SetSeparateWindow(true);
        if (visible)
            separate_->show();
    }
    RefreshMenus();
}
void EditorWindow::SetFullScreenViewport(bool enabled) {
    if (enabled == fullScreen_)
        return;
    QScopedValueRollback transition(windowTransition_, true);
    FinishSidebarDrag();
    viewport_->CancelInput();
    auto options = viewport_->Options();
    options.surface.reset();
    viewport_->SetOptions(options);
    if (enabled) {
        restoreSeparate_ = separate_ != nullptr;
        if (separate_)
            SetSeparateWindow(false);
        normalGeometry_ = saveGeometry();
        normalSize_ = size();
        normalWindowState_ = windowState();
        normalSidebarWidth_ = session_.Document().envelope.settings.editPanelWidth;
        fullScreen_ = true;
        static_cast<Canvas*>(canvas_)->SetFullScreen(true);
        menuBar()->hide();
        // setWindowState preserves visibility. Hidden acceptance never calls
        // showFullScreen or changes a display mode.
        setWindowState(normalWindowState_ | Qt::WindowFullScreen);
    } else {
        // Keep the fullscreen layout active through native resize notifications,
        // so they cannot alter the saved sidebar ratio on the way back.
        setWindowState(normalWindowState_);
        restoreGeometry(normalGeometry_);
        if (!(normalWindowState_ & Qt::WindowMaximized))
            resize(normalSize_);
        fullScreen_ = false;
        static_cast<Canvas*>(canvas_)->SetFullScreen(false);
        menuBar()->show();
        session_.SetSidebarWidth(normalSidebarWidth_);
        sidebarClientWidth_ = 0;
    }
    // Retire device/surface bindings; a later Render reacquires the current
    // native surface without reloading or advancing the CPU scene.
    viewport_->SetOptions(options);
    if (!enabled && restoreSeparate_) {
        restoreSeparate_ = false;
        SetSeparateWindow(true);
    }
    Refresh();
}
bool EditorWindow::SetLanguage(const QString& language) {
    if (!language_.Set(language))
        return false;
    TranslateStandardDialogs();
    if (!testing_) {
        QSettings settings;
        settings.setValue("language", language);
    }
    BuildMenus();
    Texts();
    Refresh();
    return true;
}
void EditorWindow::TranslateStandardDialogs() {
    QCoreApplication::removeTranslator(&standardTranslation_);
    if (language_.Code() != "en" &&
        standardTranslation_.load("qtbase_" + language_.Code(),
                                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&standardTranslation_);
}
void EditorWindow::Texts() {
    for (const auto& pair : controls_)
        if (auto* button = qobject_cast<QAbstractButton*>(pair.second))
            button->setText(language_.Text(QString("control.%1").arg(pair.first)));
}
void EditorWindow::Layout() {
    if (!canvas_)
        return;
    if (fullScreen_) {
        const auto& settings = session_.Document().envelope.settings;
        const auto rect =
            ui::MmdSeparateViewport(canvas_->width(), canvas_->height(), int(settings.renderWidth),
                                    int(settings.renderHeight), true);
        viewportContainer_->setGeometry(rect.x, rect.y, std::max(1, rect.width),
                                        std::max(1, rect.height));
        viewport_->resize(std::max(1, rect.width), std::max(1, rect.height));
        for (const auto& pair : controls_)
            pair.second->hide();
        timeline_->hide();
        interpolation_->hide();
        canvas_->update();
        return;
    }
    timeline_->show();
    interpolation_->show();
    side_ = std::max(250, session_.Document().envelope.settings.editPanelWidth);
    const int clientWidth = canvas_->width();
    if (separate_) {
        side_ = std::max(250, clientWidth - 3);
        session_.SetSidebarWidth(side_);
    } else if (clientWidth >= 255) {
        if (!sidebarClientWidth_ || side_ != sidebarStoredWidth_)
            sidebarRatio_ = float(double(side_) / double(clientWidth));
        else if (clientWidth != sidebarClientWidth_) {
            side_ = ui::ResizeSidebar(sidebarRatio_, clientWidth);
            session_.SetSidebarWidth(side_);
        }
        sidebarClientWidth_ = clientWidth;
        sidebarStoredWidth_ = side_;
    }
    layout_.cameraMode = session_.Selected().model == 255;
    const auto& options = session_.Document().envelope.settings.options;
    layout_.cameraPanelExpanded = options[1];
    layout_.lightPanelExpanded = options[2];
    layout_.accessoryPanelExpanded = options[3];
    layout_.bonePanelExpanded = options[4];
    layout_.morphPanelExpanded = options[5];
    layout_.selfShadowPanelExpanded = options[6];
    const auto viewport =
        separate_ ? ui::MmdSeparateViewport(separate_->width(), separate_->height(),
                                            int(session_.Document().envelope.settings.renderWidth),
                                            int(session_.Document().envelope.settings.renderHeight))
                  : ui::MmdViewport(canvas_->width(), canvas_->height(), side_,
                                    int(session_.Document().envelope.settings.renderWidth),
                                    int(session_.Document().envelope.settings.renderHeight));
    if (viewportContainer_) {
        viewportContainer_->setGeometry(viewport.x, viewport.y, std::max(1, viewport.width),
                                        std::max(1, viewport.height));
        // A hidden QWidget container postpones its child QWindow resize until
        // exposure. Input/projection still need the same dimensions beforehand.
        viewport_->resize(std::max(1, viewport.width), std::max(1, viewport.height));
    }
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
    for (const auto& rect : ui::MmdViewportControls(
             separate_ ? -9 : side_, separate_ ? separate_->width() : canvas_->width(), viewport))
        place(rect);
    timeline_->setGeometry(6, 145, std::max(1, side_ - 25), std::max(1, canvas_->height() - 393));
    static_cast<QScrollBar*>(Control(kTimelineHScroll))
        ->setPageStep(std::max(1, (side_ - 100) / 13));
    interpolation_->move(8, canvas_->height() - 135);
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
    if (separate_)
        separate_->update();
}
void EditorWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    Layout();
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
                                                    language_.Code() == "en"),
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
        if (!viewport_->Render(reason))
            lastError_ = QString::fromStdString(reason);
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
    bool confirmed;
    if (session_.EnhancedModelDirty() && !testing_) {
        QMessageBox dialog(QMessageBox::NoIcon, language_.Text("dialog.enhance.quit_title"),
                           language_.Text("dialog.enhance.quit"),
                           QMessageBox::Ok | QMessageBox::Cancel, this);
        dialog.setTextFormat(Qt::PlainText);
        dialog.setOption(QMessageBox::Option::DontUseNativeDialog);
        dialog.QDialog::setWindowTitle(language_.Text("dialog.enhance.quit_title"));
        dialog.setWindowFlag(Qt::WindowStaysOnTopHint);
        confirmed = dialog.exec() == QMessageBox::Ok;
    } else
        confirmed = ConfirmUnsaved();
    if (confirmed) {
        if (edgeDialog_)
            edgeDialog_->close();
        if (gravityDialog_)
            gravityDialog_->close();
        if (englishDialog_)
            englishDialog_->close();
        if (physicsDialog_)
            physicsDialog_->close();
        if (separate_)
            separate_->hide();
        e->accept();
    } else
        e->ignore();
}
QString EditorWindow::ChooseFile(FileKind kind, bool save, const QString& title,
                                 const QString& filter, const QString& suggestedName,
                                 bool confirmOverwrite) {
    const auto initial = fileHistory_.Initial(kind, QCoreApplication::applicationDirPath());
    QFileDialog dialog(separate_ ? separate_ : this, title, initial, filter);
    dialog.setObjectName("editor.fileDialog");
    dialog.setProperty("mmdInitialDirectory", initial);
    dialog.setProperty("mmdFileKind", int(kind));
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontConfirmOverwrite, !confirmOverwrite);
    dialog.setOption(QFileDialog::DontUseNativeDialog, testing_);
    if (save && filter.startsWith("Polygon Model files"))
        dialog.setDefaultSuffix("pmd");
    if (!suggestedName.isEmpty())
        dialog.selectFile(suggestedName);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return {};
    const auto path = dialog.selectedFiles().front();
    fileHistory_.Confirm(kind, path);
    return path;
}
void EditorWindow::FileAction(const QString& id, const QString& inputPath) {
    if (id == "model.enhanced_save") {
        const auto resource = session_.Runtime().Resource(session_.Selected().model);
        if (!resource || resource->SourceKind() != format::ModelKind::Pmd || session_.Playing())
            return;
        const auto path =
            inputPath.isEmpty()
                ? ChooseFile(FileKind::Model, true, language_.Text("enhance.save_title"),
                             "Polygon Model files(*.pmd);;All Files(*.*)")
                : inputPath;
        if (!path.isEmpty()) {
            std::string reason;
            Result(session_.SaveEnhancedModel(session_.Selected().model, Path(path), AtomicSave,
                                              reason),
                   reason);
            Refresh();
        }
        return;
    }
    if (id == "file.lipsync") {
        const auto path = inputPath.isEmpty()
                              ? ChooseFile(FileKind::Vsq, false, language_.Text("dialog.vsq.title"),
                                           "vsq files(*.vsq)")
                              : inputPath;
        if (!path.isEmpty()) {
            LoadLipSync(path);
            Refresh();
        }
        return;
    }
    if (id == "view.screen_size") {
        ScreenSize();
        return;
    }
    if (id == "background.load") {
        const auto path =
            ChooseFile(FileKind::Background, false, language_.Text("background.load_title"),
                       language_.Text("background.image_filter"));
        if (path.isEmpty())
            return;
        std::string r;
        Result(session_.ImportPicture(Path(path), unsigned(viewport_->width()),
                                      unsigned(viewport_->height()), r),
               r);
        Refresh();
        return;
    }
    if (id == "background.show") {
        std::string r;
        // Native toggle is a no-op if no image has ever been loaded.
        if (session_.Document().picture.enabled || session_.Assets().picture)
            Result(session_.ShowPicture(!session_.Document().picture.enabled, r), r);
        Refresh();
        return;
    }
    if (id == "background.black" || id.startsWith("background.capture.")) {
        std::string r;
        Result(session_.Edit(
                   "background",
                   [&](auto& d, auto&) {
                       if (id == "background.black")
                           d.appearance.blackBackground = !d.appearance.blackBackground;
                       else
                           d.view.captureMode = std::uint32_t(id.section('.', -1).toUInt());
                       return true;
                   },
                   r),
               r);
        Refresh();
        return;
    }
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
        auto p = ChooseFile(FileKind::Project, false, language_.Text(id), "MMD Project (*.pmm)");
        if (!p.isEmpty())
            Open(Path(p));
        return;
    }
    if (id == "file.save" || id == "file.save_as") {
        auto p = session_.Path();
        if (p.empty() || id == "file.save_as")
            p = Path(
                ChooseFile(FileKind::Project, true, language_.Text(id), "MMD Project (*.pmm)"));
        if (!p.empty())
            Save(p);
        return;
    }
    if (id == "file.wave") {
        const auto path = ChooseFile(FileKind::Wave, false, language_.Text(id), "WAV (*.wav)");
        if (path.isEmpty())
            return;
        std::string r;
        Result(session_.ImportWave(Path(path), r), r);
        Refresh();
        return;
    }
    if (id == "file.sequence") {
        const auto path =
            ChooseFile(FileKind::Project, true, language_.Text(id), {}, "frames", false);
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
            timer_.start();
        Refresh();
        return;
    }
    if (id == "file.image") {
        auto path = ChooseFile(FileKind::Project, true, language_.Text(id), "PNG (*.png)");
        if (path.isEmpty())
            return;
        QImage image;
        std::string r;
        const auto& settings = session_.Document().envelope.settings;
        if (Result(RenderImage(session_.Runtime(), session_.Assets(), assets_, viewport_->Options(),
                               settings.renderWidth, settings.renderHeight, image, r,
                               session_.DisplayOptions()),
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
        const auto path = inputPath.isEmpty() ? ChooseFile(pose ? FileKind::Pose : FileKind::Motion,
                                                           save, language_.Text(id), filter)
                                              : inputPath;
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
void EditorWindow::DropFiles(const QList<QUrl>& urls) {
    for (const auto& url : urls) {
        if (!url.isLocalFile())
            continue;
        auto path = url.toLocalFile();
        // Preserve DragQueryFileW's UTF-16 truncation, including classification
        // of path components before the filename. QFile receives the same path.
        path.truncate(int(editor::DroppedFilePath(path.toStdU16String()).size()));
        const auto kind = editor::ClassifyDroppedFile(path.toStdU16String());
        if (kind == DroppedFileKind::Unknown)
            continue;
        if (kind == DroppedFileKind::Project && session_.Dirty()) {
            QMessageBox dialog(QMessageBox::NoIcon, language_.Text("drop.project_title"),
                               language_.Text("drop.unsaved"),
                               QMessageBox::Ok | QMessageBox::Cancel, separate_ ? separate_ : this);
            dialog.setObjectName("mmd.dropUnsaved");
            if (dialog.exec() != QMessageBox::Ok)
                break;
        }
        if ((kind == DroppedFileKind::Pose || kind == DroppedFileKind::Vocaloid ||
             kind == DroppedFileKind::Kinect) &&
            session_.Selected().model == 255) {
            Result(false, language_.Text("drop.select_model").toStdString());
            continue;
        }
        idleRenderState_ = 1;
        const auto remember = [&](FileKind type) { fileHistory_.Confirm(type, path); };
        std::string reason;
        switch (kind) {
        case DroppedFileKind::Project:
            remember(FileKind::Project);
            Open(Path(path));
            break;
        case DroppedFileKind::Model:
            remember(FileKind::Model);
            Result(session_.ImportModel(Path(path), reason), reason);
            break;
        case DroppedFileKind::Accessory:
            remember(FileKind::Accessory);
            Result(session_.ImportAccessory(Path(path), reason), reason);
            break;
        case DroppedFileKind::Pose:
        case DroppedFileKind::Motion:
            remember(kind == DroppedFileKind::Pose ? FileKind::Pose : FileKind::Motion);
            FileAction(kind == DroppedFileKind::Pose ? "file.pose_open" : "file.motion_open", path);
            break;
        case DroppedFileKind::Wave:
            remember(FileKind::Wave);
            Result(session_.ImportWave(Path(path), reason), reason);
            break;
        case DroppedFileKind::Vocaloid:
            LoadLipSync(path);
            break;
        case DroppedFileKind::Bitmap:
        case DroppedFileKind::Jpeg:
            if (kind == DroppedFileKind::Bitmap)
                remember(FileKind::Background);
            Result(session_.ImportPicture(Path(path), unsigned(viewport_->width()),
                                          unsigned(viewport_->height()), reason),
                   reason);
            break;
        default:
            // Classified native media/hardware commands retain their explicit
            // unsupported status until their corresponding stage is implemented.
            Result(false, language_.Text("menu.unavailable").toStdString());
            break;
        }
        Refresh();
    }
}
void EditorWindow::LoadLipSync(const QString& path) {
    std::string reason;
    unsigned failed = 0;
    if (session_.ImportVsq(Path(path), reason, &failed)) {
        lastError_.clear();
        KeyOverflow(failed, 20000, true);
        return;
    }
    QString message;
    if (reason.compare(0, 12, "vsq-missing-") == 0) {
        const auto vowel = reason.back();
        const auto kana = vowel == 'a'   ? 0x3042
                          : vowel == 'i' ? 0x3044
                          : vowel == 'u' ? 0x3046
                                         : 0x304a;
        message = language_.Text("dialog.vsq.missing")
                      .arg(language_.Code() == "en" ? QChar(vowel) : QChar(ushort(kana)));
    } else
        message = language_.Text(reason == "vsq-sysex-unsupported" ? "dialog.vsq.sysex"
                                 : reason == "vsq-open-error"      ? "dialog.vsq.open"
                                                                   : "dialog.vsq.analysis");
    lastError_ = message;
    if (!testing_) {
        const auto title = language_.Text(reason.compare(0, 12, "vsq-missing-") == 0
                                              ? "dialog.vsq.missing_title"
                                              : "dialog.vsq.analysis_title");
        QMessageBox dialog(QMessageBox::NoIcon, title, message, QMessageBox::Ok, this);
        dialog.setOption(QMessageBox::Option::DontUseNativeDialog);
        dialog.QDialog::setWindowTitle(title);
        const bool running = timer_.isActive();
        timer_.stop();
        dialog.exec();
        clock_.restart();
        if (running)
            timer_.start();
    }
}
void EditorWindow::ScreenSize() {
    std::string reason;
    QSize maximum;
    if (!Result(viewport_->OutputLimits(maximum, reason), reason))
        return;
    const auto& settings = session_.Document().envelope.settings;
    ScreenSizeDialog dialog(language_, QSize(settings.renderWidth, settings.renderHeight), maximum,
                            this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    dialog.invalid = [&](bool tooLarge) {
        QMessageBox::information(
            &dialog, QString(),
            language_.Text(tooLarge ? "dialog.screen.too_big" : "dialog.screen.positive"));
    };
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto value = dialog.Value();
    viewport_->CancelInput();
    if (Result(session_.SetRenderSize(unsigned(value.width()), unsigned(value.height()), reason),
               reason)) {
        // The next frame rebuilds size-dependent targets/history, retaining
        // CPU poses, the physical world and per-model command history.
        auto options = viewport_->Options();
        options.surface.reset();
        viewport_->SetOptions(options);
        Layout();
        Refresh();
    }
}
void EditorWindow::Numeric() {
    if (session_.Selected().model != 255 && session_.ActiveBone() < 0)
        return;
    const auto camera = session_.Runtime().Session()->Pose().camera;
    auto initial = editor::CameraNumericValues(camera.position, camera.rotation, camera.distance);
    for (const auto& m : session_.Document().models)
        if (m.slot == session_.Selected().model && session_.ActiveBone() >= 0) {
            const auto bone = std::size_t(session_.ActiveBone());
            if (bone < m.bonePose.size()) {
                const auto& p = m.bonePose[bone].components;
                const auto degrees = BoneRotationDegrees({p[3], p[4], p[5], p[6]});
                for (int i = 0; i < 3; ++i) {
                    initial[i] = p[i];
                    initial[i + 3] = degrees[i];
                }
            }
        }
    NumericDialog dialog(session_.Selected().model == 255, language_, initial, this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto& values = dialog.Values();
    std::string r;
    if (session_.Selected().model == 255) {
        auto next = camera;
        next.position = {values[0], values[1], values[2]};
        next.rotation = editor::CameraNumericRotation(values);
        next.distance = -values[6];
        Result(session_.SetCamera(next, r), r);
    } else if (session_.ActiveBone() >= 0)
        Result(session_.SetBone(session_.Selected().model, std::uint32_t(session_.ActiveBone()),
                                {values[0], values[1], values[2]},
                                BoneRotationFromDegrees({values[3], values[4], values[5]}), r),
               r);
    Refresh();
}
void EditorWindow::EdgeThickness() {
    if (edgeDialog_ || session_.Selected().model == 255)
        return;
    for (const auto& model : session_.Document().models) {
        if (model.slot != session_.Selected().model)
            continue;
        edgeDialog_ = new EdgeThicknessDialog(language_, model.edgeScale, this);
        edgeModel_ = model.slot;
        edgeResource_ = session_.Runtime().Resource(model.slot);
        edgeDialog_->setAttribute(Qt::WA_DeleteOnClose);
        edgeDialog_->setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
        edgeDialog_->changed = [this](float value) {
            std::string reason;
            if (Result(session_.SetEdgeThickness(value, reason), reason))
                Refresh();
        };
        if (!testing_)
            edgeDialog_->show();
        break;
    }
}
void EditorWindow::OutlineColor() {
    const auto& rgb = session_.Document().appearance.outlineColor;
    // Native CHOOSECOLOR initializes red/blue as bytes and green as a word.
    // Loaded PMM values outside the picker range retain this packing behavior.
    QColorDialog dialog(QColor(rgb[0] & 255, rgb[1] & 255, ((rgb[1] >> 8) & 255) | (rgb[2] & 255)),
                        this);
    dialog.setObjectName("mmd.outlineColor");
    dialog.setOption(QColorDialog::DontUseNativeDialog, testing_);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto color = dialog.selectedColor();
    if (color.isValid() &&
        session_.SetOutlineColor(
            {std::uint8_t(color.red()), std::uint8_t(color.green()), std::uint8_t(color.blue())}))
        UpdateFrame();
}
void EditorWindow::CenterBias() {
    CenterBiasDialog dialog(language_, this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::string reason;
    Result(session_.OffsetRootBoneKeys(dialog.Values(), reason), reason);
    Refresh();
}
void EditorWindow::GroundShadowColor() {
    if (groundShadowDialog_)
        return;
    groundShadowDialog_ =
        new GroundShadowDialog(language_, session_.Document().physics.projectedShadowAmbient, this);
    groundShadowDialog_->setAttribute(Qt::WA_DeleteOnClose);
    groundShadowDialog_->setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    groundShadowDialog_->changed = [this](float value) {
        if (session_.SetGroundShadowColor(value))
            UpdateFrame();
    };
    if (!testing_)
        groundShadowDialog_->show();
}
void EditorWindow::MorphScale() {
    MorphScaleDialog dialog(language_, this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto& values = dialog.Values();
    std::string reason;
    Result(session_.ScaleMorphKeys(values[0], values[1], reason), reason);
    Refresh();
}
void EditorWindow::ShiftMouthFrames() {
    FrameShiftDialog dialog(language_, this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::string reason;
    Result(session_.ShiftMouthKeys(dialog.Value(), reason), reason);
    Refresh();
}
void EditorWindow::RegisterBlinks() {
    const auto message = [&](const char* title, const QString& text) {
        if (!testing_) {
            QMessageBox report(QMessageBox::NoIcon, language_.Text(title), text, QMessageBox::Ok,
                               this);
            report.setWindowFlag(Qt::WindowStaysOnTopHint);
            report.exec();
        }
    };
    if (!session_.HasBlinkMorph()) {
        message("dialog.blink.missing_title", language_.Text("dialog.blink.missing"));
        return;
    }
    BlinkDialog dialog(language_, this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    unsigned count = 0, failed = 0;
    std::string reason;
    const bool ok = Result(
        session_.RegisterBlinks(dialog.Values()[0], dialog.Values()[1], count, reason, {}, &failed),
        reason);
    Refresh();
    if (ok) {
        MorphOverflow(failed);
        message("dialog.blink.report_title", language_.Text("dialog.blink.report").arg(count));
    }
}
void EditorWindow::MorphOverflow(unsigned failedKeys) {
    if (!testing_)
        for (unsigned i = 0; i < failedKeys; ++i) {
            QMessageBox overflow(QMessageBox::NoIcon, language_.Text("dialog.blink.capacity_title"),
                                 language_.Text("dialog.blink.capacity"), QMessageBox::Ok, this);
            overflow.exec();
        }
}
void EditorWindow::KeyOverflow(unsigned failedKeys, unsigned capacity, bool model) {
    if (!testing_)
        for (unsigned i = 0; i < failedKeys; ++i) {
            QMessageBox overflow(QMessageBox::NoIcon, QString(),
                                 language_
                                     .Text(model ? "dialog.model_key_capacity.message"
                                                 : "dialog.key_capacity.message")
                                     .arg(capacity),
                                 QMessageBox::Ok, this);
            overflow.setOption(QMessageBox::Option::DontUseNativeDialog);
            overflow.QDialog::setWindowTitle(language_.Text("dialog.key_capacity.title"));
            overflow.exec();
        }
}
bool EditorWindow::PasteKeys(bool mirrored, bool selectedTrack, std::string& reason) {
    std::vector<editor::Track> rejected;
    const auto frame = unsigned(session_.Document().timeline.currentFrame);
    const bool ok = selectedTrack ? session_.PasteToSelectedTrack(frame, reason, &rejected)
                                  : session_.Paste(frame, mirrored, reason, &rejected);
    if (ok)
        for (auto kind : rejected)
            KeyOverflow(1,
                        kind == editor::Track::Bone    ? format::PmmKeyCapacity<format::PmmBoneKey>
                        : kind == editor::Track::Morph ? 20000
                        : kind == editor::Track::Display ? 1000
                                                         : 10000,
                        format::PmmModelKey(kind));
    return ok;
}
void EditorWindow::CameraScale() {
    CameraScaleDialog dialog(language_, this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::string reason;
    Result(session_.ScaleCameraKeys(dialog.Values(), reason), reason);
    Refresh();
}
void EditorWindow::BoneScale() {
    BoneScaleDialog dialog(language_, this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::string reason;
    Result(session_.ScaleBoneKeys(dialog.Values(), reason), reason);
    Refresh();
}
void EditorWindow::PhysicsKeys() {
    PhysicsKeysDialog dialog(language_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::string reason;
    Result(session_.SetSelectedKeyPhysics(dialog.PhysicsEnabled(), reason), reason);
    Refresh();
}
QString EditorWindow::ModelLabel(std::uint8_t slot) const {
    for (const auto& model : session_.Document().models)
        if (model.slot == slot)
            return QString::fromUtf8(
                editor::EnglishNameDisplay(
                    language_.Code() == "en"
                        ? std::string(model.sourceName.begin(), model.sourceName.end())
                        : std::string(model.name.begin(), model.name.end()))
                    .c_str());
    return {};
}
void EditorWindow::ToonTextures() {
    ToonDialog dialog(session_, language_, this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    dialog.missing = [this](const QString& name) {
        lastError_ = language_.Text("dialog.toon.missing").arg(name);
        if (!testing_) {
            QMessageBox message(QMessageBox::NoIcon, language_.Text("dialog.toon.title"),
                                lastError_, QMessageBox::Ok, this);
            message.setTextFormat(Qt::PlainText);
            message.exec();
        }
    };
    const bool ok = dialog.exec() == QDialog::Accepted;
    Refresh();
    if (ok && !testing_) {
        QMessageBox message(QMessageBox::NoIcon, language_.Text("dialog.enhance.title"),
                            language_.Text("dialog.enhance.preserve"), QMessageBox::Ok, this);
        message.setWindowFlag(Qt::WindowStaysOnTopHint);
        message.setTextFormat(Qt::PlainText);
        message.exec();
    }
}
void EditorWindow::EnglishNames() {
    const auto resource = session_.Runtime().Resource(session_.Selected().model);
    if (!resource || resource->SourceKind() != format::ModelKind::Pmd || session_.Playing())
        return;
    if (englishDialog_ || session_.Selected().model == 255 || session_.Playing())
        return;
    englishDialog_ = new EnglishNameDialog(session_, language_, this);
    englishDialog_->setAttribute(Qt::WA_DeleteOnClose);
    englishDialog_->setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    englishDialog_->changed = [this] { Refresh(); };
    englishDialog_->failed = [this](const std::string& reason) { Result(false, reason); };
    englishDialog_->preserve = [this] {
        const auto title = language_.Text("dialog.enhance.title");
        const auto text = language_.Text("dialog.enhance.preserve");
        if (!testing_) {
            QMessageBox dialog(QMessageBox::NoIcon, title, text, QMessageBox::Ok, this);
            dialog.setTextFormat(Qt::PlainText);
            dialog.QDialog::setWindowTitle(title);
            dialog.exec();
        }
    };
    connect(englishDialog_, &QObject::destroyed, this, [this] { Refresh(); });
    Refresh();
    if (!testing_)
        englishDialog_->show();
}
void EditorWindow::ModelPhysics() {
    const auto resource = session_.Runtime().Resource(session_.Selected().model);
    if (physicsDialog_ || !resource || !resource->PmdSource() || session_.Playing())
        return;
    physicsDialog_ = new ModelPhysicsDialog(session_, language_, this);
    physicsDialog_->setAttribute(Qt::WA_DeleteOnClose);
    physicsDialog_->setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    physicsDialog_->changed = [this] { Refresh(); };
    physicsDialog_->failed = [this](const std::string& reason) { Result(false, reason); };
    physicsDialog_->report = [this](const QString& title, const QString& text) {
        lastError_ = text;
        if (!testing_) {
            QMessageBox message(QMessageBox::NoIcon, title, text, QMessageBox::Ok, this);
            message.setTextFormat(Qt::PlainText);
            message.exec();
        }
    };
    physicsDialog_->preserve = [this] {
        if (!testing_) {
            QMessageBox message(QMessageBox::NoIcon, language_.Text("dialog.enhance.title"),
                                language_.Text("dialog.enhance.preserve"), QMessageBox::Ok, this);
            message.setTextFormat(Qt::PlainText);
            message.exec();
        }
    };
    connect(physicsDialog_, &QObject::destroyed, this, [this] { Refresh(); });
    Refresh();
    if (!testing_)
        physicsDialog_->show();
}
void EditorWindow::ModelOrder(bool calculation) {
    const auto kind =
        calculation ? format::ModelOrderKind::Calculation : format::ModelOrderKind::Draw;
    std::vector<std::pair<std::uint8_t, QString>> rows;
    for (auto slot : format::PmmModelOrder(session_.Document(), kind))
        rows.emplace_back(slot, ModelLabel(slot));
    ModelOrderDialog dialog(calculation, language_, rows, this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::string reason;
    Result(session_.SetModelOrder(kind, dialog.Order(), reason), reason);
    Refresh();
}
void EditorWindow::AccessoryOrder() {
    std::vector<std::pair<std::uint8_t, QString>> rows;
    for (const auto& row : format::PmmAccessoryOrder(session_.Document()))
        rows.emplace_back(row.slot,
                          Name({row.name.begin(), std::find(row.name.begin(), row.name.end(), 0)}));
    AccessoryOrderDialog dialog(language_, rows, session_.Document().physics.accessoryRenderOrder,
                                this);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::string reason;
    if (!Result(session_.SetAccessoryOrder(dialog.Order(), dialog.SplitOrder(), reason), reason))
        return;
    accessoryScopeModel_ = session_.Selected().model;
    accessoryScopeResource_ = session_.Runtime().Resource(accessoryScopeModel_);
    scopeSelections_[session_.Selected().model] = 0;
    Refresh();
}
void EditorWindow::GravitySettings() {
    if (gravityDialog_)
        return;
    gravityDialog_ =
        new GravityDialog(language_, session_.Runtime().Session()->Pose().gravity, this);
    gravityEpoch_ = session_.GravityEpoch();
    gravityDialog_->setAttribute(Qt::WA_DeleteOnClose);
    gravityDialog_->setWindowFlag(Qt::WindowStaysOnTopHint, separate_ != nullptr);
    const auto refresh = [this] {
        gravityEpoch_ = session_.GravityEpoch();
        if (!refreshing_)
            Refresh();
    };
    gravityDialog_->changed = [this, refresh](unsigned channel, float value) {
        std::string reason;
        if (Result(session_.SetGravityChannel(channel, value, reason), reason))
            refresh();
    };
    gravityDialog_->noiseChanged = [this, refresh](bool value) {
        std::string reason;
        if (Result(session_.SetGravityNoise(value, reason), reason))
            refresh();
    };
    gravityDialog_->registered = [this, refresh] {
        std::string reason;
        if (Result(session_.RegisterGravity(reason), reason) || reason == "gravity-key-capacity")
            refresh();
    };
    gravityDialog_->closed = [this, refresh] {
        std::string reason;
        if (Result(session_.RestoreGravity(reason), reason))
            refresh();
    };
    if (!testing_)
        gravityDialog_->show();
}
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
