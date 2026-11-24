#pragma once

#include "retail_height.h"

#include <memory>
#include <optional>

// On-foot body backend only. The world, live owner and shared terrain must outlive
// this object. An optional existing height controller is exclusively borrowed by
// this actor, must belong to the same terrain/world, and must outlive its body.
// This caller invariant is not inferred from raw addresses. Otherwise a one-slot
// height controller is
// constructed before the actor body with the original supplied-body NULL value.
// Fresh owned attributes zero the live current-jump counter; borrowing existing
// attributes does not reset it or construct an unintegrated jump snapshot. The
// later actor body is never substituted into that constructor-only pointer.
// Body creation/destruction/placement/refiltering occur outside world.Step; native
// callbacks must not throw. The component does not install fake terrain/jump or
// scene callbacks, integrate movement, or own the live owner/scene hierarchy.
class RetailActorPhysics {
  public:
    RetailActorPhysics(RetailHeightTerrain&, b2World&, RetailPhysicsOwnerState&,
                       RetailHeightController* existingHeight = nullptr);
    ~RetailActorPhysics();
    RetailActorPhysics(const RetailActorPhysics&) = delete;
    RetailActorPhysics& operator=(const RetailActorPhysics&) = delete;
    // Original initPhysic body creation, idempotent when this actor already has
    // its body. positionCm is the original GetPosition result. Node bounds are
    // the supplied skin node's raw bbox Z, not translated or guessed from root Z.
    // Player is an explicit owner identity; nullptr gives the other-character role.
    void initialize(const RetailHeightPoint& positionCm, std::optional<RetailPhysicsZBounds> skinNodeBounds,
                    const RetailPhysicsOwnerState* player);
    // Leaf body portion of Character::SetPosition, after the caller commits the
    // original scene setPosition/updateAbsolutePosition(false) and synchronizes
    // live owner Z/root Z. It uses the requested XY, preserves body angle and
    // refilters every shape. It does not zero velocity, reset height or cancel
    // destinations. Original out-of-world SetXForm freezing remains observable.
    void setBodyPositionAfterSceneUpdate(const RetailHeightPoint& requestedCm);
    // Host synchronization for the existing native value-based metadata adapter.
    // Update when the real node bbox changes, before the original refilter site.
    // This operation alone does not invent a new refilter timing.
    void synchronizeSkinNodeBounds(std::optional<RetailPhysicsZBounds>);
    void removeCollisions();
    void resetCollisions();
    b2Body* body() const { return actorBody; }
    const RetailPhysicsBodyData* metadata() const { return bodyData.get(); }
    RetailHeightController& heightController() const { return *height; }
    // Original saved category/mask/group; meaningful after successful initialize.
    const b2FilterData& savedFilter() const { return originalFilter; }

  private:
    b2World& world;
    RetailPhysicsOwnerState& owner;
    std::unique_ptr<RetailHeightController> ownedHeight;
    RetailHeightController* height = nullptr;
    std::unique_ptr<RetailPhysicsBodyData> bodyData;
    b2Body* actorBody = nullptr;
    b2FilterData originalFilter{};
};
