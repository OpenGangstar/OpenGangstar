#include "retail_steering.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace {
RetailSteeringPoint currentPoint(const RetailSteeringActor& actor)
{
    return {actor.positionXYCm.x, actor.positionXYCm.y, actor.owner.positionZCm};
}

RetailSteeringPoint normalize(RetailSteeringPoint value)
{
    // Retail irr::core::vector3d<float>::normalize, 0x0020a2d1: float
    // arithmetic, exact zero test, sqrtf and reciprocal multiplication.
    const float xy = value.x * value.x + value.y * value.y;
    const float squared = xy + value.z * value.z;
    if (squared == 0.f) return value;
    const float inverse = 1.f / std::sqrt(squared);
    return {value.x * inverse, value.y * inverse, value.z * inverse};
}

RetailSteeringPoint interpolate(const RetailSteeringPoint& current,
    const RetailSteeringPoint& destination, float fraction)
{
    return {(destination.x - current.x) * fraction + current.x,
            (destination.y - current.y) * fraction + current.y,
            (destination.z - current.z) * fraction + current.z};
}

b2AABB queryBounds(const RetailSteeringPoint& current,
    const RetailSteeringPoint& destination, float margin, bool padEqualAxes)
{
    b2AABB bounds;
    bounds.lowerBound.Set((current.x > destination.x ? destination.x : current.x) / 100.f - margin,
                          (current.y > destination.y ? destination.y : current.y) / 100.f - margin);
    bounds.upperBound.Set((current.x > destination.x ? current.x : destination.x) / 100.f + margin,
                          (current.y > destination.y ? current.y : destination.y) / 100.f + margin);
    if (padEqualAxes) {
        if (bounds.lowerBound.x == bounds.upperBound.x) bounds.upperBound.x += .01f;
        if (bounds.lowerBound.y == bounds.upperBound.y) bounds.upperBound.y += .01f;
    }
    // The original broad phase asserts these invariants. Do not invent a query
    // margin for the three-output overload's zero-margin degenerate input.
    if (!(bounds.lowerBound.x < bounds.upperBound.x && bounds.lowerBound.y < bounds.upperBound.y))
        throw std::domain_error("Retail steering requires a nondegenerate broad-phase query.");
    return bounds;
}

bool candidateFilter(b2Shape& shape, const RetailSteeringActor& actor, bool skipOwnerless,
                     const RetailPhysicsBodyData*& captured)
{
    const auto& filter = shape.GetFilterData();
    if (!filter.categoryBits &&
        (filter.groupIndex != actor.physicsIndex + 1 || !filter.maskBits)) return false;
    if (shape.IsSensor()) return false;
    const auto* data = static_cast<const RetailPhysicsBodyData*>(shape.GetBody()->GetUserData());
    captured = data; // Original keeps this pointer across TestSegment.
    if (data && (data->owner == &actor.owner || (skipOwnerless && !data->owner))) return false;
    return true;
}

const RetailPhysicsBodyData& metadata(b2Shape& shape)
{
    const auto* data = static_cast<const RetailPhysicsBodyData*>(shape.GetBody()->GetUserData());
    if (!data) throw std::logic_error("An intersected retail steering body requires native metadata.");
    return *data;
}

bool hasNode(const RetailPhysicsBodyData& data)
{
    if (data.liveNode && data.nodeBounds)
        throw std::logic_error("Live steering node cannot also use snapshot bounds.");
    return bool(data.liveNode) || bool(data.nodeBounds);
}

RetailPhysicsZBounds heightBounds(const RetailPhysicsBodyData& data)
{
    if (const auto node = data.liveNode) {
        if (!data.owner || &node->ownerIdentity() != data.owner)
            throw std::logic_error("Live steering requires its genuine node/owner binding.");
        // Both original overloads use slot52 getTransformedBoundingBox, AFTER
        // a segment hit, before computing the height/scratch temporary.
        return node->transformedBoundingBoxCm();
    }
    return data.nodeBounds.value_or(RetailPhysicsZBounds{data.heightsCm[0], data.heightsCm[1]});
}

