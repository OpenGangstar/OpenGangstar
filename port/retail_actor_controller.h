#pragma once

#include "retail_actor_physics.h"
#include "retail_animation.h"
#include "retail_steering.h"

#include <cstdint>

// Borrowed original Character/Player state. None of these words is derived from
// a clip's duration, the renderer's timeline, or an estimated walking speed.
struct RetailDirectPlayerState {
    std::uint32_t movementFlags = 0;
    RetailHeightPoint destinationCm{};
    std::int32_t destinationLimitSquared = 0;
    RetailHeightPoint adjustedDestinationCm{};
    bool destinationReachable = false;
    RetailHeightPoint turnDirection{};
    float turnSpeed = 0;
    bool turning = false;
    RetailAnimationClock animation;
};

// Required live game bindings. This bounded controller does not implement the
// scene quaternion, animation catalog/weapon, target manager or whole Update.
// selectAnimation must operate on this controller's live movementFlags and the
// actor's retained animation clock; it must preserve original reload ordering.
// A real vehicle is outside this on-foot contract and must not be hidden.
class RetailDirectPlayerHost {
  public:
    virtual ~RetailDirectPlayerHost() = default;
    virtual bool isDead() = 0;
    virtual bool isDown() = 0;
    virtual bool hasVehicle() = 0;
    virtual std::int32_t animationContext() = 0; // Original Character word +212.
    virtual std::int32_t weaponType() = 0;
    virtual bool weaponTwoHanded() = 0;
    virtual RetailHeightPoint positionCm() = 0;
    virtual void turnToward(const RetailHeightPoint&) = 0;
    virtual void selectAnimation(const RetailAnimationRequest&) = 0;
    virtual void checkWeaponReload() = 0;
    virtual void clearTarget() = 0; // Player::_SetTarget(nullptr, false).
    virtual void setAnimatedRootTranslation(const RetailHeightPoint&) = 0;
};

class RetailDirectPlayerController {
  public:
    // All references outlive this object. The supplied identity must be the
    // genuine player used by its RetailContactFilter and body initialization.
    RetailDirectPlayerController(b2World&, RetailActorPhysics&, RetailPhysicsOwnerState&,
                                 RetailDirectPlayerState&, RetailDirectPlayerHost&,
                                 const RetailPhysicsOwnerState* genuinePlayer);
    void walkTo(const RetailHeightPoint&);
    void runTo(const RetailHeightPoint&);
    void stop();
    void startTurn(float speed);
    // Complete original request mapping, including the commands188/202/212.
    // Unrecognized requests return-1 before context/weapon reads.
    std::int32_t specificAnimation(std::int32_t request);
    // Original CheckDestination; cinematic bypass is a separate Player::Update
    // branch. The caller selects the original outer Update branch, not this leaf.
    void checkDestination(bool inCinematic);
    // Original 0x305104..30519c scripted-destination slice, including the reload
    // call before testing live flags. Returns whether this call completed a move.
    bool updateScriptedDestination();
    // Consume one result from the verified animation binding using this exact
    // state's clock. Must run before shared RetailWorldUpdate::advance. Does not
    // swallow the caller's subsequent Player::switchAnim/height responsibilities.
    void applyAnimationStep(const RetailAnimationStep&);

  private:
    b2World& world;
    RetailActorPhysics& physics;
    RetailPhysicsOwnerState& owner;
    RetailDirectPlayerState& state;
    RetailDirectPlayerHost& host;
    void requireOnFoot();
    void selectSpecific(std::int32_t request, bool loop, bool rootMotion);
};
