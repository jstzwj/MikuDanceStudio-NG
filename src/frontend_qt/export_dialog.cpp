#include "export_dialog.hpp"
#include "editor_window.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
namespace mikudancestudio::qt {
ExportTask::~ExportTask() {
    cancel.store(true);
    wait();
}
void ExportTask::run() {
    auto progress = [&](unsigned done, unsigned count) {
        completed.store(done);
        total.store(count);
        return !cancel.load();
    };
    try {
        success = format == 0 ? ExportImages(snapshot, builtins, backend, destination, settings,
                                             progress, reason)
                              : ExportMovie(snapshot, builtins, backend, destination, settings,
                                            format == 1 ? VideoProfile::Mp4 : VideoProfile::Mp4Pcm,
                                            audio, progress, reason);
    } catch (const std::exception& error) {
        reason = std::string("export-exception:") + error.what();
    } catch (...) {
        reason = "export-exception";
    }
}
ExportDialog::ExportDialog(editor::EditorSession& session, Language& language,
                           std::filesystem::path builtins,
                           std::function<rhi::BackendOptions()> backend, QWidget* parent)
    : QDialog(parent), session_(session), language_(language), builtins_(std::move(builtins)),
      backend_(std::move(backend)) {
    setObjectName("export.dialog");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(530, 430);
    auto* layout = new QVBoxLayout(this);
    settings_ = new QWidget(this);
    auto* form = new QFormLayout(settings_);
    const auto label = [&](const char* id) {
        auto* text = new QLabel(settings_);
        text->setProperty("exportText", id);
        return text;
    };
    format_ = new QComboBox(settings_);
    format_->setObjectName("export.format");
    format_->addItems({"PNG", "MP4 · MPEG-4 / AAC", "MP4 · Motion JPEG / PCM"});
    form->addRow(label("export.format"), format_);
    rate_ = new QComboBox(settings_);
    rate_->setObjectName("export.rate");
    rate_->setEditable(true);
    rate_->setInsertPolicy(QComboBox::NoInsert);
    rate_->addItems({"24", "25", "30", "50", "60", "24000/1001", "30000/1001", "60000/1001"});
    rate_->setCurrentIndex(2);
    form->addRow(label("export.rate"), rate_);
    const auto pair = [&](QSpinBox*& a, QSpinBox*& b, const char* name, int low, int high, int av,
                          int bv) {
        auto* row = new QWidget(settings_);
        auto* boxes = new QHBoxLayout(row);
        boxes->setContentsMargins(0, 0, 0, 0);
        a = new QSpinBox(row);
        b = new QSpinBox(row);
        a->setRange(low, high);
        b->setRange(low, high);
        a->setValue(av);
        b->setValue(bv);
        a->setObjectName(QString("export.") + name + ".first");
        b->setObjectName(QString("export.") + name + ".last");
        boxes->addWidget(a);
        boxes->addWidget(b);
        return row;
    };
    const auto& d = session.Document();
    form->addRow(label("export.range"), pair(first_, last_, "range", 0, 30 * 60 * 60 * 6 - 1,
                                             std::max(0, d.timeline.playStartFrame),
                                             std::max(0, d.timeline.playStopFrame)));
    form->addRow(label("export.size"),
                 pair(width_, height_, "size", 1, 8192, d.envelope.settings.renderWidth,
                      d.envelope.settings.renderHeight));
    alpha_ = new QCheckBox(settings_);
    alpha_->setObjectName("export.alpha");
    alpha_->setChecked(true);
    audio_ = new QCheckBox(settings_);
    audio_->setObjectName("export.audio");
    audio_->setChecked(d.wave.enabled);
    form->addRow(alpha_);
    form->addRow(audio_);
    auto* row = new QWidget(settings_);
    auto* paths = new QHBoxLayout(row);
    paths->setContentsMargins(0, 0, 0, 0);
    destination_ = new QLineEdit(row);
    destination_->setObjectName("export.destination");
    browse_ = new QPushButton(row);
    paths->addWidget(destination_);
    paths->addWidget(browse_);
    form->addRow(label("export.destination"), row);
    auto* note = label("export.note");
    note->setWordWrap(true);
    form->addRow(note);
    layout->addWidget(settings_);
    progress_ = new QProgressBar(this);
    progress_->setObjectName("export.progress");
    layout->addWidget(progress_);
    status_ = new QLabel(this);
    status_->setObjectName("export.status");
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    start_ = new QPushButton(this);
    start_->setObjectName("export.start");
    cancel_ = new QPushButton(this);
    cancel_->setObjectName("export.cancel");
    buttons->addWidget(start_);
    buttons->addWidget(cancel_);
    layout->addLayout(buttons);
    connect(format_, &QComboBox::currentIndexChanged, this, [this](int index) {
        alpha_->setEnabled(index == 0);
        audio_->setEnabled(index != 0);
    });
    audio_->setEnabled(false);
    connect(browse_, &QPushButton::clicked, this, [this] {
        const auto filter = format_->currentIndex() ? "MP4 (*.mp4)" : QString();
        QFileDialog dialog(this, language_.Text("export.destination"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setOption(QFileDialog::DontUseNativeDialog);
        dialog.setNameFilter(filter);
        dialog.selectFile(format_->currentIndex() ? "video.mp4" : "frames");
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
            destination_->setText(dialog.selectedFiles().front());
    });
    connect(start_, &QPushButton::clicked, this, [this] { Start(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (Running()) {
            task_->cancel.store(true);
            status_->setText(language_.Text("export.stopping"));
        } else
            close();
    });
    timer_.setInterval(40);
    connect(&timer_, &QTimer::timeout, this, [this] { Poll(); });
    Retranslate();
}
ExportDialog::~ExportDialog() {
    timer_.stop();
    task_.reset();
}
bool ExportDialog::Running() const {
    return task_ && task_->isRunning();
}
void ExportDialog::SelectVideo(bool video) {
    if (!Running())
        format_->setCurrentIndex(video ? 1 : 0);
}
void ExportDialog::Retranslate() {
    setWindowTitle(language_.Text("export.title"));
    for (auto* label : settings_->findChildren<QLabel*>()) {
        const auto id = label->property("exportText").toString();
        if (!id.isEmpty())
            label->setText(language_.Text(id));
    }
    format_->setItemText(0, language_.Text("export.png"));
    alpha_->setText(language_.Text("export.alpha"));
    audio_->setText(language_.Text("export.audio"));
    browse_->setText(language_.Text("export.browse"));
    start_->setText(language_.Text("export.start"));
    cancel_->setText(language_.Text("dialog.cancel"));
}
void ExportDialog::Start() {
    if (Running())
        return;
    auto task = std::make_unique<ExportTask>();
    task->settings.first = unsigned(first_->value());
    task->settings.last = unsigned(last_->value());
    task->settings.width = unsigned(width_->value());
    task->settings.height = unsigned(height_->value());
    auto rate = rate_->currentText().split('/');
    bool numeratorValid = false, denominatorValid = true;
    task->settings.rateNumerator = rate[0].toUInt(&numeratorValid);
    task->settings.rateDenominator = rate.size() == 2 ? rate[1].toUInt(&denominatorValid) : 1;
    if (rate.size() > 2 || !numeratorValid || !denominatorValid) {
        status_->setText("invalid-export-rate");
        return;
    }
    task->format = format_->currentIndex();
    task->audio = audio_->isChecked();
    task->settings.alpha = task->format == 0 && alpha_->isChecked() ? render::ExportAlpha::Straight
                                                                    : render::ExportAlpha::Opaque;
    render::ExportTimeline timeline;
    std::string reason;
    if (destination_->text().trimmed().isEmpty() ||
        !render::MakeExportTimeline(task->settings, timeline, reason)) {
        status_->setText(
            QString::fromStdString(reason.empty() ? "export-destination-required" : reason));
        return;
    }
    task->snapshot = render::CaptureExport(session_);
    task->snapshot.sharedToons = Path(QCoreApplication::applicationDirPath()) / "data";
    task->backend = backend_();
    task->backend.surface.reset();
    task->builtins = builtins_;
    task->destination = Path(destination_->text());
    task_ = std::move(task);
    settings_->setEnabled(false);
    start_->setEnabled(false);
    closeAfter_ = false;
    status_->setText(language_.Text("export.working"));
    progress_->setRange(0, int(timeline.frames));
    progress_->setValue(0);
    task_->start();
    timer_.start();
}
void ExportDialog::Poll() {
    if (!task_)
        return;
    progress_->setMaximum(int(task_->total.load()));
    progress_->setValue(int(task_->completed.load()));
    if (task_->isRunning())
        return;
    task_->wait();
    timer_.stop();
    settings_->setEnabled(true);
    start_->setEnabled(true);
    status_->setText(task_->success                        ? language_.Text("export.done")
                     : task_->reason == "export-cancelled" ? language_.Text("export.cancelled")
                                                           : QString::fromStdString(task_->reason));
    if (closeAfter_)
        close();
}
void ExportDialog::closeEvent(QCloseEvent* event) {
    if (Running()) {
        closeAfter_ = true;
        task_->cancel.store(true);
        status_->setText(language_.Text("export.stopping"));
        event->ignore();
    } else
        QDialog::closeEvent(event);
}
}
