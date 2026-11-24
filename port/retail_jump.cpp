#include "retail_jump.h"

#include <cstring>
#include <stdexcept>

namespace {
constexpr float noHeight = 999999.f;
void incrementOriginalCounter(std::int32_t& value) {
    // Original ARM addition wraps. Avoid signed C++ overflow even if a host
    // supplies INT_MAX; keep the single authoritative owner's complete bits.
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    ++bits;
    std::memcpy(&value, &bits, sizeof(value));
}
} // namespace

RetailJumpController::RetailJumpController(RetailPhysicsOwnerState& owner) : owner(owner) {
    owner.currentJumpState = 0;
}

void RetailJumpController::setCurrentShape(b2Shape& shape) {
    if (shape.GetType() != e_polygonShape)
        throw std::invalid_argument("Retail jump requires a four-vertex native polygon.");
    const auto& polygon = static_cast<const b2PolygonShape&>(shape);
    if (polygon.GetVertexCount() != 4)
        throw std::invalid_argument("Retail jump requires a four-vertex native polygon.");
    auto* body = shape.GetBody();
    const auto* metadata = static_cast<const RetailPhysicsBodyData*>(body->GetUserData());
    if (!metadata)
        throw std::invalid_argument("Retail jump requires typed native body metadata.");
    const auto& transform = body->GetXForm();
    const auto* vertices = polygon.GetVertices();
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& v = vertices[i];
        auto& p = snapshot.quadCm[i];
        p[0] = ((v.x * transform.R.col1.x + v.y * transform.R.col2.x) + transform.position.x) * 100.f;
        p[1] = ((v.x * transform.R.col1.y + v.y * transform.R.col2.y) + transform.position.y) * 100.f;
        p[2] = metadata->heightsCm[i];
    }
    const auto& a = snapshot.quadCm[0];
    const auto& b = snapshot.quadCm[1];
    const auto& c = snapshot.quadCm[2];
    const auto& d = snapshot.quadCm[3];
    snapshot.centreCm[0] = (((a[0] + b[0]) + c[0]) + d[0]) * .25f;
    snapshot.centreCm[1] = (((a[1] + b[1]) + c[1]) + d[1]) * .25f;
    snapshot.centreCm[2] = (d[2] + ((a[2] + b[2]) + c[2])) * .25f;
    snapshot.enabled = true;
    snapshot.opaqueJumpFlag = metadata->opaqueJumpFlag;
}

float RetailJumpController::height(const std::array<float, 3>& point, std::size_t) {
    const float dx = point[0] - snapshot.centreCm[0];
    const float dy = point[1] - snapshot.centreCm[1];
    const float dz = point[2] - snapshot.centreCm[2];
    if (!((dx * dx + dy * dy) + dz * dz < 2250000.f)) {
        snapshot.enabled = false;
        owner.currentJumpState = 0;
        return noHeight;
    }
    const auto& quad = snapshot.quadCm;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& a = quad[i];
        const auto& b = quad[(i + 1) & 3];
        const float side = (b[0] - a[0]) * (point[1] - a[1]) - (b[1] - a[1]) * (point[0] - a[0]);
        if (side < 0.f)
            return noHeight;
    }
    const auto& a = quad[0];
    const auto& b = quad[1];
    const auto& c = quad[2];
    const float inverse = 1.f / ((b[0] - a[0]) * (-(a[1] - c[1])) + (b[1] - a[1]) * (a[0] - c[0]));
    const float wb = ((point[0] - c[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (point[1] - c[1])) * inverse;
    incrementOriginalCounter(owner.currentJumpState);
    const float base = (b[2] - a[2]) * wb + a[2];
    const float wa = ((point[0] - b[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (point[1] - b[1])) * inverse;
    return base - (a[2] - c[2]) * ((1.f - wb) - wa);
}

float RetailJumpController::selectSuccessfulTerrainHeight(const std::array<float, 3>& point, float ordinary,
                                                          std::size_t slot) {
    if (!snapshot.enabled)
        return ordinary;
    if (slot == 0)
        owner.currentJumpState = 0;
    const float jump = height(point, slot);
    return ordinary < jump && jump != noHeight ? jump : ordinary;
}
