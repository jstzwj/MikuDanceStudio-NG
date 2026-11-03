// Original dialog 684/683 kernels, intercepted USER32 controls. No window,
// original entrypoint, GPU or Bullet object is created by this observer.
#include <iomanip>
namespace {
std::string PhysicsJson(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '\"')
            result += '\\';
        if (c < 32) {
            char escape[7];
            std::snprintf(escape, sizeof escape, "\\u%04x", unsigned(c));
            result += escape;
        } else
            result += char(c);
    }
    return result + '\"';
}
std::map<int, bool> physicsChecks, physicsShown;
unsigned physicsReports = 0;
LRESULT WINAPI PhysicsSend(HWND control, UINT message, WPARAM w, LPARAM l) {
    const int id = int(std::uintptr_t(control));
    auto& combo = englishCombos[id];
    switch (message) {
    case BM_SETCHECK:
        physicsChecks[id] = w != 0;
        return 0;
    case CB_DELETESTRING:
        if (w < combo.names.size())
            combo.names.erase(combo.names.begin() + w);
        combo.cursor = -1;
        return LRESULT(combo.names.size());
    case CB_INSERTSTRING:
        if (w > combo.names.size())
            w = combo.names.size();
        combo.names.insert(combo.names.begin() + w, reinterpret_cast<const char*>(l));
        return LRESULT(w);
    default:
        return EnglishSend(control, message, w, l);
    }
}
BOOL WINAPI PhysicsSetText(HWND control, LPCSTR text) {
    englishInputs[int(std::uintptr_t(control))] = text;
    return TRUE;
}
BOOL WINAPI PhysicsShow(HWND control, int mode) {
    physicsShown[int(std::uintptr_t(control))] = mode != SW_HIDE;
    return TRUE;
}
BOOL WINAPI PhysicsRadio(HWND, int first, int last, int selected) {
    for (int i = first; i <= last; ++i)
        physicsChecks[i] = i == selected;
    return TRUE;
}
UINT WINAPI PhysicsChecked(HWND, int id) {
    return physicsChecks[id] ? BST_CHECKED : BST_UNCHECKED;
}
int WINAPI PhysicsMessage(HWND, LPCSTR, LPCSTR, UINT flags) {
    Check(flags == 0, "original physics message flags");
    ++physicsReports;
    return IDOK;
}
LONG_PTR WINAPI PhysicsGetLong(HWND, int) {
    return 0;
}
LONG_PTR WINAPI PhysicsSetLong(HWND, int, LONG_PTR) {
    return 0;
}
LRESULT WINAPI PhysicsSendWide(HWND control, UINT message, WPARAM w, LPARAM l) {
    Check(message == CB_ADDSTRING, "original physics wide label");
    return PhysicsSend(control, message, w, LPARAM("none"));
}
unsigned ModelPhysicsCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    std::vector<std::unique_ptr<Patch>> patches;
    const auto hook = [&](unsigned rva, auto function) {
        patches.push_back(
            std::make_unique<Patch>(original.Function<void*>(rva), &function, sizeof function));
    };
    hook(0x128658, &ResetChild);
    hook(0x128690, &EnglishText);
    hook(0x128598, &PhysicsSetText);
    hook(0x1287b8, &PhysicsSend);
    hook(0x1287e8, &PhysicsSendWide);
    hook(0x1285d0, &EnglishEnable);
    hook(0x1285f0, &PhysicsShow);
    hook(0x1285c0, &PhysicsRadio);
    hook(0x1285e0, &PhysicsChecked);
    hook(0x128618, &PhysicsMessage);
    hook(0x128748, &PhysicsGetLong);
    hook(0x128638, &PhysicsSetLong);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const auto proc = original.Function<DLGPROC>(0x8d800);
    const auto dialog = reinterpret_cast<HWND>(10000);
    unsigned total = 0;
    for (bool english : {false, true}) {
        std::memset(app.get(), 0, sizeof(*app));
        auto native = std::make_unique<mdl::ModelRecord>();
        std::vector<mdl::BoneRecord> bones(2);
        for (unsigned i = 0; i < bones.size(); ++i) {
            std::snprintf(bones[i].name, 20, "BONE_%u", i);
            std::snprintf(bones[i].nameEn, 20, "BONE_%u", i);
            bones[i].trans[0] = 7;
            bones[i].rotQuat[0] = 1;
        }
        native->boneCount = unsigned(bones.size());
        native->boneTable = bones.data();
        app->modelSlots[0] = native.get();
        app->reservedFrameCopyDialog = dialog;
        auto* raw = reinterpret_cast<unsigned char*>(app.get());
        raw[0xa1b90] = english;
        app->sceneModified = 0x7d;
        englishInputs.clear();
        englishCombos.clear();
        englishEnabled.clear();
        physicsChecks.clear();
        physicsShown.clear();
        physicsReports = 0;
        proc(dialog, WM_INITDIALOG, 0, 0);
        Check(!englishEnabled[408], "original physics disables selected model");
        for (const auto& bone : bones)
            Check(bone.trans[0] == 0 && bone.rotQuat[0] == 0 && bone.rotQuat[3] == 1,
                  "original physics neutral pose");
        std::ofstream output(root /
                             (english ? "physics-original-en.json" : "physics-original-ja.json"));
        output << "[\n";
        unsigned snapshots = 0;
        const auto snapshot = [&](const char* action, int id = 0, const char* value = "") {
            if (snapshots++)
                output << ",\n";
            output << "{\"action\":" << PhysicsJson(action) << ",\"id\":" << id
                   << ",\"value\":" << PhysicsJson(value) << ",\"body\":" << app->selectedRigidIndex
                   << ",\"joint\":" << app->selectedJointIndex << ",\"reports\":" << physicsReports
                   << ",\"text\":{";
            bool first = true;
            for (const auto& [control, text] : englishInputs)
                if (control == 705 || control >= 709 && control <= 723 || control == 740 ||
                    control >= 744 && control <= 767) {
                    if (!first)
                        output << ',';
                    first = false;
                    output << PhysicsJson(std::to_string(control)) << ':' << PhysicsJson(text);
                }
            output << "},\"combos\":{";
            first = true;
            for (int control : {704, 736, 707, 708, 741, 742, 743}) {
                if (!first)
                    output << ',';
                first = false;
                output << PhysicsJson(std::to_string(control)) << ":{"
                       << "\"count\":" << englishCombos[control].names.size()
                       << ",\"selected\":" << englishCombos[control].cursor << '}';
            }
            const auto bits = [&](float value) {
                unsigned integer;
                std::memcpy(&integer, &value, 4);
                output << ',' << integer;
            };
            output << "},\"bodies\":[";
            const auto* bodies = static_cast<mdl::RigidRecord*>(app->reservedRigidScratchArray);
            const auto* joints = static_cast<mdl::JointRecord*>(app->reservedJointScratchArray);
            // Include dead scratch slots too: their retained values affect reuse.
            for (int i = 0; i < 3; ++i) {
                if (i)
                    output << ',';
                const auto& b = bodies[i];
                output << '[' << b.bodyId << ',' << b.boneIndex << ',' << unsigned(b.group) << ','
                       << b.noCollapse << ',' << unsigned(b.shape) << ',' << unsigned(b.mode);
                for (float v : b.size)
                    bits(v);
                for (float v : b.position)
                    bits(v);
                for (float v : b.rotation)
                    bits(v);
                for (float v :
                     {b.mass, b.linearDamping, b.angularDamping, b.restitution, b.friction})
                    bits(v);
                output << ']';
            }
            output << "],\"joints\":[";
            for (int i = 0; i < 2; ++i) {
                if (i)
                    output << ',';
                const auto& j = joints[i];
                output << '[' << j.constraint << ',' << j.rigidA << ',' << j.rigidB;
                for (float v : j.position)
                    bits(v);
                for (float v : j.rotation)
                    bits(v);
                for (float v : j.limits)
                    bits(v);
                for (float v : j.springs)
                    bits(v);
                output << ']';
            }
            output << "]}";
            Check(app->sceneModified == 0x7d && raw[0xa1bb4] == 0,
                  "original staging does not dirty PMM/enhancement");
        };
        const auto command = [&](int id) {
            proc(dialog, WM_COMMAND, id, 0);
            snapshot("command", id);
        };
        const auto input = [&](int id, const char* text, bool enter) {
            englishInputs[id] = text;
            if (enter)
                original.Function<WNDPROC>(0x8e950)(reinterpret_cast<HWND>(std::uintptr_t(id)),
                                                    WM_KEYDOWN, VK_RETURN, 0);
            snapshot(enter ? "enter" : "text", id, text);
        };
        const auto combo = [&](int id, int row) {
            englishCombos[id].cursor = row;
            proc(dialog, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), id);
            const auto value = std::to_string(row);
            snapshot("combo", id, value.c_str());
        };
        snapshot("init");
        command(735);
        command(685);
        input(705, "Source", true);
        input(709, "-3", true);
        input(718, "-2", true);
        input(712, "1.23456tail", true);
        input(715, "37.25", true);
        input(719, "1 5 16 bad 2x 3\t4", false);
        input(722, ".125", true);
        input(723, ".75", true);
        command(727);
        command(729);
        physicsChecks[731] = true;
        command(731);
        combo(708, 7);
        command(724);
        command(685);
        input(705, "Destination", true);
        combo(708, 3);
        command(725);
        command(735);
        combo(742, 1);
        input(740, "Link", true);
        input(744, "2.345678", true);
        input(764, "-90", true);
        input(765, "90", true);
        input(748, "12.75", true);
        command(738);
        command(735);
        command(739);
        command(801);
        command(800);
        combo(704, 0);
        command(706);
        command(685);
        command(728);
        input(718, "-2", false);
        command(801); // Bulk collection deliberately leaves mass unclamped.
        command(800);
        command(735);
        command(737);
        command(706);
        command(
            706); // Native stale scratch selection: visible row disappears, other body survives.
        command(685);
        combo(704, 0); // Duplicate ordinal resolves to the last scratch slot.
        command(735);
        command(735);
        command(737);
        command(737);
        command(735);
        output << "\n]\n";
        Check(output.good(), "original physics evidence write");
        const auto freeMemory = *original.Function<void (**)(void*)>(0x1283d8);
        freeMemory(app->reservedRigidScratchArray);
        freeMemory(app->reservedJointScratchArray);
        total += snapshots;
    }
    return total;
}
}