bool withinHeights(const RetailPhysicsZBounds& bounds, float z)
{
    return !(bounds.lowerCm > z || bounds.upperCm < z);
}

float raySide(b2Body& body, const b2Segment& segment)
{
    const float dx = (body.GetPosition().x - segment.p1.x) * 100.f;
    const float dy = (body.GetPosition().y - segment.p1.y) * 100.f;
    const float sx = (segment.p2.x - segment.p1.x) * 100.f;
    const float sy = (segment.p2.y - segment.p1.y) * 100.f;
    return sx * dy - sy * dx;
}

void adjustDestination(b2Shape& shape, bool secondRay, const std::array<b2Segment, 2>& rays,
    const b2Vec2& perpendicular, float margin, const b2Vec2& normal,
    const RetailSteeringPoint& scratch, RetailSteeringPoint& adjusted)
{
    auto& body = *shape.GetBody();
    if (shape.GetType() == e_circleShape) {
        const auto& data = metadata(shape);
        if (!data.owner) throw std::logic_error("A selected retail circle requires an owner.");
        if (data.owner->levelCategory == 3) {
            const float distance = margin * 100.f + 50.f;
            const bool positive = !(raySide(body, rays[secondRay]) <= 0.f);
            adjusted = {positive ? scratch.x + perpendicular.x * distance : scratch.x - perpendicular.x * distance,
                        positive ? scratch.y + perpendicular.y * distance : scratch.y - perpendicular.y * distance,
                        positive ? scratch.z + distance * 0.f : scratch.z - distance * 0.f};
        } else {
            const float distance = margin * 100.f + 75.f;
            const auto& position = body.GetPosition();
            adjusted = {secondRay ? position.x * 100.f - perpendicular.x * distance : position.x * 100.f + perpendicular.x * distance,
                        secondRay ? position.y * 100.f - perpendicular.y * distance : position.y * 100.f + perpendicular.y * distance,
                        secondRay ? scratch.z - distance * 0.f : scratch.z + distance * 0.f};
        }
        return;
    }
    if (shape.GetType() != e_polygonShape)
        throw std::logic_error("Retail steering adjustment requires original circle or polygon geometry.");
    const auto& polygon = static_cast<const b2PolygonShape&>(shape);
    if (polygon.GetVertexCount() < 4)
        throw std::logic_error("Retail steering polygon adjustment requires four authored geometry entries.");
    const auto& transform = body.GetXForm();
    int normalIndex = 0;
    for (; normalIndex < 4; ++normalIndex) {
        const auto& local = polygon.GetNormals()[normalIndex];
        const float x = local.x * transform.R.col1.x + local.y * transform.R.col2.x;
        const float y = local.x * transform.R.col1.y + local.y * transform.R.col2.y;
        if (normal.x == x && normal.y == y) break;
    }
    if (normalIndex == 4)
        throw std::logic_error("Retail steering hit normal did not match its four authored polygon normals.");
    const int vertexIndex = raySide(body, rays[secondRay]) > 0.f ? (normalIndex + 1) % 4 : normalIndex;
    const auto& local = polygon.GetVertices()[vertexIndex];
    const float x = (local.x * transform.R.col1.x + local.y * transform.R.col2.x) + transform.position.x;
    const float y = (local.x * transform.R.col1.y + local.y * transform.R.col2.y) + transform.position.y;
    const auto direction = normalize({x * 100.f - transform.position.x * 100.f,
                                      y * 100.f - transform.position.y * 100.f,
                                      scratch.z - scratch.z});
    const float distance = margin * 100.f + 50.f;
    adjusted = {x * 100.f + direction.x * distance,
                y * 100.f + direction.y * distance,
                scratch.z + distance * direction.z};
}
}

