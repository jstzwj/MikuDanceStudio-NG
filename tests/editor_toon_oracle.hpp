// Actual command 261, dialog procedure and collector. USER32 controls and the
// GPU texture probe are intercepted. No original entrypoint/window is executed.
namespace {
std::array<std::string, 10> toonRequested;
std::vector<std::wstring> toonProbes;
std::vector<unsigned> toonFailures;
bool toonAccept = false, toonEnglish = true;
INT_PTR toonDialogResult = 0;
unsigned toonReminders = 0;
std::wstring toonDirectory;
int WINAPI ToonText(HWND control, LPSTR buffer, int maximum) {
    const auto index = unsigned(reinterpret_cast<std::uintptr_t>(control)) - 709;
    Check(index < 10 && maximum == 256, "native toon input contract");
    const auto& name = toonRequested[index];
    const auto count = std::min(name.size(), std::size_t(maximum - 1));
    std::memcpy(buffer, name.data(), count);
    buffer[count] = 0;
    return int(count);
}
bool ToonProbe(void*, const wchar_t* path) {
    toonProbes.emplace_back(path);
    return std::wstring(path) == toonDirectory + L"valid.bmp" ||
           std::wstring(path) == toonDirectory + L"toon01.bmp";
}
void ToonWide(void*, const char* text, wchar_t* output) {
    Check(MultiByteToWideChar(932, 0, text, -1, output, 256) > 0, "native toon fixture code page");
}
BOOL WINAPI ToonEnd(HWND, INT_PTR result) {
    toonDialogResult = result;
    return TRUE;
}
int WINAPI ToonMessage(HWND, LPCSTR text, LPCSTR title, UINT flags) {
    if (flags == MB_TOPMOST) {
        ++toonReminders;
        Check(std::string(title) ==
                  (toonEnglish ? "enhance model" : "\x83\x82\x83\x66\x83\x8b\x8a\x67\x92\xa3"),
              "native toon reminder title");
    } else {
        Check(flags == 0, "native toon missing flags");
        bool found = false;
        for (unsigned i = 0; i < 10; ++i)
            if (toonRequested[i].find("missing") == 0 &&
                std::strstr(text, toonRequested[i].c_str())) {
                toonFailures.push_back(i);
                found = true;
                break;
            }
        Check(found, "native toon per-file failure");
    }
    return IDOK;
}
INT_PTR WINAPI ToonModal(HINSTANCE, LPCSTR resource, HWND, DLGPROC procedure, LPARAM) {
    Check(std::uintptr_t(resource) == (toonEnglish ? 682u : 681u), "native toon resource");
    englishInputs.clear();
    englishCombos.clear();
    procedure(reinterpret_cast<HWND>(1), WM_INITDIALOG, 0, 0);
    Check(englishInputs.size() == 10, "native toon prefill count");
    toonDialogResult = 0;
    procedure(reinterpret_cast<HWND>(1), WM_COMMAND, toonAccept ? IDOK : IDCANCEL, 0);
    Check(toonDialogResult == IDOK || toonDialogResult == IDCANCEL, "native toon close result");
    return toonDialogResult;
}
unsigned ToonCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    auto child = &ResetChild;
    auto text = &ToonText;
    auto send = &EnglishSend;
    auto modal = &ToonModal;
    auto end = &ToonEnd;
    auto message = &ToonMessage;
    Patch a(original.Function<void*>(0x128658), &child, sizeof child);
    Patch b(original.Function<void*>(0x128690), &text, sizeof text);
    Patch c(original.Function<void*>(0x1287b8), &send, sizeof send);
    Patch d(original.Function<void*>(0x128600), &modal, sizeof modal);
    Patch e(original.Function<void*>(0x128650), &end, sizeof end);
    Patch f(original.Function<void*>(0x128618), &message, sizeof message);
    std::array<unsigned char, 12> probeJump{0x48, 0xb8}, wideJump{0x48, 0xb8};
    auto probe = &ToonProbe;
    auto wide = &ToonWide;
    std::memcpy(probeJump.data() + 2, &probe, sizeof probe);
    std::memcpy(wideJump.data() + 2, &wide, sizeof wide);
    probeJump[10] = wideJump[10] = 0xff;
    probeJump[11] = wideJump[11] = 0xe0;
    Patch g(original.Function<void*>(0x8be0), probeJump.data(), probeJump.size());
    Patch h(original.Function<void*>(0x9440), wideJump.data(), wideJump.size());
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    toonDirectory = root.wstring() + L"\\";
    std::filesystem::copy_file(root / "checker.bmp", root / "valid.bmp",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(root / "checker.bmp", root / "toon01.bmp",
                               std::filesystem::copy_options::overwrite_existing);
    unsigned count = 0;
    for (bool english : {false, true})
        for (bool accepted : {false, true})
            for (unsigned profile = 0; profile < 4; ++profile) {
                auto source = EnhancedSaveFixture(0);
                for (unsigned i = 0; i < 10; ++i) {
                    source.toonNames[i].fill(0);
                    std::snprintf(source.toonNames[i].data(), 100, "toon%02u.bmp", i + 1);
                    toonRequested[i] = source.toonNames[i].data();
                }
                std::strcpy(source.toonNames[0].data(), "old.bmp");
                if (profile > 0)
                    toonRequested[0] = "valid.bmp";
                if (profile == 2) {
                    toonRequested[2] = "missing.bmp";
                    toonRequested[7] = "missing-again.bmp";
                }
                if (profile == 3)
                    toonRequested[1] = "toon01.bmp";
                std::vector<std::uint8_t> bytes;
                std::string reason;
                Check(WriteEnhancedPmd(source, bytes, reason), reason.c_str());
                const auto path = root / "toon-native.pmd";
                {
                    std::ofstream file(path, std::ios::binary);
                    file.write(reinterpret_cast<const char*>(bytes.data()),
                               std::streamsize(bytes.size()));
                }
                EditorSession portable;
                Check(portable.ImportModel(path, reason), reason.c_str());
                auto native = std::make_unique<mdl::ModelRecord>();
                std::memcpy(native->pmdToonFileNames, source.toonNames.data(), 1000);
                std::wcscpy(native->modelDirectory, toonDirectory.c_str());
                std::memset(app.get(), 0, sizeof(*app));
                app->modelSlots[0] = native.get();
                app->sceneModified = 0x7d;
                auto* raw = reinterpret_cast<unsigned char*>(app.get());
                raw[0xa1b90] = english;
                toonEnglish = english;
                toonAccept = accepted;
                toonProbes.clear();
                toonFailures.clear();
                toonReminders = 0;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, MDS_CMD_TOON_TEXTURES);
                std::vector<unsigned> missing;
                if (accepted)
                    Check(portable.SetModelToonNames(0, toonRequested, missing, reason) ==
                              (profile != 2),
                          "portable toon result");
                std::array<std::string, 10> names;
                Check(portable.ModelToonNames(0, names, reason), reason.c_str());
                for (unsigned i = 0; i < 10; ++i) {
                    Check(names[i] == native->pmdToonFileNames[i], "native toon filename mismatch");
                    Check(englishInputs[709 + int(i)] == source.toonNames[i].data(),
                          "native toon prefill identity");
                }
                const bool success = accepted && profile != 2;
                Check(raw[0xa1bb4] == success && portable.EnhancedModelDirty() == success &&
                          app->sceneModified == 0x7d,
                      "native toon reminder/PMM flags");
                Check(toonFailures == missing && toonReminders == unsigned(success),
                      "native toon error/reminder sequence");
                const unsigned probes = accepted ? (profile == 0   ? 0
                                                    : profile == 2 ? 3
                                                    : profile == 3 ? 2
                                                                   : 1)
                                                 : 0;
                Check(toonProbes.size() == probes, "native toon probe count");
                ++count;
            }
    return count;
}
}
