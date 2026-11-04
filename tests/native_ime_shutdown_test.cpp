// Isolate OS IME teardown from Qt Widgets/QPA. Shown only on a disposable CI runner.
#include "windows_ime_session.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>

namespace {
struct Client {
    HWND window = nullptr, edit = nullptr;
    WNDPROC originalEdit = nullptr;
    QString statusPath, commandPath, error, preedit, committed;
    unsigned sequence = 0, preedits = 0, commits = 0;
    bool finishing = false;
    mikudancestudio::test::JapaneseImeSession ime;
    mikudancestudio::test::ShutdownDiagnostic diagnostic;
    QString Composition(HIMC context, DWORD index) {
        const auto bytes = ImmGetCompositionStringW(context, index, nullptr, 0);
        if (bytes <= 0)
            return {};
        QString result(bytes / sizeof(wchar_t), Qt::Uninitialized);
        ImmGetCompositionStringW(context, index, result.data(), DWORD(bytes));
        return result;
    }
    bool Publish() {
        wchar_t text[4096]{};
        GetWindowTextW(edit, text, 4096);
        QSaveFile file(statusPath);
        const QJsonObject state{{"reference", "Win32 EDIT; no QApplication or QPA"},
                                {"command", int(sequence)},
                                {"focus", GetFocus() == edit},
                                {"preedit", preedit},
                                {"preedits", int(preedits)},
                                {"commits", int(commits)},
                                {"committed", committed},
                                {"field", QString::fromWCharArray(text)},
                                {"finishing", finishing},
                                {"error", error}};
        return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(state).toJson()) >= 0 &&
               file.commit();
    }
    void Tick() {
        QFile file(commandPath);
        if (file.open(QIODevice::ReadOnly)) {
            const auto request = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            if (unsigned(request["sequence"].toInt()) > sequence) {
                sequence = unsigned(request["sequence"].toInt());
                const auto action = request["action"].toString();
                if (action == "select") {
                    SetForegroundWindow(window);
                    SetFocus(edit);
                    SendMessageW(edit, EM_SETSEL, 0, -1);
                } else if (action == "activate-ime") {
                    diagnostic.Arm();
                    error = ime.Activate(edit);
                    diagnostic.Cancel();
                } else if (action == "finish")
                    finishing = true;
            }
        }
        if (!Publish())
            return; // Atomic replacement may race the driver's short read handle.
        if (finishing) {
            KillTimer(window, 1);
            diagnostic.Arm();
            error = ime.Finish(edit);
            if (!error.isEmpty()) {
                std::fprintf(stderr, "%s\n", error.toUtf8().constData());
                Publish();
            }
            std::fprintf(stderr, "Native EDIT destroying its HWND hierarchy\n");
            std::fflush(stderr);
            DestroyWindow(window);
            window = edit = nullptr;
            PostQuitMessage(error.isEmpty() ? 0 : 1);
        }
    }
};
LRESULT CALLBACK EditProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto& client = *reinterpret_cast<Client*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_IME_COMPOSITION) {
        if (const auto context = ImmGetContext(window)) {
            if (lParam & GCS_COMPSTR) {
                client.preedit = client.Composition(context, GCS_COMPSTR);
                client.preedits += !client.preedit.isEmpty();
            }
            if (lParam & GCS_RESULTSTR) {
                client.committed = client.Composition(context, GCS_RESULTSTR);
                client.commits += !client.committed.isEmpty();
                client.preedit.clear();
            }
            ImmReleaseContext(window, context);
        }
    } else if (message == WM_IME_ENDCOMPOSITION)
        client.preedit.clear();
    return CallWindowProcW(client.originalEdit, window, message, wParam, lParam);
}
LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE)
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto* client = reinterpret_cast<Client*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_TIMER && client) {
        client->Tick();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
int main(int argc, char** argv) {
    // Check before COM initialization, window creation, focus or input changes.
    if (argc != 3 || qEnvironmentVariable("GITHUB_ACTIONS") != "true" ||
        qEnvironmentVariable("MDS_QT_ISOLATED_DISPLAY") != "1") {
        std::fprintf(stderr, "Requires an isolated GitHub runner: status command\n");
        return 2;
    }
    if (FAILED(OleInitialize(nullptr)))
        return 1;
    int result = 1;
    {
        Client client;
        client.statusPath = QString::fromLocal8Bit(argv[1]);
        client.commandPath = QString::fromLocal8Bit(argv[2]);
        WNDCLASSW type{};
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpfnWndProc = WindowProcedure;
        type.lpszClassName = L"MDS.NativeImeShutdown";
        RegisterClassW(&type);
        client.window = CreateWindowExW(0, type.lpszClassName, L"MDS native IME reference",
                                        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 420, 180,
                                        nullptr, nullptr, type.hInstance, &client);
        client.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"before",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 15, 15,
                                      360, 30, client.window, nullptr, type.hInstance, nullptr);
        if (client.window && client.edit) {
            SetWindowLongPtrW(client.edit, GWLP_USERDATA, LONG_PTR(&client));
            client.originalEdit = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(client.edit, GWLP_WNDPROC, LONG_PTR(EditProcedure)));
            ShowWindow(client.window, SW_SHOW);
            SetFocus(client.edit);
            SetTimer(client.window, 1, 25, nullptr);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            result = int(message.wParam);
        }
        if (client.window)
            DestroyWindow(client.window);
        std::fprintf(stderr, "Native EDIT message loop finished: %d; OleUninitialize\n", result);
        std::fflush(stderr);
        OleUninitialize();
        std::fprintf(stderr, "Native EDIT COM apartment released\n");
        std::fflush(stderr);
    }
    return result;
}
