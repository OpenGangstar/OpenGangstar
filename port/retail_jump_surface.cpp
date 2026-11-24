#include "retail_jump_surface.h"

#include "Common/b2BlockAllocator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace {
using Point = std::array<float, 3>;
using Quaternion = std::array<float, 4>;
constexpr float radians = 0.01745329252f;
struct ShapeProbe : b2Shape {
    using b2Shape::Create;
    using b2Shape::Destroy;
};
float rotationZDegrees(const RetailJumpMatrix& m) {
    const double y = -std::asin(double(m[2]));
    const double cosine = std::cos(y);
    double z;
    if (std::abs(cosine) > 1e-8) {
        const float inverse = static_cast<float>(1. / cosine);
        z = std::atan2(double(inverse * m[1]), double(inverse * m[0])) * 57.29577951308232;
    } else
        z = std::atan2(double(-m[4]), double(m[5])) * 57.29577951308232;
    if (z < 0.)
        z += 360.;
    return static_cast<float>(z);
}
Point rotate(const Quaternion& q, const Point& v) {
    // Original quaternion-vector product uses the negative cross orientation.
    const Point uv{(-q[1]) * v[2] + q[2] * v[1], (-q[2]) * v[0] + q[0] * v[2], (-q[0]) * v[1] + q[1] * v[0]};
    const Point uuv{(-q[1]) * uv[2] + q[2] * uv[1], (-q[2]) * uv[0] + q[0] * uv[2],
                    (-q[0]) * uv[1] + q[1] * uv[0]};
    const float twiceW = q[3] + q[3];
    return {(uuv[0] + uuv[0]) + (v[0] + uv[0] * twiceW), (uuv[1] + uuv[1]) + (v[1] + uv[1] * twiceW),
            (uuv[2] + uuv[2]) + (v[2] + uv[2] * twiceW)};
}
Point linear(const RetailJumpMatrix& m, const Point& v) {
    return {(v[0] * m[0] + v[1] * m[4]) + v[2] * m[8], (v[0] * m[1] + v[1] * m[5]) + v[2] * m[9],
            (v[0] * m[2] + v[1] * m[6]) + v[2] * m[10]};
}
const CityMesh& meshFor(const CityScene& scene, const std::string& name) {
    const auto found = std::find_if(scene.meshes.begin(), scene.meshes.end(),
                                    [&](const CityMesh& mesh) { return mesh.name == name; });
    if (found == scene.meshes.end())
        throw std::runtime_error("Missing original jump binding mesh: " + name);
    return *found;
}
bool marker(const std::string& uid, const char* name) {
    return uid.find(name) != std::string::npos;
}
} // namespace

