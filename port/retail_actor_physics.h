#pragma once

#include "retail_height.h"

#include <memory>
#include <optional>

class RetailActorScene;
class RetailActorSceneNode;
struct RetailAnimationClock;

// Concrete original actor reads: explicit Character.sceneNode and getSkinMesh
// identities in the retained instance, never guessed from source indices/UIDs.
// The scene is retained; canonical owner and clock are borrowed and must outlive
// every body retaining this binding. Local setters do not refresh cached absolute
// position. Only the supported genuine skin transformed getter is accepted.
class RetailActorCollisionBinding final : public RetailPhysicsOwnerReads, public RetailPhysicsNodeReads {
public:
    RetailActorCollisionBinding(RetailPhysicsOwnerState&, const RetailAnimationClock&,
                               std::shared_ptr<RetailActorScene>, RetailActorSceneNode& actor,
                               RetailActorSceneNode& skin);
    const RetailPhysicsOwnerState& ownerIdentity() const override;
    float getPositionZCm() const override;
    float storedRootHeightCm() const override;
    RetailPhysicsZBounds transformedBoundingBoxCm() const override;
private:
    RetailPhysicsOwnerState& owner;
    const RetailAnimationClock& clock;
    std::shared_ptr<RetailActorScene> scene;
    RetailActorSceneNode& actor;
    RetailActorSceneNode& skin;
};

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
    // its body. Constant-input fixture adapter only: positionCm is the original
    // GetPosition result and the supplied bounds stand for the node's genuine
    // transformed getter result. This does not evaluate any scene getter.
    // Player is an explicit owner identity; nullptr gives the other-character role.
    void initialize(const RetailHeightPoint& positionCm, std::optional<RetailPhysicsZBounds> skinNodeBounds,
                    const RetailPhysicsOwnerState* player);
    // Live metadata retains the actual actor/getSkinMesh binding AFTER original
    // shape/mass creation. No box getter runs during initialization. Initial
    // position and player are the original initPhysic inputs; filter lookup is
    // separately live. Existing-body return precedes validation of new inputs.
    void initializeLive(const RetailHeightPoint&, std::shared_ptr<const RetailActorCollisionBinding>,
                        const RetailPhysicsOwnerState* player);
    // Leaf body portion of Character::SetPosition, after the caller commits the
    // original scene setPosition/updateAbsolutePosition(false) and synchronizes
    // live owner Z/root Z. It uses the requested XY, preserves body angle and
    // refilters every shape. It does not zero velocity, reset height or cancel
    // destinations. Original out-of-world SetXForm freezing remains observable.
    void setBodyPositionAfterSceneUpdate(const RetailHeightPoint& requestedCm);
    // Fixture-only synchronization for the existing value-based metadata adapter.
    // Update when the real node bbox changes, before the original refilter site.
    // This operation alone does not invent a new refilter timing. Live bodies
    // reject this operation; their genuine getter runs inside filtering.
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
    void initializeBody(const RetailHeightPoint&, std::optional<RetailPhysicsZBounds>,
                        const RetailPhysicsOwnerState*, std::shared_ptr<const RetailActorCollisionBinding>);
};
