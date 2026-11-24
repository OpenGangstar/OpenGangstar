#include "retail_actor_shadow.h"

void updateRetailActorShadow(RetailActorSceneNode& shadow, const RetailHeightPoint& actorWorldCm,
                             float floorCm, const RetailHeightState& liveHeight)
{
    shadow.setPosition({actorWorldCm[0], actorWorldCm[1], floorCm + 3.f});
    if (!liveHeight.changed) return;
    const auto& normal = liveHeight.normal;
    // The caller's raw dot test precedes rotationFromTo's own normalization.
    // Preserve the multiplies/additions by zero, including their sign behavior.
    const float cosine = (normal[0] * 0.f + normal[1] * 0.f) + normal[2];
    shadow.setRotation(cosine <= -1.f
        ? retailSceneAngleAxisQuaternion(3.14159265358979323846f, {0, 0, 1})
        : retailSceneRotationFromTo(normal, {0, 0, 1}));
}
