#include "retail_scene.h"
#include "filesystem.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
uint32_t bits(float value) {
    uint32_t out;
    std::memcpy(&out, &value, 4);
    return out;
}
void close(float a, float b, const char* message) {
    require(std::abs(a - b) < .0001f, message);
}
void sharedQuaternionMath(bool emit) {
    const std::array<RetailSceneQuaternion, 10> inputs{{{1, 0, 0, 0},
                                                        {2, 0, 0, 0},
                                                        {-2, 3, 4, -5},
                                                        {.125f, .25f, .375f, .875f},
                                                        {.5f, .5f, .5f, .5f},
                                                        {-0.f, 0, -0.f, 1},
                                                        {.6f, 0, 0, .8f},
                                                        {.6f, .6f, .6f, .6f},
                                                        {3e15f, 4e15f, 0, 0},
                                                        {1e-15f, 2e-15f, -3e-15f, 4e-15f}}};
    require(retailSceneNormalizeQuaternion(inputs[1]) == RetailSceneQuaternion{1, 0, 0, 0},
            "Original standalone quaternion normalization");
    const auto untouched = retailSceneNormalizeQuaternion(inputs[5]);
    for (size_t i = 0; i < 4; ++i)
        require(bits(untouched[i]) == bits(inputs[5][i]),
                "Exact unit shortcut preserves signed-zero quaternion fields");
    const auto zero = retailSceneNormalizeQuaternion({0, 0, 0, 0});
    for (float value : zero)
        require(std::isnan(value), "Original zero quaternion is not replaced with invented identity");
    const auto dump = [&](const char* operation, const RetailSceneQuaternion& input, float angle,
                          const RetailSceneQuaternion& result) {
        if (!emit)
            return;
        std::cout << "math {\"operation\":" << std::quoted(operation) << ",\"angle\":" << bits(angle)
                  << ",\"input\":[";
        for (size_t i = 0; i < 4; ++i) {
            if (i)
                std::cout << ',';
            std::cout << bits(input[i]);
        }
        std::cout << "],\"result\":[";
        for (size_t i = 0; i < 4; ++i) {
            if (i)
                std::cout << ',';
            std::cout << bits(result[i]);
        }
        std::cout << "]}\n";
    };
    for (const auto& input : inputs)
        dump("normalize", input, 0, retailSceneNormalizeQuaternion(input));
    for (float angle : {0.f, .125f, -.625f, 1.5707963267948966f, 3.14159265358979323846f,
                        -3.14159265358979323846f, 10000.f, 1e20f})
        dump("yaw", {}, angle, retailSceneYawQuaternion(angle));
    CityNode raw{};
    raw.rotation = {0, 0, 1, 1};
    raw.scale = {1, 1, 1};
    require(retailSceneRelative(raw)[0] == -1 && retailSceneRelative(raw)[1] == -2,
            "Raw authored SNode quaternion remains unnormalized after shared helper extraction");
}
CityMesh fixtureMesh(std::string name, float z = 10) {
    CityMesh m;
    m.name = std::move(name);
    m.positions = {{0, 0, z}, {100, 0, z + 10}, {0, 100, z + 20}};
    m.buffers.push_back({"", {0, 1, 2}});
    m.rawBoundsCm = std::array<std::array<float, 3>, 2>{{{-500, -600, -700}, {500, 600, 700}}};
    return m;
}
CityNode node(int parent, std::array<float, 3> position = {}) {
    CityNode n{};
    n.name = "authored";
    n.parent = parent;
    n.translation = position;
    n.rotation = {0, 0, 0, 1};
    n.scale = {1, 1, 1};
    return n;
}
void bindMesh(CityNode& n, const std::string& name) {
    n.instances.push_back({3, n.geometry.size(), {}, name, false});
    n.geometry.push_back({{}, name, {}});
}
CityScene scene() {
    CityScene s;
    s.path = "height-fixture.bdae";
    s.meshes.push_back(fixtureMesh("ground"));
    s.nodes.push_back(node(-1, {25, 35, 45}));
    bindMesh(s.nodes[0], "ground");
    s.visualScenes.push_back({"Scene", "display", 0, 1});
    return s;
}
CityNavigation navigation(const CityMesh& m) {
    CityNavigation nav;
    nav.buffers.resize(m.buffers.size());
    for (size_t i = 0; i < m.buffers.size(); ++i) {
        const auto count = m.buffers[i].indices.size() / 3;
        nav.buffers[i].roadFlags.resize(count);
        for (size_t lane = 0; lane < 8; ++lane)
            nav.buffers[i].neighbors[lane].assign(count, lane < 6 ? UINT16_MAX : 0);
        nav.buffers[i].overlaps.resize(count);
    }
    nav.materialBuffers.fill(0);
    nav.cliffVertexKeys.resize(m.positions.size() * 2);
    return nav;
}
b2AABB bounds() {
    b2AABB b;
    b.lowerBound.Set(-2000, -2000);
    b.upperBound.Set(2000, 2000);
    return b;
}
void hierarchyAndHeight() {
    auto source = scene();
    source.nodes[0].instances.insert(source.nodes[0].instances.begin(), {4, std::nullopt, {}, "lamp", false});
    source.lightUids.push_back("lamp");
    source.nodes.push_back(node(-1, {25, 35, 45}));
    source.meshes.push_back(fixtureMesh("second", 50));
    bindMesh(source.nodes[1], "second");
    source.visualScenes[0].nodeCount = 2;
    RetailHeightSceneBinding binding(source);
    source.meshes.clear(); // Binding owns its own source and stable mesh identities.
    const auto& root = binding.root();
    require(root.type == 0x72656164 && root.children.size() == 1,
            "Original root factory type and visual child");
    const auto& visual = *root.children[0];
    require(visual.type == 0x6e656164 && visual.children.size() == 2, "Visual/SNode hierarchy is retained");
    const auto& container = *visual.children[0];
    require(container.children.size() == 2 && container.children[0]->type == 0x7468676c,
            "Authored light-before-mesh instance order and genuine nonmesh type");
    const auto& first = *container.children[1];
    const auto& second = *visual.children[1]->children[0];
    require(&binding.globalHeightmap() == &first && first.mesh == &binding.source().meshes[0],
            "CreateNeighborTris retains first recognized mesh, stable owned identity");
    require(first.boundsCm == *binding.source().meshes[0].rawBoundsCm && first.boundsCm[0][0] == -500 &&
                first.absoluteTranslationCm == std::array<float, 3>{25, 35, 45},
            "Raw box remains distinct from extrema/transformed box and translation is inherited");
    auto nav = navigation(*first.mesh);
    RetailHeightTerrain terrain(first, nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController controller(terrain, world);
    controller.initializeFromScene(0, {35, 45, 999});
    require(controller.state(0).selectedNode == &first, "Genuine borrowed global view initializes height");
    close(controller.height({35, 45, 999}), 58, "Native scene-bound original terrain plane");
    controller.initializeFromNode(0, root, {35, 45, 999});
    require(controller.state(0).selectedNode == &second && &terrain.heightmapNode() == &first,
            "Last-successful scene helper is distinct from first retained global selection");
    require(world.GetProxyCount() == 0, "Scene selection does not fabricate collider refresh");
    auto excluded = scene();
    excluded.meshes[0].rawBoundsCm =
        std::array<std::array<float, 3>, 2>{{{1000, 1000, -10}, {2000, 2000, 10}}};
    RetailHeightSceneBinding outsideRawBox(std::move(excluded));
    RetailHeightTerrain rawTerrain(outsideRawBox.globalHeightmap(), nav);
    RetailHeightController rawController(rawTerrain, world);
    rawController.initializeFromScene(0, {35, 45, 999});
    require(!rawController.state(0).hasHeightmap,
            "Serialized raw-box gate rejects despite translated vertex containment; no extrema fallback");
}
void transformsAndRejects() {
    auto s = scene();
    s.nodes[0] = node(-1, {100, 200, 300});
    s.nodes[0].rotation = {0, 1, 0, 0};
    auto child = node(0, {9, 8, 7});
    child.scale = {-1, 1, 1};
    bindMesh(child, "ground");
    s.nodes.push_back(child);
    s.visualScenes[0].nodeCount = 2;
    RetailHeightSceneBinding binding(s);
    const auto& actual = binding.absoluteTransform(binding.globalHeightmap());
    const RetailSceneMatrix expected{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 91, 208, 293, 1};
    for (size_t i = 0; i < 16; ++i)
        require(bits(actual[i]) == bits(expected[i]), "Original generated-child literal matrix bits");
    for (const char* marker : {"car_light", "light_park", "light_street", "stage_light", "sign"})
        require(retailSceneMeshRuntimeType(std::string("prefix/") + marker + ".bdae") == retailSceneEmptyType,
                "Original database substring mesh type");
    require(retailSceneMeshRuntimeType("SIGN.bdae") == retailSceneMeshType,
            "Original type matching is case sensitive");
    const auto rejects = [](CityScene bad) {
        try {
            RetailHeightSceneBinding value(std::move(bad));
        } catch (const std::exception&) {
            return;
        }
        throw std::runtime_error("Unsupported height-scene factory silently accepted");
    };
    auto defaultFactory = scene();
    defaultFactory.nodes[0].userProperties = "billboard=1";
    require(defaultFactory.nodes[0].property("billboard") == "1", "Billboard property fixture is genuine");
    RetailHeightSceneBinding ignoredGameProperty(defaultFactory);
    require(ignoredGameProperty.globalHeightmap().type == retailSceneMeshType,
            "Height load uses DefaultFactory, which does not select CGameFactory property billboards");
    auto bad = scene();
    bad.path = "road_sign.bdae";
    rejects(bad);
    bad = scene();
    bad.meshes[0].rawBoundsCm.reset();
    rejects(bad);
    bad = scene();
    bad.nodes[0].factorySelector = 1;
    rejects(bad);
    bad = scene();
    bad.nodes[0].instances.push_back({2, std::nullopt, {}, {}, false});
    rejects(bad);
    bad = scene();
    bad.nodes[0].geometry[0].sourcePath = "external.bdae";
    rejects(bad);
    bad = scene();
    bad.nodes[0].instances.clear();
    rejects(bad);
    bad = scene();
    bad.visualScenes.clear();
    rejects(bad);
    bad = scene();
    bad.nodes[0].parent = 0;
    rejects(bad);
    bad = scene();
    bad.hasSceneAnimations = true;
    rejects(bad);
    bad = scene();
    bad.nodes[0].instances.insert(bad.nodes[0].instances.begin(), {3, std::nullopt, {}, "spline", true});
    RetailHeightSceneBinding spline(std::move(bad));
    require(spline.root().children[0]->children[0]->children.size() == 1,
            "Original nonmesh geometry construction returns NULL, not a fake height mesh");
}
struct Serialized {
    std::vector<unsigned char> data = std::vector<unsigned char>(156);
    uint32_t allocate(size_t size) {
        const auto p = static_cast<uint32_t>(data.size());
        data.resize(p + size);
        return p;
    }
    void word(uint32_t p, uint32_t v) {
        for (unsigned i = 0; i < 4; ++i)
            data.at(p + i) = static_cast<unsigned char>(v >> (8 * i));
    }
    void scalar(uint32_t p, float v) { word(p, bits(v)); }
    uint32_t text(const std::string& v) {
        const auto p = allocate((v.size() + 4) & ~size_t(3));
        std::copy(v.begin(), v.end(), data.begin() + p);
        return p;
    }
    void write(const std::filesystem::path& path) {
        std::copy_n("BRES", 4, data.begin());
        word(4, 0xfffe);
        word(8, 32);
        word(12, static_cast<uint32_t>(data.size()));
        word(20, 32);
        word(24, 32);
        word(28, 32);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        require(bool(out), "Write serialized scene/animation fixture");
    }
};
void rawDecoder() {
    struct Temporary {
        std::filesystem::path path;
        bool owned = false;
        ~Temporary() {
            if (!owned)
                return;
            std::error_code ignored;
            std::filesystem::remove(path / "raw.bdae", ignored);
            std::filesystem::remove(path, ignored);
        }
    } temp{std::filesystem::temp_directory_path() /
           ("OpenGangstar-scene-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    temp.owned = std::filesystem::create_directory(temp.path);
    require(temp.owned, "Create raw scene fixture folder");
    Serialized f;
    const auto geometry = f.allocate(16), mesh = f.allocate(44), stream = f.allocate(32),
               buffer = f.allocate(60);
    const auto offsets = f.allocate(4), types = f.allocate(4), vertices = f.allocate(36),
               indices = f.allocate(8);
    f.word(32 + 76, 1);
    f.word(32 + 80, geometry);
    f.word(geometry, f.text("ground"));
    f.word(geometry + 12, mesh);
    f.word(mesh, 1);
    f.word(mesh + 4, 3);
    f.word(mesh + 8, stream);
    f.word(mesh + 12, 1);
    f.word(mesh + 16, buffer);
    const std::array<float, 6> rawBox{-500, -600, -700, 500, 600, 700};
    for (size_t i = 0; i < 6; ++i)
        f.scalar(mesh + 20 + uint32_t(i * 4), rawBox[i]);
    f.word(stream, 12);
    f.word(stream + 4, 1);
    f.word(stream + 8, offsets);
    f.word(stream + 16, types);
    f.word(types, 6);
    f.word(stream + 28, vertices);
    const auto m = fixtureMesh("ground");
    for (size_t i = 0; i < 3; ++i)
        for (size_t k = 0; k < 3; ++k)
            f.scalar(vertices + uint32_t(i * 12 + k * 4), m.positions[i][k]);
    for (unsigned field : {13u, 16u, 19u, 22u, 23u})
        f.data[buffer + field] = 255;
    f.word(buffer + 8, 1);
    f.word(buffer + 24, 3);
    f.word(buffer + 28, indices);
    f.word(buffer + 32, 2u << 16);
    f.data[indices + 2] = 1;
    f.data[indices + 4] = 2;
    const auto visual = f.allocate(16), n = f.allocate(80), instance = f.allocate(8), ref = f.allocate(20);
    f.word(32 + 108, 1);
    f.word(32 + 112, visual);
    f.word(visual, f.text("Scene"));
    f.word(visual + 4, f.text("display"));
    f.word(visual + 8, 1);
    f.word(visual + 12, n);
    f.word(n, f.text("source-node"));
    f.scalar(n + 12, 25);
    f.scalar(n + 16, 35);
    f.scalar(n + 20, 45);
    f.scalar(n + 36, 1);
    for (unsigned k = 0; k < 3; ++k)
        f.scalar(n + 40 + k * 4, 1);
    f.word(n + 64, 1);
    f.word(n + 68, instance);
    f.word(instance, 3);
    f.word(instance + 4, ref);
    f.word(ref + 4, f.text("#ground"));
    const auto active = f.allocate(8), vref = f.allocate(8);
    f.word(32 + 116, 1);
    f.word(32 + 120, active);
    f.word(active, 6);
    f.word(active + 4, vref);
    f.word(vref + 4, f.text("#Scene"));
    f.write(temp.path / "raw.bdae");
    ResourceFileSystem files(temp.path);
    auto decoded = loadCityScene(files, "raw.bdae");
    require(decoded.visualScenes.size() == 1 && decoded.visualScenes[0].uid == "Scene" &&
                decoded.visualScenes[0].displayName == "display" && decoded.nodes[0].instances.size() == 1 &&
                decoded.nodes[0].instances[0].geometry == 0,
            "Authoritative decoder retains visual/instance identity");
    RetailHeightSceneBinding binding(std::move(decoded));
    require(binding.globalHeightmap().boundsCm == *m.rawBoundsCm,
            "Actual decoder reads SMesh+20 box without extrema reconstruction");
}
void retail(const std::filesystem::path& root, bool emit) {
    ResourceFileSystem files(root);
    auto binding = loadOriginalHeightScene(files);
    const auto& global = binding->globalHeightmap();
    require(binding->source().meshes.size() == 1 && global.mesh->name == "ground_height01-mesh" &&
                global.type == retailSceneMeshType,
            "Original loader selects genuine supplied ground mesh by runtime traversal");
    std::cout << "Actual scene graph count " << binding->nodeCount() << ", source SNodes "
              << binding->source().nodes.size() << ", local lights " << binding->source().lightUids.size()
              << ".\n";
    require(binding->source().nodes.size() == 138 && binding->nodeCount() == 141,
            "Supplied root/visual/SNode/mesh hierarchy and unresolved light lookup accounted for");
    require(global.boundsCm == *global.mesh->rawBoundsCm, "Supplied raw bounds reach native view unchanged");
    const std::array<float, 3> expected{-34649.3984375f, -40383.80078125f, 745.8629760742188f};
    for (size_t i = 0; i < 3; ++i)
        require(bits(global.absoluteTranslationCm[i]) == bits(expected[i]),
                "Supplied final absolute height position");
    std::cout << "Height scene nodes " << binding->nodeCount() << ", canonical mesh " << global.mesh->name
              << ", absolute cm " << std::setprecision(9) << global.absoluteTranslationCm[0] << ','
              << global.absoluteTranslationCm[1] << ',' << global.absoluteTranslationCm[2] << ".\n";
    if (emit) {
        std::function<void(const RetailHeightSceneNode&, const std::string&)> dumpNode =
            [&](const auto& node, const auto& path) {
                std::cout << "node {\"path\":" << std::quoted(path) << ",\"type\":" << node.type
                          << ",\"global\":" << (&node == &global ? "true" : "false") << ",\"matrix\":[";
                const auto& matrix = binding->absoluteTransform(node);
                for (size_t i = 0; i < matrix.size(); ++i) {
                    if (i)
                        std::cout << ',';
                    std::cout << bits(matrix[i]);
                }
                std::cout << "],\"bbox\":[";
                if (node.mesh)
                    for (size_t i = 0; i < 6; ++i) {
                        if (i)
                            std::cout << ',';
                        std::cout << bits(node.boundsCm[i / 3][i % 3]);
                    }
                std::cout << "]}\n";
                for (size_t i = 0; i < node.children.size(); ++i)
                    dumpNode(*node.children[i], path + "/" + std::to_string(i));
            };
        dumpNode(binding->root(), "root");
    }
    const auto city = loadCityAssets(files);
    const auto opening = loadCityOpening(files, city);
    RetailHeightTerrain terrain(global, opening.navigation);
    b2World world(bounds(), b2Vec2(0, 0), true);
    std::vector<RetailHeightPoint> queries{{0, 0, 0},
                                           {0, 0, 999999},
                                           global.absoluteTranslationCm,
                                           {global.boundsCm[0][0], global.boundsCm[0][1], 0},
                                           {global.boundsCm[1][0], global.boundsCm[1][1], 0},
                                           {global.boundsCm[0][0] - 1, 0, 0},
                                           {global.boundsCm[1][0] + 1, 0, 0},
                                           {0, global.boundsCm[0][1] - 1, 0},
                                           {0, global.boundsCm[1][1] + 1, 0},
                                           {(global.boundsCm[0][0] + global.boundsCm[1][0]) * .5f,
                                            (global.boundsCm[0][1] + global.boundsCm[1][1]) * .5f, 999},
                                           {1000, 2000, 3000},
                                           {-1000, -2000, -3000}};
    for (size_t buffer : {0u, 2u, 4u, 7u}) {
        const auto& indices = global.mesh->buffers[buffer].indices;
        RetailHeightPoint p{};
        for (size_t axis = 0; axis < 3; ++axis)
            p[axis] = ((global.mesh->positions[indices[0]][axis] + global.mesh->positions[indices[1]][axis]) +
                       global.mesh->positions[indices[2]][axis]) /
                          3.f +
                      global.absoluteTranslationCm[axis];
        queries.push_back(p);
    }
    for (const auto& point : queries) {
        RetailHeightController controller(terrain, world);
        controller.initializeFromScene(0, point);
        const auto& state = controller.state(0);
        require(!state.hasHeightmap || state.selectedNode == &global,
                "Genuine canonical height identity retained");
        if (emit) {
            std::cout << "seed {\"point\":[" << bits(point[0]) << ',' << bits(point[1]) << ','
                      << bits(point[2]) << "],\"selected\":" << (state.hasHeightmap ? "true" : "false")
                      << ",\"buffer\":" << state.triangle.buffer
                      << ",\"index\":" << state.triangle.indexOffset
                      << ",\"changed\":" << (state.changed ? "true" : "false") << ",\"vertices\":[";
            for (size_t i = 0; i < 9; ++i) {
                if (i)
                    std::cout << ',';
                std::cout << bits(state.vertices[i / 3][i % 3]);
            }
            std::cout << "],\"normal\":[" << bits(state.normal[0]) << ',' << bits(state.normal[1]) << ','
                      << bits(state.normal[2]) << "],\"cache\":[" << bits(state.cached[0]) << ','
                      << bits(state.cached[1]) << ',' << bits(state.cached[2]) << "]}\n";
        }
    }
    require(world.GetBodyCount() == 1 && world.GetProxyCount() == 0,
            "Actual scene-bound height selection does not fabricate collider refresh");
    std::cout << queries.size() << " native scene-bound original helper queries.\n";
    size_t centroidCount = 0, cliffBodies = 0, maxCliffs = 0;
    {
        RetailHeightController controller(terrain, world, 2);
        for (size_t buffer = 0; buffer < global.mesh->buffers.size(); ++buffer) {
            const auto& indices = global.mesh->buffers[buffer].indices;
            for (size_t offset = 0; offset < indices.size(); offset += 3) {
                const auto& a = global.mesh->positions[indices[offset]];
                const auto& b = global.mesh->positions[indices[offset + 1]];
                const auto& c = global.mesh->positions[indices[offset + 2]];
                const float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
                if (!(area > 0))
                    continue;
                RetailHeightPoint p;
                for (size_t axis = 0; axis < 3; ++axis)
                    p[axis] = ((a[axis] + b[axis]) + c[axis]) / 3.f + global.absoluteTranslationCm[axis];
                const CityNavigation::TriangleRef ref{static_cast<uint16_t>(buffer),
                                                      static_cast<uint16_t>(offset)};
                controller.initializeSelected(1, ref, p);
                require(std::isfinite(controller.height(p, 1)),
                        "Actual loaded-placement centroid height is finite");
                controller.initializeSelected(0, ref, p);
                require(world.GetBodyCount() == 1 + int(controller.bodies(0).size()) &&
                            world.GetProxyCount() == int(controller.bodies(0).size()),
                        "Every actual loaded-placement cliff has owned native body and real original-world "
                        "proxy");
                for (const auto* body : controller.bodies(0))
                    require(!body->IsFrozen(), "Loaded-placement cliff stays in actual world");
                cliffBodies += controller.bodies(0).size();
                maxCliffs = std::max(maxCliffs, controller.bodies(0).size());
                ++centroidCount;
            }
        }
    }
    require(centroidCount == 12998 && world.GetBodyCount() == 1 && world.GetProxyCount() == 0,
            "Complete loaded-placement centroid enumeration and native cliff teardown");
    std::cout << "Loaded height binding: " << centroidCount << " eligible centroids, " << cliffBodies
              << " real original-world cliff body instances, max " << maxCliffs
              << ", no range/capacity/frozen failures.\n";

    const auto catalog = loadCityAnimationCatalog(files, "anim_ingame.bdae");
    require(catalog.size() == 213, "Supplied animation catalogue cardinality");
    for (size_t i = 0; i < catalog.size(); ++i)
        require(catalog[i].source && catalog[i].source->index == i, "Supplied exact source animation index");
    std::cout << catalog.size() << " ordered source animation ranges decoded.\n";
}
} // namespace
int wmain(int argc, wchar_t** argv) {
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
#endif
    try {
        const auto allocationBaseline = b2_byteCount;
        const bool mathDump = argc == 2 && std::wstring(argv[1]) == L"--math-oracle-dump";
        sharedQuaternionMath(mathDump);
        hierarchyAndHeight();
        transformsAndRejects();
        rawDecoder();
        if (argc >= 2 && !mathDump)
            retail(argv[1], argc == 3 && std::wstring(argv[2]) == L"--oracle-dump");
        require(b2_byteCount == allocationBaseline,
                "Native scene-bound height ownership releases allocations");
        std::cout << "Original decoded scene/factory/global-height binding fixtures passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
