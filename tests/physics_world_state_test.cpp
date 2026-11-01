#include "mikudancestudio/physics_world_state.hpp"

#include "btBulletDynamicsCommon.h"

#include <cstdio>
#include <cstring>
#include <array>

int main() {
    mikudancestudio::SetPhysicsGroundEnabled(nullptr, false);
    mikudancestudio::PhysicsWorldState state;
    mikudancestudio::DestroyPhysicsWorld(state); // interrupted startup
    mikudancestudio::CreatePhysicsWorld(state);

    if (state.world == nullptr || state.groundBody == nullptr ||
        state.world->getNumCollisionObjects() != 1 ||
        state.world->getGravity() != btVector3(0.0f, -98.0f, 0.0f) ||
        state.groundBody->getRestitution() != 0.88f) {
        std::fputs("headless physics world did not match its initialization contract\n", stderr);
        return 1;
    }

    // Replacing flags must not also reset the broadphase, material, activation
    // timer or velocity. The native command writes exactly one integer field.
    state.groundBody->setDeactivationTime(2.5f);
    state.groundBody->setFriction(.25f);
    std::array<unsigned char, sizeof(btRigidBody)> enabled;
    std::memcpy(enabled.data(), state.groundBody, enabled.size());
    for (int i = 0; i < 4; ++i) {
        mikudancestudio::SetPhysicsGroundEnabled(state.groundBody, false);
        if (state.groundBody->getCollisionFlags() != -1 ||
            state.groundBody->getDeactivationTime() != 2.5f ||
            state.groundBody->getFriction() != .25f)
            return 2;
        mikudancestudio::SetPhysicsGroundEnabled(state.groundBody, true);
        if (std::memcmp(enabled.data(), state.groundBody, enabled.size()) != 0)
            return 3;
    }
    auto* shape = new btSphereShape(.5f);
    btVector3 inertia;
    shape->calculateLocalInertia(1, inertia);
    auto start = btTransform::getIdentity();
    start.setOrigin(btVector3(0, 5, 0));
    auto* motion = new btDefaultMotionState(start);
    btRigidBody::btRigidBodyConstructionInfo info(1, motion, shape, inertia);
    auto* body = new btRigidBody(info);
    body->forceActivationState(DISABLE_DEACTIVATION);
    state.world->addRigidBody(body);
    // Same world/body, on -> off -> on. Actual collisions must disappear and
    // return; testing only the public checkbox would miss the original bug.
    for (bool floor : {true, false, true}) {
        mikudancestudio::SetPhysicsGroundEnabled(state.groundBody, floor);
        body->setWorldTransform(start);
        body->setInterpolationWorldTransform(start);
        motion->setWorldTransform(start);
        body->setLinearVelocity(btVector3(0, 0, 0));
        body->setAngularVelocity(btVector3(0, 0, 0));
        body->clearForces();
        state.world->updateSingleAabb(body);
        for (int i = 0; i < 60; ++i)
            state.world->stepSimulation(1.f / 60, 10, 1.f / 60);
        const float y = body->getWorldTransform().getOrigin().y();
        if ((floor && (y < .49f || y > .51f)) || (!floor && y >= -40)) {
            std::fprintf(stderr, "floor=%d body y=%f\n", floor, y);
            return 4;
        }
    }
    mikudancestudio::DestroyPhysicsWorld(state);
    mikudancestudio::DestroyPhysicsWorld(state); // repeat shutdown
    if (state.world || state.groundBody || state.solver || state.broadphase || state.dispatcher ||
        state.collisionConfig) {
        std::fputs("headless physics world retained owned pointers\n", stderr);
        return 1;
    }
    std::puts("headless physics world creation and cleanup passed");
}
