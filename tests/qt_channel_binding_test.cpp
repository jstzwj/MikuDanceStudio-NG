#include "editor_window.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>
#include <QSlider>
#include <QTest>
#include <iostream>
using namespace mikudancestudio;
using namespace editor;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " " << reason << '\n';               \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(QGuiApplication::platformName() == "offscreen" && argc == 3);
    const auto root = std::filesystem::absolute(argv[1]);
    for (const char* file :
         {"facial.pmd", "articulated.pmd", "articulated.pmx", "tint-morph.pmx"}) {
        qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
        auto& session = window.Session();
        CHECK(session.ImportModel(root / file, reason));
        window.Refresh(); // Populate caches before changing only document bindings.
        const auto resource = session.Runtime().Resource(0);
        const auto boneCount = unsigned(resource->BoneNames().size());
        const auto morphCount = unsigned(resource->MorphNames().size());
        CHECK(session.Edit(
            "reverse-document-channels",
            [&](auto& doc, auto&) {
                auto& m = doc.models[0];
                std::reverse(m.boneNames.begin(), m.boneNames.end());
                std::reverse(m.bonePose.begin(), m.bonePose.end());
                std::reverse(m.boneKeys.dense.begin(), m.boneKeys.dense.end());
                std::reverse(m.morphNames.begin(), m.morphNames.end());
                std::reverse(m.morphPose.begin(), m.morphPose.end());
                std::reverse(m.morphKeys.dense.begin(), m.morphKeys.dense.end());
                for (auto& index : m.ikSources)
                    index = int(boneCount) - 1 - index;
                if (m.selection.selectedBone >= 0)
                    m.selection.selectedBone = int(boneCount) - 1 - m.selection.selectedBone;
                return true;
            },
            reason));
        CHECK(session.Runtime().Resource(0) == resource);
        for (bool morph : {false, true}) {
            const auto count = morph ? morphCount : boneCount;
            for (unsigned i = 0; i < count; ++i) {
                int mapped = -2, roundTrip = -2;
                CHECK(morph ? session.Runtime().DocumentMorphIndex(0, int(i), mapped, reason)
                            : session.Runtime().DocumentBoneIndex(0, int(i), mapped, reason));
                CHECK(mapped == int(count - 1 - i));
                CHECK(morph ? session.Runtime().ResourceMorphIndex(0, mapped, roundTrip, reason)
                            : session.Runtime().ResourceBoneIndex(0, mapped, roundTrip, reason));
                CHECK(roundTrip == int(i));
            }
            for (int invalid : {-1, int(count), 999999}) {
                int output = 123;
                CHECK(!(morph ? session.Runtime().DocumentMorphIndex(0, invalid, output, reason)
                              : session.Runtime().DocumentBoneIndex(0, invalid, output, reason)));
                CHECK(output == 123 && !reason.empty());
                CHECK(!(morph ? session.Runtime().ResourceMorphIndex(0, invalid, output, reason)
                              : session.Runtime().ResourceBoneIndex(0, invalid, output, reason)));
                CHECK(output == 123 && !reason.empty());
            }
        }
        window.Refresh(); // Same resource, language and group flags: cache must invalidate.
        QTest::mouseClick(window.TimelineWidget(), Qt::LeftButton, Qt::NoModifier, QPoint(30, 23));
        CHECK(session.ActiveBone() == int(boneCount - 1 - resource->DisplayRootBone()));
        for (const char* language : {"en", "ja", "zh_CN"}) {
            CHECK(window.SetLanguage(language));
            CHECK(session.Seek(0, reason));
            window.Refresh();
            const auto frames = session.Runtime().Counters().frames;
            auto* scope = static_cast<QComboBox*>(window.Control(panel::kRegisterScopeCombo));
            static_cast<QLineEdit*>(window.Control(panel::kRangeStartEdit))->setText("0");
            static_cast<QLineEdit*>(window.Control(panel::kRangeStopEdit))->setText("0");
            for (unsigned lane = 0; lane < 4; ++lane) {
                auto* combo =
                    static_cast<QComboBox*>(window.Control(panel::kMorphCombo0 + lane * 5));
                unsigned listed = 0;
                for (unsigned i = 0; i < morphCount; ++i)
                    if (resource->Document().morphs[i].header.panel == lane + 1) {
                        const unsigned channel = morphCount - 1 - i;
                        CHECK(combo->itemData(int(listed)).toUInt() == channel);
                        CHECK(combo->itemText(int(listed)) ==
                              QString::fromUtf8(resource->MorphNames()[i].c_str()));
                        combo->setCurrentIndex(int(listed++));
                        combo->activated(combo->currentIndex());
                        auto* slider =
                            static_cast<QSlider*>(window.Control(panel::kMorphSlider0 + lane * 5));
                        slider->setValue(int(25 + i * 10));
                        CHECK(std::abs(session.Document().models[0].morphPose[channel] -
                                       float(25 + i * 10) / 100) < 1e-6f);
                    }
                CHECK(unsigned(combo->count()) == listed);
            }
            // Root and group entries carry resource identities, while their key
            // selection must address the reversed PMM channel numbers.
            unsigned row = 0;
            for (const auto& group : resource->DisplayGroups()) {
                if (group.role == 1) {
                    ++row;
                    continue;
                }
                for (const auto& entry : group.entries) {
                    const unsigned channel =
                        (entry.morph ? morphCount : boneCount) - 1 - entry.index;
                    const auto kind = entry.morph ? Track::Morph : Track::Bone;
                    session.Selected().keys.clear();
                    const auto& names = entry.morph ? resource->Document().morphs[entry.index].names
                                                    : resource->Document().bones[entry.index].names;
                    const auto& bytes =
                        std::string(language) == "en" && !names[1].empty() ? names[1] : names[0];
                    std::string name;
                    CHECK(text::DecodeUtf16Le(bytes.data(), bytes.size(), name));
                    const int index = scope->findText(QString::fromUtf8(name.c_str()));
                    CHECK(index >= 0);
                    scope->setCurrentIndex(index);
                    scope->activated(index);
                    window.Dispatch(panel::kSelectRangeButton);
                    CHECK(session.Selected().keys.size() == 1);
                    CHECK(session.Selected().keys.count({kind, 0, channel, 0}));
                }
                if (group.role == 0 && !group.entries.empty()) {
                    QTest::mouseClick(window.TimelineWidget(), Qt::LeftButton, Qt::NoModifier,
                                      QPoint(30, int(23 + row * 14)));
                    CHECK(session.ActiveBone() == int(boneCount - 1 - group.entries[0].index));
                } else if (group.role >= 2) {
                    QTest::mouseClick(window.TimelineWidget(), Qt::LeftButton, Qt::NoModifier,
                                      QPoint(30, int(23 + row * 14)));
                    for (const auto& entry : group.entries) {
                        const unsigned channel =
                            (entry.morph ? morphCount : boneCount) - 1 - entry.index;
                        if (entry.morph)
                            CHECK(session.Selected().tracks.count({Track::Morph, 0, channel, 0}));
                        else
                            CHECK(std::find(session.Selected().bones.begin(),
                                            session.Selected().bones.end(),
                                            channel) != session.Selected().bones.end());
                    }
                    const QPoint header(5, int(23 + row * 14));
                    QTest::mouseClick(window.TimelineWidget(), Qt::LeftButton, Qt::NoModifier,
                                      header);
                    for (unsigned n = 0; n < group.entries.size(); ++n) {
                        const auto& entry = group.entries[n];
                        const unsigned channel =
                            (entry.morph ? morphCount : boneCount) - 1 - entry.index;
                        QTest::mouseClick(window.TimelineWidget(), Qt::LeftButton, Qt::NoModifier,
                                          QPoint(30, int(23 + (row + n + 1) * 14)));
                        if (entry.morph)
                            CHECK(session.Selected().tracks.size() == 1 &&
                                  session.Selected().tracks.count({Track::Morph, 0, channel, 0}));
                        else
                            CHECK(session.Selected().bones == std::vector<std::uint32_t>{channel} &&
                                  session.ActiveBone() == int(channel));
                    }
                    QTest::mouseClick(window.TimelineWidget(), Qt::LeftButton, Qt::NoModifier,
                                      header);
                }
                ++row;
            }
            auto* ik = static_cast<QComboBox*>(window.Control(panel::kIkChainCombo));
            const auto& model = session.Document().models[0];
            CHECK(unsigned(ik->count()) == model.ikSources.size());
            if (ik->count()) {
                CHECK(ik->currentText() == "I");
                static_cast<QAbstractButton*>(window.Control(panel::kIkOffRadio))->click();
                CHECK(!session.Document().models[0].ikPose[0]);
                static_cast<QAbstractButton*>(window.Control(panel::kIkOnRadio))->click();
                CHECK(session.Document().models[0].ikPose[0]);
            }
            CHECK(session.Runtime().Counters().frames == frames);
            CHECK(!window.isVisible());
            CHECK(window.Save(root / "qt-bindings.pmm") && window.Open(root / "qt-bindings.pmm"));
            int mapped;
            CHECK(session.Runtime().DocumentBoneIndex(0, 0, mapped, reason) &&
                  mapped == int(boneCount - 1));
            CHECK(session.Runtime().DocumentMorphIndex(0, 0, mapped, reason) &&
                  mapped == int(morphCount - 1));
        }
        CHECK(session.UnloadModel(0, reason));
        int output = 123;
        CHECK(!session.Runtime().ResourceMorphIndex(0, 0, output, reason) && output == 123);
        CHECK(!session.Runtime().ResourceBoneIndex(0, 0, output, reason) && output == 123);
    }
    std::cout
        << "Reordered PMM channels: Morph controls, timeline, scopes, IK and saved reload passed\n";
}
