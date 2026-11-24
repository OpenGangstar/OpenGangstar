#include "retail_contacts.h"

#include <cmath>
#include <initializer_list>

namespace {
RetailPhysicsOwnerState* owner(b2Shape& shape)
{
    auto* data = static_cast<RetailPhysicsBodyData*>(shape.GetBody()->GetUserData());
    return data ? data->owner : nullptr;
}

bool eligibleNpc(const RetailContactNpcState& state)
{
    return (state.state428 == 0 || state.state428 == 1 || state.state428 == 2) && !state.hasPathToFollow;
}

void reissueRun(RetailContactHost& host, RetailPhysicsOwnerState& actor)
{
    if (host.npc(actor).flags356 & 4u) host.runToLocationBase(actor, host.destination(actor));
}

void retargetNpc(RetailContactHost& host, RetailPhysicsOwnerState& actor, const b2Vec2& normal)
{
    auto& state = host.npc(actor);
    if (!eligibleNpc(state)) return;
    const auto forward = host.front(actor);
    const float xy = normal.y * forward.x + (-normal.x) * forward.y;
    const float dot = xy + 0.f * forward.z;
    const bool reverse = dot < 0.f;
    const float x = reverse ? -normal.y : normal.y;
    const float y = reverse ? normal.x : -normal.x;
    const float z = reverse ? -0.f : 0.f;
    const auto current = host.position(actor);
    host.setDestination(actor, {500.f * x + current.x, 500.f * y + current.y, z + current.z}, 100);
    reissueRun(host, actor);
}

bool nearlyZero(RetailContactVector value)
{
    // Original vector3d<float>::equals(zero, bit-pattern0x358637bd), inclusive.
    constexpr float epsilon = 0.000001f;
    return 0.f <= value.x + epsilon && 0.f >= value.x - epsilon &&
           0.f <= value.y + epsilon && 0.f >= value.y - epsilon &&
           0.f <= value.z + epsilon && 0.f >= value.z - epsilon;
}

bool processRocket(RetailContactHost& host, RetailPhysicsOwnerState& rocketOwner,
                   RetailPhysicsOwnerState* other, bool otherSensor)
{
    auto& rocket = host.rocket(rocketOwner);
    if (other && (rocket.launcher == other || otherSensor)) return true;
    if (rocket.exploded92) return false;
    if (other && other->levelCategory == 3 && !host.dead(*other)) {
        host.vehicle(*other).rocketContactWord924 = 5000;
        host.explode(rocketOwner, false);
    } else host.explode(rocketOwner, true);
    return true;
}

void sensorVehicle(RetailContactHost& host, RetailPhysicsOwnerState& actor, RetailPhysicsOwnerState* other)
{
    auto& state = host.vehicle(actor);
    ++state.sensorContactCount;
    if (state.turnSoundPlaying || !(state.flags344 & 0x800u) || host.dead(actor)) return;
    if (other == host.playerOwner() || other == host.playerVehicle()) host.playHorn(actor);
}

float dot(RetailContactVector direction, float x, float y)
{
    const float xy = x * direction.x + y * direction.y;
    return xy + 0.f * direction.z;
}

void characterVehicle(RetailContactHost& host, RetailPhysicsOwnerState& actor,
                      RetailPhysicsOwnerState& vehicle, bool playerDriver, bool playerGuard,
                      const b2ContactPoint& point)
{
    if (!(host.speed(vehicle) > 18.f) || host.character(actor).lastHitVehicle ||
        host.dying(actor) || host.dead(actor) || playerGuard) return;
    const float squared = point.velocity.x * point.velocity.x + point.velocity.y * point.velocity.y;
    const float amount = (std::sqrt(squared) / 1000.f) * 3600.f;
    if (!(amount > 18.f)) return;
    const float negativeFront = dot(host.front(actor), -point.velocity.x, -point.velocity.y);
    float strongest = negativeFront > 0.f ? negativeFront : 0.f;
    int direction = negativeFront > 0.f ? 1 : 0;
    const float negativeRight = dot(host.right(actor), -point.velocity.x, -point.velocity.y);
    if (strongest < negativeRight) { strongest = negativeRight; direction = 3; }
    const float positiveFront = dot(host.front(actor), point.velocity.x, point.velocity.y);
    if (strongest < positiveFront) { strongest = positiveFront; direction = 0; }
    const float positiveRight = dot(host.right(actor), point.velocity.x, point.velocity.y);
    if (strongest < positiveRight) direction = 2;
    if (&actor == host.playerOwner()) host.lockPlayerControls();
    else host.setTarget(actor, vehicle);
    host.character(actor).lastHitVehicle = &vehicle;
    host.hitByCar(actor, direction, amount, vehicle);
    if (playerDriver && &actor != host.playerOwner() && host.health(actor) <= 0)
        host.playerOnObjectKilled(actor);
}

RetailContactVector projection(const b2ContactPoint& point, bool first)
{
    const float x = first ? -point.velocity.x : point.velocity.x;
    const float y = first ? -point.velocity.y : point.velocity.y;
    const float scalar = x * point.normal.x + y * point.normal.y;
    return {point.normal.x * scalar, point.normal.y * scalar, 0.f};
}
}

