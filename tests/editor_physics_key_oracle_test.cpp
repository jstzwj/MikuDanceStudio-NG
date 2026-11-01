#include "editor_key_edit_native.hpp"
#include "editor_physics_key_fixture.hpp"
unsigned ended = 0;
BOOL WINAPI End(HWND, INT_PTR result) {
    Check(result == IDOK, "native physics dialog close result");
    ++ended;
    return TRUE;
}
bool Write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
           std::string&) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    f.close();
    return bool(f);
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3)
        return 2;
    OriginalPhysicsModule original(argv[1]);
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1),
        seek(original.Function<void*>(0xcbd90), &ret, 1);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    float maximum = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx"})
        for (bool reverse : {false, true})
            for (unsigned variant = 0; variant < 3; ++variant)
                for (bool enabled : {false, true})
                    for (bool dirty : {false, true}) {
                        editor::EditorSession portable;
                        std::string reason;
                        Check(PhysicsKeyFixture(portable, argv[2], file, reverse, variant, reason),
                              reason.c_str());
                        Check(portable.Save(std::filesystem::path(argv[2]) / "physics-key.pmm",
                                            Write, reason),
                              reason.c_str());
                        if (dirty)
                            Check(portable.SetBone(0, 0, {9, 8, 7}, {0, 0, 0, 1}, reason),
                                  reason.c_str());
                        Native native(original, portable);
                        auto* model = native.model.get();
                        std::fill(app.begin(), app.end(), 0);
                        std::memcpy(app.data() + 0xbe8, &model, sizeof model);
                        int frame = portable.Document().timeline.currentFrame;
                        std::memcpy(app.data() + 0x1450, &frame, 4);
                        app[0xa1b31] = dirty;
                        const auto revision = portable.Revision();
                        const bool undoBefore = portable.CanUndo(), redoBefore = portable.CanRedo();
                        original.Function<void (*)(bool)>(0x9ed40)(!enabled);
                        Check(portable.SetSelectedKeyPhysics(enabled, reason), reason.c_str());
                        native.Compare(portable, maximum);
                        Check(bool(app[0xa1b31]) == dirty && portable.Dirty() == dirty,
                              "native physics-key dirty flag differs");
                        if (model->UndoCursor() == 29) {
                            Check(portable.Revision() == revision &&
                                      portable.CanUndo() == undoBefore &&
                                      portable.CanRedo() == redoBefore,
                                  "empty physics command altered history");
                        } else {
                            for (unsigned cycle = 0; cycle < 2; ++cycle) {
                                original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcef90)(
                                    model, &frame);
                                Check(portable.Undo(reason), reason.c_str());
                                native.Compare(portable, maximum);
                                original.Function<void (*)(mdl::ModelRecord*, int*)>(0xcfb80)(
                                    model, &frame);
                                Check(portable.Redo(reason), reason.c_str());
                                native.Compare(portable, maximum);
                            }
                        }
                        ++cases;
                    }
    auto end = &End;
    Patch finish(original.Function<void*>(0x128650), &end, sizeof end);
    HMODULE resources =
        LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    Check(resources != nullptr, "physics dialog resources");
    for (bool english : {false, true}) {
        editor::EditorSession portable;
        std::string reason;
        Check(PhysicsKeyFixture(portable, argv[2], "articulated.pmx", false, 1, reason),
              reason.c_str());
        Native native(original, portable);
        auto* model = native.model.get();
        std::fill(app.begin(), app.end(), 0);
        std::memcpy(app.data() + 0xbe8, &model, sizeof model);
        app[0xa1b90] = english;
        HWND window = CreateDialogParamA(resources, MAKEINTRESOURCEA(english ? 813 : 812), nullptr,
                                         original.Function<DLGPROC>(0x9ec00), 0);
        Check(window && !IsWindowVisible(window), "physics dialog must remain hidden");
        HWND combo = GetDlgItem(window, 669);
        Check(SendMessageA(combo, CB_GETCOUNT, 0, 0) == 2 &&
                  SendMessageA(combo, CB_GETCURSEL, 0, 0) == 0,
              "native physics combo initial state");
        char label[64]{};
        SendMessageA(combo, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(label));
        Check(std::strcmp(label, english ? "ON (X mark)" : "ON (X\x88\xf3)") == 0,
              "native physics ON label");
        SendMessageA(combo, CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(label));
        Check(std::strcmp(label, "OFF") == 0, "native physics OFF label");
        RECT units{0, 0, 4, 8}, rect{};
        MapDialogRect(window, &units);
        GetWindowRect(combo, &rect);
        Check(units.right == 7 && units.bottom == (english ? 15 : 12) &&
                  rect.bottom - rect.top == (english ? 23 : 20),
              "native physics combo geometry");
        Check(!(GetWindowLongPtrA(combo, GWL_STYLE) & WS_TABSTOP), "native physics combo tab gate");
        ended = 0;
        SendMessageA(window, WM_COMMAND, IDCANCEL, 0);
        Check(ended == 1 && model->UndoCursor() == 29, "native physics cancellation edited keys");
        SendMessageA(combo, CB_SETCURSEL, 1, 0);
        SendMessageA(window, WM_COMMAND, IDOK, 0);
        Check(ended == 2 && model->UndoCursor() != 29 && !app[0xa1b31],
              "native physics acceptance gate");
        Check(portable.SetSelectedKeyPhysics(false, reason), reason.c_str());
        native.Compare(portable, maximum);
        DestroyWindow(window);
    }
    FreeLibrary(resources);
    std::printf(
        "Original x64 physics-key edit: %u full-pool/history cases, two hidden original dialogs, exact values\n",
        cases);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
