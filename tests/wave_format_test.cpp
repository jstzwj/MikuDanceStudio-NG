#include "mikudancestudio/wave_format.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace mikudancestudio::format;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    std::vector<std::uint8_t> bytes;
    const auto text = [&](const char* s) { bytes.insert(bytes.end(), s, s + 4); };
    const auto u16 = [&](unsigned n) {
        bytes.push_back(std::uint8_t(n));
        bytes.push_back(std::uint8_t(n >> 8));
    };
    const auto u32 = [&](unsigned n) {
        u16(n);
        u16(n >> 16);
    };
    text("RIFF");
    u32(40);
    text("WAVE");
    text("fmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(48000);
    u32(96000);
    u16(2);
    u16(16);
    text("data");
    u32(4);
    u16(0x8000);
    u16(0x7fff);
    WaveAudio wave;
    std::string reason;
    CHECK(ReadWave(bytes, wave, reason));
    CHECK(wave.sourceBlockAlign == 2);
    CHECK(wave.samples[0] == -1.f);
    CHECK(wave.samples[1] == 32767 / 32768.f);
    const auto valid = bytes;
    bytes.pop_back();
    CHECK(!ReadWave(bytes, wave, reason));
    CHECK(wave.samples.size() == 2);
    bytes = valid;
    bytes[32] = 3;
    CHECK(!ReadWave(bytes, wave, reason));
    bytes = valid;
    bytes[20] = 3;
    bytes[32] = 4;
    bytes[34] = 32;
    const unsigned rate = 192000;
    std::memcpy(bytes.data() + 28, &rate, 4);
    bytes[44] = 0;
    bytes[45] = 0;
    bytes[46] = 0x80;
    bytes[47] = 0x7f;
    CHECK(!ReadWave(bytes, wave, reason));
    wave.sampleRate = 44100;
    wave.channels = 2;
    wave.samples.resize(44100 * 2 * 3);
    CHECK(wave.FrameOffset(1) == 1470);
    CHECK(wave.FrameOffset(30) == 44100);
    CHECK(wave.FrameOffset(90) == 132300);
    CHECK(wave.FrameOffset(100) == 132300);
    wave.sampleRate = 48000;
    CHECK(wave.FrameOffset(1) == 1600);
    std::puts(
        "WAV: PCM extrema, truncation, alignment, nonfinite samples and rational frame offsets passed");
}
