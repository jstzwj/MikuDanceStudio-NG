#include "editor_window.hpp"
#include "model_physics_dialog.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/pmd_writer.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QMouseEvent>
#include <QFocusEvent>
#include "mikudancestudio/background_placement.hpp"
#include <QSignalBlocker>
#include <fstream>
#include <iostream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ' ' << reason << '\n';                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
unsigned Bits(float value) {
    unsigned result;
    std::memcpy(&result, &value, 4);
    return result;
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
        bool validScale = false;
        const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&validScale);
        CHECK(validScale && std::abs(app.devicePixelRatio() - scale) < 1e-6);
    }

    CHECK(argc == 5 && QGuiApplication::platformName() == "offscreen");
    const auto root = std::filesystem::absolute(argv[1]);
    QFile templates(QString::fromLocal8Bit(argv[3])), evidence(QString::fromLocal8Bit(argv[4]));
    CHECK(templates.open(QIODevice::ReadOnly) && evidence.open(QIODevice::ReadOnly));
    const auto references = QJsonDocument::fromJson(templates.readAll()).object();
    const auto snapshots = QJsonDocument::fromJson(evidence.readAll()).object();
    editor::EditorSession seed;
    CHECK(seed.ImportModel(root / "triangle.pmd", reason));
    auto source = *seed.Runtime().Resource(0)->PmdSource();
    source.bones.resize(2, source.bones[0]);
    source.rigids.clear();
    source.joints.clear();
    source.englishBoneNames.resize(2);
    source.englishMorphNames.resize(source.morphs.empty() ? 0 : source.morphs.size() - 1);
    source.englishGroupNames.resize(source.groupNames.size());
    for (unsigned i = 0; i < 2; ++i) {
        std::snprintf(source.bones[i].pmdName, 20, "BONE_%u", i);
        std::snprintf(source.englishBoneNames[i].data(), 20, "BONE_%u", i);
    }
    std::vector<std::uint8_t> bytes;
    CHECK(format::WriteEnhancedPmd(source, bytes, reason));
    {
        std::ofstream file(root / "physics-dialog.pmd", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        CHECK(file);
    }
    for (const char* language : {"en", "ja", "zh_CN"}) {
        qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
        CHECK(window.SetLanguage(language));
        auto& session = window.Session();
        const auto action = [&]() -> QAction* {
            for (auto* item : window.findChildren<QAction*>())
                if (item->property("mmdCommandId").toInt() == MDS_CMD_PHYSICS_MODEL)
                    return item;
            return nullptr;
        };
        CHECK(action() && !action()->isEnabled());
        CHECK(session.ImportModel(root / "physics-dialog.pmd", reason));
        window.Refresh();
        CHECK(action()->isEnabled());
        action()->trigger();
        auto* dialog =
            dynamic_cast<qt::ModelPhysicsDialog*>(window.findChild<QDialog*>("mmd.modelPhysics"));
        CHECK(dialog && dialog->Active() && !dialog->isVisible());
        CHECK(!window.Control(panel::kMainComboModel)->isEnabled() &&
              !window.Control(panel::kPlayButton)->isEnabled());
        window.Dispatch(panel::kPlayButton);
        CHECK(!session.Playing());
        const auto revision = session.Revision();
        unsigned reports = 0;
        dialog->failed = [&](const std::string& message) { reason = message; };
        dialog->report = [&](const QString&, const QString&) { ++reports; };
        const auto profile = std::string(language) == "en" ? "en" : "ja";
        const auto resource = references[QString("model_physics.") + profile].toObject();
        CHECK(dialog->Template().id == resource["id"].toInt());
        const auto controls = resource["controls"].toArray();
        CHECK(dialog->Template().count == std::size_t(controls.size()));
        const int baseY = std::string(language) == "en" ? 15 : 12;
        for (int i = 0; i < controls.size(); ++i) {
            auto* widget = dialog->findChild<QWidget*>(QString("numeric.control.%1").arg(i));
            CHECK(widget);
            const auto control = controls[i].toObject();
            const auto r = control["rect"].toArray();
            CHECK(widget->geometry() ==
                  QRect((r[0].toInt() * 7 + 2) / 4, (r[1].toInt() * baseY + 4) / 8,
                        (r[2].toInt() * 7 + 2) / 4,
                        control["kind"].toString() == "Combobox" ? baseY + 8
                                                                 : (r[3].toInt() * baseY + 4) / 8));
        }
        const auto steps = snapshots[profile].toArray();
        for (int step = 0; step < steps.size(); ++step) {
            const auto state = steps[step].toObject();
            const auto kind = state["action"].toString();
            const int id = state["id"].toInt();
            reason = "step " + std::to_string(step) + " " + kind.toStdString() + " " +
                     std::to_string(id);
            if (kind == "command")
                static_cast<QAbstractButton*>(dialog->Control(id))->click();
            else if (kind == "text" || kind == "enter") {
                auto* field = static_cast<QLineEdit*>(dialog->Control(id));
                field->setText(state["value"].toString());
                if (kind == "enter")
                    QTest::keyClick(field, Qt::Key_Return);
            } else if (kind == "combo") {
                auto* combo = static_cast<QComboBox*>(dialog->Control(id));
                const int row = state["value"].toString().toInt();
                combo->setCurrentIndex(row);
                QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, row));
            }
            CHECK(dialog->SelectedBody() == state["body"].toInt() &&
                  dialog->SelectedJoint() == state["joint"].toInt());
            CHECK(reports == unsigned(state["reports"].toInt()));
            const auto text = state["text"].toObject();
            for (auto i = text.begin(); i != text.end(); ++i) {
                const auto actual =
                    static_cast<QLineEdit*>(dialog->Control(i.key().toInt()))->text();
                if (actual != i.value().toString()) {
                    reason += " field " + i.key().toStdString() +
                              " actual=" + actual.toStdString() +
                              " expected=" + i.value().toString().toStdString();
                    CHECK(false);
                }
            }
            const auto combos = state["combos"].toObject();
            for (auto i = combos.begin(); i != combos.end(); ++i) {
                auto* combo = static_cast<QComboBox*>(dialog->Control(i.key().toInt()));
                const auto expected = i.value().toObject();
                if (combo->count() != expected["count"].toInt() ||
                    combo->currentIndex() != expected["selected"].toInt()) {
                    reason += " combo " + i.key().toStdString() +
                              " actual=" + std::to_string(combo->currentIndex()) +
                              " expected=" + std::to_string(expected["selected"].toInt());
                    CHECK(false);
                }
            }
            const auto& edit = dialog->State();
            const auto bodies = state["bodies"].toArray();
            for (int i = 0; i < bodies.size() && i < int(edit.bodies.size()); ++i) {
                const auto& b = edit.bodies[i];
                const auto expected = bodies[i].toArray();
                QJsonArray actual{edit.BodyOrdinal(i), b.boneIndex,  int(b.group),
                                  int(b.noCollapse),   int(b.shape), int(b.mode)};
                for (float v : b.size)
                    actual.append(double(Bits(v)));
                for (float v : b.position)
                    actual.append(double(Bits(v)));
                for (float v : b.rotation)
                    actual.append(double(Bits(v)));
                for (float v :
                     {b.mass, b.linearDamping, b.angularDamping, b.restitution, b.friction})
                    actual.append(double(Bits(v)));
                if (actual != expected) {
                    reason += " body " + std::to_string(i) + " actual=" +
                              QJsonDocument(actual).toJson(QJsonDocument::Compact).toStdString() +
                              " expected=" +
                              QJsonDocument(expected).toJson(QJsonDocument::Compact).toStdString();
                    CHECK(false);
                }
            }
            const auto joints = state["joints"].toArray();
            for (int i = 0; i < joints.size() && i < int(edit.joints.size()); ++i) {
                const auto& j = edit.joints[i];
                QJsonArray actual{edit.JointOrdinal(i), j.rigidA, j.rigidB};
                for (float v : j.position)
                    actual.append(double(Bits(v)));
                for (float v : j.rotation)
                    actual.append(double(Bits(v)));
                for (float v : j.limits)
                    actual.append(double(Bits(v)));
                for (float v : j.springs)
                    actual.append(double(Bits(v)));
                if (actual != joints[i].toArray()) {
                    reason += " joint " + std::to_string(i);
                    CHECK(false);
                }
            }
            CHECK(session.Revision() == revision && !session.EnhancedModelDirty());
            const auto frames = session.Runtime().Counters().frames;
            const auto builds = window.RenderWindow()->FrameBuilds();
            CHECK(window.RenderWindow()->PrepareFrame(640, 480, reason));
            CHECK(window.RenderWindow()->PrepareFrame(640, 480, reason));
            CHECK(window.RenderWindow()->FrameBuilds() == builds + 2 &&
                  session.Runtime().Counters().frames == frames);
        }
        dialog->reject();
        CHECK(!dialog->Active());
        CHECK(window.RenderWindow()->PrepareFrame(640, 480, reason));
        const auto& restored = window.RenderWindow()->PreparedFrame();
        CHECK(std::none_of(restored.passes.begin(), restored.passes.end(),
                           [](const auto& p) { return p.name == "editor_physics_overlay"; }));
        CHECK(window.Control(panel::kMainComboModel)->isEnabled() &&
              window.Control(panel::kPlayButton)->isEnabled());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        action()->trigger();
        dialog =
            dynamic_cast<qt::ModelPhysicsDialog*>(window.findChild<QDialog*>("mmd.modelPhysics"));
        CHECK(dialog);
        static_cast<QAbstractButton*>(dialog->Control(685))->click();
        auto& viewport = *window.RenderWindow();
        viewport.resize(640, 480);
        const auto poses = session.Document().models[0].bonePose;
        const bool undo = session.CanUndo();
        const auto dragRevision = session.Revision();
        const auto mouse = [&](QEvent::Type type, QPointF p, Qt::MouseButton button,
                               Qt::MouseButtons buttons,
                               Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QMouseEvent event(type, p, p, p, button, buttons, mods);
            QCoreApplication::sendEvent(&viewport, &event);
        };
        for (bool jointPage : {false, true}) {
            if (jointPage) {
                static_cast<QAbstractButton*>(dialog->Control(735))->click();
                static_cast<QAbstractButton*>(dialog->Control(801))->click();
            }
            for (bool upper : {false, true})
                for (int axis = 0; axis < 3; ++axis)
                    for (bool shift : {false, true}) {
                        // The never-shown host has no laid-out canvas. Its
                        // refresh can shrink the child; restore test geometry.
                        viewport.resize(640, 480);
                        const auto ratio = viewport.devicePixelRatio();
                        const int w = qRound(viewport.width() * ratio),
                                  h = qRound(viewport.height() * ratio);
                        const auto scale = render::BackgroundViewScale(w);
                        const QPointF point((w - (110 - 40 * axis) * scale) / ratio,
                                            (h - (upper ? 65 : 25) * scale) / ratio);
                        const auto start = point.toPoint();
                        const auto finish = start - QPoint(0, 10);
                        const int dy = qRound(finish.y() * ratio) - qRound(start.y() * ratio);
                        auto expected = dialog->State();
                        const unsigned mode = unsigned((upper ? 4 : 10) + axis);
                        const auto echo = expected.Drag(jointPage, 0, mode, dy, shift);
                        mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
                        if (viewport.cursor().shape() != Qt::BlankCursor)
                            std::cerr << "drag page=" << jointPage << " upper=" << upper
                                      << " axis=" << axis << " shift=" << shift
                                      << " model=" << int(session.Selected().model)
                                      << " point=" << start.x() << ',' << start.y()
                                      << " size=" << viewport.width() << ',' << viewport.height()
                                      << " active=" << dialog->Active() << '\n';
                        CHECK(viewport.cursor().shape() == Qt::BlankCursor);
                        mouse(QEvent::MouseMove, finish, Qt::NoButton, Qt::LeftButton,
                              shift ? Qt::ShiftModifier : Qt::ControlModifier);
                        QFocusEvent focus(QEvent::FocusOut);
                        QCoreApplication::sendEvent(&viewport, &focus);
                        CHECK(viewport.cursor().shape() != Qt::BlankCursor);
                        CHECK(!std::memcmp(&expected.bodies[0], &dialog->State().bodies[0],
                                           sizeof(format::RigidData)));
                        if (jointPage)
                            CHECK(!std::memcmp(&expected.joints[0], &dialog->State().joints[0],
                                               sizeof(format::JointData)));
                        char text[256];
                        std::snprintf(text, sizeof text, echo.format, double(echo.value));
                        CHECK(static_cast<QLineEdit*>(dialog->Control(echo.control))->text() ==
                              QString::fromLatin1(text));
                        mouse(QEvent::MouseMove, finish - QPoint(0, 20), Qt::NoButton,
                              Qt::LeftButton);
                        CHECK(session.Revision() == dragRevision && session.CanUndo() == undo);
                        CHECK(session.Document().models[0].bonePose.size() == poses.size());
                        for (unsigned i = 0; i < poses.size(); ++i)
                            CHECK(session.Document().models[0].bonePose[i].components ==
                                  poses[i].components);
                    }
        }
        CHECK(!viewport.isVisible() && viewport.Submissions() == 0);
        dialog->accept();
        CHECK(session.EnhancedModelDirty() &&
              session.Runtime().Resource(0)->PmdSource()->rigids.size() == 1);
        CHECK(window.Control(panel::kPlayButton)->isEnabled());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        action()->trigger();
        dialog =
            dynamic_cast<qt::ModelPhysicsDialog*>(window.findChild<QDialog*>("mmd.modelPhysics"));
        CHECK(dialog);
        CHECK(session.UnloadModel(0, reason));
        window.Refresh();
        CHECK(!dialog->Active());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(session.ImportModel(root / "triangle.pmx", reason));
        window.Refresh();
        CHECK(!action()->isEnabled());
    }
    std::cout
        << "Three-language physics dialog, original snapshots, lifecycle, cancel and apply passed\n";
}
