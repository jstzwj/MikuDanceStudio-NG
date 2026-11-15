#pragma once
#include "video_encoder.hpp"
#include "language.hpp"
#include <QDialog>
#include <QThread>
#include <QTimer>
#include <atomic>
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;
namespace mikudancestudio::qt {
struct ExportTask final : QThread {
    render::ExportSnapshot snapshot;
    render::ExportSettings settings;
    rhi::BackendOptions backend;
    std::filesystem::path builtins, destination;
    int format = 0;
    bool audio = true, success = false;
    std::string reason;
    std::atomic<bool> cancel{false};
    std::atomic<unsigned> completed{0}, total{1};
    ~ExportTask() override;
    void run() override;
};
class ExportDialog final : public QDialog {
public:
    ExportDialog(editor::EditorSession&, Language&, std::filesystem::path builtins,
                 std::function<rhi::BackendOptions()>, QWidget* parent);
    ~ExportDialog() override;
    void Retranslate();
    void SelectVideo(bool video);
    bool Running() const;

protected:
    void closeEvent(QCloseEvent*) override;

private:
    void Start();
    void Poll();
    editor::EditorSession& session_;
    Language& language_;
    std::filesystem::path builtins_;
    std::function<rhi::BackendOptions()> backend_;
    std::unique_ptr<ExportTask> task_;
    QTimer timer_;
    QComboBox *format_, *rate_;
    QSpinBox *first_, *last_, *width_, *height_;
    QCheckBox *alpha_, *audio_;
    QLineEdit* destination_;
    QPushButton *browse_, *start_, *cancel_;
    QProgressBar* progress_;
    QLabel* status_;
    QWidget* settings_;
    bool closeAfter_ = false;
};
}
