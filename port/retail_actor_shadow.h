#pragma once

#include "retail_actor_scene.h"

// Shadow-local portion of original Character::updatePosition. actorWorldCm is
// the genuine current LevelObject::GetPosition result; floorCm is Character+160,
// distinct from animation rootHeight+156. Read the live height slot's changed
// byte and normal, not hasHeightmap or an inferred comparison of normals.
// The caller supplies the actual authored shadow container retained/reparented
// by its global scene owner. This operation does not own that graph, the actor
// body, visibility, or a frame loop. Absolute caches stay stale until the
// original later scene update; no recursive refresh occurs here.
void updateRetailActorShadow(RetailActorSceneNode& shadow, const RetailHeightPoint& actorWorldCm,
                             float floorCm, const RetailHeightState& liveHeight);
