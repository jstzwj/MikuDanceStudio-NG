#include <ApplicationServices/ApplicationServices.h>

bool MoveShownPointer(int x, int y) {
    if (!CGPreflightPostEventAccess())
        return false;
    const auto event =
        CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, CGPointMake(x, y), kCGMouseButtonLeft);
    if (!event)
        return false;
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return true;
}
