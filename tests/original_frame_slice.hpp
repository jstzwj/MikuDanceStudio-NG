#pragma once
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

// Test-only ABI adapter for a contiguous original frame-driver block. Its
// instructions are preserved; the outgoing continuation returns to this caller.
// The caller supplies the exact entry register/scratch context and signature.
class OriginalFrameSlice {
    unsigned char* memory_ = nullptr;
    unsigned char* continuation_ = nullptr;
    std::array<unsigned char, 14> saved_{};
    std::vector<unsigned char> bytes_;
    static void Require(bool ok, const char* reason) {
        if (!ok)
            throw std::runtime_error(reason);
    }
    void Emit(std::initializer_list<unsigned char> b) { bytes_.insert(bytes_.end(), b); }
    template <class T> void Value(T value) {
        const auto* p = reinterpret_cast<const unsigned char*>(&value);
        bytes_.insert(bytes_.end(), p, p + sizeof value);
    }
    void Xmm(unsigned index, bool load) {
        Emit({0xf3});
        if (index >= 8)
            Emit({0x44});
        Emit({0x0f, static_cast<unsigned char>(load ? 0x6f : 0x7f),
              static_cast<unsigned char>(0x84 | ((index & 7) << 3)), 0x24});
        Value(std::uint32_t(0x2000 + (index - 6) * 16));
    }

public:
    OriginalFrameSlice(
        const OriginalPhysicsModule& original, std::uintptr_t start, std::uintptr_t end,
        std::initializer_list<unsigned char> signature,
        std::initializer_list<std::pair<unsigned, float>> registers,
        std::initializer_list<std::pair<unsigned, std::uint64_t>> wideRegisters = {}) {
        Require(signature.size() && signature.size() <= saved_.size(),
                "frame slice signature size");
        memory_ = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Require(memory_ != nullptr, "allocate original frame trampoline");
        Emit({0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
        Emit({0x48, 0x81, 0xec});
        Value(std::uint32_t(0x2208));
        for (unsigned n = 6; n <= 15; ++n)
            Xmm(n, false);
        Emit({0x48, 0x8d, 0xac, 0x24});
        Value(std::uint32_t(0x80));
        Emit({0x49, 0x89, 0xcc, 0x45, 0x31, 0xff, 0x45, 0x31, 0xed}); // r12=app,r15=0,r13=0
        for (const auto& [index, value] : registers) {
            Require(index >= 6 && index <= 15, "frame slice register");
            Emit({0xb8});
            Value(value);
            Emit({0x66});
            if (index >= 8)
                Emit({0x44});
            Emit({0x0f, 0x6e, static_cast<unsigned char>(0xc0 | ((index & 7) << 3))});
        }
        for (const auto& [index, value] : wideRegisters) {
            Require(index >= 6 && index <= 15, "frame slice wide register");
            Emit({0x48, 0xb8});
            Value(value);
            Emit({0x66, static_cast<unsigned char>(index >= 8 ? 0x4c : 0x48), 0x0f, 0x6e,
                  static_cast<unsigned char>(0xc0 | ((index & 7) << 3))});
        }
        Emit({0x48, 0xb8});
        Value(original.Function<std::uintptr_t>(start));
        Emit({0xff, 0xe0});
        const auto epilogue = reinterpret_cast<std::uintptr_t>(memory_ + bytes_.size());
        for (unsigned n = 6; n <= 15; ++n)
            Xmm(n, true);
        Emit({0x48, 0x81, 0xc4});
        Value(std::uint32_t(0x2208));
        Emit({0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5f, 0x5e, 0x5d, 0x5b, 0xc3});
        std::memcpy(memory_, bytes_.data(), bytes_.size());
        DWORD old;
        Require(VirtualProtect(memory_, 4096, PAGE_EXECUTE_READ, &old) != 0,
                "protect original frame trampoline");
        FlushInstructionCache(GetCurrentProcess(), memory_, bytes_.size());
        continuation_ = original.Function<unsigned char*>(end);
        Require(std::equal(signature.begin(), signature.end(), continuation_),
                "original frame continuation signature");
        std::copy_n(continuation_, saved_.size(), saved_.begin());
        Require(VirtualProtect(continuation_, saved_.size(), PAGE_EXECUTE_READWRITE, &old) != 0,
                "patch original frame continuation");
        const unsigned char jump[]{0xff, 0x25, 0, 0, 0, 0};
        std::memcpy(continuation_, jump, sizeof jump);
        std::memcpy(continuation_ + 6, &epilogue, 8);
        VirtualProtect(continuation_, saved_.size(), old, &old);
        FlushInstructionCache(GetCurrentProcess(), continuation_, saved_.size());
    }
    OriginalFrameSlice(const OriginalFrameSlice&) = delete;
    OriginalFrameSlice& operator=(const OriginalFrameSlice&) = delete;
    ~OriginalFrameSlice() {
        if (continuation_) {
            DWORD old;
            VirtualProtect(continuation_, saved_.size(), PAGE_EXECUTE_READWRITE, &old);
            std::copy(saved_.begin(), saved_.end(), continuation_);
            VirtualProtect(continuation_, saved_.size(), old, &old);
        }
        if (memory_)
            VirtualFree(memory_, 0, MEM_RELEASE);
    }
    void Run(void* app) const { reinterpret_cast<void (*)(void*)>(memory_)(app); }
};
