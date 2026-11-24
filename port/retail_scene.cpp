#include "retail_scene.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

RetailSceneMatrix retailSceneRelative(const CityNode& node, bool omitScale) {
    const auto& q = node.rotation;
    const float x2 = q[0] + q[0], y2 = q[1] + q[1], z2 = q[2] + q[2];
    const float xx = q[0] * x2, yy = q[1] * y2, zz = q[2] * z2;
    const float xy = x2 * q[1], xz = x2 * q[2], xw = x2 * q[3];
    const float yz = y2 * q[2], yw = y2 * q[3], zw = z2 * q[3];
    RetailSceneMatrix m{(1.f - yy) - zz,
                        xy - zw,
                        xz + yw,
                        0,
                        xy + zw,
                        (1.f - xx) - zz,
                        yz - xw,
                        0,
                        xz - yw,
                        yz + xw,
                        (1.f - yy) - xx,
                        0,
                        0,
                        0,
                        0,
                        1};
    // AddXRef copies position and rotation to the new scene root, not scale.
    if (!omitScale) {
        constexpr float tolerance = 0.000001f;
        bool unit = true;
        for (float scale : node.scale)
            unit = unit && 1.f <= scale + tolerance && 1.f >= scale - tolerance;
        if (!unit)
            for (size_t column = 0; column < 3; ++column)
                for (size_t row = 0; row < 3; ++row)
                    m[column * 4 + row] *= node.scale[column];
    }
    std::copy(node.translation.begin(), node.translation.end(), m.begin() + 12);
    return m;
}
RetailSceneMatrix retailSceneMultiply(const RetailSceneMatrix& a, const RetailSceneMatrix& b) {
    RetailSceneMatrix result{};
    for (size_t column = 0; column < 3; ++column)
        for (size_t row = 0; row < 3; ++row)
            result[column * 4 + row] =
                (a[row] * b[column * 4] + a[4 + row] * b[column * 4 + 1]) + a[8 + row] * b[column * 4 + 2];
    for (size_t row = 0; row < 3; ++row)
        result[12 + row] = ((a[row] * b[12] + a[4 + row] * b[13]) + a[8 + row] * b[14]) + a[12 + row];
    result[15] = 1;
    return result;
}

RetailSceneQuaternion retailSceneNormalizeQuaternion(RetailSceneQuaternion q) {
    const float length = ((q[0] * q[0] + q[1] * q[1]) + q[2] * q[2]) + q[3] * q[3];
    if (length != 1.f) {
        const float inverse = 1.f / std::sqrt(length);
        for (float& value : q)
            value *= inverse;
    }
    return q;
}
RetailSceneQuaternion retailSceneYawQuaternion(float angle) {
    // Original Euler set(0,0,angle), double trig/products then float normalize.
    const double sx = std::sin(0.), cx = std::cos(0.), sy = std::sin(0.), cy = std::cos(0.);
    const double sz = std::sin(double(angle) * .5), cz = std::cos(double(angle) * .5);
    const double cycz = cy * cz, sycz = sy * cz, cysz = cy * sz, sysz = sy * sz;
    RetailSceneQuaternion q{float(sx * cycz - cx * sysz), float(cx * sycz + sx * cysz),
                            float(cx * cysz - sx * sycz), float(cx * cycz + sx * sysz)};
    return retailSceneNormalizeQuaternion(q);
}

uint32_t retailSceneMeshRuntimeType(const std::string& databasePath) {
    for (const char* marker : {"car_light", "light_park", "light_street", "stage_light", "sign"})
        if (databasePath.find(marker) != std::string::npos)
            return retailSceneEmptyType;
    return retailSceneMeshType;
}

