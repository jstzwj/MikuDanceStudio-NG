#pragma once
// Test-executable C++ allocation accounting. Qt/driver mallocs and other threads
// are outside this counter; use it to compare owned C++ frame preparation only.
#include <algorithm>
#include <cstdlib>
#include <new>
#ifdef _MSC_VER
#include <malloc.h>
#endif
namespace allocation_counter {
inline thread_local bool active = false;
inline thread_local std::size_t count = 0, bytes = 0;
inline void Record(std::size_t n) {
    if (active) {
        ++count;
        bytes += n;
    }
}
inline void Begin() {
    count = bytes = 0;
    active = true;
}
inline void End() {
    active = false;
}
}
void* operator new(std::size_t n) {
    allocation_counter::Record(n);
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void* operator new(std::size_t n, std::align_val_t alignment) {
    allocation_counter::Record(n);
    const auto a = std::size_t(alignment);
#ifdef _MSC_VER
    void* p = _aligned_malloc(n ? n : a, a);
#else
    void* p = std::aligned_alloc(a, ((std::max(n, a) + a - 1) / a) * a);
#endif
    if (p)
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
