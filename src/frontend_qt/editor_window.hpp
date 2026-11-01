#pragma once
#include "timeline.hpp"
#include "interpolation.hpp"
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
    explicit EditorWindow(std::filesystem::path assets, bool testing = false,
                          std::unique_ptr<AudioOutput> audio = {});
    ~EditorWindow() override;
    editor::EditorSession& Session() { return session_; }
    QWidget* Control(int id) const;
    Timeline* TimelineWidget() const { return timeline_; }
    Interpolation* InterpolationWidget() const { return interpolation_; }
    Viewport* RenderWindow() const { return viewport_; }
    bool SetLanguage(const QString&);
    QString CurrentLanguage() const { return language_.Code(); }
    bool Open(const std::filesystem::path&);
    bool Save(const std::filesystem::path&);
    void Dispatch(int id);
    void Refresh();
    void Tick(double elapsedSeconds);
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
    Interpolation* interpolation_ = nullptr;
    Viewport* viewport_ = nullptr;
    QWidget* viewportContainer_ = nullptr;
    std::map<int, QWidget*> controls_;
    std::map<QString, QAction*> actions_;
    std::map<int, QAction*> menuActions_;
    ui::EditorLayoutOptions layout_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool testing_ = false, refreshing_ = false;
    int side_ = 250;
    int sidebarClientWidth_ = 0, sidebarStoredWidth_ = 250;
    float sidebarRatio_ = 0;
    bool sidebarDragging_ = false;
    QCursor sidebarCursor_;
    int frameNormalization_ = 100;
    std::map<std::uint8_t, int> scopeSelections_;
    std::map<std::uint8_t, std::weak_ptr<const runtime::ModelResource>> scopeResources_;
    std::vector<editor::KeyScope> scopes_;
    QString lastError_;
    AudioTransport audio_;
    void Layout();
    void FinishSidebarDrag();
    void BuildMenus();
    void RefreshMenus();
    void MenuCommand(int);
    void Texts();
    void EditField(int);
    void Slider(int, int);
    void Combo(int, int);
    void RefreshPanels();
    void RefreshScopes();
    bool PanelCommand(int);
    bool PanelEdit(int, double);
    bool PanelSlider(int, int);
    bool PanelCombo(int, int);
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
