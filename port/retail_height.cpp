#include "retail_height.h"
#include "retail_jump.h"
#include "Common/b2BlockAllocator.h"
#include <sstream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>

namespace {
using Point = RetailHeightPoint;
using Ref = CityNavigation::TriangleRef;
bool heightMeshType(std::uint32_t type) {
    return type == 0x6d656164 || type == 0x73656164;
}
const CityMesh& globalHeightMesh(const RetailHeightSceneNode& node) {
    if (!heightMeshType(node.type) || !node.mesh)
        throw std::invalid_argument("Global retail heightmap requires a recognized mesh node.");
    return *node.mesh;
}
bool inHeightBox(const Point& point, const std::array<Point, 2>& box) {
    if (!(point[0] >= box[0][0] && point[0] <= box[1][0] &&
          point[1] >= box[0][1] && point[1] <= box[1][1]))
        return false;
    const float middleZ = (box[0][2] + box[1][2]) * .5f;
    return middleZ >= box[0][2] && middleZ <= box[1][2];
}
// The historical API hides standalone construction. This access shim creates a
// real native polygon solely to ask its exact OBB-based ComputeAABB, with no
// body, contact callbacks, or broad-phase insertion.
struct ShapeProbe : b2Shape {
    using b2Shape::Create;
    using b2Shape::Destroy;
};
Point subtract(const Point& a, const Point& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Point normalize(Point p) {
    const float lengthSquared = (p[0] * p[0] + p[1] * p[1]) + p[2] * p[2];
    if (lengthSquared == 0.f)
        return p;
    const float reciprocal = 1.f / std::sqrt(lengthSquared);
    for (auto& value : p)
        value *= reciprocal;
    return p;
}
Point triangleNormal(const std::array<Point, 3>& v) {
    const auto ab = subtract(v[0], v[1]), cb = subtract(v[2], v[1]);
    return normalize({(-ab[1]) * cb[2] + ab[2] * cb[1], cb[0] * (-ab[2]) + ab[0] * cb[2],
                      (-ab[0]) * cb[1] + ab[1] * cb[0]});
}
// Original 0x3196fd. In particular, collinearity does not test projected overlap.
bool intersects(const Point& a, const Point& b, const Point& c, const Point& d) {
    if ((a[0] == b[0] && a[1] == b[1]) || (c[0] == d[0] && c[1] == d[1]))
        return false;
    const float dyCD = d[1] - c[1], dxAB = b[0] - a[0], dxCD = d[0] - c[0], dyAB = b[1] - a[1];
    const float denominator = dyCD * dxAB - dxCD * dyAB;
    const float dxAC = a[0] - c[0], dyAC = a[1] - c[1];
    const float first = dxCD * dyAC - dyCD * dxAC;
    const float second = dxAB * dyAC - dyAB * dxAC;
    if (denominator == 0.f)
        return first == 0.f && second == 0.f;
    const float u = first / denominator;
    if (!(u >= 0.f && u <= 1.f))
        return false;
    const float v = second / denominator;
    return v >= 0.f && v <= 1.f;
}
// These are the core's scalar locals, not a view of the live selected triangle.
// Reseeding changes live vertices while these locals intentionally remain old.
struct Math {
    float pxBx, pyBy, pxCx, pyCy, bxAx, byAy, bzAz, cxBx, cyBy, axCx, ayCy, azCz;
    float pxAx, pyAy;
    Math(const RetailHeightState& s, const Point& p) {
        const auto& a = s.vertices[0];
        const auto& b = s.vertices[1];
        const auto& c = s.vertices[2];
        pxBx = p[0] - b[0];
        pyBy = p[1] - b[1];
        pxCx = p[0] - c[0];
        pyCy = p[1] - c[1];
        bxAx = b[0] - a[0];
        byAy = b[1] - a[1];
        bzAz = b[2] - a[2];
        cxBx = c[0] - b[0];
        cyBy = c[1] - b[1];
        axCx = a[0] - c[0];
        ayCy = a[1] - c[1];
        azCz = a[2] - c[2];
        pxAx = p[0] - a[0];
        pyAy = p[1] - a[1];
    }
    float side(std::size_t edge) const {
        if (edge == 0)
            return pyAy * bxAx - pxAx * byAy;
        if (edge == 1)
            return pyBy * cxBx - pxBx * cyBy;
        return pyCy * axCx - pxCx * ayCy;
    }
};
bool rejected(std::uint16_t word, std::uint16_t generation) {
    const auto classification = word & 3;
    return classification == 1 || classification == 2 || (word & 0xfffc) == generation;
}
} // namespace

RetailHeightTerrain::RetailHeightTerrain(const CityMesh& mesh, const CityNavigation& navigation,
                                         Point translation)
    : mesh(mesh), navigation(navigation) {
    if (mesh.positions.empty() || mesh.positions.size() > size_t(UINT16_MAX) + 1 ||
        mesh.buffers.size() > size_t(UINT16_MAX) + 1 || mesh.buffers.size() != navigation.buffers.size())
        throw std::invalid_argument("Retail height mesh/navigation mismatch.");
    std::array<Point, 2> bounds{mesh.positions.front(), mesh.positions.front()};
    for (const auto& p : mesh.positions)
        for (size_t k = 0; k < 3; ++k) {
            if (!std::isfinite(p[k]) || !std::isfinite(translation[k]))
                throw std::invalid_argument("Nonfinite height input.");
            bounds[0][k] = std::min(bounds[0][k], p[k]);
            bounds[1][k] = std::max(bounds[1][k], p[k]);
        }
    for (size_t i = 0; i < mesh.buffers.size(); ++i) {
        const auto count = mesh.buffers[i].indices.size();
        if (count % 3 || count > 65535)
            throw std::invalid_argument("Invalid retail height index count.");
        for (auto index : mesh.buffers[i].indices)
            if (index >= mesh.positions.size())
                throw std::invalid_argument("Invalid retail height vertex.");
        const auto& buffer = navigation.buffers[i];
        for (const auto& lane : buffer.neighbors)
            if (lane.size() != count / 3)
                throw std::invalid_argument("Invalid retail height neighbor count.");
        if (buffer.overlaps.size() != count / 3)
            throw std::invalid_argument("Invalid retail height overlap count.");
        visits.push_back(buffer.neighbors[7]);
    }
    for (size_t b = 0; b < navigation.buffers.size(); ++b)
        for (size_t t = 0; t < visits[b].size(); ++t) {
            for (size_t edge = 0; edge < 3; ++edge) {
                const auto& lanes = navigation.buffers[b].neighbors;
                if (lanes[edge][t] != UINT16_MAX)
                    validate({lanes[edge + 3][t], lanes[edge][t]});
            }
            for (auto ref : navigation.buffers[b].overlaps[t])
                validate(ref);
        }
    const auto cliff = navigation.materialBuffers[static_cast<size_t>(CityNavigation::Material::cliff)];
    if (cliff >= mesh.buffers.size() || navigation.cliffVertexKeys.size() != mesh.positions.size() * 2)
        throw std::invalid_argument("Invalid retail cliff vertex map.");
    for (size_t key = 0; key < mesh.positions.size(); ++key) {
        const auto start = navigation.cliffVertexKeys[key * 2],
                   count = navigation.cliffVertexKeys[key * 2 + 1];
        if (size_t(start) + count > navigation.cliffTriangles.size())
            throw std::invalid_argument("Invalid retail cliff range.");
        for (size_t i = start; i < size_t(start) + count; ++i) {
            const auto off = navigation.cliffTriangles[i];
            if (off == UINT16_MAX)
                continue;
            const Ref ref{static_cast<uint16_t>(cliff), off};
            validate(ref);
            const auto ids = indices(ref);
            if (std::find(ids.begin(), ids.end(), key) == ids.end())
                throw std::invalid_argument("Cliff key absent from face.");
        }
    }
    defaultNode.type = 0x6d656164;
    defaultNode.mesh = &mesh;
    defaultNode.boundsCm = bounds;
    defaultNode.absoluteTranslationCm = translation;
    globalNode = &defaultNode;
}
RetailHeightTerrain::RetailHeightTerrain(const RetailHeightSceneNode& node,
                                         const CityNavigation& navigation)
    : RetailHeightTerrain(globalHeightMesh(node), navigation, node.absoluteTranslationCm) {
    validateScene(node);
    globalNode = &node;
}
void RetailHeightTerrain::validateScene(const RetailHeightSceneNode& root) const {
    std::set<const RetailHeightSceneNode*> active, complete;
    std::function<void(const RetailHeightSceneNode&)> validateNode = [&](const auto& node) {
        if (active.count(&node))
            throw std::invalid_argument("Cyclic retail height scene binding.");
        if (complete.count(&node))
            return;
        active.insert(&node);
        if (heightMeshType(node.type)) {
            if (!node.mesh || node.mesh->buffers.size() > visits.size())
                throw std::invalid_argument("Retail height scene mesh/navigation mismatch.");
            for (const auto& p : node.mesh->positions)
                for (float value : p)
                    if (!std::isfinite(value))
                        throw std::invalid_argument("Nonfinite retail height scene vertex.");
            for (size_t b = 0; b < node.mesh->buffers.size(); ++b) {
                const auto& indices = node.mesh->buffers[b].indices;
                if (indices.size() % 3 || indices.size() > 65535 || indices.size() / 3 > visits[b].size())
                    throw std::invalid_argument("Retail height scene indices/navigation mismatch.");
                for (auto index : indices)
                    if (index >= node.mesh->positions.size())
                        throw std::invalid_argument("Invalid retail height scene vertex identity.");
            }
        }
        for (const auto* child : node.children) {
            if (!child)
                throw std::invalid_argument("Null retail height scene child.");
            validateNode(*child);
        }
        active.erase(&node);
        complete.insert(&node);
    };
    validateNode(root);
}
void RetailHeightTerrain::validate(Ref ref) const {
    if (ref.buffer >= mesh.buffers.size() || ref.indexOffset % 3 ||
        size_t(ref.indexOffset) + 2 >= mesh.buffers[ref.buffer].indices.size())
        throw std::invalid_argument("Invalid retail height triangle identity.");
}
std::array<uint16_t, 3> RetailHeightTerrain::indices(Ref ref) const {
    validate(ref);
    const auto& v = mesh.buffers[ref.buffer].indices;
    return {v[ref.indexOffset], v[ref.indexOffset + 1], v[ref.indexOffset + 2]};
}
std::array<Point, 3> RetailHeightTerrain::vertices(Ref ref) const {
    return vertices(mesh, ref, globalNode->absoluteTranslationCm);
}
std::array<Point, 3> RetailHeightTerrain::vertices(const CityMesh& source, Ref ref, const Point& offset) const {
    validate(ref);
    if (ref.buffer >= source.buffers.size() ||
        size_t(ref.indexOffset) + 2 >= source.buffers[ref.buffer].indices.size())
        throw std::invalid_argument("Retail height selected-node triangle is unavailable.");
    const auto& ids = source.buffers[ref.buffer].indices;
    std::array<Point, 3> result;
    for (size_t i = 0; i < 3; ++i)
        for (size_t k = 0; k < 3; ++k)
            result[i][k] = source.positions.at(ids[ref.indexOffset + i])[k] + offset[k];
    return result;
}
void RetailHeightTerrain::loadGeometry(RetailHeightState& s, bool normalAndMarker,
                                      const CityMesh& source, const Point& offset) const {
    s.vertices = vertices(source, s.triangle, offset);
    if (normalAndMarker) {
        s.normal = triangleNormal(s.vertices);
        s.changed = true;
    }
}
bool RetailHeightTerrain::firstSeed(RetailHeightState& s, const Point& p) const {
    return firstSeedNode(s, p, *globalNode);
}
bool RetailHeightTerrain::firstSeedNode(RetailHeightState& s, const Point& p,
                                       const RetailHeightSceneNode& node) const {
    if (heightMeshType(node.type) && inHeightBox(p, node.boundsCm)) {
        for (size_t b = 0; b < node.mesh->buffers.size(); ++b) {
            // Original reads this node's absolute translation for each buffer scan.
            const auto offsetCm = node.absoluteTranslationCm;
            for (size_t offset = 0; offset < node.mesh->buffers[b].indices.size(); offset += 3) {
                if ((visits[b][offset / 3] & 3) == 1 || (visits[b][offset / 3] & 3) == 2)
                    continue;
                RetailHeightState candidate;
                candidate.triangle = {static_cast<uint16_t>(b), static_cast<uint16_t>(offset)};
                loadGeometry(candidate, false, *node.mesh, offsetCm);
                const Math m(candidate, p);
                if (m.side(0) < 0.f || m.side(1) < 0.f || m.side(2) < 0.f)
                    continue;
                s.triangle = candidate.triangle;
                s.hasHeightmap = true;
                s.selectedNode = &node;
                s.vertices = candidate.vertices;
                s.normal = triangleNormal(s.vertices);
                s.changed = true;
                s.cached = p;
                return true;
            }
        }
    }
    bool found = false;
    for (const auto* child : node.children)
        found = firstSeedNode(s, p, *child) || found;
    return found;
}

RetailHeightController::RetailHeightController(RetailHeightTerrain& terrain, b2World& world, size_t slots,
                                               b2Body* suppliedBody, RetailJumpController* jump)
    : terrain(terrain), world(world), suppliedBody(suppliedBody), jump(jump), states(slots), owned(slots) {
    if (!slots)
        throw std::invalid_argument("Retail height requires a slot.");
    for (size_t i = 0; i < terrain.pool.size(); ++i)
        if (!terrain.pool[i]) {
            poolIndex = static_cast<int>(i);
            terrain.pool[i] = true;
            break;
        }
    if (poolIndex < 0)
        throw std::runtime_error("Retail height pool exhausted.");
    for (size_t i = 1; i < slots; ++i)
        states[i].onGround = states[i].wasOnGround = false;
}
RetailHeightController::~RetailHeightController() {
    for (size_t i = 0; i < owned.size(); ++i)
        destroy(i);
    terrain.pool[poolIndex] = false;
}
void RetailHeightController::destroy(size_t slot) {
    auto& storage = owned.at(slot);
    // Native adaptation: keep metadata alive through DestroyBody/contact removal.
    for (auto* body : storage.bodies)
        world.DestroyBody(body);
    storage.bodies.clear();
    storage.metadata.clear();
}
void RetailHeightController::reset() {
    for (size_t i = 0; i < states.size(); ++i) {
        destroy(i);
        const auto cache = states[i].cached;
        states[i] = RetailHeightState{};
        states[i].cached = cache;
        states[i].onGround = states[i].wasOnGround = i == 0;
    }
}
void RetailHeightController::initializeSelected(size_t slot, Ref ref, const Point& point) {
    auto& s = states.at(slot);
    terrain.validate(ref);
    s.triangle = ref;
    s.hasHeightmap = true;
    s.selectedNode = terrain.globalNode;
    terrain.loadGeometry(s, true, terrain.mesh, terrain.globalNode->absoluteTranslationCm);
    s.cached = point;
    updateEdges(slot);
}
void RetailHeightController::initializeFromScene(size_t slot, const Point& point) {
    terrain.firstSeed(states.at(slot), point);
}
void RetailHeightController::initializeFromNode(size_t slot, const RetailHeightSceneNode& node,
                                               const Point& point) {
    terrain.validateScene(node);
    terrain.firstSeedNode(states.at(slot), point, node);
}
float RetailHeightController::setHeightAttributesFromPoint(size_t slot, const Point& point) {
    auto& s = states.at(slot);
    std::set<std::pair<uint16_t, uint16_t>> entries;
    for (;;) {
        terrain.validate(s.triangle);
        if (!entries.emplace(s.triangle.buffer, s.triangle.indexOffset).second)
            throw std::runtime_error("Nonterminating original retail height overlap recursion.");
        const auto offset = terrain.globalNode->absoluteTranslationCm;
        const auto& overlaps =
            terrain.navigation.buffers[s.triangle.buffer].overlaps[s.triangle.indexOffset / 3];
        bool restart = false;
        for (const auto candidate : overlaps) {
            const auto classification = terrain.visits[candidate.buffer][candidate.indexOffset / 3] & 3;
            if (classification == 1 || classification == 2)
                continue;
            RetailHeightState geometry;
            geometry.triangle = candidate;
            terrain.loadGeometry(geometry, false, terrain.mesh, offset);
            for (size_t edge = 0; edge < 3; ++edge) {
                const auto& a = geometry.vertices[edge];
                const auto& b = geometry.vertices[(edge + 1) % 3];
                const float side = (point[1] - a[1]) * (b[0] - a[0]) -
                                   (point[0] - a[0]) * (b[1] - a[1]);
                if (side < 0.f && intersects(s.cached, point, a, b)) {
                    s.triangle = candidate;
                    s.hasHeightmap = true;
                    s.selectedNode = terrain.globalNode;
                    restart = true;
                    break;
                }
            }
            if (restart)
                break;
            const auto& a = geometry.vertices[0];
            const auto& b = geometry.vertices[1];
            const auto& c = geometry.vertices[2];
            const float inverse =
                1.f / ((b[0] - a[0]) * (-(a[1] - c[1])) + (b[1] - a[1]) * (a[0] - c[0]));
            const float wb =
                ((point[0] - c[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (point[1] - c[1])) * inverse;
            s.triangle = candidate;
            s.hasHeightmap = true;
            s.selectedNode = terrain.globalNode;
            s.vertices = geometry.vertices;
            s.normal = triangleNormal(s.vertices);
            s.changed = true;
            const float base = (b[2] - a[2]) * wb + a[2];
            const float wa =
                ((point[0] - b[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (point[1] - b[1])) * inverse;
            return base - (a[2] - c[2]) * ((1.f - wb) - wa);
        }
        if (!restart)
            return noHeight;
    }
}
void RetailHeightController::setGroundState(size_t slot, bool value) {
    auto& s = states.at(slot);
    s.wasOnGround = s.onGround;
    s.onGround = value;
}
float RetailHeightController::height(const Point& point, size_t slot, bool detailed) {
    auto& s = states.at(slot);
    if (s.cached[2] == noHeight)
        s.cached = point;
    s.changed = false;
    if (!s.hasHeightmap)
        return s.cached[2];
    terrain.currentUsed = static_cast<uint16_t>(terrain.currentUsed + 4);
    const float result = core(slot, point, true, detailed);
    if (result != noHeight) {
        s.cached = point;
        s.cached[2] = jump ? jump->selectSuccessfulTerrainHeight(point, result, slot) : result;
    }
    return s.cached[2];
}
float RetailHeightController::core(size_t slot, const Point& p, bool crossing, bool detailed) {
    auto& s = states.at(slot);
    // Every original recursive call is a tail return. This loop avoids native
    // stack exhaustion without changing generation, state or query ordering.
    for (;;) {
        Math math(s, p);
        terrain.validate(s.triangle);
        auto& marked = terrain.visits[s.triangle.buffer][s.triangle.indexOffset / 3];
        marked = static_cast<uint16_t>((marked & 3) | terrain.currentUsed);
        bool restart = false;
        for (size_t edge = 0; edge < 3; ++edge) {
            if (!(math.side(edge) < 0.f))
                continue;
            if (crossing && !intersects(s.cached, p, s.vertices[edge], s.vertices[(edge + 1) % 3]))
                continue;
            const auto source = s.triangle;
            if (!s.selectedNode || !s.selectedNode->mesh)
                throw std::logic_error("Retail height core requires a live selected mesh node.");
            const auto* sourceNode = s.selectedNode;
            const auto sourceTranslation = sourceNode->absoluteTranslationCm;
            const auto& data = terrain.navigation.buffers[source.buffer];
            const size_t t = source.indexOffset / 3;
            const auto offset = data.neighbors[edge][t], buffer = data.neighbors[edge + 3][t];
            if (offset != UINT16_MAX && !rejected(terrain.visits[buffer][offset / 3], terrain.currentUsed)) {
                s.triangle = {buffer, offset};
                s.hasHeightmap = true;
                s.selectedNode = terrain.globalNode;
                // Original switches identity/mesh but retains the old node's
                // translation captured before evaluating this edge's neighbor.
                terrain.loadGeometry(s, true, terrain.mesh, sourceTranslation);
                detailed = false;
                restart = true;
                break;
            }
            const auto& overlaps = data.overlaps[t];
            // Original phase excludes the final stored pair (used counts words).
            for (size_t i = 0; i + 1 < overlaps.size(); ++i) {
                s.triangle = overlaps[i];
                if (rejected(terrain.visits[s.triangle.buffer][s.triangle.indexOffset / 3],
                             terrain.currentUsed))
                    continue;
                terrain.loadGeometry(s, false, *sourceNode->mesh, sourceTranslation);
                math = Math(s, p);
                bool segmentCrosses = false;
                for (size_t e = 0; e < 3; ++e)
                    if (intersects(s.cached, p, s.vertices[e], s.vertices[(e + 1) % 3])) {
                        segmentCrosses = true;
                        break;
                    }
                if (segmentCrosses || (math.side(0) >= 0.f && math.side(1) >= 0.f && math.side(2) >= 0.f)) {
                    crossing = segmentCrosses;
                    detailed = false;
                    restart = true;
                    break;
                }
            }
            if (restart)
                break;
            if (slot == 0)
                terrain.firstSeed(s, p);
            // Literal mixed reseed: math still belongs to the last fetched
            // geometry, although info and cached point may now have changed.
        }
        if (restart)
            continue;
        if (s.changed || (s.onGround && !s.wasOnGround))
            updateEdges(slot);
        const float inverse = 1.f / ((-math.ayCy) * math.bxAx + math.axCx * math.byAy);
        const auto& a = s.vertices[0];
        const auto& b = s.vertices[1];
        const auto& c = s.vertices[2];
        const float wa = ((p[0] - b[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (p[1] - b[1])) * inverse;
        const auto fail = [&]() {
            if (slot == 0 || !detailed)
                terrain.firstSeed(s, p);
            return noHeight;
        };
        if (wa < 0.f || wa > 1.f)
            return fail();
        const float wb = ((p[0] - c[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (p[1] - c[1])) * inverse;
        if (wb < 0.f || wb > 1.f)
            return fail();
        const float wc = (1.f - wb) - wa;
        if (wc < 0.f || wc > 1.f)
            return fail();
        return (wb * math.bzAz + a[2]) - wc * math.azCz;
    }
}

void RetailHeightController::updateEdges(size_t slot) {
    const auto& state = states.at(slot);
    if (!state.onGround)
        return;
    if (!state.hasHeightmap)
        throw std::logic_error("Local cliffs require selected heightmap state.");
    struct Strip {
        Point start, delta, normal;
        float lower, upper, width;
        Ref face;
        uint16_t key;
        size_t edge;
    };
    std::vector<Strip> strips;
    const auto cliff = static_cast<uint16_t>(
        terrain.navigation.materialBuffers[static_cast<size_t>(CityNavigation::Material::cliff)]);
    const auto addKey = [&](uint16_t key) {
        const auto start = terrain.navigation.cliffVertexKeys[size_t(key) * 2],
                   count = terrain.navigation.cliffVertexKeys[size_t(key) * 2 + 1];
        for (size_t map = start; map < size_t(start) + count; ++map) {
            const auto offset = terrain.navigation.cliffTriangles[map];
            if (offset == UINT16_MAX)
                continue;
            const Ref face{cliff, offset};
            const auto ids = terrain.indices(face);
            const auto vv = terrain.vertices(face);
            const auto found = std::find(ids.begin(), ids.end(), key);
            const size_t k = static_cast<size_t>(found - ids.begin());
            const std::array<Point, 3> v{vv[k], vv[(k + 1) % 3], vv[(k + 2) % 3]};
            const auto n = triangleNormal(v);
            const float nz = n[2] * 0.f;
            const float side = (n[0] * (state.cached[0] - v[0][0]) + n[1] * (state.cached[1] - v[0][1])) + nz;
            if (side < 0.f)
                continue;
            if (suppliedBody) {
                const auto velocity = suppliedBody->GetLinearVelocity();
                if ((n[0] * velocity.x + n[1] * velocity.y) + nz > 0.f)
                    continue;
            }
            for (size_t e = 0; e < 2; ++e) {
                const size_t lane = e == 0 ? k : (k + 2) % 3;
                if (terrain.navigation.buffers[cliff].neighbors[lane + 3][offset / 3] == cliff)
                    continue;
                const auto& anchor = e == 0 ? v[1] : v[0];
                const auto& end = e == 0 ? v[0] : v[2];
                bool duplicate = false;
                for (size_t other = 0; other < owned.size(); ++other)
                    if (other != slot)
                        for (auto* body : owned[other].bodies)
                            if (body->GetPosition().x == anchor[0] / 100.f &&
                                body->GetPosition().y == anchor[1] / 100.f)
                                duplicate = true;
                for (const auto& previous : strips)
                    if (previous.start[0] / 100.f == anchor[0] / 100.f &&
                        previous.start[1] / 100.f == anchor[1] / 100.f)
                        duplicate = true;
                if (duplicate)
                    continue;
                const auto delta = subtract(end, anchor);
                const auto normal = normalize({-delta[1], delta[0], 0});
                if (!((delta[0] * delta[0] + delta[1] * delta[1]) / 10000.f > 0.0064f))
                    continue;
                if (!((n[0] * normal[0] + n[1] * normal[1]) + n[2] * normal[2] > 0.f))
                    continue;
                const float lower = std::min({v[0][2], v[1][2], v[2][2]}),
                            upper = std::max({v[0][2], v[1][2], v[2][2]});
                strips.push_back(
                    {anchor, delta, normal, lower, upper, states.size() > 1 ? 100.f : 10.f, face, key, e});
                // Reviewed safety adaptation: preflight all strips before touching
                // existing bodies. Never allocate an untracked thirteenth body.
                if (strips.size() > 12)
                    throw std::runtime_error("Retail height local cliff capacity exceeded.");
            }
        }
    };
    const auto current = terrain.indices(state.triangle);
    for (auto key : current)
        addKey(key);
    const auto& lanes = terrain.navigation.buffers[state.triangle.buffer].neighbors;
    for (size_t e = 0; e < 3; ++e) {
        const auto offset = lanes[e][state.triangle.indexOffset / 3],
                   buffer = lanes[e + 3][state.triangle.indexOffset / 3];
        if (offset == UINT16_MAX || buffer == UINT16_MAX)
            continue;
        for (auto key : terrain.indices({buffer, offset}))
            if (std::find(current.begin(), current.end(), key) == current.end())
                addKey(key);
    }
    const auto polygonFor = [&](const Strip& strip) {
        b2PolygonDef polygon;
        polygon.vertexCount = 4;
        polygon.friction = 1;
        polygon.restitution = 0;
        polygon.density = 0;
        polygon.isSensor = false;
        polygon.filter.categoryBits = 0;
        polygon.filter.maskBits = 0xffff;
        polygon.filter.groupIndex = static_cast<int16>(groupIndex());
        polygon.vertices[0].Set((-strip.normal[0]) * strip.width / 100.f,
                                (-strip.normal[1]) * strip.width / 100.f);
        polygon.vertices[1].Set((strip.delta[0] - strip.width * strip.normal[0]) / 100.f,
                                (strip.delta[1] - strip.width * strip.normal[1]) / 100.f);
        polygon.vertices[2].Set(strip.delta[0] / 100.f, strip.delta[1] / 100.f);
        polygon.vertices[3].Set(0, 0);
        return polygon;
    };
    // Reviewed safety adaptation: upstream Release would silently retain a
    // shape with no proxy; Debug asserts. Ask the actual world before replacing
    // existing bodies, using the actual native polygon's OBB, not corner bounds.
    b2BlockAllocator probeAllocator;
    for (const auto& strip : strips) {
        const auto polygon = polygonFor(strip);
        auto* probe = ShapeProbe::Create(&polygon, &probeAllocator);
        if (!probe)
            throw std::runtime_error("Could not allocate retail cliff bounds probe.");
        b2XForm transform;
        transform.position.Set(strip.start[0] / 100.f, strip.start[1] / 100.f);
        transform.R.SetIdentity();
        b2AABB aabb;
        probe->ComputeAABB(&aabb, transform);
        ShapeProbe::Destroy(probe, &probeAllocator);
        if (!world.InRange(aabb)) {
            std::ostringstream message;
            message << "Retail height cliff outside world proxy range: selected " << state.triangle.buffer
                    << ':' << state.triangle.indexOffset << ", cliff " << strip.face.buffer << ':'
                    << strip.face.indexOffset << ", key " << strip.key << ", incident " << strip.edge
                    << ", anchor cm (" << strip.start[0] << ',' << strip.start[1] << "), AABB m ("
                    << aabb.lowerBound.x << ',' << aabb.lowerBound.y << ")-(" << aabb.upperBound.x << ','
                    << aabb.upperBound.y << ").";
            throw std::runtime_error(message.str());
        }
    }
    destroy(slot);
    auto& storage = owned[slot];
    storage.metadata.reserve(strips.size());
    storage.bodies.reserve(strips.size());
    try {
        for (const auto& strip : strips) {
            auto metadata = std::make_unique<RetailPhysicsBodyData>(
                RetailPhysicsBodyData{nullptr, std::nullopt, {strip.lower, strip.upper, 0, 0}, false});
            b2BodyDef body;
            body.position.Set(strip.start[0] / 100.f, strip.start[1] / 100.f);
            body.userData = metadata.get();
            auto* created = world.CreateBody(&body);
            if (!created)
                throw std::runtime_error("Could not allocate retail cliff body.");
            storage.metadata.push_back(std::move(metadata));
            storage.bodies.push_back(created);
            auto polygon = polygonFor(strip);
            if (!created->CreateShape(&polygon))
                throw std::runtime_error("Could not allocate retail cliff shape.");
        }
    } catch (...) {
        destroy(slot);
        throw;
    }
}
