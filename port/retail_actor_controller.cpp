#include "retail_actor_controller.h"

#include <stdexcept>

RetailDirectPlayerController::RetailDirectPlayerController(
    b2World& world, RetailActorPhysics& physics, RetailPhysicsOwnerState& owner,
    RetailDirectPlayerState& state, RetailDirectPlayerHost& host,
    const RetailPhysicsOwnerState* player)
    : world(world), physics(physics), owner(owner), state(state), host(host) {
    if (player != &owner || !physics.body() || !physics.metadata() ||
        physics.metadata()->owner != &owner || physics.body()->GetWorld() != &world ||
        physics.savedFilter().categoryBits != 2)
        throw std::invalid_argument("Direct player requires its genuine initialized player body/world.");
}

void RetailDirectPlayerController::requireOnFoot() {
    if (host.hasVehicle())
        throw std::logic_error("Direct player controller has no vehicle-transfer/Stop binding.");
}

std::int32_t RetailDirectPlayerController::specificAnimation(std::int32_t request) {
    struct Mapping { std::int32_t request, context1, context2, twoHanded, rocket; };
    static constexpr Mapping mappings[]{
        {0, 0, 0, 205, 208}, {165, 36, 42, 166, 169}, {179, 179, 179, 167, 170},
        {180, 38, 43, 168, 171}, {188, 39, 44, 189, 192}, {198, 198, 198, 190, 193},
        {200, 200, 200, 191, 194}, {202, 40, 48, 203, 206}, {212, 41, 49, 204, 207}};
    const Mapping* mapping = nullptr;
    for (const auto& candidate : mappings)
        if (candidate.request == request) { mapping = &candidate; break; }
    if (!mapping) return -1; // Original rejects before reading context or weapon.
    const auto context = host.animationContext();
    if (context == 1)
        return mapping->context1;
    if (context == 2)
        return mapping->context2;
    if (context != 0)
        return -1;
    if (host.weaponType() == 7)
        return mapping->rocket;
    if (host.weaponTwoHanded())
        return mapping->twoHanded;
    return request;
}

void RetailDirectPlayerController::selectSpecific(std::int32_t request, bool loop, bool rootMotion) {
    const auto animation = specificAnimation(request);
    host.selectAnimation({animation, 1.f, loop, rootMotion, false});
}

void RetailDirectPlayerController::walkTo(const RetailHeightPoint& destination) {
    if (host.isDead())
        return;
    requireOnFoot();
    state.movementFlags |= 0x8000;
    state.destinationCm = destination;
    state.destinationLimitSquared = 100 * 100;
    // Player wrapper and Character base have independent virtual death guards.
    if (host.isDead())
        return;
    state.movementFlags = (state.movementFlags & 0xffffff00u) | 2;
    host.turnToward(destination);
    selectSpecific(202, true, true);
}

void RetailDirectPlayerController::runTo(const RetailHeightPoint& destination) {
    if (host.isDead())
        return;
    requireOnFoot();
    state.movementFlags |= 0x8000;
    state.destinationCm = destination;
    state.destinationLimitSquared = 100 * 100;
    if (host.isDead() || host.isDown())
        return;
    state.movementFlags = (state.movementFlags & 0xffffff00u) | 4;
    host.turnToward(destination);
    selectSpecific(188, true, true);
}

void RetailDirectPlayerController::stop() {
    requireOnFoot();
    state.movementFlags = (state.movementFlags & 0xffffff00u) | 1;
    selectSpecific(212, false, false);
    physics.body()->SetLinearVelocity(b2Vec2(0, 0));
    physics.body()->SetAngularVelocity(0);
    host.clearTarget();
}

void RetailDirectPlayerController::startTurn(float speed) {
    const auto direction = state.turnDirection;
    if (direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2] > 0.f) {
        state.turnSpeed = speed;
        state.turning = true;
        host.selectAnimation({201, 1.f, true, false, false});
    }
}

void RetailDirectPlayerController::checkDestination(bool inCinematic) {
    requireOnFoot();
    if (inCinematic && !(state.movementFlags & 0x1000)) {
        state.adjustedDestinationCm = state.destinationCm;
        return; // Original bypass does not write destinationReachable.
    }
    const auto position = host.positionCm();
    RetailSteeringActor actor{owner, b2Vec2(position[0], position[1]),
                             physics.heightController().groupIndex() - 1};
    RetailSteeringPoint hit{}, adjusted{};
    const auto& d = state.destinationCm;
    state.destinationReachable = retailCanGoToPoint(world, actor, {d[0], d[1], d[2]}, hit, adjusted);
    if (state.destinationReachable)
        state.adjustedDestinationCm = state.destinationCm;
    else
        state.adjustedDestinationCm = {adjusted.x, adjusted.y, adjusted.z};
}

bool RetailDirectPlayerController::updateScriptedDestination() {
    requireOnFoot();
    host.checkWeaponReload();
    if (!(state.movementFlags & 0x8000))
        return false;
    // Copy before the scene callback, matching the original stack argument.
    const auto adjusted = state.adjustedDestinationCm;
    host.turnToward(adjusted);
    state.animation.rootMotion = true; // Player +153 == Character +149.
    const auto position = host.positionCm();
    const float x = state.destinationCm[0] - position[0];
    const float y = state.destinationCm[1] - position[1];
    // Retain the original destinationZ-destinationZ evaluation (including NaN).
    const float z = state.destinationCm[2] - state.destinationCm[2];
    const float distanceSquared = x * x + y * y + z * z;
    if (!(distanceSquared < static_cast<float>(state.destinationLimitSquared)))
        return false;
    state.movementFlags &= 0xffff7fffu;
    stop();
    startTurn(8.f);
    return true;
}

void RetailDirectPlayerController::applyAnimationStep(const RetailAnimationStep& step) {
    requireOnFoot();
    if (!step.evaluated)
        return;
    if (step.bodyVelocity) {
        if (step.wakeBody)
            physics.body()->WakeUp();
        physics.body()->SetLinearVelocity(b2Vec2((*step.bodyVelocity)[0], (*step.bodyVelocity)[1]));
    }
    // Original Character +156 supplies the custom collision filter's root Z.
    owner.rootZCm = state.animation.rootHeight;
    host.setAnimatedRootTranslation(step.visualRootTranslation);
}
