#pragma once

#include "Box2D.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

// Native controller state, not an ARM object view. The controller must supply
// the live LevelObject::s_type classification, not metatype tags or categoryBits.
struct RetailPhysicsOwnerState {
    std::int32_t levelCategory;
    // Constant-input adapter values. The live filter instead calls owner reads
    // at the original GetPosition / stored-root-height evaluation points.
    float positionZCm;
    float rootZCm;
    // The controller's actual signed current-jump state, not a duration estimate.
    std::int32_t currentJumpState;
};

struct RetailPhysicsZBounds {
    float lowerCm;
    float upperCm;
};

// Required live operations, not snapshots. Identities are canonical game owners,
// never adapter addresses. Implementations retain their genuine node resources;
// any borrowed owner/clock storage must outlive the bodies using them.
class RetailPhysicsOwnerReads {
public:
    virtual ~RetailPhysicsOwnerReads() = default;
    virtual const RetailPhysicsOwnerState& ownerIdentity() const = 0;
    virtual float getPositionZCm() const = 0;
    virtual float storedRootHeightCm() const = 0;
};
class RetailPhysicsNodeReads {
public:
    virtual ~RetailPhysicsNodeReads() = default;
    virtual const RetailPhysicsOwnerState& ownerIdentity() const = 0;
    virtual RetailPhysicsZBounds transformedBoundingBoxCm() const = 0;
};

// Bodies using this filter store a pointer to this native record as user data.
// Records and referenced owners must outlive their bodies. In the numerical
// adapter, nodeBounds supplies the transformed-node getter's Z values and
// requires an owner. The live path retains its actual node getter below instead.
// All Z values are centimetres; native
// Box2D positions and velocities remain metres. Ownerless local terrain has no node.
struct RetailPhysicsBodyData {
    RetailPhysicsOwnerState* owner;
    std::optional<RetailPhysicsZBounds> nodeBounds;
    std::array<float, 4> heightsCm;
    bool jumpSurface;
    // Original metadata byte +25: wrapping authored Jump producer ID, copied to
    // jump state +113. Further consumer semantics remain unverified.
    std::uint8_t opaqueJumpFlag = 0;
    // Explicit live path. Node presence is independent of owner reads, matching
    // original nullable metadata.node. Empty current node is legal terrain.
    // nodeBounds above is ONLY the constant-input numerical fixture adapter.
    std::shared_ptr<const RetailPhysicsOwnerReads> liveOwner;
    std::shared_ptr<const RetailPhysicsNodeReads> liveNode;
};

// Native reconstruction of ContactFilter::ShouldCollide at retail 0x0031dc7d.
// Existing Box2D pairs require explicit world.Refilter(shape) when host state
// changes must cause the engine to evaluate this filter again.
class RetailContactFilter : public b2ContactFilter {
public:
    using JumpShapeHandler = std::function<void(RetailPhysicsOwnerState&, b2Shape&)>;
    using PlayerLookup = std::function<const RetailPhysicsOwnerState*()>;

    // Player is an explicit native owner identity, never inferred from a category.
    // The handler must implement SetCurrentJumpShape's state changes. A positive
    // jump gate without its required owner/handler is a fatal host-binding error.
    // Exceptions during a Box2D step are not a supported recovery boundary.
    RetailContactFilter(const RetailPhysicsOwnerState* player, JumpShapeHandler jumpShape);
    // Production/live path: lookup is mandatory and called separately for each
    // eligible side. Snapshot nodeBounds cannot be used by this constructor.
    RetailContactFilter(PlayerLookup playerLookup, JumpShapeHandler jumpShape);
    void setPlayer(const RetailPhysicsOwnerState* player);
    bool ShouldCollide(b2Shape* first, b2Shape* second) override;

private:
    const RetailPhysicsOwnerState* player;
    PlayerLookup playerLookup;
    bool live = false;
    JumpShapeHandler jumpShape;
};
