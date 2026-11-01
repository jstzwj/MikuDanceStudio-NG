#pragma once
#include "viewport.hpp"
namespace mikudancestudio::qt {
using ExportProgress = std::function<bool(unsigned completed, unsigned total)>;
// Deterministic 30fps sequence, including physics warm-up from frame zero.
// The destination must not exist. A sibling staging directory is published by
// rename after success; cancellation/failure removes only that private staging.
bool ExportSequence(const editor::EditorSession&, const std::filesystem::path& builtins,
                    const rhi::BackendOptions&, const std::filesystem::path& destination,
                    unsigned first, unsigned last, unsigned width, unsigned height,
                    const ExportProgress&, std::string&);
}
