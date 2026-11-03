#pragma once
#include "model_reader.hpp"
#include <string>

namespace mikudancestudio::format {
// MMD's enhanced-model stream: English, toon and physics sections are always
// written. The input uses disk morph indices (later morphs index morph zero).
// Failure preserves output. No live vertex/resource is modified by the writer.
bool WriteEnhancedPmd(const PmdDocument&, std::vector<std::uint8_t>&, std::string& reason);
}
