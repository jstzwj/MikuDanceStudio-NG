#pragma once
#include "rhi_backend.hpp"
namespace mikudancestudio::rhi {
#ifdef MIKUDANCESTUDIO_RHI_METAL
struct MetalStats {
    std::uint64_t submissions = 0, completed = 0, presentations = 0, readbacks = 0;
};
OffscreenBackendDevice CreateMetalOffscreenDevice(unsigned width, unsigned height,
                                                  const BackendOptions&);
BackendDevice CreateMetalDevice(const SwapchainDesc&, const BackendOptions&);
MetalStats QueryMetalStats(Device&);
bool ReadMetalPresentation(Device&, RgbaImage&);
#endif
}
