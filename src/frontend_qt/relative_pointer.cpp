#include "relative_pointer.hpp"
#if defined(_WIN32) || defined(__APPLE__)
namespace mikudancestudio::qt {
std::unique_ptr<RelativePointer> CreateWaylandRelativePointer() {
    return {};
}
}
#endif
