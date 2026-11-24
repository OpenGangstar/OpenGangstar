#pragma once

#include "retail_actor_floor.h"
#include "retail_actor_shadow.h"

// Original Character::updatePosition, after actual world stepping. Quaternion
// is copied before any scene work. Nodes/body/floor/height are the genuine live
// actor resources; shadow has the separately proved global-root lifetime.
// Optional body matches the original branch. This is readback, not Character
// teleport: no body transform/refilter, height requery or recursive frame update.
void updateRetailActorPosition(RetailActorSceneNode& actor, RetailActorSceneNode& shadow,
                              const b2Body* body, const RetailActorFloorState&,
                              const RetailHeightState&, RetailSceneQuaternion rotation);

// Original outer post-world stage, even if the shared world took zero steps.
// Clears only genuine player linearXY, without wake/transform/angular changes.
// True requires the actual Player UpdateSceneNode target/facing stage, then
// copied rotation/updateRetailActorPosition. Caller runs vehicles/scripts next;
// this function does not substitute a fake absent target or another Step loop.
bool finishRetailPlayerPhysics(b2Body& player, std::uint32_t liveMovementFlags,
                              const RetailAnimationClock& liveAnimation);
