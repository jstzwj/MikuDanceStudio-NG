#include "editor_window.hpp"
#include "numeric_dialog.hpp"
#include "english_name_dialog.hpp"
#include "model_physics_dialog.hpp"
#include "toon_dialog.hpp"
#include "model_labels.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/editor_numeric.hpp"
#include "mikudancestudio/editor_rotation.hpp"
#include <QColorDialog>
#include <QMessageBox>

namespace mikudancestudio::qt {
using namespace editor;
using namespace panel;
namespace {
QString Name(const std::vector<char>& bytes) {
    std::string name;
    text::DecodeCp932({bytes.data(), bytes.size()}, name);
    return QString::fromUtf8(name.c_str());
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
    return ModelDisplayLabel(session_, slot, language_.EnglishLayout());
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

}