std::vector<RetailJumpSurfaceSource> collectRetailJumpSurfaceSources(const CityAssets& city,
                                                                     const CityScene& scene) {
    std::vector<RetailJumpSurfaceSource> sources;
    for (size_t instanceIndex = 0; instanceIndex < scene.nodes.size(); ++instanceIndex) {
        const auto& instance = scene.nodes[instanceIndex];
        for (size_t referenceIndex = 0; referenceIndex < instance.geometry.size(); ++referenceIndex) {
            const auto& reference = instance.geometry[referenceIndex];
            if (reference.sourcePath.empty())
                continue;
            const auto found = city.geometryLibraries.find(reference.sourcePath);
            if (found == city.geometryLibraries.end())
                throw std::runtime_error("Missing original jump binding library: " + reference.sourcePath);
            const auto& model = found->second;
            // These factories can construct different recognized node classes. In
            // particular the skin getter is fixed 0x73656164 even for a sign path.
            // Reject before applying the ordinary mesh's database-path predicate.
            for (const auto& node : model.nodes)
                for (const auto& authored : node.instances)
                    if (authored.kind == 1 || authored.kind == 2 || authored.kind == 11)
                        throw std::runtime_error(
                            "Unsupported node/controller/modular-skin authored jump factory.");
            const bool hasJump = std::any_of(model.meshes.begin(), model.meshes.end(),
                                             [](const CityMesh& mesh) { return marker(mesh.name, "Jump"); });
            if (!hasJump)
                continue;
            if (instance.parent >= 0 || readCityLodGroup(model).levels.size() > 1)
                throw std::runtime_error("Unsupported parent/LOD authored jump scene binding: " +
                                         reference.sourcePath);
            for (const auto& node : model.nodes) {
                if (node.factorySelector)
                    throw std::runtime_error("Unsupported alternate billboard authored jump factory.");
                if (const auto value = node.property("billboard");
                    value && std::atoi(std::string(*value).c_str()) == 1)
                    throw std::runtime_error("Unsupported billboard authored jump owner binding.");
                for (const auto& authored : node.instances)
                    if (authored.kind == 9 || authored.kind == 10)
                        throw std::runtime_error("Unsupported particle/force authored jump scene binding.");
            }
            const auto runtimeType = retailSceneMeshRuntimeType(model.path);
            if (runtimeType != retailSceneMeshType)
                continue;
            const auto root = retailSceneRelative(instance, true);
            std::vector<RetailJumpMatrix> matrices;
            for (size_t n = 0; n < model.nodes.size(); ++n) {
                const auto& node = model.nodes[n];
                if (node.parent >= static_cast<int32_t>(n) || node.parent < -1)
                    throw std::runtime_error("Invalid original jump binding hierarchy.");
                const auto& parent = node.parent >= 0 ? matrices.at(node.parent) : root;
                matrices.push_back(retailSceneMultiply(parent, retailSceneRelative(node)));
            }
            size_t visible = model.nodes.size();
            for (size_t n = 0; n < model.nodes.size() && visible == model.nodes.size(); ++n) {
                const auto& node = model.nodes[n];
                for (const auto& geometry : node.geometry)
                    if (geometry.sourcePath.empty()) {
                        const auto& mesh = meshFor(model, geometry.meshName);
                        if (marker(mesh.name, "Plane") || marker(mesh.name, "Jump"))
                            continue;
                        visible = n;
                        break;
                    }
            }
            if (visible == model.nodes.size())
                throw std::runtime_error("Missing original visible owner for authored jump.");
            const auto owner = scene.path + "#" + std::to_string(instanceIndex) + "/ref" +
                               std::to_string(referenceIndex) + "/" + reference.sourcePath + "#" +
                               std::to_string(visible);
            for (size_t n = 0; n < model.nodes.size(); ++n) {
                const auto& node = model.nodes[n];
                for (size_t g = 0; g < node.geometry.size(); ++g) {
                    const auto& geometry = node.geometry[g];
                    if (!geometry.sourcePath.empty())
                        throw std::runtime_error("Unsupported nested authored jump library binding.");
                    const auto& mesh = meshFor(model, geometry.meshName);
                    if (!marker(mesh.name, "Jump"))
                        continue;
                    if (mesh.buffers.empty())
                        throw std::runtime_error("Missing first original jump mesh buffer.");
                    sources.push_back({scene.path + "#" + std::to_string(instanceIndex) + "/ref" +
                                           std::to_string(referenceIndex) + "/" + reference.sourcePath + "#" +
                                           std::to_string(n) + "/geometry" + std::to_string(g),
                                       owner, mesh.name, mesh.positions,
                                       // Factory-created geometry children have identity local TRS,
                                       // but original mult34 still performs the float additions.
                                       // Preserve their signed-zero effects separately from containers.
                                       retailSceneMultiply(matrices[n], retailSceneIdentity),
                                       retailSceneMultiply(matrices[visible], retailSceneIdentity),
                                       runtimeType});
                }
            }
        }
    }
    return sources;
}

std::uint8_t RetailJumpSurfaceSequence::consume() {
    const auto id = next;
    next = static_cast<uint8_t>(unsigned(next) + 1u);
    return id;
}
RetailJumpSurfaceCatalog::RetailJumpSurfaceCatalog(RetailJumpSurfaceSequence& sequence)
    : sequence(sequence) {}
RetailJumpSurfaceCatalog::~RetailJumpSurfaceCatalog() {
    deactivate();
}

