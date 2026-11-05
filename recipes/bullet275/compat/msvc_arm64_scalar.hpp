#pragma once
// Bullet 2.75 selects x86 SSE solely from WIN32 + MSVC. Select its stock
// single-precision scalar branch on native Windows ARM64, consistently in the
// libraries and all consumers, without changing the archived upstream files.
#if defined(_MSC_VER) && defined(_M_ARM64)
#pragma push_macro("WIN32")
#undef WIN32
#include "LinearMath/btScalar.h"
#pragma pop_macro("WIN32")
static_assert(sizeof(btScalar) == sizeof(float), "Bullet ARM64 must retain float btScalar");
#ifdef BT_USE_SSE
#error Bullet ARM64 must use the stock scalar implementation
#endif
#endif
