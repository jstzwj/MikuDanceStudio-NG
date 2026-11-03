#pragma once
#include <string>
#include <string_view>
#include <cstddef>

namespace mikudancestudio::editor {
// The native ANSI edit control replaces unrepresentable Unicode with '?'.
// This explicitly lossy policy is only for model display metadata, never paths
// or motion-track identifiers. Record truncation is by bytes, even mid-CP932.
std::string EnglishNameBytes(std::string_view utf8, std::size_t byteLimit);
std::string EnglishNameDisplay(std::string_view cp932);
}
