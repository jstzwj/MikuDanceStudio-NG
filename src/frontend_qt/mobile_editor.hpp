#pragma once
#include "language.hpp"
#include "mikudancestudio/mmd_editor_layout.hpp"
#include <QWidget>
#include <map>
class QStackedWidget;
class QToolBox;
class QPushButton;
class QVBoxLayout;
namespace mikudancestudio::qt {
class Timeline;
class Interpolation;
// Owns layout containers only. Session, commands and editor widgets stay shared
// with the desktop layout; switching size never reconstructs the document.
class MobileEditor final : public QWidget {
public:
    MobileEditor(Language&, QWidget* canvas);
    void Arrange(const std::map<int, QWidget*>&, Timeline*, Interpolation*, QWidget* viewport,
                 bool cameraMode);
    void Restore(const std::map<int, QWidget*>&, Timeline*, Interpolation*, QWidget* viewport);
    static bool Required(QSize size);

private:
    Language& language_;
    QWidget* view_ = nullptr;
    QWidget* drawer_ = nullptr;
    QWidget* timelinePage_ = nullptr;
    QWidget* readouts_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QToolBox* tools_ = nullptr;
    QVBoxLayout* viewLayout_ = nullptr;
    QPushButton *timelineButton_ = nullptr, *toolsButton_ = nullptr;
    std::vector<QWidget*> sections_;
    bool cameraMode_ = true;
    bool arranged_ = false;
    void SelectPage(int);
    void BuildSections(bool);
};
}
