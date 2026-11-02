#pragma once
#include "rhi_backend.hpp"
namespace mikudancestudio::rhi {
#ifdef MIKUDANCESTUDIO_RHI_METAL
OffscreenBackendDevice CreateMetalOffscreenDevice(unsigned width, unsigned height,
                                                  const BackendOptions&);
#endif
}
