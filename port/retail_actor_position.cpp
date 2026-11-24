#include "retail_actor_position.h"

void updateRetailActorPosition(RetailActorSceneNode& actor, RetailActorSceneNode& shadow,
                              const b2Body* body, const RetailActorFloorState& floor,
                              const RetailHeightState& height, RetailSceneQuaternion rotation)
{
    if (body) {
        const float x = body->GetPosition().x * 100.f;
        const float y = body->GetPosition().y * 100.f;
        const auto previous = retailActorCachedPosition(actor);
        // Qualified LevelObject placement, deliberately without the separate
        // Character teleport body's SetXForm/Refilter operation.
        actor.setPosition({x, y, previous[2]});
        actor.updateAbsolutePosition(false);
    }
    actor.setRotation(rotation);
    actor.setPosition(retailActorCachedPosition(actor));
    updateRetailActorShadow(shadow, retailActorCachedPosition(actor), floor.heightCm, height);
}

bool finishRetailPlayerPhysics(b2Body& player, std::uint32_t flags, const RetailAnimationClock& animation)
{
    // Native Box2D2.0.1 SetLinearVelocity is exactly the member assignment;
    // it performs no wake/transform/contact operation, like the original stores.
    player.SetLinearVelocity(b2Vec2(0, 0));
    return !(flags & 0x400) || !(animation.animation == -1 || animation.animation == 162);
}