bool retailCanGoToPoint(b2World& world, const RetailSteeringActor& actor,
                       const RetailSteeringPoint& destination)
{
    const auto current = currentPoint(actor);
    std::array<b2Shape*, 100> shapes{};
    world.Query(queryBounds(current, destination, 0.f, true), shapes.data(), 100);
    // The original point overload also normalizes this 3D temporary, although
    // its segment remains the unoffset current-to-destination XY segment.
    [[maybe_unused]] const auto direction = normalize({destination.x - current.x,
        destination.y - current.y, destination.z - current.z});
    const b2Segment segment{b2Vec2(current.x / 100.f, current.y / 100.f),
                            b2Vec2(destination.x / 100.f, destination.y / 100.f)};
    float fraction = 1.f;
    for (auto* shape : shapes) {
        if (!shape) break;
        const RetailPhysicsBodyData* captured = nullptr;
        if (!candidateFilter(*shape, actor, false, captured)) continue;
        b2Vec2 normal;
        if (!shape->TestSegment(shape->GetBody()->GetXForm(), &fraction, &normal, segment, 1.f)) continue;
        if (!captured) throw std::logic_error("An intersected retail steering body requires native metadata.");
        const auto& data = *captured;
        if (hasNode(data) || data.heightsCm[0] != data.heightsCm[1]) {
            const auto bounds = heightBounds(data);
            const float z = (destination.z - current.z) * fraction + current.z;
            if (!withinHeights(bounds, z)) continue;
        }
        return fraction >= 1.f;
    }
    // The original retains TestSegment's fraction even after height rejection.
    return fraction == 1.f;
}

bool retailCanGoToPoint(b2World& world, const RetailSteeringActor& actor,
    const RetailSteeringPoint& destination, RetailSteeringPoint& hit, RetailSteeringPoint& adjusted)
{
    const auto current = currentPoint(actor);
    const auto category = actor.owner.levelCategory;
    const float margin = category == 3 ? 1.5f : (category == 5 || category == 8 ? .5f : 0.f);
    std::array<b2Shape*, 100> shapes{};
    world.Query(queryBounds(current, destination, margin, false), shapes.data(), 100);
    const auto direction = normalize({destination.x - current.x, destination.y - current.y, destination.z - current.z});
    const b2Vec2 perpendicular(direction.y, -direction.x);
    std::array<b2Segment, 2> rays;
    for (int i = 0; i < 2; ++i) {
        const float x = margin * perpendicular.x, y = margin * perpendicular.y;
        rays[i].p1.Set(i ? current.x / 100.f + x : current.x / 100.f - x,
                       i ? current.y / 100.f + y : current.y / 100.f - y);
        rays[i].p2.Set(i ? destination.x / 100.f + x : destination.x / 100.f - x,
                       i ? destination.y / 100.f + y : destination.y / 100.f - y);
    }
    float closest = 1.f;
    b2Shape* selected = nullptr;
    b2Vec2 selectedNormal;
    bool selectedSecond = false;
    RetailSteeringPoint scratch{0, 0, 0};
    for (auto* shape : shapes) {
        if (!shape) break;
        const RetailPhysicsBodyData* captured = nullptr;
        if (!candidateFilter(*shape, actor, true, captured)) continue;
        float fraction = 1.f;
        b2Vec2 normal;
        bool second = false;
        if (!shape->TestSegment(shape->GetBody()->GetXForm(), &fraction, &normal, rays[0], 1.f)) {
            second = true;
            if (!shape->TestSegment(shape->GetBody()->GetXForm(), &fraction, &normal, rays[1], 1.f)) continue;
        }
        if (!captured) throw std::logic_error("An intersected retail steering body requires native metadata.");
        const auto& data = *captured;
        if (hasNode(data) || data.heightsCm[0] != data.heightsCm[1]) {
            const auto bounds = heightBounds(data);
            scratch = interpolate(current, destination, fraction);
            if (!withinHeights(bounds, scratch.z)) continue;
        }
        if (!(fraction < closest)) continue;
        closest = fraction;
        selected = shape;
        selectedNormal = normal;
        selectedSecond = second;
    }
    if (closest != 1.f)
        adjustDestination(*selected, selectedSecond, rays, perpendicular, margin, selectedNormal, scratch, adjusted);
    hit = interpolate(current, destination, closest);
    return closest == 1.f;
}
