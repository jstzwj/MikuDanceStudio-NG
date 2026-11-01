#pragma once
#include "mmd_app.hpp"
#include "mmd_editor_chrome.hpp"
namespace mikudancestudio {
using BottomPanelLayout = ui::BottomPanelLayout;
inline ui::EditorLayoutOptions EditorLayout(const MMDApp* app) {
    return {app->CameraMode() != 0,
            app->PanelExpanded(UiPanel::Camera) != 0,
            app->PanelExpanded(UiPanel::Light) != 0,
            app->PanelExpanded(UiPanel::Accessory) != 0,
            app->PanelExpanded(UiPanel::Bone) != 0,
            app->PanelExpanded(UiPanel::Morph) != 0,
            app->PanelExpanded(UiPanel::SelfShadow) != 0};
}
inline BottomPanelLayout ComputeBottomPanelLayout(const MMDApp* app) {
    return ui::ComputeBottomPanelLayout(EditorLayout(app));
}
}
