// Test-owned native input activation and shutdown diagnostics.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <msctf.h>
#include <imm.h>
#include <wrl/client.h>
#include <DbgHelp.h>
#include <QString>
#include <thread>
#include <cstdio>
namespace mikudancestudio::test {
class ShutdownDiagnostic {
public:
    ShutdownDiagnostic() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_,
                        THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                        FALSE, 0);
    }
    ~ShutdownDiagnostic() {
        Cancel();
        if (done_)
            CloseHandle(done_);
        if (main_)
            CloseHandle(main_);
    }
    void Cancel() {
        if (done_)
            SetEvent(done_);
        if (worker_.joinable())
            worker_.join();
    }
    void Arm() {
        Cancel();
        if (done_)
            ResetEvent(done_);
        const auto path = qEnvironmentVariable("MDS_IME_DIAGNOSTIC_PATH").toStdWString();
        if (!done_ || path.empty())
            return;
        // Capture only this generated-fixture client, from a separate thread:
        // a blocked IMM/TSF call cannot service a GUI-thread diagnostic timer.
        worker_ = std::thread([this, path] {
            if (WaitForSingleObject(done_, 5000) != WAIT_TIMEOUT)
                return;
            const auto process = GetCurrentProcess();
            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
            if (main_ && SymInitialize(process, "", TRUE)) {
                DWORD64 addresses[64]{};
                unsigned count = 0;
                if (SuspendThread(main_) != DWORD(-1)) {
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_FULL;
                    if (GetThreadContext(main_, &context)) {
                        STACKFRAME64 frame{};
#ifdef _WIN64
                        constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
                        frame.AddrPC.Offset = context.Rip;
                        frame.AddrStack.Offset = context.Rsp;
                        frame.AddrFrame.Offset = context.Rbp;
#else
                        constexpr DWORD machine = IMAGE_FILE_MACHINE_I386;
                        frame.AddrPC.Offset = context.Eip;
                        frame.AddrStack.Offset = context.Esp;
                        frame.AddrFrame.Offset = context.Ebp;
#endif
                        frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode =
                            AddrModeFlat;
                        while (count < 64 &&
                               StackWalk64(machine, process, main_, &frame, &context, nullptr,
                                           SymFunctionTableAccess64, SymGetModuleBase64, nullptr) &&
                               frame.AddrPC.Offset)
                            addresses[count++] = frame.AddrPC.Offset;
                    }
                    ResumeThread(main_);
                }
                for (unsigned index = 0; index < count; ++index) {
                    IMAGEHLP_MODULE64 module{};
                    module.SizeOfStruct = sizeof(module);
                    SymGetModuleInfo64(process, addresses[index], &module);
                    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
                    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                    symbol->MaxNameLen = MAX_SYM_NAME;
                    DWORD64 displacement = 0;
                    SymFromAddr(process, addresses[index], &displacement, symbol);
                    std::fprintf(stderr, "IME shutdown frame %u: %s+0x%llx %s+0x%llx\n", index,
                                 module.ModuleName, addresses[index] - module.BaseOfImage,
                                 symbol->Name, displacement);
                }
                SymCleanup(process);
                std::fflush(stderr);
            }
            const auto output = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (output != INVALID_HANDLE_VALUE) {
                const auto written =
                    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), output,
                                      MiniDumpNormal, nullptr, nullptr, nullptr);
                std::fprintf(stderr, "IME shutdown diagnostic dump=%d error=%lu\n", written,
                             written ? 0ul : GetLastError());
                std::fflush(stderr);
                CloseHandle(output);
            }
        });
    }

private:
    HANDLE done_;
    HANDLE main_ = nullptr;
    std::thread worker_;
};
class JapaneseImeSession {
public:
    QString Activate(HWND window) {
        using Microsoft::WRL::ComPtr;
        ComPtr<ITfInputProcessorProfiles> profiles;
        auto result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                       CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
        if (FAILED(result))
            return QString("TSF profiles: 0x%1").arg(unsigned(result), 8, 16, QLatin1Char('0'));
        ComPtr<IEnumTfLanguageProfiles> available;
        result = profiles->EnumLanguageProfiles(0x411, &available);
        if (FAILED(result))
            return "Japanese TSF profile enumeration failed";
        TF_LANGUAGEPROFILE profile{};
        ULONG fetched = 0;
        while (available->Next(1, &profile, &fetched) == S_OK && fetched) {
            if (profile.catid != GUID_TFCAT_TIP_KEYBOARD)
                continue;
            ComPtr<ITfInputProcessorProfileMgr> manager;
            result = profiles.As(&manager);
            if (SUCCEEDED(result))
                result = manager->ActivateProfile(
                    TF_PROFILETYPE_INPUTPROCESSOR, 0x411, profile.clsid, profile.guidProfile,
                    nullptr, TF_IPPMF_ENABLEPROFILE | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
            if (FAILED(result))
                return QString("TSF activation: 0x%1")
                    .arg(unsigned(result), 8, 16, QLatin1Char('0'));
            activated_ = true;
            const auto context = ImmGetContext(window);
            if (!context)
                return "Japanese IME has no native input context";
            const auto opened = ImmSetOpenStatus(context, TRUE);
            DWORD previousConversion = 0, previousSentence = 0;
            ImmGetConversionStatus(context, &previousConversion, &previousSentence);
            std::fprintf(stderr, "Japanese IME initial conversion=0x%lx sentence=0x%lx\n",
                         previousConversion, previousSentence);
            const auto converted = ImmSetConversionStatus(
                context, IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE | IME_CMODE_ROMAN,
                IME_SMODE_PLAURALCLAUSE);
            DWORD conversion = 0, sentence = 0;
            ImmGetConversionStatus(context, &conversion, &sentence);
            std::fprintf(stderr, "Japanese IME conversion=0x%lx sentence=0x%lx open=%d\n",
                         conversion, sentence, ImmGetOpenStatus(context));
            ImmReleaseContext(window, context);
            return opened && converted ? QString() : QString("Cannot select native Hiragana mode");
        }
        return "No installed Japanese TSF keyboard profile";
    }
    QString Finish(HWND window) {
        if (!activated_)
            return {};
        // ActivateProfile selects an input method; it is not a reference-counted
        // acquisition. Do not inject DeactivateProfile into a natural-exit test:
        // the editor closes its windows and lets IMM/TSF perform normal teardown.
        std::fprintf(stderr, "IME close native composition/open state\n");
        std::fflush(stderr);
        if (const auto context = ImmGetContext(window)) {
            ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmSetOpenStatus(context, FALSE);
            ImmReleaseContext(window, context);
        }
        activated_ = false;
        std::fprintf(stderr, "IME composition closed; native window/COM teardown follows\n");
        std::fflush(stderr);
        return {};
    }

private:
    bool activated_ = false;
};
}
