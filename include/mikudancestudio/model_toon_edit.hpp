#pragma once
#include "rhi_image.hpp"
#include <array>
#include <filesystem>
#include <memory>

namespace mikudancestudio {
// Live PMD enhancement data. CPU images survive device replacement and file
// removal, just as the original shared texture cache does. Never serialized in PMM.
struct ModelToonEdit {
    std::array<std::array<char, 100>, 10> names{};
    std::array<std::filesystem::path, 10> paths{};
    std::array<std::shared_ptr<const rhi::RgbaImage>, 10> images{};
};
}
