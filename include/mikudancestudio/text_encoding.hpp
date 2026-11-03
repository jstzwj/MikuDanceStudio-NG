#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace mikudancestudio::text {
// Explicit disk encodings. Failure preserves output; no locale or ACP lookup.
bool DecodeCp932(std::string_view bytes, std::string& utf8);
// Display-only model metadata: replace malformed bytes, retaining no locale
// dependency. Disk paths and motion identities must keep the strict decoder.
std::string DecodeCp932Display(std::string_view bytes);
// Strict, deterministic inverse. Rejects malformed UTF-8 and unrepresentable
// characters instead of losing model names or silently changing paths.
bool EncodeCp932(std::string_view utf8, std::string& bytes);
bool DecodeUtf16Le(const std::uint8_t* bytes, std::size_t size, std::string& utf8);
}
