#pragma once
#include "pmm_format.hpp"
#include <cstdint>
namespace mikudancestudio::format {
// Native table capacity includes frame-zero channel roots. These are resource
// limits of the editable document, shared by loading, editing and serialization.
template <class T> inline constexpr std::uint32_t PmmKeyCapacity = 10000;
template <>
inline constexpr std::uint32_t PmmKeyCapacity<PmmBoneKey> = sizeof(void*) == 8 ? 600000 : 300000;
template <> inline constexpr std::uint32_t PmmKeyCapacity<PmmMorphKey> = 20000;
template <> inline constexpr std::uint32_t PmmKeyCapacity<PmmDisplayKey> = 1000;
}
