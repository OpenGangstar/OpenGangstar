#include "retail_actor_target.h"

#include <stdexcept>

bool retailActorIsFacingTarget(RetailActorHeadingHost& actor, RetailActorTargetLookup& targets)
{
    const auto target = targets.currentTarget();
    if (!target) throw std::runtime_error("Facing requires the current nonnull actor target");
    const auto targetPosition = target->getPosition();
    const auto actorPosition = actor.getPosition();
    // Original soft-float calls subtract Y before X. Both Z values are ignored.
    const float y = targetPosition[1] - actorPosition[1];
    const float x = targetPosition[0] - actorPosition[0];
    const auto direction = retailSceneNormalizeVector({x, y, 0.f});
    auto front = actor.getFront();
    front[2] = 0.f;
    front = retailSceneNormalizeVector(front);
    const float dot = (front[0] * direction[0] + front[1] * direction[1]) + front[2] * direction[2];
    return dot > .9f;
}

void updateRetailPlayerSceneNode(RetailActorHeadingHost& heading, RetailActorTargetLookup& targets,
                                RetailActorSceneNode& actor, RetailActorSceneNode& shadow,
                                const b2Body* body, const RetailActorFloorState& floor,
                                const RetailHeightState& height)
{
    const bool hasTarget = bool(targets.currentTarget());
    if (hasTarget) (void)retailActorIsFacingTarget(heading, targets);
    const auto rotation = heading.getRotation();
    updateRetailActorPosition(actor, shadow, body, floor, height, rotation);
}

void updateRetailNpcSceneNode(RetailActorHeadingHost& heading, RetailActorSceneNode& actor,
                             RetailActorSceneNode& shadow, const b2Body* body,
                             const RetailActorFloorState& floor, const RetailHeightState& height)
{
    const auto rotation = heading.getRotation();
    updateRetailActorPosition(actor, shadow, body, floor, height, rotation);
}
