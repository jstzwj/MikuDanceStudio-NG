#include "mikudancestudio/vac_format.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <cmath>
#include <cstdint>
#include <locale>
#include <sstream>

namespace mikudancestudio::format {
bool ReadVac(std::string_view bytes, VacAccessory& output, std::string& reason) {
    const auto fail = [&](const char* why) {
        reason = why;
        return false;
    };
    if (bytes.empty() || bytes.size() > (64u << 10) || bytes.find('\0') != bytes.npos)
        return fail("invalid-vac-text");
    // Windows fopen("r") translates CRLF before fgets/fscanf see the bytes.
    std::string normalized;
    normalized.reserve(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n')
            continue;
        normalized.push_back(bytes[i]);
    }
    std::istringstream input(normalized);
    input.imbue(std::locale::classic());
    std::string title, mesh, line;
    VacAccessory next;
    // Valid files fit the original fgets buffers. Reject overlong fields rather
    // than reproducing its truncated-field and stack-overwrite failure modes.
    if (!std::getline(input, title) || title.size() > 98 || !std::getline(input, mesh) ||
        mesh.empty() || mesh.size() > 254 || mesh.find_first_of("\r\t") != mesh.npos ||
        !text::DecodeCp932(mesh, next.meshPath))
        return fail("invalid-vac-mesh-path");
    const auto triple = [&](std::array<float, 3>& values) {
        char comma;
        return bool(input >> values[0]) && input.get(comma) && comma == ',' &&
               bool(input >> values[1]) && input.get(comma) && comma == ',' &&
               bool(input >> values[2]);
    };
    if (!(input >> next.scale) || !triple(next.position) || !triple(next.rotation))
        return fail("invalid-vac-transform");
    // The first fgets consumes the rotation line's remainder; the next skips
    // the attachment label. Loading a VAC does not attach it to that bone.
    std::int32_t disabled = 0;
    if (!std::getline(input, line) || line.size() > 98 || !std::getline(input, line) ||
        line.size() > 98)
        return fail("invalid-vac-shadow-switch");
    // Original six-line VACs predate the shadow field. A failed conversion
    // leaves the initially zero native field intact and therefore enables it.
    input >> disabled;
    if (!std::isfinite(next.scale))
        return fail("nonfinite-vac-transform");
    for (unsigned i = 0; i < 3; ++i)
        if (!std::isfinite(next.position[i]) || !std::isfinite(next.rotation[i]))
            return fail("nonfinite-vac-transform");
    // The original fscanf writes an int into the byte-sized shadow field and
    // subsequently tests its low byte. Keep that result without adjacent writes.
    next.shadowEnabled = std::uint8_t(disabled) == 0;
    output = std::move(next);
    reason.clear();
    return true;
}
}