struct RetailHeightSceneBinding::Node {
    RetailHeightSceneNode view;
    RetailSceneMatrix absolute;
};
RetailHeightSceneBinding::Node& RetailHeightSceneBinding::add(uint32_t type,
                                                              const RetailSceneMatrix& absolute, Node* parent,
                                                              const CityMesh* mesh) {
    auto owned = std::make_unique<Node>();
    owned->absolute = absolute;
    owned->view.type = type;
    owned->view.mesh = mesh;
    std::copy_n(absolute.begin() + 12, 3, owned->view.absoluteTranslationCm.begin());
    if (mesh) {
        if (!mesh->rawBoundsCm)
            throw std::runtime_error("Missing genuine raw SMesh box for height scene binding.");
        owned->view.boundsCm = *mesh->rawBoundsCm;
    }
    auto& node = *owned;
    nodes.push_back(std::move(owned));
    if (parent)
        parent->view.children.push_back(&node.view);
    return node;
}
RetailHeightSceneBinding::RetailHeightSceneBinding(CityScene source) : scene(std::move(source)) {
    if (scene.path.empty())
        throw std::invalid_argument("Height scene requires its original database path.");
    if (scene.hasSceneAnimations)
        throw std::runtime_error("Unsupported animated height-scene factory binding.");
    rootNode = &add(0x72656164, retailSceneIdentity, nullptr);
    size_t covered = 0;
    for (const auto& visual : scene.visualScenes) {
        if (visual.firstNode != covered || visual.nodeCount > scene.nodes.size() - covered)
            throw std::runtime_error("Invalid decoded active visual-scene node ranges.");
        const auto end = covered + visual.nodeCount;
        auto& visualNode =
            add(0x6e656164, retailSceneMultiply(rootNode->absolute, retailSceneIdentity), rootNode);
        std::vector<std::vector<size_t>> children(visual.nodeCount);
        std::vector<unsigned> depths(visual.nodeCount);
        std::vector<size_t> roots;
        for (size_t i = covered; i < end; ++i) {
            const auto parent = scene.nodes[i].parent;
            if (parent == -1)
                roots.push_back(i);
            else {
                if (parent < 0 || size_t(parent) < covered || size_t(parent) >= i)
                    throw std::runtime_error("Invalid decoded height-scene source hierarchy.");
                depths[i - covered] = depths[size_t(parent) - covered] + 1;
                if (depths[i - covered] > 64)
                    throw std::runtime_error("Invalid decoded height-scene source depth.");
                children[size_t(parent) - covered].push_back(i);
            }
        }
        std::function<void(size_t, Node&)> build = [&](size_t index, Node& parent) {
            const auto& authored = scene.nodes[index];
            if (authored.factorySelector)
                throw std::runtime_error("Unsupported original billboard height-scene factory.");
            auto& container =
                add(0x6e656164, retailSceneMultiply(parent.absolute, retailSceneRelative(authored)), &parent);
            std::vector<bool> boundGeometry(authored.geometry.size());
            for (const auto& instance : authored.instances) {
                if (instance.kind == 3 && instance.geometry) {
                    const auto& ref = authored.geometry.at(*instance.geometry);
                    if (boundGeometry.at(*instance.geometry))
                        throw std::runtime_error("Duplicate height-scene instance identity.");
                    boundGeometry[*instance.geometry] = true;
                    if (!ref.sourcePath.empty())
                        throw std::runtime_error("Unsupported external height-scene mesh factory.");
                    const auto mesh =
                        std::find_if(scene.meshes.begin(), scene.meshes.end(),
                                     [&](const CityMesh& value) { return value.name == ref.meshName; });
                    if (mesh == scene.meshes.end())
                        throw std::runtime_error("Missing height-scene mesh identity.");
                    add(retailSceneMeshRuntimeType(scene.path),
                        retailSceneMultiply(container.absolute, retailSceneIdentity), &container, &*mesh);
                } else if (instance.kind == 3) {
                    if (!instance.localSpline || !instance.sourcePath.empty())
                        throw std::runtime_error("Missing original height-scene geometry instance binding.");
                } else if (instance.kind == 4) {
                    // Local name lookup can legitimately return NULL; no generated light in that branch.
                    if (std::find(scene.lightUids.begin(), scene.lightUids.end(), instance.target) !=
                        scene.lightUids.end())
                        add(0x7468676c, retailSceneMultiply(container.absolute, retailSceneIdentity),
                            &container);
                } else if (instance.kind == 1 || instance.kind == 2 ||
                           (instance.kind >= 9 && instance.kind <= 11))
                    throw std::runtime_error("Unsupported original height-scene instance factory kind " +
                                             std::to_string(instance.kind));
                // Source kind3 spline returns NULL; 0/5/6/7/8/>11 are ignored by original switch.
            }
            if (std::find(boundGeometry.begin(), boundGeometry.end(), false) != boundGeometry.end())
                throw std::runtime_error("Missing original height-scene instance metadata.");
            for (const auto child : children[index - covered])
                build(child, container);
        };
        for (const auto index : roots)
            build(index, visualNode);
        covered = end;
    }
    if (covered != scene.nodes.size())
        throw std::runtime_error("Missing decoded active visual-scene grouping.");
    // CreateNeighborTris: ordered preorder, first recognized mesh retained; no nearest-Z selection.
    std::function<void(const RetailHeightSceneNode&)> select = [&](const auto& node) {
        if (!selected && (node.type == retailSceneMeshType || node.type == retailSceneSkinnedMeshType))
            selected = &node;
        for (const auto* child : node.children)
            select(*child);
    };
    select(rootNode->view);
    if (!selected)
        throw std::runtime_error("Original height scene has no recognized mesh node.");
}
RetailHeightSceneBinding::~RetailHeightSceneBinding() = default;
const RetailHeightSceneNode& RetailHeightSceneBinding::root() const {
    return rootNode->view;
}
const RetailHeightSceneNode& RetailHeightSceneBinding::globalHeightmap() const {
    return *selected;
}
const RetailSceneMatrix&
RetailHeightSceneBinding::absoluteTransform(const RetailHeightSceneNode& view) const {
    for (const auto& node : nodes)
        if (&node->view == &view)
            return node->absolute;
    throw std::invalid_argument("Height scene node belongs to another binding.");
}
std::unique_ptr<RetailHeightSceneBinding> loadOriginalHeightScene(FileSystemBase& assets) {
    return std::make_unique<RetailHeightSceneBinding>(loadCityScene(assets, "./santa_monica_heightmap.bdae"));
}
