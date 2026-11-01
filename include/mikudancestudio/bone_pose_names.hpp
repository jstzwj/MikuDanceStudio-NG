#pragma once
#include <cstddef>
#include <string_view>

namespace mikudancestudio::editor {
// Native pose paste resolves an exact source bone first. It searches the first
// CP932 side marker anywhere in that name, then compares only the opposite
// marker's suffix. Prefixes need not match; the first matching bone wins.
template <class NameAt>
int FindBonePoseTarget(std::string_view copied, std::size_t count, const NameAt& nameAt,
                       bool mirrored) {
    int source = -1;
    for (std::size_t i = 0; i < count; ++i)
        if (nameAt(i) == copied) {
            source = int(i);
            break;
        }
    if (source < 0 || !mirrored)
        return source;
    const auto opposite = [&](std::string_view marker, std::string_view other) {
        const auto hit = copied.find(marker);
        if (hit == std::string_view::npos)
            return -1;
        for (std::size_t i = 0; i < count; ++i) {
            const std::string_view name = nameAt(i);
            const auto at = name.find(other);
            if (at != std::string_view::npos && name.substr(at + 2) == copied.substr(hit + 2))
                return int(i);
        }
        return -1;
    };
    const int right = opposite("\x8d\xb6", "\x89\x45");
    if (right >= 0)
        return right;
    const int left = opposite("\x89\x45", "\x8d\xb6");
    return left >= 0 ? left : source;
}
}
