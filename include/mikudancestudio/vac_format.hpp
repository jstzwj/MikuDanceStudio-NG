#pragma once
#include <array>
#include <string>
#include <string_view>

namespace mikudancestudio::format {
// VAC uses CP932 text, radians, and an inverted integer shadow switch. The
// title and attachment line are ignored by MMD's accessory-object loader.
struct VacAccessory {
    std::string meshPath;
    float scale = 1;
    std::array<float, 3> position{}, rotation{};
    bool shadowEnabled = true;
};
// No filesystem/locale dependence. Failed or incomplete input preserves output.
bool ReadVac(std::string_view bytes, VacAccessory& output, std::string& reason);
}
