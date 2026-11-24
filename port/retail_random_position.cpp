#include "retail_random_position.h"

#include <cmath>
#include <stdexcept>

namespace {
using Point = RetailHeightPoint;
using Ref = CityNavigation::TriangleRef;
float distanceSquared(const Point& a, const Point& b) {
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return (x * x + y * y) + z * z;
}
} // namespace

RetailRandomPosition::RetailRandomPosition(const RetailHeightTerrain& terrain,
                                           RetailRandomPositionSource& random)
    : terrain(terrain), random(random) {
    const auto count = terrain.mesh.buffers.size();
    // LoadArrays initializes only the eight named material entries. An extra or
    // duplicate binding otherwise leaves an original cost entry uninitialized.
    if (count != 8)
        throw std::invalid_argument("Random-position costs require eight original material bindings.");
    std::array<bool, 8> seen{};
    for (auto index : terrain.navigation.materialBuffers) {
        if (index >= count || seen[index])
            throw std::invalid_argument("Incomplete original random-position material mapping.");
        seen[index] = true;
    }
    for (auto& mode : costs) mode.resize(count);
    // Original LoadArrays0x31a2be..31a3cc: material order is the original file's
    // road,sidewalk,park,buildingBase,intersection,cliff,water,deathWater order.
    for (std::size_t m = 0; m < 8; ++m) {
        const auto buffer = terrain.navigation.materialBuffers[m];
        costs[0][buffer] = (m == 1 || m == 2) ? 1.f : 999.f;
        costs[1][buffer] = (m == 0 || m == 4) ? 1.f : 999.f;
    }
    for (std::size_t b = 0; b < count; ++b)
        if (terrain.navigation.buffers[b].roadFlags.size() != terrain.mesh.buffers[b].indices.size() / 3)
            throw std::invalid_argument("Random-position wrong-way byte count mismatch.");
}

std::uint32_t RetailRandomPosition::roll() {
    const auto value = random.lrand48();
    if (value < 0)
        throw std::runtime_error("Invalid shared lrand48 binding: negative result.");
    return static_cast<std::uint32_t>(value);
}
Ref RetailRandomPosition::neighbor(Ref ref, std::size_t edge) const {
    terrain.validate(ref);
    if (edge >= 3)
        throw std::runtime_error("Original random edge has no valid reciprocal lane.");
    const auto& lanes = terrain.navigation.buffers[ref.buffer].neighbors;
    Ref target{lanes[edge + 3][ref.indexOffset / 3], lanes[edge][ref.indexOffset / 3]};
    if (target.indexOffset != UINT16_MAX) terrain.validate(target);
    return target;
}
std::uint8_t RetailRandomPosition::wrongWay(Ref ref) const {
    terrain.validate(ref);
    return terrain.navigation.buffers[ref.buffer].roadFlags[ref.indexOffset / 3];
}
Point RetailRandomPosition::vertex(Ref ref, std::size_t corner) const {
    terrain.validate(ref);
    if (corner >= 3) throw std::runtime_error("Invalid original random-position edge corner.");
    return terrain.mesh.positions[terrain.mesh.buffers[ref.buffer].indices[ref.indexOffset + corner]];
}
Point RetailRandomPosition::center(Ref ref) const {
    const auto a = vertex(ref, 0), b = vertex(ref, 1), c = vertex(ref, 2);
    // GetTriCenter0x31a4cd adds third to the rounded a+b before multiplying.
    const auto offset = terrain.globalNode->absoluteTranslationCm;
    Point result;
    for (std::size_t k = 0; k < 3; ++k)
        result[k] = (c[k] + (a[k] + b[k])) * (1.f / 3.f) + offset[k];
    return result;
}
Point RetailRandomPosition::randomDestination(std::uint16_t buffer, std::uint16_t& offset,
                                               const Point& position, float minimum, float maximum) {
    const auto count = terrain.mesh.buffers.at(buffer).indices.size();
    const auto randomValue = roll(); // Original consumes before reading the count.
    if (!count) throw std::runtime_error("Original random destination divides by an empty index count.");
    const auto start = (randomValue % count) / 3;
    const auto triangles = count / 3;
    const float minSquared = minimum * minimum, maxSquared = maximum * maximum;
    const float middleSquared = (minSquared + maxSquared) * .5f;
    float bestScore = 1000000000.f;
    Point best{};
    bool found = false;
    std::size_t current = start > 1 ? start : 0;
    for (;;) {
        const auto off = static_cast<std::uint16_t>(current * 3);
        const auto candidate = center({buffer, off});
        const float distance = distanceSquared(candidate, position);
        if (minSquared <= distance && maxSquared >= distance) {
            offset = off;
            return candidate;
        }
        const float score = std::abs(distance - middleSquared);
        if (score < bestScore) {
            offset = off;
            bestScore = score;
            best = candidate;
            found = true;
        }
        ++current;
        // Literal asymmetric original scan. For a start>=2 it omits the
        // predecessor; starts0/1 instead execute the complete ascending scan.
        if (start > 1) {
            if (current == start - 1) break;
            if (current >= triangles) current = 0;
        } else if (current >= triangles) break;
    }
    if (!found)
        throw std::runtime_error("Original random destination leaves its output triangle uninitialized.");
    return best;
}

