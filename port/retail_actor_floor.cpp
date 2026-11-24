#include "retail_actor_floor.h"

namespace {
RetailHeightPoint cachedPosition(const RetailActorSceneNode& actor)
{
    const auto& matrix = actor.absolute();
    return {matrix[12], matrix[13], matrix[14]};
}
}

void updateRetailActorFloor(RetailActorFloorState& floor, RetailActorSceneNode& actor,
                           RetailHeightController& height, const RetailAnimationClock& animation)
{
    const auto saved = cachedPosition(actor);
    const auto query = cachedPosition(actor);
    floor.heightCm = height.height(query, 0, false);
    // Read the live root height AFTER the height query and retained-floor write.
    actor.setPosition({saved[0], saved[1], floor.heightCm + animation.rootHeight});
    actor.updateAbsolutePosition(false);
}
