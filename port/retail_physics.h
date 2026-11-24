#pragma once

#include "Box2D.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

// Native controller state, not an ARM object view. The controller must supply
// the live LevelObject::s_type classification, not metatype tags or categoryBits.
struct RetailPhysicsOwnerState {
    std::int32_t levelCategory;
    float positionZCm;
    float rootZCm;
    // The controller's actual signed current-jump state, not a duration estimate.
    std::int32_t currentJumpState;
};

struct RetailPhysicsZBounds {
    float lowerCm;
    float upperCm;
};

// Bodies using this filter store a pointer to this native record as user data.
// Records and referenced owners must outlive their bodies. Present nodeBounds
// represents an original scene node and requires an owner; the bounds are the
// node's supplied bounding-box Z values. All Z values are centimetres; native
// Box2D positions and velocities remain metres. Ownerless local terrain has no node.
struct RetailPhysicsBodyData {
    RetailPhysicsOwnerState* owner;
    std::optional<RetailPhysicsZBounds> nodeBounds;
    std::array<float, 4> heightsCm;
    bool jumpSurface;
};

// Native reconstruction of ContactFilter::ShouldCollide at retail 0x0031dc7d.
// Existing Box2D pairs require explicit world.Refilter(shape) when host state
// changes must cause the engine to evaluate this filter again.
class RetailContactFilter : public b2ContactFilter {
public:
    using JumpShapeHandler = std::function<void(RetailPhysicsOwnerState&, b2Shape&)>;

    // Player is an explicit native owner identity, never inferred from a category.
    // The handler must implement SetCurrentJumpShape's state changes. A positive
    // jump gate without its required owner/handler is a fatal host-binding error.
    // Exceptions during a Box2D step are not a supported recovery boundary.
    RetailContactFilter(const RetailPhysicsOwnerState* player, JumpShapeHandler jumpShape);
    void setPlayer(const RetailPhysicsOwnerState* player);
    bool ShouldCollide(b2Shape* first, b2Shape* second) override;

private:
    const RetailPhysicsOwnerState* player;
    JumpShapeHandler jumpShape;
};
