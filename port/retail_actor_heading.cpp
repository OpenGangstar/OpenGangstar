#include "retail_actor_heading.h"

#include "retail_scene.h"

#include <cmath>

namespace {
using Point = RetailActorHeadingPoint;
using Quaternion = RetailActorHeadingQuaternion;

float dot(const Point& a, const Point& b) {
    return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
}

Quaternion multiply(const Quaternion& a, const Quaternion& b) {
    // Original operator*0x24eb35's reversed cross orientation and f32 order.
    // Do not replace with a conventional quaternion library's a*b convention.
    const float w = ((b[3] * a[3] - b[0] * a[0]) - b[1] * a[1]) - b[2] * a[2];
    const float x = ((b[3] * a[0] + a[3] * b[0]) + b[1] * a[2]) - a[1] * b[2];
    const float y = ((b[3] * a[1] + a[3] * b[1]) + b[2] * a[0]) - a[2] * b[0];
    const float z = ((b[3] * a[2] + a[3] * b[2]) + b[0] * a[1]) - a[0] * b[1];
    return {x, y, z, w}; // Original turnSceneNode does not normalize the product.
}
} // namespace

void retailActorTurnTowardPoint(RetailActorHeadingHost& scene, const Point& point) {
    const auto position = scene.getPosition();
    const float x = point[0] - position[0];
    const float y = point[1] - position[1];
    const auto direction = retailSceneNormalizeVector({x, y, 0});
    // Original constant-axis dot emits a sign-bit negation for its Y term.
    const float cosine = ((-direction[0]) * 0.f + (-direction[1])) + direction[2] * 0.f;
    const auto rotation = cosine <= -1.f ? retailSceneAngleAxisQuaternion(3.14159265358979323846f, {0, 0, 1})
                                       : retailSceneRotationFromTo({0, -1, 0}, {-x, y, 0});
    scene.setRotation(retailSceneNormalizeQuaternion(rotation));
}

bool retailActorTurnTowardDirection(RetailActorHeadingHost& scene, Point direction, float step) {
    const auto right = scene.getRight();
    const float sign = dot(right, direction) > 0.f ? 1.f : -1.f;
    const auto rotation = scene.getRotation();
    const float angle = (sign * step) * -0.017453292519943295f;
    scene.setRotation(multiply(rotation, retailSceneYawQuaternion(angle)));
    return dot(scene.getFront(), direction) < -.99f;
}
