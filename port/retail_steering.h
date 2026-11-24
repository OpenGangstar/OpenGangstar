#pragma once

#include "retail_physics.h"

struct RetailSteeringPoint {
    float x, y, z;
};

// Borrowed live owner identity and position. XY is centimetres, as is the
// owner's existing positionZCm. physicsIndex is the actual PhysicAttributes
// pool index; owner.levelCategory is live s_type, not shape categoryBits.
struct RetailSteeringActor {
    RetailPhysicsOwnerState& owner;
    b2Vec2 positionXYCm;
    std::int32_t physicsIndex;
};

// Native counterparts of retail LevelObject::canGoToPoint at 0x00318be5 and
// 0x00317fb5. The overloads intentionally retain different query/filter rules.
// A clear three-output query always writes hitCm but leaves adjustedCm alone.
// Original missing metadata and insufficient/unmatched polygon geometry are
// invalid native host bindings and throw; callers must not substitute a slide.
bool retailCanGoToPoint(b2World& world, const RetailSteeringActor& actor,
                       const RetailSteeringPoint& destinationCm);
bool retailCanGoToPoint(b2World& world, const RetailSteeringActor& actor,
                       const RetailSteeringPoint& destinationCm,
                       RetailSteeringPoint& hitCm, RetailSteeringPoint& adjustedCm);
