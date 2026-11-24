#pragma once

#include "Box2D.h"

#include <cstddef>
#include <cstdint>

// Native application state, not an ARM layout. Keep one state/world binding for
// the application's lifetime; waking/resuming GS3DStuff resets its timestamp,
// not this accumulator. The state and actual shared world outlive the binding.
struct RetailWorldStepState {
    float accumulatorSeconds = 0;
    std::int32_t completedSteps = 0;
};

// Required live host operations for the original eligible vehicle vector.
// Each indexed operation resolves the CURRENT entry, not a copied snapshot.
// size() is read again after each callback, including contact-driven mutations
// during Step. An actually empty collection is valid; unimplemented vehicles
// must not be represented by empty hooks. Callbacks must not throw or reenter
// this update, destroy its world/state, or write the application step state.
class RetailWorldVehicles {
  public:
    virtual ~RetailWorldVehicles() = default;
    virtual std::size_t size() const = 0;
    virtual void updateFrame(std::size_t index, std::int32_t elapsedMs) = 0;
    virtual void updatePhysics(std::size_t index, float seconds) = 0;
    virtual void synchronizePhysics(std::size_t index) = 0;
};

// GS3DStuff::update's physics leaf only. Actor updates/animation precede it;
// player/actor scene synchronization and the original timestamp commit follow
// it. It neither owns bodies nor installs/replaces collision callbacks.
class RetailWorldUpdate {
  public:
    static constexpr float fixedStepSeconds = 1.f / 60.f;
    RetailWorldUpdate(b2World&, RetailWorldStepState&);
    RetailWorldUpdate(const RetailWorldUpdate&) = delete;
    RetailWorldUpdate& operator=(const RetailWorldUpdate&) = delete;

    // elapsedMs is the raw modulo-2^32 subtraction of current and previous
    // integer device milliseconds. Preserve it for vehicle Update(int); only
    // the accumulator interprets its signed bits and clamps to [0,250]. Reuse
    // the host clock; do not truncate each floating frame delta independently.
    void advance(std::uint32_t elapsedMs, RetailWorldVehicles&);

  private:
    b2World& world;
    RetailWorldStepState& state;
};
