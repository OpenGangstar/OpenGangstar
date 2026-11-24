#pragma once

#include "retail_physics.h"

struct RetailContactVector { float x, y, z; };
struct RetailContactNpcState {
    std::int32_t state428;
    bool hasPathToFollow;
    std::uint32_t flags356;
    RetailContactVector accumulatedNormal;
};
struct RetailContactVehicleState {
    std::int32_t sensorContactCount;
    bool turnSoundPlaying;
    std::uint32_t flags344;
    RetailPhysicsOwnerState* driver;
    std::int32_t rocketContactWord924;
};
struct RetailContactRocketState {
    RetailPhysicsOwnerState* launcher;
    bool exploded92;
};
struct RetailContactCharacterState {
    RetailPhysicsOwnerState* lastHitVehicle;
    // Observed PlayerTest primary-base word372. Its broader meaning is pending.
    std::uint32_t playerWord372;
};

// Mandatory native game bindings, not optional callbacks. Role records are live
// game state and must reflect changes made by the game operations below. A role
// accessor without a genuine binding must fail explicitly. No ARM layouts apply.
class RetailContactHost {
public:
    virtual ~RetailContactHost() = default;
    virtual RetailContactNpcState& npc(RetailPhysicsOwnerState&) = 0;
    virtual RetailContactVehicleState& vehicle(RetailPhysicsOwnerState&) = 0;
    virtual RetailContactRocketState& rocket(RetailPhysicsOwnerState&) = 0;
    virtual RetailContactCharacterState& character(RetailPhysicsOwnerState&) = 0;
    virtual RetailPhysicsOwnerState* playerOwner() = 0;
    virtual RetailPhysicsOwnerState* playerVehicle() = 0;
    virtual RetailContactVector position(RetailPhysicsOwnerState&) = 0;
    virtual RetailContactVector front(RetailPhysicsOwnerState&) = 0;
    virtual RetailContactVector right(RetailPhysicsOwnerState&) = 0;
    virtual float speed(RetailPhysicsOwnerState&) = 0;
    virtual bool dead(RetailPhysicsOwnerState&) = 0;
    virtual bool dying(RetailPhysicsOwnerState&) = 0;
    virtual std::int32_t health(RetailPhysicsOwnerState&) = 0;
    virtual std::int32_t metatype(RetailPhysicsOwnerState&) = 0;
    virtual RetailContactVector destination(RetailPhysicsOwnerState&) = 0;
    virtual void setDestination(RetailPhysicsOwnerState&, RetailContactVector, int limitCm) = 0;
    virtual void runToLocationBase(RetailPhysicsOwnerState&, RetailContactVector) = 0;
    virtual void setTarget(RetailPhysicsOwnerState&, RetailPhysicsOwnerState& vehicle) = 0;
    virtual void hitByCar(RetailPhysicsOwnerState&, int direction, float amount, RetailPhysicsOwnerState& vehicle) = 0;
    virtual void lockPlayerControls() = 0;
    virtual void playerOnObjectKilled(RetailPhysicsOwnerState&) = 0;
    virtual void playHorn(RetailPhysicsOwnerState&) = 0;
    virtual void explode(RetailPhysicsOwnerState&, bool drawEffect) = 0;
    virtual void collision(RetailPhysicsOwnerState&, RetailContactVector, int otherCategory) = 0;
    virtual void raisePlayerEvent4() = 0; // Original EvPlayer event type22, code4.
};

// Bodies carry RetailPhysicsBodyData; referenced owners/role records and host
// must outlive the world callbacks. Missing required game bindings are fatal:
// a b2World cannot safely resume after a host exception escapes Step.
class RetailContactListener : public b2ContactListener {
public:
    explicit RetailContactListener(RetailContactHost& host);
    void Add(const b2ContactPoint*) override;
    void Persist(const b2ContactPoint*) override;
    void Remove(const b2ContactPoint*) override;
    void Result(const b2ContactResult*) override;
private:
    RetailContactHost& host;
};
