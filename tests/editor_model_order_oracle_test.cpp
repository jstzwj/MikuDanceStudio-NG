#include "editor_key_edit_native.hpp"
#include "mikudancestudio/accessory_layout.hpp"

namespace {
unsigned ended;
INT_PTR endResult;
BOOL WINAPI End(HWND, INT_PTR result) {
    ++ended;
    endResult = result;
    return TRUE;
}
struct Window {
    HWND handle = CreateWindowExA(0, "STATIC", "", WS_OVERLAPPED, 0, 0, 200, 200, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    ~Window() {
        if (handle)
            DestroyWindow(handle);
    }
};
HWND Combo(HWND parent, int id) {
    return CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | CBS_DROPDOWNLIST, 0, 0, 100, 100, parent,
                           reinterpret_cast<HMENU>(std::uintptr_t(id)), GetModuleHandleW(nullptr),
                           nullptr);
}
unsigned Accessories(OriginalPhysicsModule& original, HMODULE resources) {
    const unsigned char ret = 0xc3;
    Patch timeline(original.Function<void*>(0x5f7f0), &ret, 1);
    Patch readouts(original.Function<void*>(0x20ce0), &ret, 1);
    Patch panel(original.Function<void*>(0x5e510), &ret, 1);
    unsigned cases = 0;
    for (bool english : {false, true})
        for (bool cancel : {false, true})
            for (unsigned count : {1u, 3u}) {
                std::vector<unsigned char> app(0xa3000);
                auto* pointer = app.data();
                Patch global(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
                Window owner;
                Check(owner.handle && !IsWindowVisible(owner.handle), "hidden accessory owner");
                std::memcpy(app.data() + 0xa16c8, &owner.handle, sizeof owner.handle);
                app[0xa1b90] = english;
                HWND combo = Combo(owner.handle, 471);
                Check(combo && Combo(owner.handle, 434), "accessory combo owners");
                std::vector<std::unique_ptr<mdl::AccessoryRecord>> objects;
                format::PmmSceneDocument document;
                const std::uint8_t slots[]{7, 42, 254};
                for (unsigned i = 0; i < count; ++i) {
                    auto object = std::make_unique<mdl::AccessoryRecord>();
                    object->order = std::uint8_t(i);
                    std::snprintf(object->name, 100, "accessory %u", i);
                    object->rowSelected = i == 1;
                    auto* address = object.get();
                    std::memcpy(app.data() + 0x9e840 + slots[i] * 8, &address, sizeof address);
                    SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(object->name));
                    format::PmmSceneAccessory item;
                    item.slot = slots[i];
                    item.order = std::uint8_t(i);
                    std::copy_n(object->name, 100, item.name.begin());
                    document.accessories.push_back(item);
                    objects.push_back(std::move(object));
                }
                const auto proc = original.Function<DLGPROC>(0x572c0);
                HWND dialog = CreateDialogParamA(resources, MAKEINTRESOURCEA(english ? 659 : 627),
                                                 owner.handle, proc, 0);
                Check(dialog && !IsWindowVisible(dialog), "hidden native accessory dialog");
                HWND list = GetDlgItem(dialog, 628), name = GetDlgItem(dialog, 629);
                HWND split = GetDlgItem(dialog, 635);
                Check(SendMessageA(list, LB_GETCOUNT, 0, 0) == count &&
                          SendMessageA(list, LB_GETCURSEL, 0, 0) == -1,
                      "native accessory initialization");
                for (const char* text : {"-1", "999", "1x", "  +1suffix", "abc", "0"}) {
                    SetWindowTextA(split, text);
                    char actual[100]{};
                    GetWindowTextA(split, actual, 100);
                    const bool one = text[0] == '1' || text[0] == ' ';
                    const int value = text[0] == '9' ? int(count) : one ? 1 : 0;
                    Check(SendMessageA(list, LB_GETCURSEL, 0, 0) ==
                              (unsigned(value) < count ? value : -1),
                          "native split selection and prefix conversion");
                    if (!one)
                        Check(std::string(actual) == std::to_string(value),
                              "native split clamp text");
                }
                auto rows = format::PmmAccessoryOrder(document);
                SendMessageA(list, LB_SETCURSEL, count - 1, 0);
                SendMessageA(dialog, WM_COMMAND, MAKEWPARAM(628, LBN_SELCHANGE),
                             reinterpret_cast<LPARAM>(list));
                char selected[100]{};
                GetWindowTextA(name, selected, 100);
                Check(std::string(selected) == rows.back().name.data(), "native selected name");
                SetWindowTextA(name, "renamed");
                SendMessageA(dialog, WM_COMMAND, 633, 0);
                std::copy_n("renamed", 8, rows.back().name.begin());
                SetWindowTextA(name, "pending");
                if (count > 1) {
                    SendMessageA(dialog, WM_COMMAND, 630, 0);
                    std::swap(rows[count - 1], rows[count - 2]);
                }
                GetWindowTextA(name, selected, 100);
                Check(std::string(selected) == "pending", "native move retains pending rename");
                SetWindowTextA(split, "1suffix");
                ended = 0;
                SendMessageA(dialog, WM_COMMAND, cancel ? 2 : 632, 0);
                Check(ended == 1 && endResult == (cancel ? 2 : 1), "accessory modal result");
                if (!cancel) {
                    Check(format::SetPmmAccessoryOrder(document, rows, 1),
                          "portable accessory order");
                    Check(app[0x9f03c] == rows.front().slot &&
                              *reinterpret_cast<int*>(app.data() + 0xa1b50) == 1,
                          "native selected accessory and split");
                    HWND scope = GetDlgItem(owner.handle, 434);
                    Check(SendMessageA(scope, CB_GETCOUNT, 0, 0) == count + 4 &&
                              SendMessageA(scope, CB_GETCURSEL, 0, 0) == 0,
                          "native accessory order resets the scope combo");
                    for (unsigned row = 0; row < count; ++row) {
                        char label[100]{};
                        SendMessageA(scope, CB_GETLBTEXT, row + 4, reinterpret_cast<LPARAM>(label));
                        Check(std::string(label) == rows[row].name.data(),
                              "native global accessory scope labels");
                    }
                }
                for (unsigned i = 0; i < count; ++i) {
                    const auto& expected = document.accessories[i];
                    Check(objects[i]->order == expected.order &&
                              !std::memcmp(objects[i]->name, expected.name.data(), 100),
                          "native accessory names/order bytes");
                    Check(bool(objects[i]->rowSelected) ==
                              (cancel ? i == 1 : slots[i] == rows.front().slot),
                          "native accessory registration row");
                }
                Check(app[0xa1b31] == 0 &&
                          *reinterpret_cast<void**>(app.data() + 0xa1b48) == nullptr,
                      "accessory dirty flag and scratch destruction");
                DestroyWindow(dialog);
                ++cases;
            }
    return cases;
}
}
int wmain(int argc, wchar_t** argv) try {
    Check(argc == 2, "usage: editor_model_order_oracle_test original.exe");
    OriginalPhysicsModule original(argv[1]);
    const unsigned char ret = 0xc3;
    Patch refresh(original.Function<void*>(0x66b10), &ret, 1);
    auto finish = &End;
    Patch end(original.Function<void*>(0x128650), &finish, sizeof finish);
    auto resources =
        LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    Check(resources != nullptr, "original resources");
    unsigned cases = 0;
    for (bool english : {false, true})
        for (bool calculation : {false, true})
            for (bool cancel : {false, true})
                for (unsigned count : {0u, 1u, 3u}) {
                    std::vector<unsigned char> app(0xa3000, 0);
                    auto* pointer = app.data();
                    Patch global(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
                    Window main;
                    Check(main.handle && !IsWindowVisible(main.handle),
                          "hidden main control owner");
                    std::memcpy(app.data() + 0xa16c8, &main.handle, sizeof main.handle);
                    app[0xa1b90] = english;
                    auto combo = Combo(main.handle, 436);
                    Check(combo && Combo(main.handle, 474) && Combo(main.handle, 449),
                          "hidden model combos");
                    SendMessageA(combo, CB_ADDSTRING, 0,
                                 reinterpret_cast<LPARAM>("camera/light/accessory"));
                    constexpr std::uint8_t slots[]{7, 42, 254};
                    std::vector<std::unique_ptr<mdl::ModelRecord>> models;
                    format::PmmSceneDocument doc;
                    doc.envelope.header.version = format::PmmVersion::V2;
                    for (unsigned i = 0; i < count; ++i) {
                        auto model = std::make_unique<mdl::ModelRecord>();
                        model->comboSelIndex = std::uint8_t(i + 1);
                        model->comboSelIndex2 = std::uint8_t(count - 1 - i);
                        std::snprintf(model->name, sizeof model->name, "jp-%u", i);
                        std::snprintf(model->nameEn, sizeof model->nameEn, "en-%u", i);
                        auto* p = model.get();
                        std::memcpy(app.data() + 0xbe8 + slots[i] * sizeof p, &p, sizeof p);
                        SendMessageA(
                            combo, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(english ? model->nameEn : model->name));
                        format::PmmSceneModel value;
                        value.slot = slots[i];
                        value.selection.comboIndex = model->comboSelIndex;
                        value.previousDisplayOrder = model->comboSelIndex2;
                        doc.models.push_back(value);
                        models.push_back(std::move(model));
                    }
                    SendMessageA(combo, CB_SETCURSEL, count ? 1 : 0, 0);
                    const auto kind = calculation ? format::ModelOrderKind::Calculation
                                                  : format::ModelOrderKind::Draw;
                    auto expected = format::PmmModelOrder(doc, kind);
                    const auto proc = original.Function<DLGPROC>(calculation ? 0x58490 : 0x57d70);
                    const auto id = calculation ? (english ? 811 : 810) : (english ? 805 : 804);
                    HWND dialog =
                        CreateDialogParamA(resources, MAKEINTRESOURCEA(id), main.handle, proc, 0);
                    Check(dialog && !IsWindowVisible(dialog), "order dialog must remain hidden");
                    HWND list = GetDlgItem(dialog, 628);
                    Check(SendMessageA(list, LB_GETCOUNT, 0, 0) == count &&
                              SendMessageA(list, LB_GETCURSEL, 0, 0) == -1,
                          "original list initial state");
                    Check(!(GetWindowLongPtrA(list, GWL_STYLE) & WS_TABSTOP), "list tab policy");
                    for (unsigned i = 0; i < count; ++i) {
                        char label[100]{};
                        SendMessageA(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(label));
                        const auto source =
                            unsigned(std::find(std::begin(slots), std::end(slots), expected[i]) -
                                     std::begin(slots));
                        Check(std::strcmp(label, english ? models[source]->nameEn
                                                         : models[source]->name) == 0,
                              "native order labels/ranks");
                    }
                    ended = 0;
                    SendMessageA(dialog, WM_COMMAND, 633, 0);
                    Check(!ended, "zero-size Enter button must do nothing");
                    if (count) {
                        SendMessageA(list, LB_SETCURSEL, count - 1, 0);
                        SendMessageA(dialog, WM_COMMAND, 631, 0);
                        Check(SendMessageA(list, LB_GETCURSEL, 0, 0) == count - 1,
                              "last row down boundary");
                        if (count > 1) {
                            SendMessageA(dialog, WM_COMMAND, 630, 0);
                            std::swap(expected[count - 1], expected[count - 2]);
                            Check(SendMessageA(list, LB_GETCURSEL, 0, 0) == count - 2,
                                  "up selection follows row");
                        }
                        SendMessageA(list, LB_SETCURSEL, 0, 0);
                        SendMessageA(dialog, WM_COMMAND, 630, 0);
                        Check(SendMessageA(list, LB_GETCURSEL, 0, 0) == 0, "first row up boundary");
                    }
                    SendMessageA(dialog, WM_COMMAND, cancel ? 2 : 632, 0);
                    Check(ended == 1 && endResult == (cancel ? 2 : 1), "native end result");
                    if (!cancel)
                        Check(format::SetPmmModelOrder(doc, kind, expected),
                              "portable order apply");
                    for (unsigned i = 0; i < count; ++i)
                        Check(models[i]->comboSelIndex == doc.models[i].selection.comboIndex &&
                                  models[i]->comboSelIndex2 == doc.models[i].previousDisplayOrder,
                              "native order bytes differ");
                    Check(app[0xa1b31] == 0, "order command dirtied scene");
                    if (!cancel && !calculation)
                        Check(SendMessageA(combo, CB_GETCURSEL, 0, 0) == 0,
                              "draw order selects camera entry");
                    Check(*reinterpret_cast<void**>(app.data() + 0xa1b48) == nullptr,
                          "scratch was not freed");
                    DestroyWindow(dialog);
                    ++cases;
                }
    std::printf(
        "Original accessory order: %u hidden dialogs, rename, split, selection and cancel\n",
        Accessories(original, resources));
    FreeLibrary(resources);
    std::printf(
        "Original model order: %u hidden dialog cases, exact slots/labels/order bytes/cancel/boundaries\n",
        cases);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