Point RetailRandomPosition::next(RetailHeightController& controller, std::size_t slot, int mode,
                                 Point position) {
    if (&controller.terrain != &terrain)
        throw std::invalid_argument("Random-position controller belongs to a different terrain context.");
    return next(controller.states.at(slot), mode, position);
}

Point RetailRandomPosition::next(RetailHeightState& state, int mode, Point position) {
    // Original early return tests only the selected index offset, before RNG.
    if (state.triangle.indexOffset == UINT16_MAX) return position;
    if (mode < 0 || mode > 1)
        throw std::invalid_argument("Original random-position cost mode is outside its two-entry table.");
    terrain.validate(state.triangle);
    auto& cursor = state.randomEdge;
    const auto previousOffset = cursor.triangle.indexOffset;
    if (cursor.edge != UINT16_MAX) {
        const auto target = neighbor(cursor.triangle, cursor.edge);
        if (target.indexOffset == UINT16_MAX)
            throw std::runtime_error("Original cached random edge crosses an absent neighbor.");
        cursor.triangle = target;
    } else {
        float closest = 9999999.f;
        std::uint16_t edge = 0;
        for (std::size_t e = 0; e < 3; ++e) {
            const auto target = neighbor(state.triangle, e);
            if (target.indexOffset == UINT16_MAX || costs[mode][target.buffer] == 999.f) continue;
            const auto flags = wrongWay(state.triangle);
            if (mode == 1 && ((flags & 3) == e + 1 || ((flags >> 2) & 3) == e + 1)) continue;
            const auto a = vertex(state.triangle, e), b = vertex(state.triangle, (e + 1) % 3);
            const auto offset = terrain.globalNode->absoluteTranslationCm;
            Point midpoint;
            for (std::size_t k = 0; k < 3; ++k) midpoint[k] = (a[k] + b[k]) * .5f + offset[k];
            const float distance = distanceSquared(midpoint, position);
            if (distance < closest) { closest = distance; edge = static_cast<std::uint16_t>(e); }
        }
        cursor.edge = edge;
        cursor.triangle = state.triangle;
    }

    const auto start = roll() % 3;
    int chosen = -1;
    for (std::size_t n = 0; n < 3; ++n) {
        const auto e = (start + n) % 3;
        const auto target = neighbor(cursor.triangle, e);
        if (target.indexOffset == UINT16_MAX || costs[mode][target.buffer] == 999.f) continue;
        if (target.indexOffset == previousOffset) { chosen = static_cast<int>(e); continue; }
        const auto flags = wrongWay(cursor.triangle);
        if (mode == 1 && ((flags & 3) == e + 1 || ((flags >> 2) & 3) == e + 1)) continue;
        chosen = static_cast<int>(e);
        break;
    }
    if (mode == 0 && chosen == -1) {
        const auto sidewalk = static_cast<std::uint16_t>(terrain.navigation.materialBuffers[1]);
        auto offset = cursor.triangle.indexOffset;
        const auto result = randomDestination(sidewalk, offset, position, 0, 0);
        cursor.triangle = {sidewalk, offset};
        cursor.edge = UINT16_MAX;
        return result;
    }
    // Original mode1/no-choice defaults to edge0 and comparison ordinal1.
    if (chosen < 0) chosen = 0;
    if (mode != 1 || ((wrongWay(cursor.triangle) >> 4) & 3) != chosen + 1) {
        cursor.edge = static_cast<std::uint16_t>(chosen);
    } else {
        const auto old = cursor.triangle;
        const auto target = neighbor(old, static_cast<std::size_t>(chosen));
        if (target.indexOffset == UINT16_MAX)
            throw std::runtime_error("Original vehicle random edge crosses an absent neighbor.");
        cursor.triangle = target;
        for (std::size_t e = 0; e < 3; ++e) {
            const auto back = neighbor(target, e);
            if (back.indexOffset == old.indexOffset && back.buffer == old.buffer) {
                cursor.edge = static_cast<std::uint16_t>((e + 1) % 3);
                break;
            }
        }
    }
    // Both paths now proceed directly to point construction. The original
    // road/intersection check sets the flag without changing this edge.
    const auto road = terrain.navigation.materialBuffers[0];
    if (cursor.triangle.buffer == road) {
        const auto target = neighbor(cursor.triangle, cursor.edge);
        if (target.buffer == terrain.navigation.materialBuffers[4]) cursor.intersectionSeen = true;
    }
    const auto a = vertex(cursor.triangle, cursor.edge), b = vertex(cursor.triangle, (cursor.edge + 1) % 3);
    const auto lanes = wrongWay(cursor.triangle) >> 6;
    const float width = lanes ? 1.f / (float(lanes) + float(lanes)) : .5f;
    const auto maximumOrdinal = std::uint16_t(lanes ? lanes : 1);
    if (cursor.laneOrdinal > maximumOrdinal) cursor.laneOrdinal = maximumOrdinal;
    const float lane = (float(cursor.laneOrdinal) + float(cursor.laneOrdinal)) - 1.f;
    const auto offset = terrain.globalNode->absoluteTranslationCm;
    Point result;
    for (std::size_t k = 0; k < 3; ++k) {
        const float delta = b[k] - a[k];
        result[k] = (width * (delta * lane) + a[k]) + offset[k];
    }
    return result;
}
