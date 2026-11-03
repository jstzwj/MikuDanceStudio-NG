#include "mikudancestudio/vsq_format.hpp"
#include <algorithm>
#include <charconv>
#include <climits>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace mikudancestudio::format {
namespace {
struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t at = 0, end;
    unsigned Byte() {
        if (at == end)
            throw std::runtime_error("vsq-analysis-error");
        return bytes.at(at++);
    }
    unsigned Big(unsigned count) {
        unsigned result = 0;
        while (count--)
            result = (result << 8) | Byte();
        return result;
    }
    unsigned Delta() {
        unsigned result = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const auto byte = Byte();
            result = (result << 7) | (byte & 127);
            if (!(byte & 128))
                return result;
        }
        throw std::runtime_error("vsq-analysis-error");
    }
    std::string Text(unsigned count) {
        if (count > end - at)
            throw std::runtime_error("vsq-analysis-error");
        std::string value(bytes.begin() + at, bytes.begin() + at + count);
        at += count;
        return value;
    }
};
void Require(bool value) {
    if (!value)
        throw std::runtime_error("vsq-analysis-error");
}
int Integer(std::string_view text) {
    int result = 0;
    const auto read = std::from_chars(text.data(), text.data() + text.size(), result);
    Require(read.ec == std::errc{} && read.ptr == text.data() + text.size() && result >= 0);
    return result;
}
std::string Four(unsigned index) {
    const auto value = std::to_string(index);
    Require(value.size() <= 4);
    return std::string(4 - value.size(), '0') + value;
}
void Classify(VsqEvent& event) {
    if (event.vowel == 'M')
        event.vowel = 'u';
    const char* needles[]{"k'", "S", "tS", "J", "C", "m'", "4'", "g'", "dZ", "b'", "p'"};
    const char* upper = "KSTNHMRGZBP";
    const char* lower = "kstnhmrgzbp";
    for (unsigned i = 0; i < std::size(needles); ++i)
        if (event.consonant == needles[i]) {
            if (event.vowel == 'i')
                event.consonant.assign(1, lower[i]);
            else if (event.vowel == 'a' || event.vowel == 'u' || event.vowel == 'o')
                event.consonant.assign(1, upper[i]);
        }
    for (const auto& pair :
         {std::pair{"4", "r"}, {"j", "y"}, {"p\\", "h"}, {"dz", "z"}, {"ts", "t"}})
        if (event.consonant == pair.first)
            event.consonant = pair.second;
    if (event.vowel == 'm' || event.vowel == 'N')
        event.vowel = 'n';
}
}
bool ReadVsq(const std::vector<std::uint8_t>& bytes, VsqDocument& out, std::string& reason) {
    try {
        Require(bytes.size() <= 64u * 1024 * 1024);
        Reader input{bytes, 0, bytes.size()};
        Require(input.Text(4) == "MThd" && input.Big(4) == 6);
        Require(input.Big(2) <= 1);
        const auto tracks = input.Big(2), division = input.Big(2);
        Require(tracks && division && division < 0x8000);
        struct Tempo {
            int time, value;
        };
        std::vector<Tempo> tempos;
        std::string text;
        for (unsigned i = 0; i < tracks; ++i) {
            Require(input.Text(4) == "MTrk");
            const auto length = input.Big(4);
            Require(length <= input.end - input.at);
            Reader track{bytes, input.at, input.at + length};
            input.at += length;
            unsigned tick = 0;
            bool ended = false;
            while (track.at < track.end) {
                const auto delta = track.Delta();
                Require(delta <= unsigned(INT_MAX) - tick);
                tick += delta;
                const auto status = track.Byte();
                if ((status >= 0x80 && status <= 0x9f) || (status >= 0xb0 && status <= 0xbf)) {
                    track.Big(2);
                    continue;
                }
                if (status == 0xf0 || status == 0xf7)
                    throw std::runtime_error("vsq-sysex-unsupported");
                Require(status == 0xff);
                const auto type = track.Byte(), size = track.Byte();
                // The original reads one byte, rather than SMF variable length.
                Require(size < 128);
                const auto payload = track.Text(size);
                if (type == 0x2f) {
                    Require(!size && track.at == track.end);
                    ended = true;
                    break;
                }
                if (type == 1) {
                    Require(size >= 8);
                    text.append(payload, 8, std::string::npos);
                } else if (type == 0x51) {
                    Require(size == 3);
                    const auto tempo = (unsigned(std::uint8_t(payload[0])) << 16) |
                                       (unsigned(std::uint8_t(payload[1])) << 8) |
                                       unsigned(std::uint8_t(payload[2]));
                    Require(tempo != 0);
                    tempos.push_back({int(tick), int(tempo)});
                } else
                    Require(type == 0 || (type >= 2 && type <= 5) || (type == 0x58 && size == 4) ||
                            (type == 0x59 && size == 2));
            }
            Require(ended);
        }
        Require(input.at == input.end && !tempos.empty() && text.find('\0') == std::string::npos);
        std::map<std::string, std::vector<std::string>> sections;
        std::istringstream lines(text);
        std::string line, section;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            Require(line.size() < 255);
            if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                Require(!sections.count(section));
                sections[section];
            } else if (!line.empty())
                sections[section].push_back(line);
        }
        const auto list = sections.find("EventList");
        Require(list != sections.end() && list->second.size() > 1);
        VsqDocument result;
        std::ostringstream report;
        report.imbue(std::locale::classic());
        report << std::fixed << std::setprecision(3);
        for (unsigned i = 1; i < list->second.size(); ++i) {
            const auto& entry = list->second[i];
            const auto equal = entry.find("=ID#");
            if (equal == std::string::npos)
                break;
            const float sourceTick = float(Integer(std::string_view(entry).substr(0, equal)));
            Require(sourceTick < 2147483648.f);
            int tick = int(sourceTick);
            const auto note = sections.find("ID#" + Four(i));
            Require(note != sections.end());
            std::string handle;
            int length = -1;
            for (const auto& field : note->second) {
                if (field.compare(0, 7, "Length=") == 0)
                    length = Integer(std::string_view(field).substr(7));
                if (field.compare(0, 12, "LyricHandle=") == 0)
                    handle = field.substr(12, 6);
            }
            Require(length >= 0 && handle.size() == 6);
            const auto lyric = sections.find(handle);
            Require(lyric != sections.end() && !lyric->second.empty());
            const auto& value = lyric->second.front();
            const auto quote = value.find('"'), comma = value.find("\",\"");
            Require(quote != std::string::npos && comma != std::string::npos && comma > quote);
            const auto close = value.find('"', comma + 3);
            Require(close != std::string::npos);
            VsqEvent event;
            event.token = value.substr(quote + 1, comma - quote - 1);
            Require(event.token.size() <= 7);
            const auto phoneme = value.substr(comma + 3, close - comma - 3);
            Require(!phoneme.empty());
            if (phoneme.size() == 1)
                event.vowel = phoneme[0];
            else {
                const auto space = phoneme.find(' ');
                if (space != std::string::npos) {
                    Require(space <= 5 && space + 1 < phoneme.size());
                    event.consonant = phoneme.substr(0, space);
                    event.vowel = phoneme[space + 1];
                } else
                    event.vowel = 'N';
            }
            unsigned current = 0;
            float seconds = 0;
            const auto add = [&](int ticks, int tempo) {
                const float beats = float(ticks) / float(division);
                const float micros = beats * float(tempo);
                seconds = seconds + micros * 0.000001f;
            };
            while (current + 1 < tempos.size() && tick > tempos[current + 1].time) {
                // MMD subtracts the absolute next entry time each time. Retain
                // that behavior, including three or more tempo changes.
                add(tempos[current + 1].time, tempos[current].value);
                tick -= tempos[++current].time;
            }
            add(tick, tempos[current].value);
            event.start = seconds;
            const float beats = float(length) / float(division);
            const float micros = float(tempos[current].value) * beats;
            event.end = seconds + micros * 0.000001f;
            Classify(event);
            report << std::setw(7) << event.start << " -" << std::setw(7) << event.end << "   "
                   << std::left << std::setw(4) << event.token << std::right << ' ' << std::setw(1)
                   << event.consonant << ' ' << event.vowel << "\r\n";
            result.events.push_back(std::move(event));
        }
        Require(!result.events.empty());
        result.report = report.str();
        out = std::move(result);
        reason.clear();
        return true;
    } catch (const std::runtime_error& error) {
        reason = error.what();
        return false;
    }
}
}