bool RetailJumpSurfaceCatalog::append(const RetailJumpSurfaceSource& source) {
    if (world)
        throw std::logic_error("Append original jump definitions only while inactive.");
    if (source.runtimeType != retailSceneMeshType && source.runtimeType != retailSceneSkinnedMeshType)
        return false;
    if (!marker(source.uid, "Jump") || source.verticesCm.size() != 4)
        return false;
    if (source.ownerIdentity.empty() || (!entries.empty() && ownerIdentity != source.ownerIdentity))
        throw std::invalid_argument("An original jump catalog requires one explicit scene owner.");
    for (const auto& matrix : {source.currentAbsolute, source.placementAbsolute})
        for (float value : matrix)
            if (!std::isfinite(value))
                throw std::invalid_argument("Nonfinite original jump transform.");
    for (const auto& vertex : source.verticesCm)
        for (float value : vertex)
            if (!std::isfinite(value))
                throw std::invalid_argument("Nonfinite original jump vertex.");
    auto entry = std::make_unique<RetailJumpSurfaceDefinition>();
    entry->identity = source.identity;
    entry->ownerIdentity = source.ownerIdentity;
    entry->body.position.Set(source.currentAbsolute[12] / 100.f, source.currentAbsolute[13] / 100.f);
    entry->body.angle = rotationZDegrees(source.currentAbsolute) * radians;
    if (!std::isfinite(entry->body.angle))
        throw std::runtime_error("Unsupported nonfinite original jump rotation.");
    size_t first = 0;
    for (; first < 4; ++first)
        if (source.verticesCm[first][0] < 0.f && source.verticesCm[first][1] < 50.f)
            break;
    std::array<Point, 4> transformed;
    for (size_t i = 0; i < 4; ++i)
        transformed[i] = linear(source.placementAbsolute, source.verticesCm[(first + i) & 3]);
    for (size_t i = 0; i < 4; ++i)
        entry->metadata.heightsCm[i] = source.placementAbsolute[14] + transformed[i][2];
    entry->metadata.opaqueJumpFlag = sequence.consume();
    entry->body.userData = &entry->metadata;
    entry->polygon.vertexCount = 4;
    entry->polygon.density = 1.f;
    entry->polygon.restitution = 0.f;
    entry->polygon.filter.categoryBits = 1;
    entry->polygon.filter.maskBits = 0xffff;
    entry->polygon.filter.groupIndex = 0;
    const auto q = retailSceneYawQuaternion(entry->body.angle);
    for (size_t i = 0; i < 4; ++i) {
        const auto point = rotate(q, transformed[i]);
        entry->polygon.vertices[i].Set(point[0] / 100.f, point[1] / 100.f);
    }
    entries.push_back(std::move(entry));
    ownerIdentity = source.ownerIdentity;
    return true;
}

void RetailJumpSurfaceCatalog::activate(b2World& target) {
    if (world) {
        if (world != &target)
            throw std::logic_error("Deactivate original jump catalog before changing worlds.");
        return;
    }
    b2BlockAllocator allocator;
    for (const auto& entry : entries) {
        auto* probe = ShapeProbe::Create(&entry->polygon, &allocator);
        if (!probe)
            throw std::runtime_error("Could not allocate original jump polygon probe.");
        b2XForm transform;
        transform.position = entry->body.position;
        transform.R.Set(entry->body.angle);
        b2AABB bounds;
        probe->ComputeAABB(&bounds, transform);
        ShapeProbe::Destroy(probe, &allocator);
        if (!target.InRange(bounds))
            throw std::runtime_error("Authored jump polygon outside world proxy range: " + entry->identity);
    }
    liveBodies.reserve(entries.size());
    world = &target;
    try {
        for (const auto& entry : entries) {
            auto* body = target.CreateBody(&entry->body);
            if (!body)
                throw std::runtime_error("Could not allocate original jump body.");
            liveBodies.push_back(body);
            if (!body->CreateShape(&entry->polygon))
                throw std::runtime_error("Could not allocate original jump polygon.");
        }
    } catch (...) {
        deactivate();
        throw;
    }
}
void RetailJumpSurfaceCatalog::deactivate() {
    if (!world)
        return;
    for (auto*& body : liveBodies) {
        world->DestroyBody(body);
        body = nullptr;
    }
    liveBodies.clear();
    world = nullptr;
}
