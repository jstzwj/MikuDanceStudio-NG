#pragma once
#include "mmd_editor_chrome.hpp"
namespace mikudancestudio::ui {
struct PanelLabel {
    const char* id;
    int size, x, y;
};
// MMD label anchors; English and Japanese have distinct positions.
inline std::vector<PanelLabel> MmdPanelLabels(int side, int bottom,
                                              const EditorLayoutOptions& options, bool en) {
    std::vector<PanelLabel> result;
    const auto label = [&](const char* id, int size, int x, int y) {
        result.push_back({id, size, x, y});
    };
    if (en) {
        label("chrome.text.frame_manipulation", 12, side / 2 - 50, 51);
        label("chrome.text.dash", 12, side / 2 + 55, bottom - 198);
    } else {
        label("chrome.FrameManip", 12, side / 2 - 35, 51);
        label("chrome.Wave", 12, side / 2 + 52, bottom - 198);
    }

    if (en) {
        label("chrome.text.Interpolation_curve", 12, 60, bottom - 155);
        label("chrome.text.auto", 12, 160, bottom - 104);
    } else {
        label("chrome.InterpCurve", 12, 70, bottom - 155);
        label("chrome.AutoSet", 12, 160, bottom - 104);
    }

    if (en)
        label("chrome.text.bone_frame_manipulation", 12, side / 2 - 70, 7);
    else
        label("chrome.BoneFrameManip", 12, side / 2 - 55, 7);

    if (en) {
        label("chrome.text.model_manipulation", 12, 238, bottom - 155);
        label("chrome.text.disp", 12, 240, bottom - 83);
    } else {
        label("chrome.ModelManip", 12, 258, bottom - 155);
        label("chrome.Disp", 12, 240, bottom - 83);
    }
    label("chrome.text.ON", 12, 244, bottom - 35);
    label("chrome.Off", 12, 244, bottom - 18);

    // --- manipulation-mode panel labels ----------------------------------
    // v9 accumulates the horizontal shift of the right-hand panels exactly
    // like the original (each active panel reserves its width).
    const BottomPanelLayout panelLayout = ComputeBottomPanelLayout(options);
    int panelShift = panelLayout.leading;
    if (options.cameraMode != 0) {
        if (options.cameraPanelExpanded != 0) {
            if (en) {
                label("chrome.text.camera", 12, 423, bottom - 155);
                label("chrome.text.view_angle", 11, 368, bottom - 107);
                label("chrome.text.follow_bone", 11, 368, bottom - 67);
            } else {
                label("chrome.CameraManip", 12, 410, bottom - 155);
                label("chrome.ViewAngle", 11, 368, bottom - 107);
                label("chrome.FollowBone", 11, 368, bottom - 67);
            }
        } else {
            if (en) {
                label("chrome.text.c", 12, 368, bottom - 130);
                label("chrome.text.a", 12, 368, bottom - 110);
                label("chrome.text.m", 12, 366, bottom - 90);
                label("chrome.text.e", 12, 368, bottom - 70);
                label("chrome.text.r", 12, 369, bottom - 50);
                label("chrome.text.a", 12, 368, bottom - 30);
            } else {
                label("chrome.Ka", 12, 365, bottom - 130);
                label("chrome.Me", 12, 365, bottom - 110);
                label("chrome.Ra", 12, 365, bottom - 90);
                label("chrome.Sou", 12, 364, bottom - 70);
                label("chrome.Saku", 12, 364, bottom - 50);
            }
        }
    } else if (options.bonePanelExpanded != 0) {
        if (en)
            label("chrome.text.bone_manipulation", 12, 413, bottom - 155);
        else
            label("chrome.BoneManip", 12, 425, bottom - 155);
    } else {
        if (en) {
            label("chrome.text.b", 12, 367, bottom - 130);
            label("chrome.text.o", 12, 367, bottom - 110);
            label("chrome.text.n", 12, 367, bottom - 90);
            label("chrome.text.e", 12, 367, bottom - 70);
        } else {
            label("chrome.Bo", 12, 364, bottom - 130);
            label("chrome.Bar", 12, 364, bottom - 110);
            label("chrome.N", 12, 365, bottom - 90);
            label("chrome.Sou", 12, 364, bottom - 70);
            label("chrome.Saku", 12, 364, bottom - 50);
        }
    }

    // light panel (only reachable in camera mode, mirrors the original)
    if (options.cameraMode != 0) {
        if (options.lightPanelExpanded != 0) {
            if (en) {
                label("chrome.text.light_manipulation", 12, 548 - panelShift, bottom - 155);
                label("chrome.text.R", 12, 502 - panelShift, bottom - 137);
                label("chrome.text.G", 12, 502 - panelShift, bottom - 119);
                label("chrome.text.B", 12, 502 - panelShift, bottom - 101);
                label("chrome.text.X", 12, 502 - panelShift, bottom - 81);
                label("chrome.text.Y", 12, 502 - panelShift, bottom - 63);
                label("chrome.text.Z", 12, 502 - panelShift, bottom - 45);
            } else {
                label("chrome.LightManip", 12, 563 - panelShift, bottom - 155);
                label("chrome.Aka", 12, 503 - panelShift, bottom - 137);
                label("chrome.Midori", 12, 503 - panelShift, bottom - 119);
                label("chrome.Ao", 12, 503 - panelShift, bottom - 101);
                label("chrome.X", 12, 504 - panelShift, bottom - 81);
                label("chrome.Y", 12, 505 - panelShift, bottom - 63);
                label("chrome.Z", 12, 505 - panelShift, bottom - 45);
            }
        } else {
            if (en) {
                label("chrome.text.l", 12, 505 - panelShift, bottom - 130);
                label("chrome.text.i", 12, 505 - panelShift, bottom - 110);
                label("chrome.text.g", 12, 504 - panelShift, bottom - 90);
                label("chrome.text.h", 12, 504 - panelShift, bottom - 70);
                label("chrome.text.t", 12, 505 - panelShift, bottom - 50);
            } else {
                label("chrome.Shou", 12, 502 - panelShift, bottom - 130);
                label("chrome.Mei", 12, 502 - panelShift, bottom - 110);
                label("chrome.Sou", 12, 502 - panelShift, bottom - 90);
                label("chrome.Saku", 12, 502 - panelShift, bottom - 70);
            }
        }
    } else if (options.morphPanelExpanded != 0) {
        // facial panel
        if (en) {
            label("chrome.text.facial_manipulation", 12, 683 - panelShift, bottom - 155);
            label("chrome.text.eyes", 12, 606 - panelShift, bottom - 137);
            label("chrome.text.brow", 12, 606 - panelShift, bottom - 67);
            label("chrome.text.mouth", 12, 736 - panelShift, bottom - 137);
            label("chrome.text.other", 12, 736 - panelShift, bottom - 67);
        } else {
            label("chrome.FacialManip", 12, 703 - panelShift, bottom - 155);
            label("chrome.Me2", 12, 606 - panelShift, bottom - 137);
            label("chrome.Mayu", 12, 606 - panelShift, bottom - 67);
            label("chrome.Lip", 12, 736 - panelShift, bottom - 137);
            label("chrome.Sonota", 12, 736 - panelShift, bottom - 67);
        }
    } else {
        if (en) {
            label("chrome.text.f", 12, 602 - panelShift, bottom - 130);
            label("chrome.text.a", 12, 601 - panelShift, bottom - 110);
            label("chrome.text.c", 12, 601 - panelShift, bottom - 90);
            label("chrome.text.e", 12, 601 - panelShift, bottom - 70);
        } else {
            label("chrome.Hyou", 12, 599 - panelShift, bottom - 130);
            label("chrome.Jou", 12, 599 - panelShift, bottom - 110);
            label("chrome.Sou", 12, 599 - panelShift, bottom - 90);
            label("chrome.Saku", 12, 599 - panelShift, bottom - 70);
        }
    }
    panelShift = panelLayout.afterLightOrFace;

    // self-shadow panel
    if (options.cameraMode != 0) {
        if (options.selfShadowPanelExpanded != 0) {
            if (en) {
                label("chrome.text.self_shadow_manipulation", 12, 708 - panelShift, bottom - 155);
                label("chrome.text.shadow_range", 12, 690 - panelShift, bottom - 95);
                label("chrome.text.detail", 12, 690 - panelShift, bottom - 54);
                label("chrome.text.far", 12, 840 - panelShift, bottom - 54);
            } else {
                label("chrome.SelfShadow", 12, 733 - panelShift, bottom - 155);
                label("chrome.ShadowRange", 12, 690 - panelShift, bottom - 100);
                label("chrome.Kirei", 12, 690 - panelShift, bottom - 53);
                label("chrome.Enpou", 12, 830 - panelShift, bottom - 53);
            }
        } else {
            if (en) {
                label("chrome.text.s", 12, 688 - panelShift, bottom - 130);
                label("chrome.text.s", 12, 688 - panelShift, bottom - 109);
                label("chrome.text.h", 12, 688 - panelShift, bottom - 96);
                label("chrome.text.a", 12, 688 - panelShift, bottom - 83);
                label("chrome.text.d", 12, 688 - panelShift, bottom - 70);
                label("chrome.text.o", 12, 688 - panelShift, bottom - 57);
                label("chrome.text.w", 12, 687 - panelShift, bottom - 44);
            } else {
                label("chrome.Se", 12, 685 - panelShift, bottom - 130);
                label("chrome.Ru", 12, 685 - panelShift, bottom - 110);
                label("chrome.Fu", 12, 685 - panelShift, bottom - 90);
                label("chrome.Kage", 12, 685 - panelShift, bottom - 70);
                label("chrome.Sou", 12, 685 - panelShift, bottom - 50);
                label("chrome.Saku", 12, 685 - panelShift, bottom - 30);
            }
        }
    }
    panelShift = panelLayout.afterSelfShadow;

    // accessory panel
    if (options.cameraMode != 0) {
        panelShift = panelLayout.accessory;
        if (options.accessoryPanelExpanded != 0) {
            if (en) {
                label("chrome.text.accessory_manipulation", 12, 723 - panelShift, bottom - 155);
                label("chrome.text.X", 12, 686 - panelShift, bottom - 68);
                label("chrome.text.Y", 12, 749 - panelShift, bottom - 68);
                label("chrome.text.Z", 12, 812 - panelShift, bottom - 68);
                label("chrome.text.Rx", 12, 684 - panelShift, bottom - 48);
                label("chrome.text.Ry", 12, 747 - panelShift, bottom - 48);
                label("chrome.text.Rz", 12, 810 - panelShift, bottom - 48);
                label("chrome.text.Si", 12, 687 - panelShift, bottom - 28);
                label("chrome.text.Tr", 12, 747 - panelShift, bottom - 28);
                label("chrome.text.display", 12, 837 - panelShift, bottom - 141);
                label("chrome.text.shadow", 12, 837 - panelShift, bottom - 126);
            } else {
                label("chrome.AccManip", 12, 738 - panelShift, bottom - 155);
                label("chrome.X", 12, 685 - panelShift, bottom - 68);
                label("chrome.Y", 12, 748 - panelShift, bottom - 68);
                label("chrome.Z", 12, 811 - panelShift, bottom - 68);
                label("chrome.text.Rx", 12, 683 - panelShift, bottom - 48);
                label("chrome.text.Ry", 12, 746 - panelShift, bottom - 48);
                label("chrome.text.Rz", 12, 809 - panelShift, bottom - 48);
                label("chrome.text.Si", 12, 684 - panelShift, bottom - 28);
                label("chrome.text.Tr", 12, 747 - panelShift, bottom - 28);
                label("chrome.Disp", 12, 840 - panelShift, bottom - 141);
                label("chrome.Kage", 12, 840 - panelShift, bottom - 126);
            }
        } else {
            if (en) {
                label("chrome.text.a", 12, 688 - panelShift, bottom - 130);
                label("chrome.text.c", 12, 688 - panelShift, bottom - 117);
                label("chrome.text.c", 12, 688 - panelShift, bottom - 104);
                label("chrome.text.e", 12, 688 - panelShift, bottom - 91);
                label("chrome.text.s", 12, 688 - panelShift, bottom - 78);
                label("chrome.text.s", 12, 688 - panelShift, bottom - 65);
                label("chrome.text.o", 12, 688 - panelShift, bottom - 52);
                label("chrome.text.r", 12, 689 - panelShift, bottom - 39);
                label("chrome.text.y", 12, 688 - panelShift, bottom - 26);
            } else {
                label("chrome.A", 12, 685 - panelShift, bottom - 130);
                label("chrome.Ku", 12, 685 - panelShift, bottom - 115);
                label("chrome.Se", 12, 685 - panelShift, bottom - 100);
                label("chrome.Sa", 12, 685 - panelShift, bottom - 85);
                label("chrome.Ri", 12, 687 - panelShift, bottom - 70);
                label("chrome.Sou", 12, 685 - panelShift, bottom - 55);
                label("chrome.Saku", 12, 685 - panelShift, bottom - 40);
            }
        }
    }
    panelShift = panelLayout.final;

    // viewpoint cluster
    if (en) {
        label("chrome.text.view", 12, 949 - panelShift, bottom - 155);
        label("chrome.text.model", 12, 918 - panelShift, bottom - 98);
        label("chrome.text.bone", 12, 988 - panelShift, bottom - 98);
    } else {
        label("chrome.Shiten", 12, 943 - panelShift, bottom - 155);
        label("chrome.Model", 12, 918 - panelShift, bottom - 98);
        label("chrome.Bone", 12, 988 - panelShift, bottom - 98);
    }

    // play controls
    if (en) {
        label("chrome.text.dash", 12, 945 - panelShift, bottom - 34);
        label("chrome.text.play", 12, 932 - panelShift, bottom - 77);
        label("chrome.text.repeat", 10, 974 - panelShift, bottom - 57);
        label("chrome.text.from_flame", 10, 902 - panelShift, bottom - 18);
        label("chrome.text.stop_flame", 10, 967 - panelShift, bottom - 18);
    } else {
        label("chrome.Wave", 12, 939 - panelShift, bottom - 34);
        label("chrome.Play", 12, 929 - panelShift, bottom - 77);
        label("chrome.Repeat", 10, 969 - panelShift, bottom - 57);
        label("chrome.FlameStart", 10, 903 - panelShift, bottom - 18);
        label("chrome.FlameStop", 10, 968 - panelShift, bottom - 18);
    }
    if (en)
        label("chrome.text.vol", 12, 1021 - panelShift, bottom - 77);
    else
        label("chrome.Volume", 12, 1016 - panelShift, bottom - 77);

    return result;
}
}
