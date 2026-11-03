#pragma once
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

inline std::vector<std::uint8_t>
VsqFixture(const std::vector<std::string>& phonemes = {"a", "i", "M", "e", "o"},
           unsigned division = 480,
           const std::vector<std::pair<unsigned, unsigned>>& tempos = {{0, 500000}}) {
    std::string text = "[EventList]\n0=ID#0000\n";
    const auto four = [](unsigned value) {
        const auto number = std::to_string(value);
        return std::string(4 - number.size(), '0') + number;
    };
    for (unsigned i = 0; i < phonemes.size(); ++i)
        text += std::to_string(960 + i * 720) + "=ID#" + four(i + 1) + "\n";
    for (unsigned i = 0; i < phonemes.size(); ++i)
        text += "[ID#" + four(i + 1) + "]\nLength=" + std::to_string(360 + i * 41) +
                "\nLyricHandle=h#" + four(i) + "\n";
    for (unsigned i = 0; i < phonemes.size(); ++i)
        text += "[h#" + four(i) + "]\nL0=\"x\",\"" + phonemes[i] + "\"\n";
    std::vector<std::uint8_t> result;
    const auto big = [](auto& bytes, unsigned value, unsigned count) {
        while (count--)
            bytes.push_back(std::uint8_t(value >> (count * 8)));
    };
    const auto delta = [](auto& bytes, unsigned value) {
        unsigned buffer[4], count = 0;
        do {
            buffer[count++] = value & 127;
            value >>= 7;
        } while (value);
        while (count--)
            bytes.push_back(std::uint8_t(buffer[count] | (count ? 128 : 0)));
    };
    const auto literal = [](auto& bytes, const std::string& value) {
        bytes.insert(bytes.end(), value.begin(), value.end());
    };
    literal(result, "MThd");
    big(result, 6, 4);
    big(result, 1, 2);
    big(result, unsigned(tempos.size()), 2);
    big(result, division, 2);
    for (unsigned i = 0; i < tempos.size(); ++i) {
        std::vector<std::uint8_t> track;
        delta(track, tempos[i].first);
        track.insert(track.end(), {0xff, 0x51, 3});
        big(track, tempos[i].second, 3);
        if (i + 1 == tempos.size())
            for (std::size_t at = 0; at < text.size(); at += 100) {
                const auto count = (std::min)(std::size_t(100), text.size() - at);
                track.insert(track.end(), {0, 0xff, 1, std::uint8_t(count + 8)});
                literal(track, "DM:0000:");
                literal(track, text.substr(at, count));
            }
        track.insert(track.end(), {0, 0xff, 0x2f, 0});
        literal(result, "MTrk");
        big(result, unsigned(track.size()), 4);
        result.insert(result.end(), track.begin(), track.end());
    }
    return result;
}
inline bool WriteVsqFixture(const std::filesystem::path& path,
                            const std::vector<std::uint8_t>& bytes = VsqFixture()) {
    std::ofstream file(path, std::ios::binary);
    return bool(
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())));
}
inline std::vector<std::vector<std::uint8_t>> VsqProfiles() {
    std::vector<std::string> consonants;
    for (const auto* prefix : {"k'", "S", "tS", "J", "C", "m'", "4'", "g'", "dZ", "b'", "p'", "4",
                               "j", "p\\", "dz", "ts"})
        for (char vowel : std::string("aiMeoh"))
            consonants.push_back(std::string(prefix) + ' ' + vowel);
    consonants.insert(consonants.end(), {"a", "i", "u", "e", "o", "m", "N", "n", "sil", "p\\"});
    return {VsqFixture(),
            VsqFixture({"k' a", "S i", "tS M", "C h", "4 h", "p\\ h", "n", "N", "j a", "e"}, 441,
                       {{0, 500001}, {480, 333333}, {900, 666667}}),
            VsqFixture({"e", "a", "i"}, 960, {{0, 666667}, {320, 500003}}),
            VsqFixture(consonants, 480, {{0, 500003}}),
            VsqFixture({"a", "i", "u", "e", "o"}, 441,
                       {{0, 444443}, {360, 333331}, {810, 555553}, {1020, 666667}})};
}
