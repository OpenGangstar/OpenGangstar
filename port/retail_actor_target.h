#pragma once

#include "retail_actor_heading.h"
#include "retail_actor_position.h"

#include <memory>

// Required virtual LevelObject position endpoint, returning a value snapshot.
// A lease must keep the actual selected entity and its current position binding
// valid, not merely retain an old scene after that entity has been pooled.
class RetailActorTargetPosition {
public:
    virtual ~RetailActorTargetPosition() = default;
    virtual RetailActorHeadingPoint getPosition() = 0;
};

class RetailActorTargetLookup {
public:
    virtual ~RetailActorTargetLookup() = default;
    // Resolve the common Character target anew on EVERY call. nullptr means
    // genuine known NULL. An unresolved/expired nonnull entity must throw;
    // it must never silently become absent. The producer/entity manager owns
    // this contract; this leaf supplies no registry or scene-snapshot adapter.
    virtual std::shared_ptr<RetailActorTargetPosition> currentTarget() = 0;
};

// Original PlayerTest::isFacingTarget0x3025cd, a predicate with no turn/write.
// Requires a nonnull target; checked native NULL rejection replaces the original
// unconditional dereference. Target position precedes cached actor position.
bool retailActorIsFacingTarget(RetailActorHeadingHost&, RetailActorTargetLookup&);

// Original PlayerTest::UpdateSceneNode0x302685. Caller binds heading and actor
// to the SAME fixed live entity for this serial operation; all other resources
// have updateRetailActorPosition's genuine ownership/lifetime requirements.
// Call only when finishRetailPlayerPhysics permits the scene-update stage.
// Two fresh lookups when the outer check is nonnull; its temporary lease ends
// before the predicate lookup. The predicate result is intentionally ignored.
void updateRetailPlayerSceneNode(RetailActorHeadingHost&, RetailActorTargetLookup&,
                                RetailActorSceneNode& actor, RetailActorSceneNode& shadow,
                                const b2Body*, const RetailActorFloorState&,
                                const RetailHeightState&);

// Original NPC::UpdateSceneNode0x2fd801. No Player target/facing stage.
void updateRetailNpcSceneNode(RetailActorHeadingHost&, RetailActorSceneNode& actor,
                             RetailActorSceneNode& shadow, const b2Body*,
                             const RetailActorFloorState&, const RetailHeightState&);
