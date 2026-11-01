#include <Windows.h>
#include "mikudancestudio/physics_world_state.hpp"
#include "btBulletDynamicsCommon.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
static void Check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
#include "original_physics_module.hpp"

// Execute the actual ground-body store from each hash-verified original path.
// The surrounding UI/file IO is omitted. R14 is set/preserved by the adapter;
// no original entrypoint, window, device or desktop input is invoked.
class Store {
    void* code_ = nullptr;

public:
    Store(const OriginalPhysicsModule& original, unsigned rva, unsigned bytes) {
        std::vector<unsigned char> code{0x41, 0x56, 0x41, 0xbe, 1, 0, 0, 0}; // push r14; mov r14d,1
        const auto* first = original.Function<const unsigned char*>(rva);
        code.insert(code.end(), first, first + bytes);
        code.insert(code.end(), {0x41, 0x5e, 0xc3}); // pop r14; ret
        code_ = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        Check(code_ != nullptr, "allocate native floor slice");
        std::memcpy(code_, code.data(), code.size());
        DWORD previous;
        Check(VirtualProtect(code_, code.size(), PAGE_EXECUTE_READ, &previous) != 0,
              "protect floor slice");
        FlushInstructionCache(GetCurrentProcess(), code_, code.size());
    }
    ~Store() { VirtualFree(code_, 0, MEM_RELEASE); }
    void operator()(btRigidBody& body) const {
        reinterpret_cast<void (*)(btRigidBody*)>(code_)(&body);
    }
};
int main(int argc, char** argv) try {
    Check(argc == 2, "provide original x64 executable");
    OriginalPhysicsModule original(argv[1]);
    const struct {
        unsigned rva, bytes;
        bool enabled;
    } cases[]{
        {0x500bc, 10, true}, {0x500f7, 11, false}, // menu
        {0x81c1a, 7, true},  {0x81c52, 10, false}, // PMM v2 load
        {0x884b6, 10, true}, {0x884ee, 10, false}  // PMM v1 load
    };
    btStaticPlaneShape shape(btVector3(0, 1, 0), 0);
    btRigidBody::btRigidBodyConstructionInfo info(0, nullptr, &shape);
    btRigidBody body(info);
    body.setFriction(.25f);
    body.setRestitution(.88f);
    body.setDeactivationTime(2.5f);
    body.forceActivationState(DISABLE_DEACTIVATION);
    for (const auto& item : cases) {
        Store native(original, item.rva, item.bytes);
        for (int flags : {0, 1, 128, -1}) {
            body.setCollisionFlags(flags);
            native(body);
            Check(body.getCollisionFlags() == (item.enabled ? 1 : -1), "native floor flag");
            Check(body.getDeactivationTime() == 2.5f, "native floor timer preserved");
            std::array<unsigned char, sizeof body> expected;
            std::memcpy(expected.data(), &body, expected.size());
            body.setCollisionFlags(flags);
            mikudancestudio::SetPhysicsGroundEnabled(&body, item.enabled);
            Check(std::memcmp(expected.data(), &body, expected.size()) == 0,
                  "complete ground-body bytes differ");
        }
    }
    std::puts("24 exact native floor body checkpoints: menu, PMM v1 and v2; no UI/device");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
