#pragma once
#include <cstdint>
#include <array>
namespace mikudancestudio {
// Live display switches, separate from PMM tracks and undoable document state.
struct SceneDisplayOptions {
    bool transparentModels = false;
    bool hideModels = false;
    bool mipmaps = true;
    // PMM loading initializes each model's displayState to zero without
    // resetting the global effect parameter. Ordinary model imports inherit it.
    std::array<bool, 255> opaqueModels{};
    bool Transparent(std::uint8_t slot) const {
        return transparentModels && slot < opaqueModels.size() && !opaqueModels[slot];
    }
};
}
