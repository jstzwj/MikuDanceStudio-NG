#include "mikudancestudio/wave_format.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <winioctl.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif
using namespace mikudancestudio::format;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ": " << reason << '\n';                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
namespace {
std::uint64_t Resident() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)
               ? counters.WorkingSetSize
               : 0;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                     &count) == KERN_SUCCESS
               ? info.resident_size
               : 0;
#else
    std::ifstream file("/proc/self/statm");
    std::uint64_t virtualPages = 0, residentPages = 0;
    file >> virtualPages >> residentPages;
    return residentPages * std::uint64_t(sysconf(_SC_PAGESIZE));
#endif
}
void U16(std::ostream& out, unsigned n) {
    const char b[]{char(n), char(n >> 8)};
    out.write(b, 2);
}
void U32(std::ostream& out, unsigned n) {
    U16(out, n);
    U16(out, n >> 16);
}
}
int main() {
    const auto folder =
        std::filesystem::temp_directory_path() /
        ("mds-wave-stream-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(folder));
    const auto path = folder / "three-hours.wav";
    {
        std::ofstream create(path, std::ios::binary);
        CHECK(create);
    }
#if defined(_WIN32)
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(handle != INVALID_HANDLE_VALUE);
    DWORD returned = 0;
    const bool sparse =
        DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(handle);
    CHECK(sparse);
#endif
    constexpr std::uint64_t frames = 48000ull * 10800;
    {
        // Preserve the sparse attribute set above: ofstream's implicit trunc
        // can force Windows to initialize the entire two-gigabyte hole.
        std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
        out.write("RIFF", 4);
        U32(out, unsigned(36 + frames * 4));
        out.write("WAVEfmt ", 8);
        U32(out, 16);
        U16(out, 1);
        U16(out, 2);
        U32(out, 48000);
        U32(out, 192000);
        U16(out, 4);
        U16(out, 16);
        out.write("data", 4);
        U32(out, unsigned(frames * 4));
        out.seekp(std::streamoff(44 + frames * 4 - 1));
        out.put(0);
        for (auto n : std::array<std::uint64_t, 4>{0, 48000, 48000ull * 3600, frames - 1}) {
            out.seekp(std::streamoff(44 + n * 4));
            U16(out, 0x8000);
            U16(out, 0x7fff);
        }
        CHECK(out);
    }
    const auto before = Resident();
    std::shared_ptr<const WaveSource> stream;
    CHECK(OpenWaveFile(path, stream, reason));
    CHECK(stream->SampleFrames() == frames && stream->sampleRate == 48000 && stream->channels == 2);
    std::array<float, WaveSource::MaxReadFrames * 2> block;
    for (auto n : std::array<std::uint64_t, 4>{frames - 1, 48000ull * 3600, 48000, 0}) {
        CHECK(stream->ReadFrames(n, 2, block.data(), reason));
        CHECK(block[0] == -1.f && block[1] == 32767 / 32768.f);
        CHECK(block[2] == 0 && block[3] == 0);
    }
    CHECK(stream->ReadFrames(UINT64_MAX, 4, block.data(), reason));
    CHECK(std::all_of(block.begin(), block.begin() + 8, [](float f) { return f == 0; }));
    CHECK(!stream->ReadFrames(0, WaveSource::MaxReadFrames + 1, block.data(), reason));
    // Concurrent independent cursors must not interfere with file seeks.
    bool second = false;
    std::thread worker([&] {
        std::array<float, 2> samples;
        std::string error;
        second = true;
        for (unsigned i = 0; i < 1000; ++i)
            if (!stream->ReadFrames(48000ull * 3600, 1, samples.data(), error) || samples[0] != -1)
                second = false;
    });
    bool first = true;
    for (unsigned i = 0; i < 1000; ++i)
        if (!stream->ReadFrames(48000, 1, block.data(), reason) || block[0] != -1)
            first = false;
    worker.join();
    CHECK(first && second);
    const auto after = Resident();
    CHECK(before && after && after < before + (32u << 20));
    auto keep = stream;
    CHECK(!OpenWaveFile(folder / "missing.wav", stream, reason) && stream == keep);
    std::cout << "{\"stage\":\"P6.3\",\"logicalSeconds\":10800,\"fileBytes\":"
              << std::filesystem::file_size(path) << ",\"residentBefore\":" << before
              << ",\"residentAfter\":" << after
              << ",\"maxReadFrames\":8192,"
                 "\"concurrentSeeks\":2000,\"passed\":true}\n";
    stream.reset();
    // keep owns the file until here; explicit close before Windows cleanup.
    keep.reset();
    CHECK(std::filesystem::remove(path));
    // Known extrema in every declared encoding, data-before-fmt, multiple
    // payloads and an odd-sized ancillary chunk exercise the shared layout.
    const auto smallPath = folder / "small.wav";
    for (unsigned channels : {1u, 2u})
        for (unsigned encoding : {8u, 16u, 24u, 32u, 132u}) {
            const auto bits = encoding == 132 ? 32 : encoding;
            const auto bytes = channels * (bits / 8);
            {
                std::ofstream file(smallPath, std::ios::binary);
                file.write("RIFF", 4);
                U32(file, 0);
                file.write("WAVEdata", 8);
                U32(file, bytes);
                const auto sample = [&](std::uint32_t raw) {
                    for (unsigned c = 0; c < bits / 8; ++c)
                        file.put(char(raw >> (c * 8)));
                };
                for (unsigned c = 0; c < channels; ++c)
                    sample(bits == 8 ? 0 : encoding == 132 ? 0xbf800000 : (1u << (bits - 1)));
                if (bytes & 1)
                    file.put(0);
                file.write("JUNK", 4);
                U32(file, 1);
                file.put(7);
                file.put(0);
                file.write("fmt ", 4);
                U32(file, 16);
                U16(file, encoding == 132 ? 3 : 1);
                U16(file, channels);
                U32(file, 48000);
                U32(file, 48000 * bytes);
                U16(file, bytes);
                U16(file, bits);
                file.write("data", 4);
                U32(file, bytes);
                for (unsigned c = 0; c < channels; ++c)
                    sample(bits == 8 ? 255 : encoding == 132 ? 0x3f800000 : (1u << (bits - 1)) - 1);
                if (bytes & 1)
                    file.put(0);
                const auto size = file.tellp();
                file.seekp(4);
                U32(file, unsigned(size) - 8);
                CHECK(file);
            }
            CHECK(OpenWaveFile(smallPath, stream, reason));
            CHECK(stream->SampleFrames() == 2 && stream->channels == channels);
            CHECK(stream->ReadFrames(0, 4, block.data(), reason));
            for (unsigned c = 0; c < channels; ++c) {
                CHECK(block[c] == -1);
                const auto maximum =
                    encoding == 132 ? 1.f : float(1. - 1. / double(std::uint64_t(1) << (bits - 1)));
                CHECK(block[channels + c] == maximum);
                CHECK(block[channels * 2 + c] == 0 && block[channels * 3 + c] == 0);
            }
            stream.reset();
        }
    CHECK(std::filesystem::remove(smallPath) && std::filesystem::remove(folder));
    return 0;
}
