#pragma once
#include "timeline.hpp"
#include "viewport.hpp"
#include "audio.hpp"
#include "mikudancestudio/mmd_editor_layout.hpp"
#include <QMainWindow>
#include <QElapsedTimer>
#include <QTimer>
#include <QAction>
#include <map>
namespace mikudancestudio::qt {
class EditorWindow final : public QMainWindow {
public:
    explicit EditorWindow(std::filesystem::path assets, bool testing = false);
    ~EditorWindow() override;
    editor::EditorSession& Session() { return session_; }
    QWidget* Control(int id) const;
    Timeline* TimelineWidget() const { return timeline_; }
    Viewport* RenderWindow() const { return viewport_; }
    bool SetLanguage(const QString&);
    QString CurrentLanguage() const { return language_.Code(); }
    bool Open(const std::filesystem::path&);
    bool Save(const std::filesystem::path&);
    void Dispatch(int id);
    void Refresh();
    QString LastError() const { return lastError_; }

protected:
    void resizeEvent(QResizeEvent*) override;
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    editor::EditorSession session_;
    Language language_;
    std::filesystem::path assets_;
    QWidget* canvas_ = nullptr;
    Timeline* timeline_ = nullptr;
    Viewport* viewport_ = nullptr;
    QWidget* viewportContainer_ = nullptr;
    std::map<int, QWidget*> controls_;
    std::map<QString, QAction*> actions_;
    ui::EditorLayoutOptions layout_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool testing_ = false, refreshing_ = false;
    int side_ = 250;
    QString lastError_;
    format::VpdDocument poseClipboard_;
    AudioTransport audio_;
    void Layout();
    void BuildMenus();
    void Texts();
    void EditField(int);
    void Slider(int, int);
    void Combo(int, int);
    void FileAction(const QString&);
    void Numeric();
    bool Result(bool, const std::string&);
    bool ConfirmUnsaved();
    void CreateViewport(const rhi::BackendOptions&);
    void UpdateFrame();
};
std::filesystem::path Path(const QString&);
QString Path(const std::filesystem::path&);
}
