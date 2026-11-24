#pragma once

#include <array>

using RetailActorHeadingPoint = std::array<float, 3>;
using RetailActorHeadingQuaternion = std::array<float, 4>; // X,Y,Z,W.

// Required live Character/scene operations. Position is original GetPosition;
// Right/Front are original virtual getters, ordinarily the NEGATED first/second
// columns of the CACHED absolute matrix. setRotation must preserve the original
// local write/dirty behavior, not implicitly refresh those cached columns.
// The actor/node binding stays valid and fixed during each serial operation;
// getter/setter data remain live, including the final post-set Front query.
// There is no native scene-instance adapter or empty/default host here.
class RetailActorHeadingHost {
  public:
    virtual ~RetailActorHeadingHost() = default;
    virtual RetailActorHeadingPoint getPosition() = 0;
    virtual RetailActorHeadingPoint getRight() = 0;
    virtual RetailActorHeadingPoint getFront() = 0;
    virtual RetailActorHeadingQuaternion getRotation() = 0;
    virtual void setRotation(const RetailActorHeadingQuaternion&) = 0;
};

// Character::turnToward(point) at0x2f3e99. Reads position once, ignores pointZ,
// writes the original normalized quaternion, and does not refresh scene caches.
void retailActorTurnTowardPoint(RetailActorHeadingHost&, const RetailActorHeadingPoint&);

// Character::turnToward(direction,step) at0x2f22d5. Direction is snapshotted by
// value, remains unnormalized, and step uses the original degree/radian order.
// Reads Right, rotation, writes rotation, then reads Front. The return test is
// strict dot(Front,direction)<-0.99 against that LIVE getter's cached transform.
bool retailActorTurnTowardDirection(RetailActorHeadingHost&, RetailActorHeadingPoint direction, float step);
