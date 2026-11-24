#include "retail_physics.h"

#include <stdexcept>
#include <utility>

namespace {
bool insideRetailHeightRectangle(const b2Body& body)
{
    const auto& position = body.GetPosition();
    return position.x > -450.f && position.x < -405.f &&
           position.y > -560.f && position.y < -420.f;
}

std::optional<RetailPhysicsZBounds> verticalBounds(const RetailPhysicsBodyData& data,
    const RetailPhysicsBodyData& other, const b2Body& body, bool zeroCategory,
    const RetailPhysicsOwnerState* player)
{
    if (data.nodeBounds) {
        if (!data.owner)
            throw std::logic_error("Node-backed retail physics metadata requires an owner.");
        const auto& owner = *data.owner;
        if (owner.levelCategory == 3 && owner.currentJumpState > 0 && !other.nodeBounds)
            return std::nullopt;
        if (owner.levelCategory == 5 || data.owner == player) {
            const float lower = (owner.positionZCm - owner.rootZCm) - 25.f;
            // Preserve the original operation order, including both upper-bound additions.
            return RetailPhysicsZBounds{lower, (lower + 150.f) + 25.f};
        }
        return data.nodeBounds;
    }
    if (data.jumpSurface) {
        RetailPhysicsZBounds result{data.heightsCm[0], data.heightsCm[0]};
        for (size_t i = 1; i < data.heightsCm.size(); ++i) {
            if (data.heightsCm[i] < result.lowerCm) result.lowerCm = data.heightsCm[i];
            if (data.heightsCm[i] > result.upperCm) result.upperCm = data.heightsCm[i];
        }
        return result;
    }
    return RetailPhysicsZBounds{
        zeroCategory || insideRetailHeightRectangle(body) ? data.heightsCm[0] : -999999.f,
        data.heightsCm[1]};
}

bool enteringJumpSurface(b2Shape& surface, b2Shape& other)
{
    // The original ARM body +20/+24 fields are m_xf.R.col2, not the first column.
    const auto& direction = surface.GetBody()->GetXForm().R.col2;
    const auto& velocity = other.GetBody()->GetLinearVelocity();
    const float x = velocity.x * direction.x;
    const float y = velocity.y * direction.y;
    return x + y > 0.f;
}
}

RetailContactFilter::RetailContactFilter(const RetailPhysicsOwnerState* player, JumpShapeHandler jumpShape)
    : player(player), jumpShape(std::move(jumpShape))
{
}

void RetailContactFilter::setPlayer(const RetailPhysicsOwnerState* value)
{
    player = value;
}

bool RetailContactFilter::ShouldCollide(b2Shape* first, b2Shape* second)
{
    const auto& a = first->GetFilterData();
    const auto& b = second->GetFilterData();
    if ((!a.categoryBits && !a.groupIndex && !a.maskBits) ||
        (!b.categoryBits && !b.groupIndex && !b.maskBits)) return false;
    const bool zeroCategory = !a.categoryBits || !b.categoryBits;
    if (!(zeroCategory && a.groupIndex == b.groupIndex) &&
        (!(b.categoryBits & a.maskBits) || !(a.categoryBits & b.maskBits))) return false;

    auto* firstData = static_cast<RetailPhysicsBodyData*>(first->GetBody()->GetUserData());
    auto* secondData = static_cast<RetailPhysicsBodyData*>(second->GetBody()->GetUserData());
    if (!firstData || !secondData) return true;

    const auto firstBounds = verticalBounds(*firstData, *secondData, *first->GetBody(), zeroCategory, player);
    if (!firstBounds) return false;
    const auto secondBounds = verticalBounds(*secondData, *firstData, *second->GetBody(), zeroCategory, player);
    if (!secondBounds) return false;
    const bool overlap = firstBounds->lowerCm < secondBounds->upperCm &&
                         firstBounds->upperCm > secondBounds->lowerCm;

    // This gate runs even without vertical overlap. The first jump-surface flag
    // takes precedence; the original does not try the second flag after a
    // nonpositive first direction test.
    b2Shape* surface = nullptr;
    RetailPhysicsBodyData* actor = nullptr;
    if (firstData->jumpSurface) {
        if (enteringJumpSurface(*first, *second)) { surface = first; actor = secondData; }
    } else if (secondData->jumpSurface) {
        if (enteringJumpSurface(*second, *first)) { surface = second; actor = firstData; }
    }
    if (surface) {
        if (!actor->owner)
            throw std::logic_error("Retail jump-surface filtering requires the other body's owner.");
        if (!jumpShape)
            throw std::logic_error("Retail jump-surface filtering requires a SetCurrentJumpShape binding.");
        jumpShape(*actor->owner, *surface);
        return false;
    }
    return overlap;
}