RetailContactListener::RetailContactListener(RetailContactHost& value) : host(value) {}

void RetailContactListener::Add(const b2ContactPoint* point)
{
    auto* first = owner(*point->shape1);
    auto* second = owner(*point->shape2);
    if (first && first->levelCategory == 12 && processRocket(host, *first, second, point->shape2->IsSensor())) return;
    if (second && second->levelCategory == 12 && processRocket(host, *second, first, point->shape1->IsSensor())) return;
    if (point->shape1->IsSensor() && first && first->levelCategory == 3) {
        if (!point->shape2->IsSensor()) sensorVehicle(host, *first, second);
        return;
    }
    if (point->shape2->IsSensor() && second && second->levelCategory == 3) {
        if (!point->shape1->IsSensor()) sensorVehicle(host, *second, first);
        return;
    }
    if (first && second) {
        RetailPhysicsOwnerState* character = nullptr;
        RetailPhysicsOwnerState* vehicle = nullptr;
        bool playerDriver = false, playerGuard = false;
        for (auto* actor : {first, second}) {
            if (actor->levelCategory == 3) {
                vehicle = actor;
                if (host.vehicle(*actor).driver == host.playerOwner()) playerDriver = true;
            } else if (actor->levelCategory == 5) character = actor;
            else if (actor->levelCategory == 8) {
                character = actor;
                playerGuard = host.character(*actor).playerWord372 != 0;
            }
        }
        if (character && vehicle) characterVehicle(host, *character, *vehicle, playerDriver, playerGuard, *point);
    }
    bool firstVehicleDispatched = false;
    if (first && first->levelCategory == 3) {
        host.collision(*first, projection(*point, true), second ? second->levelCategory : -1);
        // Original r1=1 at0x3204f2 records dispatch history. A callback can
        // change the owner's live category before the second vehicle branch.
        firstVehicleDispatched = true;
    }
    if (second && second->levelCategory == 3) {
        if (firstVehicleDispatched) {
            if ((host.metatype(*first) == 38 && host.playerVehicle() == second) ||
                (host.metatype(*second) == 38 && host.playerVehicle() == first)) host.raisePlayerEvent4();
        }
        host.collision(*second, projection(*point, false), first ? first->levelCategory : -1);
    }
    if (first && !second && first->levelCategory == 5) retargetNpc(host, *first, point->normal);
    else if (!first && second && second->levelCategory == 5) retargetNpc(host, *second, point->normal);
}

void RetailContactListener::Persist(const b2ContactPoint* point)
{
    auto* first = owner(*point->shape1);
    auto* second = owner(*point->shape2);
    if (bool(first) == bool(second)) return;
    auto& actor = *(first ? first : second);
    if (actor.levelCategory != 5) return;
    auto& state = host.npc(actor);
    if (!eligibleNpc(state)) return;
    auto& accumulated = state.accumulatedNormal;
    if (nearlyZero(accumulated)) {
        accumulated = {point->normal.x, point->normal.y, 0.f};
        return;
    }
    accumulated.x = point->normal.x + accumulated.x;
    accumulated.y = point->normal.y + accumulated.y;
    accumulated.z = accumulated.z + 0.f;
    const auto current = host.position(actor);
    host.setDestination(actor, {500.f * accumulated.x + current.x,
        500.f * accumulated.y + current.y, accumulated.z * 500.f + current.z}, 100);
    reissueRun(host, actor);
}

void RetailContactListener::Remove(const b2ContactPoint* point)
{
    auto* first = owner(*point->shape1);
    auto* second = owner(*point->shape2);
    if ((first && first->levelCategory == 12) || (second && second->levelCategory == 12)) return;
    if (point->shape1->IsSensor()) {
        if (!point->shape2->IsSensor() && first) --host.vehicle(*first).sensorContactCount;
    } else if (point->shape2->IsSensor() && second) --host.vehicle(*second).sensorContactCount;
}

void RetailContactListener::Result(const b2ContactResult*)
{
    // Original derived ContactListener::Result is genuinely empty (matched).
}
