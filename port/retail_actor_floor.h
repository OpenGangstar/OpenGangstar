#pragma once

#include "retail_actor_scene.h"
#include "retail_animation.h"

// Original Character+160. This is distinct from animation rootHeight+156.
struct RetailActorFloorState {
    float heightCm = 0;
};

// The actual outer Player/NPC update calls this after its animation/root-height
// update and before shared world advancement. The actor, its SAME live height
// controller and animation clock outlive the call. Read cached absolute position
// twice in source order; do not refresh it first or invert parent transforms.
// Commits local position then updateAbsolutePosition(false), leaving descendants
// for the later global scene frame. Owns no clock/body/teleport/scene-host loop.
void updateRetailActorFloor(RetailActorFloorState&, RetailActorSceneNode& actor,
                           RetailHeightController&, const RetailAnimationClock&);
