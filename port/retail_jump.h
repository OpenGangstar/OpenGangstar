#pragma once

#include "retail_physics.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct RetailJumpState {
    std::array<std::array<float, 3>, 4> quadCm{};
    std::array<float, 3> centreCm{};
    bool enabled = false;
    std::uint8_t opaqueJumpFlag = 0;
};

// Snapshot reconstruction of PhysicAttributes' jump surface. The live owner must
// outlive this object; currentJumpState has one authoritative home in that owner.
// setCurrentShape requires a real four-vertex Box2D polygon whose body user data
// is a live RetailPhysicsBodyData. Geometry/heights/flag are copied immediately;
// neither the shape, its body nor its metadata is retained after that call.
class RetailJumpController {
  public:
    // Represents original PhysicAttributes construction and initializes the
    // owner's currentJumpState to zero. Construct once with the actor lifetime;
    // attaching an existing instance to terrain does not reset that live state.
    explicit RetailJumpController(RetailPhysicsOwnerState&);
    RetailJumpController(const RetailJumpController&) = delete;
    RetailJumpController& operator=(const RetailJumpController&) = delete;
    void setCurrentShape(b2Shape&);
    // Like original GetJumpHeight, this direct query is not gated by enabled.
    // The original constructor's zero quad can therefore yield NaN and increment.
    // slot is retained for source correspondence; the original does not use it.
    float height(const std::array<float, 3>& pointCm, std::size_t slot = 0);
    const RetailJumpState& state() const { return snapshot; }
    const RetailPhysicsOwnerState& ownerState() const { return owner; }

  private:
    friend class RetailHeightController;
    RetailPhysicsOwnerState& owner;
    RetailJumpState snapshot;
    float selectSuccessfulTerrainHeight(const std::array<float, 3>&, float ordinary, std::size_t slot);
};
