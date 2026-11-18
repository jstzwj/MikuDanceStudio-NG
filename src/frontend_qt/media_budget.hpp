#pragma once
#include <chrono>
#include <cstddef>

namespace mikudancestudio::qt {
struct MediaBudget {
    static constexpr int VideoSide = 8192;
    static constexpr int VideoPixels = 16777216;
    static constexpr int VideoPacketBytes = 64 * 1024 * 1024;
    static constexpr int VideoCodecThreads = 2;
    static constexpr auto VideoTimeout = std::chrono::seconds(10);
    static constexpr std::size_t AudioReadBytes = 64 * 1024;
    static constexpr unsigned EncoderAudioFrames = 1024;
    static constexpr auto EncoderTimeout = std::chrono::seconds(30);
};
}
