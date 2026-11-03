// Included by the original-executable observer. Synthetic
// controls intercept USER32 calls; the original English-name kernels execute
// without a desktop window, GPU, audio device or original executable entrypoint.
#include "mikudancestudio/english_name_edit.hpp"
struct EnglishComboState {
    std::vector<std::string> names;
    int cursor = -1;
};
std::map<int, std::string> englishInputs;
std::map<int, EnglishComboState> englishCombos;
std::map<int, bool> englishEnabled;
int WINAPI EnglishText(HWND edit, LPSTR text, int maximum) {
    const auto& source = englishInputs[int(reinterpret_cast<std::uintptr_t>(edit))];
    const auto count = std::min(std::size_t(std::max(0, maximum - 1)), source.size());
    std::memcpy(text, source.data(), count);
    if (maximum)
        text[count] = 0;
    return int(count);
}
int WINAPI EnglishLength(HWND edit) {
    return int(englishInputs[int(reinterpret_cast<std::uintptr_t>(edit))].size());
}
LRESULT WINAPI EnglishSend(HWND control, UINT message, WPARAM w, LPARAM l) {
    const int id = int(reinterpret_cast<std::uintptr_t>(control));
    auto& combo = englishCombos[id];
    switch (message) {
    case CB_RESETCONTENT:
        combo = {};
        return 0;
    case CB_ADDSTRING:
        combo.names.emplace_back(reinterpret_cast<const char*>(l));
        return LRESULT(combo.names.size() - 1);
    case CB_GETCURSEL:
        return combo.cursor;
    case CB_GETCOUNT:
        return LRESULT(combo.names.size());
    case CB_SETCURSEL:
        combo.cursor = w < combo.names.size() ? int(w) : -1;
        return combo.cursor;
    case EM_SETSEL:
        return 0;
    case EM_REPLACESEL:
        englishInputs[id] = reinterpret_cast<const char*>(l);
        return 0;
    case BM_SETCHECK:
        return 0;
    default:
        throw std::runtime_error("unexpected English-name control message");
    }
}
BOOL WINAPI EnglishEnable(HWND window, BOOL enabled) {
    englishEnabled[int(reinterpret_cast<std::uintptr_t>(window))] = enabled != 0;
    return TRUE;
}
HWND WINAPI EnglishFocus(HWND control) {
    return control;
}
BOOL WINAPI EnglishDestroy(HWND) {
    return TRUE;
}
unsigned EnglishNameCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    auto child = &ResetChild;
    auto text = &EnglishText;
    auto length = &EnglishLength;
    auto send = &EnglishSend;
    auto enable = &EnglishEnable;
    auto focus = &EnglishFocus;
    auto destroy = &EnglishDestroy;
    auto message = &AboutMessage;
    Patch a(original.Function<void*>(0x128658), &child, sizeof child);
    Patch b(original.Function<void*>(0x128690), &text, sizeof text);
    Patch c(original.Function<void*>(0x1286c8), &length, sizeof length);
    Patch d(original.Function<void*>(0x1287b8), &send, sizeof send);
    Patch e(original.Function<void*>(0x1285d0), &enable, sizeof enable);
    Patch f(original.Function<void*>(0x128720), &destroy, sizeof destroy);
    Patch g(original.Function<void*>(0x128618), &message, sizeof message);
    // UI-only refresh tails, outside the name/selection/mirror kernels.
    const unsigned char ret = 0xc3;
    Patch refresh(original.Function<void*>(0x5f7f0), &ret, 1);
    Patch rows(original.Function<void*>(0xc70e0), &ret, 1);
    Patch readout(original.Function<void*>(0x20ce0), &ret, 1);
    Patch h(original.Function<void*>(0x1286d0), &focus, sizeof focus);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned count = 0;
    for (const char* file : {"english.pmd", "english.pmx"})
        for (bool accepted : {false, true})
            for (unsigned profile = 0; profile < 4; ++profile) {
                EditorSession portable;
                std::string reason;
                Check(portable.ImportModel(root / file, reason), reason.c_str());
                const auto resource = portable.Runtime().Resource(0);
                PmmModelEnglishNames initial;
                Check(portable.ModelEnglishNames(0, initial, reason), reason.c_str());
                auto native = std::make_unique<mdl::ModelRecord>();
                std::vector<mdl::BoneRecord> bones(initial.bones.size());
                std::vector<mdl::MorphRecord> morphs(initial.morphs.size());
                std::vector<mdl::DisplayGroup> groups(initial.groups.size());
                std::vector<mdl::FrameGroup> boneRows(bones.size()), morphRows(morphs.size() - 1);
                std::vector<unsigned char> selection(bones.size());
                const auto copy = [](auto& to, const std::string& from) {
                    Check(from.size() < sizeof(to), "English-name fixture capacity");
                    std::memcpy(to, from.c_str(), from.size() + 1);
                };
                copy(native->nameEn, initial.name);
                copy(native->commentEn, initial.comment);
                for (unsigned i = 0; i < bones.size(); ++i) {
                    copy(bones[i].nameEn, initial.bones[i]);
                    bones[i].type = mdl::BoneType(unsigned(resource->EditingBones()[i].type));
                    boneRows[i].targetIndex = std::uint16_t(i);
                    copy(boneRows[i].nameEn, initial.bones[i]);
                }
                for (unsigned i = 0; i < morphs.size(); ++i) {
                    copy(morphs[i].nameEn, initial.morphs[i]);
                    if (i) {
                        morphRows[i - 1].targetIndex = std::uint16_t(i);
                        copy(morphRows[i - 1].nameEn, initial.morphs[i]);
                    }
                }
                for (unsigned i = 0; i < groups.size(); ++i)
                    copy(groups[i].nameEn, initial.groups[i]);
                native->boneTable = bones.data();
                native->morphs = morphs.data();
                native->displayGroups = groups.data();
                native->groupCount = std::uint8_t(groups.size());
                native->boneCount = unsigned(bones.size());
                native->morphCount = unsigned(morphs.size());
                native->displayBones = boneRows.data();
                native->displayBoneCount = unsigned(boneRows.size());
                native->displayFrames = morphRows.data();
                native->facialFrameCount = std::uint8_t(morphRows.size());
                native->boneSelection = selection.data();
                native->UndoCursor() = native->LatestUndoCursor() = 29;
                native->comboSelIndex = 1;
                std::memset(app.get(), 0, sizeof(*app));
                app->modelSlots[0] = native.get();
                app->sceneModified = 0x7d;
                auto* raw = reinterpret_cast<unsigned char*>(app.get());
                raw[0xa1b90] = 1;
                englishInputs.clear();
                englishCombos.clear();
                englishEnabled.clear();
                aboutMessage = {};
                original.Function<void (*)(HWND)>(0x8ce20)(reinterpret_cast<HWND>(1));
                Check(!englishEnabled[436] && native->selectedBone == 0 && selection[0],
                      "original English-name initialization selection and model lock");
                Check(englishCombos[669].names == initial.bones &&
                          englishCombos[673].names ==
                              std::vector<std::string>(initial.morphs.begin() + 1,
                                                       initial.morphs.end()),
                      "original English-name combo order");
                const auto revision = portable.Revision(),
                           frames = portable.Runtime().Counters().frames;
                for (unsigned kind = 0; kind < 4; ++kind) {
                    const unsigned editIds[]{667, 672, 676, 680};
                    const auto value = profile == 0   ? "short"
                                       : profile == 1 ? std::string(300, 'x')
                                       : profile == 2 ? std::string(18, 'b') + "\x82\xa0"
                                                      : std::string(48, 'g') + "\x82\xa0";
                    englishInputs[int(editIds[kind])] = value;
                    original.Function<void (*)(MMDAppState*, HWND, int)>(0x8c790)(
                        app.get(), reinterpret_cast<HWND>(std::uintptr_t(editIds[kind])),
                        int(kind));
                    const EnglishNameKind kinds[]{EnglishNameKind::Model, EnglishNameKind::Bone,
                                                  EnglishNameKind::Morph, EnglishNameKind::Group};
                    Check(portable.SetEnglishName(0, kinds[kind],
                                                  kind == 2   ? 1
                                                  : kind == 3 ? 3
                                                              : 0,
                                                  EnglishNameDisplay(value), reason),
                          reason.c_str());
                }
                if (profile == 0) {
                    // The actual dialog commits the old cursor before moving.
                    for (unsigned kind = 0; kind < 3; ++kind) {
                        const int comboIds[]{669, 673, 677}, editIds[]{672, 676, 680};
                        const EnglishNameKind kinds[]{EnglishNameKind::Bone, EnglishNameKind::Morph,
                                                      EnglishNameKind::Group};
                        const auto size = unsigned(englishCombos[comboIds[kind]].names.size());
                        unsigned cursor = 0;
                        for (unsigned step = 0; step < size + 2; ++step) {
                            const auto value =
                                "old cursor " + std::to_string(kind) + "/" + std::to_string(step);
                            englishInputs[editIds[kind]] = value;
                            const unsigned first = kind == 1 ? 1 : kind == 2 ? 3 : 0;
                            Check(portable.SetEnglishName(0, kinds[kind], cursor + first, value,
                                                          reason),
                                  reason.c_str());
                            const bool previous = step == size + 1;
                            const unsigned command = 670 + kind * 4 + unsigned(previous);
                            original.Function<INT_PTR (*)(HWND, UINT, WPARAM, LPARAM)>(0x8b780)(
                                reinterpret_cast<HWND>(1), WM_COMMAND, command, 0);
                            cursor = (cursor + (previous ? size - 1 : 1u)) % size;
                            Check(englishCombos[comboIds[kind]].cursor == int(cursor),
                                  "native English-name cursor wrap");
                            PmmModelEnglishNames names;
                            Check(portable.ModelEnglishNames(0, names, reason), reason.c_str());
                            const auto& entries = kind == 0   ? names.bones
                                                  : kind == 1 ? names.morphs
                                                              : names.groups;
                            Check(englishInputs[editIds[kind]] == entries[cursor + first],
                                  "native English-name old input and new field");
                            if (kind == 0) {
                                const auto type = unsigned(resource->EditingBones()[cursor].type);
                                const int expectedBone =
                                    type == 6 || type == 7 || type == 9 ? -1 : int(cursor);
                                Check(native->selectedBone == expectedBone,
                                      "native English-name hidden bone selection");
                                for (unsigned i = 0; i < selection.size(); ++i)
                                    Check(selection[i] == unsigned(int(i) == expectedBone),
                                          "native English-name selection clear");
                            }
                        }
                    }
                }
                englishInputs[668] =
                    profile == 0 ? "line1\r\nline2\nline3" : std::string(250, 'c') + "\r\nA\r\nB";
                Check(portable.SetEnglishName(0, EnglishNameKind::Comment, 0, englishInputs[668],
                                              reason),
                      reason.c_str());
                original.Function<INT_PTR (*)(HWND, UINT, WPARAM, LPARAM)>(0x8b780)(
                    reinterpret_cast<HWND>(1), WM_COMMAND, accepted ? IDOK : IDCANCEL, 0);
                if (accepted)
                    Check(portable.MarkEnhancedModel(0, reason), reason.c_str());
                PmmModelEnglishNames actual;
                Check(portable.ModelEnglishNames(0, actual, reason), reason.c_str());
                Check(actual.name == native->nameEn && actual.comment == native->commentEn,
                      "original English model name/comment differs");
                for (unsigned i = 0; i < bones.size(); ++i)
                    Check(actual.bones[i] == bones[i].nameEn &&
                              actual.bones[i] == boneRows[i].nameEn,
                          "original English bone/mirror differs");
                for (unsigned i = 0; i < morphs.size(); ++i)
                    Check(actual.morphs[i] == morphs[i].nameEn &&
                              (!i || actual.morphs[i] == morphRows[i - 1].nameEn),
                          "original English morph/mirror differs");
                for (unsigned i = 0; i < groups.size(); ++i)
                    Check(actual.groups[i] == groups[i].nameEn, "original English group differs");
                Check(app->sceneModified == 0x7d && raw[0xa1bb4] == accepted &&
                          englishEnabled[436] && native->UndoCursor() == 29 &&
                          native->LatestUndoCursor() == 29 && portable.Revision() == revision &&
                          portable.Runtime().Counters().frames == frames &&
                          aboutMessage.calls == unsigned(accepted),
                      "original English-name apply side effects");
                if (accepted)
                    Check(
                        aboutMessage.title == "enhance model" &&
                            aboutMessage.text ==
                                "Please preserve the edit result as a new model by 'save enhanced model'.",
                        "original English-name preserve reminder");
                ++count;
            }
    return count;
}
