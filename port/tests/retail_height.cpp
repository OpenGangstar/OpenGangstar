#include "retail_height.h"
#include "filesystem.h"
#include <cstring>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void close(float a, float b, const char* message) {
    require(std::abs(a - b) < 0.0001f, message);
}
b2AABB bounds() {
    b2AABB b;
    b.lowerBound.Set(-2000, -2000);
    b.upperBound.Set(2000, 2000);
    return b;
}
struct Fixture {
    CityMesh mesh;
    CityNavigation nav;
    Fixture(std::vector<RetailHeightPoint> vertices, std::vector<uint16_t> indices) {
        mesh.positions = std::move(vertices);
        mesh.buffers.push_back({"", std::move(indices)});
        const auto count = mesh.buffers[0].indices.size() / 3;
        nav.buffers.resize(1);
        nav.buffers[0].roadFlags.resize(count);
        for (size_t i = 0; i < 8; ++i)
            nav.buffers[0].neighbors[i].assign(count, i < 6 ? UINT16_MAX : 0);
        nav.buffers[0].overlaps.resize(count);
        nav.materialBuffers.fill(0);
        nav.cliffVertexKeys.resize(mesh.positions.size() * 2);
    }
};
RetailHeightSceneNode sceneNode(const CityMesh& mesh, RetailHeightPoint translation = {},
                               uint32_t type = 0x6d656164) {
    RetailHeightSceneNode node;
    node.type = type;
    node.mesh = &mesh;
    node.absoluteTranslationCm = translation;
    node.boundsCm = {mesh.positions.front(), mesh.positions.front()};
    for (const auto& p : mesh.positions)
        for (size_t k = 0; k < 3; ++k) {
            node.boundsCm[0][k] = std::min(node.boundsCm[0][k], p[k]);
            node.boundsCm[1][k] = std::max(node.boundsCm[1][k], p[k]);
        }
    return node;
}
void sceneInitialization() {
    Fixture far({{200, 0, 0}, {300, 0, 0}, {200, 100, 0}}, {0, 1, 2});
    Fixture near({{0, 0, 10}, {100, 0, 30}, {0, 100, 50}}, {0, 1, 2});
    auto a = sceneNode(near.mesh, {0, 0, 10});
    auto b = sceneNode(near.mesh, {0, 0, 100}, 0x73656164);
    auto miss = sceneNode(far.mesh);
    auto root = sceneNode(far.mesh);
    root.children = {&a, &b, &miss};
    RetailHeightTerrain terrain(root, far.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world, 2);
    close(height.height({25, 25, 987}, 1), 987, "Nonempty scene does not lazily seed absent wrapper node");
    require(!height.state(1).hasHeightmap && !height.state(1).selectedNode && terrain.generation() == 0,
            "Absent selected node bypasses scene traversal/core generation");
    for (float z : {-999.f, 10.f, 100.f, 999.f}) {
        height.initializeFromScene(1, {25, 25, z});
        require(height.state(1).selectedNode == &b && height.state(1).vertices[0][2] == 110 &&
                    height.state(1).cached == RetailHeightPoint{25, 25, z} && height.state(1).changed,
                "Last successful sibling wins independently of query Z; later miss preserves it");
    }
    require(height.bodies(1).empty() && terrain.generation() == 0,
            "Scene helper does not create cliffs or advance query generations");
    close(height.height({25, 25, -900}, 1), 125, "Selected child geometry feeds actual wrapper/core");
    auto parent = sceneNode(near.mesh);
    parent.children = {&b};
    height.initializeFromNode(1, parent, {25, 25, 999});
    require(height.state(1).selectedNode == &parent, "Successful parent returns before its children");
    RetailHeightSceneNode container;
    container.children = {&a, &b};
    height.initializeFromNode(1, container, {25, 25, 999});
    require(height.state(1).selectedNode == &b, "Nonmesh container visits ordered children");
    const auto before = height.state(1);
    bool failed = false;
    container.children = {&container};
    try { height.initializeFromNode(1, container, {25, 25, 999}); }
    catch (const std::invalid_argument&) { failed = true; }
    require(failed && height.state(1).cached == before.cached && height.state(1).selectedNode == &b,
            "Cyclic scene binding rejects before mutating selected state");
    container.children = {nullptr};
    failed = false;
    try { height.initializeFromNode(1, container, {25, 25, 999}); }
    catch (const std::invalid_argument&) { failed = true; }
    require(failed, "Missing scene child is explicit unsupported binding");
    container.children.clear();container.type = 0x6d656164;
    failed = false;
    try { height.initializeFromNode(1, container, {25, 25, 999}); }
    catch (const std::invalid_argument&) { failed = true; }
    require(failed, "Recognized node requires actual mesh metadata");
}
void liveSceneBoundsAndClassification() {
    Fixture f({{0, 0, 10}, {100, 0, 30}, {0, 100, 50}}, {0, 1, 2});
    auto node = sceneNode(f.mesh, {1000, 0, 0});
    RetailHeightTerrain terrain(node, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world, 2);
    height.initializeFromScene(1, {1025, 25, 999});
    require(!height.state(1).hasHeightmap, "Raw bbox rejects point contained by translated triangle");
    node.boundsCm[0][0] += 1000;node.boundsCm[1][0] += 1000;
    height.initializeFromScene(1, {1025, 25, 999});
    require(height.state(1).selectedNode == &node && height.state(1).vertices[0][0] == 1000,
            "Raw bbox updates are read between queries rather than frozen at construction");
    node.absoluteTranslationCm[2] = 500;
    height.initializeFromScene(1, {1025, 25, std::numeric_limits<float>::quiet_NaN()});
    require(height.state(1).vertices[0][2] == 510 && std::isnan(height.state(1).cached[2]),
            "Live absolute translation is refreshed; query Z is copied but never ranks scene matches");
    const auto before = height.state(1).vertices;
    node.boundsCm[0][2] = std::numeric_limits<float>::max() * .5f;
    node.boundsCm[1][2] = std::numeric_limits<float>::max();
    node.absoluteTranslationCm[2] = 900;
    height.initializeFromScene(1, {1025, 25, 0});
    require(height.state(1).vertices == before, "Overflowing box-own midpoint rejects without replacing old geometry");
    node.boundsCm[0][2] = std::numeric_limits<float>::quiet_NaN();
    height.initializeFromScene(1, {1025, 25, 0});
    require(height.state(1).vertices == before, "Unordered raw box midpoint follows failed-node branch");
    for (uint16_t excluded : {1, 2}) {
        Fixture stacked({{0, 0, 10}, {100, 0, 10}, {0, 100, 10},
                         {0, 0, 100}, {100, 0, 100}, {0, 100, 100}}, {0, 1, 2, 3, 4, 5});
        stacked.nav.buffers[0].neighbors[7] = {excluded, 0xfffb};
        RetailHeightTerrain classified(stacked.mesh, stacked.nav);
        RetailHeightController selected(classified, world, 2);
        selected.initializeFromScene(1, {25, 25, -999});
        require(selected.state(1).triangle.indexOffset == 3 && selected.state(1).vertices[0][2] == 100 &&
                    classified.visitWords()[0][1] == 0xfffb && classified.generation() == 0,
                "Scene skips low class1/2, accepts class3 despite upper generation bits, and does not mark visits");
    }
}
void pointHeightAttributes() {
    const std::vector<RetailHeightPoint> vertices{{0, 0, 10}, {100, 0, 30}, {0, 100, 50},
                                                {200, 0, 100}, {300, 0, 300}, {200, 100, 500}};
    for (const auto point : {RetailHeightPoint{225, 25, 555}, RetailHeightPoint{25, 25, 555}}) {
        Fixture f(vertices, {0, 1, 2, 3, 4, 5});f.nav.buffers[0].overlaps[0] = {{0, 3}};
        RetailHeightTerrain terrain(f.mesh, f.nav);b2World world(bounds(), b2Vec2(0, 0), true);
        RetailHeightController height(terrain, world, 2);height.initializeSelected(1, {0, 0}, point);
        close(height.setHeightAttributesFromPoint(1, point), point[0] == 225 ? 250.f : -150.f,
              "Full point overlap includes final pair and accepts outside extrapolation without a crossing");
        require(height.state(1).triangle.indexOffset == 3 && height.state(1).cached == point &&
                    height.state(1).selectedNode == &terrain.heightmapNode() && terrain.generation() == 0,
                "Point helper commits canonical identity/geometry and preserves cache/generation");
    }
    for (bool destinationEmpty : {false, true}) {
        Fixture f(vertices, {0, 1, 2, 3, 4, 5});f.nav.buffers[0].overlaps[0] = {{0, 3}};
        if (!destinationEmpty) f.nav.buffers[0].overlaps[1] = {{0, 0}};
        RetailHeightTerrain terrain(f.mesh, f.nav);b2World world(bounds(), b2Vec2(0, 0), true);
        RetailHeightController height(terrain, world, 2);height.initializeSelected(1, {0, 0}, {225, 25, 77});
        const auto before = height.state(1);
        const auto result = height.setHeightAttributesFromPoint(1, {25, 25, 555});
        close(result, destinationEmpty ? 999999.f : 25.f, "Point crossing restarts destination's full overlap list");
        require(height.state(1).cached == before.cached &&
                    height.state(1).triangle.indexOffset == (destinationEmpty ? 3 : 0),
                "Recursive point transition preserves cache and exact successful/failed identity");
        if (destinationEmpty)
            require(height.state(1).vertices == before.vertices && height.state(1).normal == before.normal,
                    "Recursive point miss leaves old geometry alongside new identity");
    }
    for (uint16_t excluded : {1, 2}) {
        Fixture f(vertices, {0, 1, 2, 3, 4, 5});f.nav.buffers[0].overlaps[0] = {{0, 3}};
        f.nav.buffers[0].neighbors[7][1] = excluded;
        RetailHeightTerrain terrain(f.mesh, f.nav);b2World world(bounds(), b2Vec2(0, 0), true);
        RetailHeightController height(terrain, world, 2);height.initializeSelected(1, {0, 0}, {25, 25, 77});
        close(height.setHeightAttributesFromPoint(1, {225, 25, 555}), 999999,
              "Point classification-exhausted list returns sentinel");
        require(height.state(1).triangle.indexOffset == 0, "Rejected point candidate does not mutate identity");
    }
    Fixture cycle({{200, 0, 100}, {300, 0, 300}, {200, 100, 500}}, {0, 1, 2});
    cycle.nav.buffers[0].overlaps[0] = {{0, 0}};
    RetailHeightTerrain terrain(cycle.mesh, cycle.nav);b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world, 2);height.initializeSelected(1, {0, 0}, {225, 25, 77});
    bool failed = false;
    try { height.setHeightAttributesFromPoint(1, {25, 25, 555}); }
    catch (const std::runtime_error& e) { failed = std::string(e.what()).find("Nonterminating") != std::string::npos; }
    require(failed && height.state(1).cached == RetailHeightPoint{225, 25, 77},
            "Repeated original recursion state reports explicit error without a finite fallback or arbitrary cap");
}
void mixedSceneSources() {
    Fixture f({{0, 0, 10}, {100, 0, 10}, {0, 100, 10}, {100, 100, 20}}, {0, 1, 2, 1, 3, 2});
    f.nav.buffers[0].neighbors[1][0] = 3;f.nav.buffers[0].neighbors[4][0] = 0;
    auto root = sceneNode(f.mesh, {0, 0, 50});
    auto alternate = f.mesh;for (auto& p : alternate.positions) p[2] = 1000;
    auto child = sceneNode(alternate, {0, 0, 200});
    RetailHeightTerrain terrain(root, f.nav);b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world, 2);height.initializeFromNode(1, child, {25, 25, 77});
    close(height.height({75, 75, 999}, 1), 215, "Neighbor fetch uses global mesh plus OLD selected-node translation");
    require(height.state(1).selectedNode == &root && height.state(1).vertices[0][2] == 210 &&
                height.state(1).vertices[1][2] == 220 && height.state(1).vertices[2][2] == 210,
            "Neighbor switches identity while retaining captured translation, rather than new global translation50");
    height.initializeFromNode(1, child, {25, 25, 77});
    child.absoluteTranslationCm[2] = 300;
    close(height.height({75, 75, 999}, 1), 315,
          "Outside-edge search reads changed selected-node translation at query time");
    require(height.state(1).vertices[0][2] == 310 && height.state(1).vertices[1][2] == 320,
            "Stored old geometry does not freeze later host absolute-translation reads");
    Fixture overlap({{0, 0, 10}, {100, 0, 10}, {0, 100, 10},
                     {200, 0, 100}, {300, 0, 100}, {200, 100, 100}}, {0, 1, 2, 3, 4, 5});
    overlap.nav.buffers[0].overlaps[0] = {{0, 3}, {0, 0}};
    auto localMesh = overlap.mesh;
    for (size_t i = 0; i < localMesh.positions.size(); ++i) localMesh.positions[i][2] = i < 3 ? 1000.f : 2000.f;
    auto localNode = sceneNode(localMesh, {0, 0, 200});
    RetailHeightTerrain overlapTerrain(overlap.mesh, overlap.nav);RetailHeightController local(overlapTerrain, world, 2);
    local.initializeFromNode(1, localNode, {25, 25, 77});
    close(local.height({225, 25, 999}, 1), 2200, "Core local overlap reads selected-node mesh and translation");
    require(local.state(1).selectedNode == &localNode && local.state(1).triangle.indexOffset == 3,
            "Local overlap does not silently switch to global mesh/node");
    Fixture seed({{0, 0, 10}, {100, 0, 30}, {0, 100, 50}}, {0, 1, 2});
    Fixture a({{200, 0, 100}, {300, 0, 100}, {200, 100, 100}}, {0, 1, 2});
    Fixture b({{200, 0, 200}, {300, 0, 200}, {200, 100, 200}}, {0, 1, 2});
    auto first = sceneNode(a.mesh), last = sceneNode(b.mesh), global = sceneNode(seed.mesh);
    global.children = {&first, &last};RetailHeightTerrain reseed(global, seed.nav);
    RetailHeightController mixed(reseed, world);mixed.initializeSelected(0, {0, 0}, {25, 25, 77});
    close(mixed.height({225, 25, 400}), 215, "Scene reseed retains old interpolation deltas after last sibling commits");
    require(mixed.state(0).selectedNode == &last && mixed.state(0).vertices[0][2] == 200 &&
                mixed.state(0).cached == RetailHeightPoint{225, 25, 215},
            "Complete wrapper/core records last child geometry and original mixed result");
}
void sceneSeedBodyBoundary() {
    Fixture f({{100, 0, 0}, {0, 0, 0}, {100, 0, 100}}, {0, 1, 2});
    f.nav.cliffVertexKeys = {0, 1, 1, 1, 2, 1};
    f.nav.cliffTriangles = {0, 0, 0};
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world);
    height.initializeFromScene(0, {50, 0, 0});
    require(height.state(0).hasHeightmap && height.state(0).onGround && height.bodies(0).empty() &&
                world.GetBodyCount() == 1 && world.GetProxyCount() == 0,
            "Grounded scene helper does not implicitly create eligible local-cliff bodies");
    height.updateEdges(0);
    require(height.bodies(0).size() == 1 && world.GetProxyCount() == 1,
            "Explicit original edge-refresh boundary creates the actual eligible native cliff");
}
void basicHeight() {
    Fixture f({{0, 0, 10}, {100, 0, 30}, {0, 100, 50}}, {0, 1, 2});
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world);
    close(height.height({25, 25, 71}), 71, "Uninitialized height must cache supplied Z");
    require(terrain.generation() == 0, "Unselected query must not advance generation");
    height.initializeSelected(0, {0, 0}, {25, 25, 71});
    close(height.height({25, 25, -900}), 25, "Sloped original plane interpolation");
    close(height.height({100, 0, 900}), 30, "Shared vertex equality included");
    require(terrain.generation() == 8 && (terrain.visitWords()[0][0] & 0xfffc) == 8, "Shared visit protocol");
    require(f.nav.buffers[0].neighbors[7][0] == 0, "Must not mutate decoded navigation");
    height.reset();
    close(height.height({2, 2, 555}), 30, "Reset preserves valid cache");
    Fixture degenerate({{0, 0, 0}, {10, 0, 10}, {20, 0, 20}}, {0, 1, 2});
    RetailHeightTerrain degTerrain(degenerate.mesh, degenerate.nav);
    RetailHeightController deg(degTerrain, world);
    deg.initializeSelected(0, {0, 0}, {1, 0, 77});
    require(std::isnan(deg.height({1, 0, 77})), "Original zero-area arithmetic produces NaN");
}
void neighborAndCache() {
    Fixture f({{0, 0, 10}, {100, 0, 10}, {0, 100, 10}, {100, 100, 20}}, {0, 1, 2, 1, 3, 2});
    f.nav.buffers[0].neighbors[1][0] = 3;
    f.nav.buffers[0].neighbors[4][0] = 0;
    f.nav.buffers[0].neighbors[2][1] = 0;
    f.nav.buffers[0].neighbors[5][1] = 0;
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world, 2);
    height.initializeSelected(0, {0, 0}, {25, 25, 10});
    close(height.height({75, 75, 999}), 15, "Cross authored neighbor");
    require(height.state(0).triangle.indexOffset == 3 && height.state(0).changed,
            "Neighbor commits exact identity");
    height.initializeSelected(1, {0, 0}, {25, 25, 33});
    close(height.height({-10, 20, 50}, 1, true), 33, "Nonzero detailed slot preserves cached miss");
    require(height.state(1).triangle.indexOffset == 0, "Detailed nonzero miss does not global reseed");
    height.initializeSelected(0, {0, 0}, {25, 25, 10});
    // Classification excludes a candidate, not a preselected seed.
    f.nav.buffers[0].neighbors[7][1] = 1;
    RetailHeightTerrain excluded(f.mesh, f.nav);
    RetailHeightController rejected(excluded, world, 2);
    rejected.initializeSelected(1, {0, 0}, {25, 25, 42});
    close(rejected.height({75, 75, 20}, 1, true), 42, "Class-one neighbor excluded");
    rejected.initializeSelected(1, {0, 3}, {75, 75, 10});
    close(rejected.height({75, 75, 20}, 1, true), 15, "Selected seed is not class-rejected");
}
void overlapAndMixedReseed() {
    Fixture f({{0, 0, 10}, {100, 0, 10}, {0, 100, 10}, {200, 0, 40}, {300, 0, 40}, {200, 100, 40}},
              {0, 1, 2, 3, 4, 5});
    f.nav.buffers[0].overlaps[0] = {{0, 3}};
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world, 2);
    height.initializeSelected(1, {0, 0}, {25, 25, 77});
    close(height.height({225, 25, 400}, 1, true), 77, "Local phase excludes final/only overlap");
    require(height.state(1).triangle.indexOffset == 0, "Skipped final pair does not mutate identity");
    // With a trailing pair, the useful first pair participates; no closest-Z policy.
    f.nav.buffers[0].overlaps[0].push_back({0, 0});
    RetailHeightTerrain both(f.mesh, f.nav);
    RetailHeightController included(both, world, 2);
    included.initializeSelected(1, {0, 0}, {25, 25, 77});
    close(included.height({225, 25, -900}, 1, true), 40, "Ordered overlap traversal");
    // First global XY seed after an exhausted edge search changes live state,
    // while interpolation still uses the old scalar deltas, as in original ARM.
    RetailHeightController mixed(terrain, world);
    mixed.initializeSelected(0, {0, 0}, {25, 25, 77});
    const auto result = mixed.height({225, 25, 400});
    require(mixed.state(0).triangle.indexOffset == 3, "Slot-zero global reseed first XY identity");
    close(result, 40, "Mixed reseed flat geometry result");
}
void originalOracleCases() {
    // Original ARM core executed by the reference-only Unicorn harness. Only
    // AEABI math and concrete scene getters were intercepted; values below are
    // independent of the native implementation. New global triangle's actual
    // plane would give 110/125/125/110/175, rather than these mixed-local values.
    const std::array<RetailHeightPoint, 5> points{
        {{210, 10, 400}, {225, 25, 400}, {250, 25, 400}, {275, 10, 400}, {225, 75, 400}}};
    const std::array<uint32_t, 5> expected{0x42d40000, 0x42e60000, 0x42f00000, 0x42ee0000, 0x43070000};
    for (size_t i = 0; i < points.size(); ++i) {
        Fixture f({{0, 0, 10}, {100, 0, 30}, {0, 100, 50}, {200, 0, 100}, {300, 0, 100}, {200, 100, 200}},
                  {0, 1, 2, 3, 4, 5});
        f.nav.buffers[0].overlaps[0] = {{0, 3}};
        RetailHeightTerrain terrain(f.mesh, f.nav);
        b2World world(bounds(), b2Vec2(0, 0), true);
        RetailHeightController height(terrain, world);
        height.initializeSelected(0, {0, 0}, {25, 25, 77});
        const float result = height.height(points[i]);
        uint32_t bits;
        std::memcpy(&bits, &result, 4);
        require(bits == expected[i], "Original ARM mixed-reseed result bits");
        require(height.state(0).triangle.indexOffset == 3 && height.state(0).changed,
                "ARM mixed-reseed state");
    }
    Fixture f({{0, 0, 10},
               {100, 0, 30},
               {0, 100, 50},
               {200, 0, 100},
               {300, 0, 100},
               {200, 100, 200},
               {400, 0, 0},
               {500, 0, 0},
               {400, 100, 0}},
              {0, 1, 2, 3, 4, 5, 6, 7, 8});
    f.nav.buffers[0].overlaps[0] = {{0, 3}, {0, 6}};
    f.nav.buffers[0].neighbors[7][1] = 1;
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world, 2);
    height.initializeSelected(1, {0, 0}, {25, 25, 77});
    close(height.height({225, 25, 400}, 1, true), 77, "Rejected overlap preserves old cache");
    require(height.state(1).triangle.indexOffset == 3 && !height.state(1).changed,
            "ARM prefilter identity mutation");
    close(height.state(1).vertices[0][0], 0, "Rejected identity retains old geometry");
}
void originalWrapperReseed() {
    Fixture f({{0, 0, 10}, {100, 0, 30}, {0, 100, 50}, {200, 0, 100}, {300, 0, 100}, {200, 100, 200}},
              {0, 1, 2, 3, 4, 5});
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world, 2);
    // Explicit Init can supply a cached point outside the selected triangle.
    // Original wrapper0x31ff49 returns400 after core failure/global reseed;
    // the core itself returns999999, but its reseed has changed the cache.
    height.initializeSelected(1, {0, 0}, {225, 25, 77});
    close(height.height({225, 25, 400}, 1, false), 400,
          "Original wrapper observes core reseed cache mutation");
    require(height.state(1).triangle.indexOffset == 3 && height.state(1).changed,
            "Original wrapper reseed state");
}
void allNeighborEdges() {
    const std::array<RetailHeightPoint, 3> fourth{{{100, -100, 50}, {100, 100, 50}, {-100, 0, 50}}};
    const std::array<RetailHeightPoint, 3> queries{{{25, -25, 900}, {75, 75, 900}, {-25, 25, 900}}};
    const std::array<std::vector<uint16_t>, 3> indices{
        {{0, 1, 2, 1, 0, 3}, {0, 1, 2, 1, 3, 2}, {0, 1, 2, 0, 2, 3}}};
    for (size_t e = 0; e < 3; ++e) {
        Fixture f({{0, 0, 10}, {100, 0, 10}, {0, 100, 10}, fourth[e]}, indices[e]);
        f.nav.buffers[0].neighbors[e][0] = 3;
        f.nav.buffers[0].neighbors[e + 3][0] = 0;
        RetailHeightTerrain terrain(f.mesh, f.nav);
        b2World world(bounds(), b2Vec2(0, 0), true);
        RetailHeightController height(terrain, world, 2);
        height.initializeSelected(1, {0, 0}, {25, 25, 10});
        close(height.height(queries[e], 1, true), e == 1 ? 30.f : 20.f,
              "Each authored edge selects correct neighbor");
        require(height.state(1).triangle.indexOffset == 3, "Exact neighbor index offset");
    }
}
void capacity() {
    std::vector<RetailHeightPoint> vertices{{0, 0, 0}, {0, 0, 100}};
    std::vector<uint16_t> indices;
    for (uint16_t i = 0; i < 13; ++i) {
        vertices.push_back({-100.f - 20.f * i, 0, 0});
        indices.insert(indices.end(), {0, static_cast<uint16_t>(i + 2), 1});
    }
    Fixture f(vertices, indices);
    f.nav.cliffTriangles.clear();
    for (uint16_t i = 0; i < 13; ++i)
        f.nav.cliffTriangles.push_back(i * 3);
    f.nav.cliffVertexKeys[1] = 13;
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world);
    height.initializeSelected(0, {0, 0}, {0, -10, 0});
    const auto before = world.GetBodyCount();
    bool rejected = false;
    try {
        height.initializeSelected(0, {0, 0}, {0, 10, 0});
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("capacity") != std::string::npos;
    }
    require(rejected && world.GetBodyCount() == before && height.bodies(0).empty(),
            "Capacity preflight creates no leaked or truncated colliders");
    require(height.state(0).cached[1] == 10 && height.state(0).changed,
            "Capacity error does not roll back slot mutation");
}
void shortEdgeOracleCases() {
    const auto scalar = [](uint32_t bits) {
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    };
    // Original AddCollisionShapes instruction window 0x31e6e0..0x31e708,
    // independently executed with the same AEABI hooks as the core oracle.
    // The diagonal cases would pass if deltas were divided by 100 before squaring.
    struct Case {
        uint32_t x, y;
        bool accepted;
    };
    const std::array<Case, 6> cases{{{0x40ffffff, 0, false},
                                     {0x41000000, 0, false},
                                     {0x41000001, 0, true},
                                     {0x3b03126f, 0x40ffffff, false},
                                     {0x3b449ba6, 0x40ffffff, false},
                                     {0x3c03126f, 0x40fffff8, false}}};
    for (const auto& test : cases) {
        const float x = scalar(test.x), y = scalar(test.y);
        Fixture f({{x, y, 0}, {0, 0, 0}, {x, y, 100}}, {0, 1, 2});
        f.nav.cliffVertexKeys = {0, 1, 1, 1, 2, 1};
        f.nav.cliffTriangles = {0, 0, 0};
        RetailHeightTerrain terrain(f.mesh, f.nav);
        b2World world(bounds(), b2Vec2(0, 0), true);
        RetailHeightController height(terrain, world);
        height.initializeSelected(0, {0, 0}, {x * .5f - y, y * .5f + x, 0});
        require(height.bodies(0).size() == size_t(test.accepted), "Original short-edge arithmetic order");
    }
    Fixture oversized({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2});
    oversized.mesh.buffers.resize(size_t(UINT16_MAX) + 2);
    oversized.nav.buffers.resize(size_t(UINT16_MAX) + 2);
    bool rejected = false;
    try {
        RetailHeightTerrain invalid(oversized.mesh, oversized.nav);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Oversized buffer count rejected before reference narrowing");
}
void edgeGatesAndWrap() {
    for (float length : {8.f, 8.001f}) {
        Fixture f({{length, 0, 0}, {0, 0, 0}, {length, 0, 100}}, {0, 1, 2});
        f.nav.cliffVertexKeys = {0, 1, 1, 1, 2, 1};
        f.nav.cliffTriangles = {0, 0, 0};
        RetailHeightTerrain terrain(f.mesh, f.nav);
        b2World world(bounds(), b2Vec2(0, 0), true);
        RetailHeightController height(terrain, world);
        height.initializeSelected(0, {0, 0}, {length * .5f, 0, 0});
        require(height.bodies(0).size() == (length == 8.f ? 0 : 1),
                "Strict original edge-length threshold and side equality");
        height.initializeSelected(0, {0, 0}, {length * .5f, -1, 0});
        require(height.bodies(0).empty(), "Negative face-side rejects");
    }
    Fixture f({{100, 0, 0}, {0, 0, 0}, {100, 0, 100}}, {0, 1, 2});
    f.nav.cliffVertexKeys = {0, 1, 1, 1, 2, 1};
    f.nav.cliffTriangles = {0, 0, 0};
    // Equal cliff-buffer neighbor suppresses internal strips independently of offset.
    for (size_t e = 0; e < 3; ++e)
        f.nav.buffers[0].neighbors[e + 3][0] = 0;
    RetailHeightTerrain internal(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController noEdge(internal, world);
    noEdge.initializeSelected(0, {0, 0}, {50, 10, 0});
    require(noEdge.bodies(0).empty(), "Internal cliff adjacency excluded");
    Fixture floor({{0, 0, 10}, {100, 0, 10}, {0, 100, 10}, {100, 100, 20}}, {0, 1, 2, 1, 3, 2});
    floor.nav.buffers[0].neighbors[1][0] = 3;
    floor.nav.buffers[0].neighbors[4][0] = 0;
    RetailHeightTerrain terrain(floor.mesh, floor.nav);
    RetailHeightController wrap(terrain, world, 2);
    wrap.initializeSelected(1, {0, 0}, {25, 25, 10});
    for (size_t i = 0; i < 16383; ++i)
        close(wrap.height({25, 25, 10}, 1, true), 10, "Stable seed through visit generations");
    require(terrain.generation() == 65532, "Generation reaches unsigned wrap boundary");
    close(wrap.height({75, 75, 900}, 1, true), 10,
          "Generation zero rejects unvisited-zero neighbor exactly as original");
    require(terrain.generation() == 0 && wrap.state(1).triangle.indexOffset == 0,
            "Original wrap marker state");
    close(wrap.height({75, 75, 900}, 1, true), 15, "Next generation admits neighbor again");
}
void contactLifetime() {
    struct Listener : b2ContactListener {
        size_t removed = 0;
        void Remove(const b2ContactPoint* p) override {
            auto* a = static_cast<RetailPhysicsBodyData*>(p->shape1->GetBody()->GetUserData());
            auto* b = static_cast<RetailPhysicsBodyData*>(p->shape2->GetBody()->GetUserData());
            require(a && b, "Metadata alive through native contact removal");
            require(a->heightsCm[1] == 100 || b->heightsCm[1] == 100, "Terrain metadata remains readable");
            ++removed;
        }
    } listener;
    Fixture f({{100, 0, 0}, {0, 0, 0}, {100, 0, 100}}, {0, 1, 2});
    f.nav.cliffVertexKeys = {0, 1, 1, 1, 2, 1};
    f.nav.cliffTriangles = {0, 0, 0};
    RetailHeightTerrain terrain(f.mesh, f.nav);
    RetailPhysicsOwnerState actor{5, 50, 0, 0};
    RetailPhysicsBodyData actorData{&actor, RetailPhysicsZBounds{0, 100}, {0, 0, 0, 0}, false};
    RetailContactFilter filter(nullptr, {});
    b2World world(bounds(), b2Vec2(0, 0), true);
    world.SetContactFilter(&filter);
    world.SetContactListener(&listener);
    RetailHeightController height(terrain, world);
    height.initializeSelected(0, {0, 0}, {50, 10, 0});
    b2BodyDef bd;
    bd.position.Set(.5f, .1f);
    bd.userData = &actorData;
    auto* moving = world.CreateBody(&bd);
    b2CircleDef circle;
    circle.radius = .3f;
    circle.density = 1;
    circle.filter.categoryBits = 4;
    circle.filter.maskBits = 0xffff;
    circle.filter.groupIndex = static_cast<int16>(height.groupIndex());
    moving->CreateShape(&circle);
    moving->SetMassFromShapes();
    for (size_t i = 0; i < 8; ++i)
        world.Step(1.f / 60.f, 10);
    require(moving->GetPosition().y > .25f, "Real custom-filter cliff contacts resolve circle overlap");
    height.reset();
    require(listener.removed > 0, "DestroyBody emits removal with live metadata");
}
void retailAssets(const std::filesystem::path& root, bool dump, bool initializationDump) {
    ResourceFileSystem resources(root);
    const auto city = loadCityAssets(resources);
    const auto opening = loadCityOpening(resources, city);
    require(opening.heightMap.meshes.size() == 1 && !opening.navigation.buffers.empty(),
            "Original decoded height inputs");
    const auto& mesh = opening.heightMap.meshes.front();
    RetailHeightTerrain terrain(mesh, opening.navigation);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world, 2);
    size_t checked = 0;
    for (size_t b = 0; b < mesh.buffers.size(); ++b)
        for (size_t off = 0; off < mesh.buffers[b].indices.size(); off += 3) {
            const auto& ids = mesh.buffers[b].indices;
            const auto& a = mesh.positions[ids[off]];
            const auto& v = mesh.positions[ids[off + 1]];
            const auto& c = mesh.positions[ids[off + 2]];
            const float area = (v[0] - a[0]) * (c[1] - a[1]) - (v[1] - a[1]) * (c[0] - a[0]);
            if (!(area > 0))
                continue;
            const RetailHeightPoint p{(a[0] + v[0] + c[0]) / 3.f, (a[1] + v[1] + c[1]) / 3.f,
                                      (a[2] + v[2] + c[2]) / 3.f};
            height.initializeSelected(1, {static_cast<uint16_t>(b), static_cast<uint16_t>(off)}, p);
            const auto result = height.height(p, 1, true);
            require(std::isfinite(result), "Retail initialized centroid height is finite");
            if (dump) {
                uint32_t rb;
                std::memcpy(&rb, &result, 4);
                std::cout << "oracle " << b << ' ' << off << ' ' << terrain.generation() << ' ' << rb;
                for (float coordinate : p) {
                    uint32_t bits;
                    std::memcpy(&bits, &coordinate, 4);
                    std::cout << ' ' << bits;
                }
                std::cout << ' ' << height.state(1).triangle.buffer << ' '
                          << height.state(1).triangle.indexOffset << '\n';
            }
            ++checked;
        }
    require(checked > 10000, "Enumerated complete eligible retail height triangles");
    std::cout << checked << " retail triangle centroid checks passed.\n";
    {
        const auto bits = [](float value) { uint32_t b; std::memcpy(&b, &value, 4); return b; };
        RetailHeightController point(terrain, world, 2);
        size_t pointChecks = 0, accepted = 0, finalPairs = 0;
        const auto generation = terrain.generation();
        for (size_t b = 0; b < mesh.buffers.size(); ++b)
            for (size_t off = 0; off < mesh.buffers[b].indices.size(); off += 3) {
                const auto& ids = mesh.buffers[b].indices;
                RetailHeightPoint p;
                for (size_t k = 0; k < 3; ++k)
                    p[k] = ((mesh.positions[ids[off]][k] + mesh.positions[ids[off + 1]][k]) +
                            mesh.positions[ids[off + 2]][k]) / 3.f;
                const CityNavigation::TriangleRef source{static_cast<uint16_t>(b), static_cast<uint16_t>(off)};
                point.initializeSelected(1, source, p);
                const auto before = point.state(1);
                const auto result = point.setHeightAttributesFromPoint(1, p);
                const auto& state = point.state(1);
                require(state.cached == p && terrain.generation() == generation,
                        "Every retail point record preserves cache/query generation");
                const auto& candidates = opening.navigation.buffers[b].overlaps[off / 3];
                std::optional<CityNavigation::TriangleRef> first;
                size_t chosen = 0;
                for (size_t i = 0; i < candidates.size(); ++i) {
                    const auto candidate = candidates[i];
                    const auto low = terrain.visitWords()[candidate.buffer][candidate.indexOffset / 3] & 3;
                    if (low != 1 && low != 2) { first = candidate; chosen = i; break; }
                }
                if (first) {
                    require(state.triangle.buffer == first->buffer && state.triangle.indexOffset == first->indexOffset &&
                                state.selectedNode == &terrain.heightmapNode() && std::isfinite(result),
                            "Every retail full-overlap record selects its first class-eligible candidate");
                    ++accepted;
                    if (chosen + 1 == candidates.size()) ++finalPairs;
                } else {
                    require(result == RetailHeightController::noHeight && state.triangle.buffer == source.buffer &&
                                state.triangle.indexOffset == source.indexOffset && state.vertices == before.vertices,
                            "Every empty/class-exhausted retail overlap list preserves old geometry and identity");
                }
                if (initializationDump) {
                    std::cout << "point " << b << ' ' << off;
                    for (float x : p) std::cout << ' ' << bits(x);
                    std::cout << ' ' << bits(result) << ' ' << state.triangle.buffer << ' ' << state.triangle.indexOffset
                              << ' ' << int(state.changed);
                    for (float x : state.normal) std::cout << ' ' << bits(x);
                    for (float x : state.cached) std::cout << ' ' << bits(x);
                    std::cout << '\n';
                }
                ++pointChecks;
            }
        require(pointChecks > 14000 && accepted > 0 && finalPairs > 0,
                "Complete retail overlap corpus exercises final-pair acceptance as well as misses");
        std::cout << "Retail point attributes: " << pointChecks << " records, " << accepted << " accepted, "
                  << finalPairs << " accepted final pairs.\n";
        // Read actual serialized SMesh bounds from the retained BRES bytes. This
        // test proves their equality with the old vertex-extrema-only boundary;
        // the production decoder is not extended or replaced by this test.
        const auto& bytes = opening.heightMap.bytes;
        const auto word = [&](size_t at) {
            require(at + 4 <= bytes.size(), "Retained BRES metadata range");
            uint32_t value; std::memcpy(&value, bytes.data() + at, 4); return value;
        };
        auto actualNode = sceneNode(mesh);
        size_t meshes = 0;
        for (size_t i = 0; i < word(opening.heightMap.rootOffset + 76); ++i) {
            const auto geometry = word(opening.heightMap.rootOffset + 80) + i * 16;
            if (word(geometry + 8) != 0) continue;
            const auto storedMesh = word(geometry + 12);
            for (size_t k = 0; k < 6; ++k)
                require(word(storedMesh + 20 + k * 4) == bits(actualNode.boundsCm[k / 3][k % 3]),
                        "Actual serialized height bbox equals decoded vertex extrema bit-for-bit");
            ++meshes;
        }
        require(meshes == 1, "One actual serialized height mesh");
        size_t sourceInstances = 0;
        for (const auto& node : opening.heightMap.nodes)
            for (const auto& geometry : node.geometry)
                if (geometry.sourcePath.empty() && geometry.meshName == mesh.name) {
                    require(node.parent == -1 && node.rotation == std::array<float, 4>{0, 0, 0, 1} &&
                                node.scale == RetailHeightPoint{1, 1, 1},
                            "Explicit source-height placement has no unimplemented parent/matrix composition");
                    actualNode.absoluteTranslationCm = node.translation;
                    ++sourceInstances;
                }
        require(sourceInstances == 1, "One actual source-height node instance");
        // This fixture supplies the source placement explicitly. It does not
        // claim that GS3DStuff's complete scene-loading hierarchy is bound.
        RetailHeightTerrain sourceScene(actualNode, opening.navigation);
        size_t sceneChecks = 0;
        for (size_t b = 0; b < mesh.buffers.size(); ++b)
            for (size_t off : {size_t(0), mesh.buffers[b].indices.size() - 3}) {
                const auto& ids = mesh.buffers[b].indices;
                RetailHeightPoint p;
                for (size_t k = 0; k < 3; ++k)
                    p[k] = (((mesh.positions[ids[off]][k] + mesh.positions[ids[off + 1]][k]) +
                             mesh.positions[ids[off + 2]][k]) / 3.f) + actualNode.absoluteTranslationCm[k];
                RetailHeightController scene(sourceScene, world, 2);
                scene.initializeFromScene(1, p);
                if (initializationDump) {
                    const auto& s = scene.state(1);
                    std::cout << "scene " << b << ' ' << off;
                    for (float x : p) std::cout << ' ' << bits(x);
                    std::cout << ' ' << int(s.hasHeightmap) << ' ' << s.triangle.buffer << ' ' << s.triangle.indexOffset
                              << ' ' << int(s.changed);
                    for (float x : s.normal) std::cout << ' ' << bits(x);
                    for (float x : s.cached) std::cout << ' ' << bits(x);
                    std::cout << '\n';
                }
                require(sourceScene.generation() == 0 && scene.bodies(1).empty(),
                        "Actual source-scene initialization preserves generation/body ownership");
                ++sceneChecks;
            }
        std::cout << "Retail scene attributes: " << sceneChecks << " source-placement queries and exact stored bbox.\n";
    }
    RetailHeightController colliders(terrain, world);
    size_t shapes = 0, maximum = 0, capacityFailures = 0, rangeFailures = 0, frozen = 0;
    std::string firstRangeFailure;
    for (size_t b = 0; b < mesh.buffers.size(); ++b)
        for (size_t off = 0; off < mesh.buffers[b].indices.size(); off += 3) {
            const auto& ids = mesh.buffers[b].indices;
            const auto& a = mesh.positions[ids[off]];
            const auto& v = mesh.positions[ids[off + 1]];
            const auto& c = mesh.positions[ids[off + 2]];
            const float area = (v[0] - a[0]) * (c[1] - a[1]) - (v[1] - a[1]) * (c[0] - a[0]);
            if (!(area > 0))
                continue;
            const RetailHeightPoint point{(a[0] + v[0] + c[0]) / 3.f, (a[1] + v[1] + c[1]) / 3.f,
                                          (a[2] + v[2] + c[2]) / 3.f};
            try {
                colliders.initializeSelected(0, {static_cast<uint16_t>(b), static_cast<uint16_t>(off)},
                                             point);
            } catch (const std::runtime_error& error) {
                if (std::string(error.what()).find("outside world proxy range") != std::string::npos) {
                    ++rangeFailures;
                    if (dump)
                        std::cout << "range-error " << error.what() << '\n';
                    if (firstRangeFailure.empty())
                        firstRangeFailure = error.what();
                    colliders.reset();
                } else if (std::string(error.what()).find("capacity") != std::string::npos) {
                    ++capacityFailures;
                    colliders.reset();
                } else
                    throw;
            }
            require(world.GetBodyCount() == 1 + static_cast<int>(colliders.bodies(0).size()),
                    "Retail cliff audit body ownership");
            require(world.GetProxyCount() == static_cast<int>(colliders.bodies(0).size()),
                    "Every owned retail cliff shape has an actual broad-phase proxy");
            maximum = std::max(maximum, colliders.bodies(0).size());
            shapes += colliders.bodies(0).size();
            for (const auto* body : colliders.bodies(0))
                if (body->IsFrozen())
                    ++frozen;
        }
    std::cout << "Retail cliff enumeration: " << shapes << " body instances, max " << maximum << " per slot, "
              << capacityFailures << " explicit capacity errors, " << rangeFailures
              << " explicit range errors, " << frozen << " frozen instances.\n";
    if (!firstRangeFailure.empty())
        std::cout << firstRangeFailure << '\n';
    require(!capacityFailures, "Retail supplied terrain exceeds supported local-body capacity");
    require(rangeFailures == 254 && shapes == 16330, "Original-world supplied cliff compatibility count");
    // Test-only expanded bounds: validate all native polygon construction and
    // actual proxy/body lifetime without changing the original production world.
    b2AABB expanded = bounds();
    expanded.upperBound.x = 2500;
    b2World geometryWorld(expanded, b2Vec2(0, 0), true);
    RetailHeightTerrain geometryTerrain(mesh, opening.navigation);
    RetailHeightController geometry(geometryTerrain, geometryWorld);
    size_t fullShapes = 0, fullMax = 0;
    for (size_t b = 0; b < mesh.buffers.size(); ++b)
        for (size_t off = 0; off < mesh.buffers[b].indices.size(); off += 3) {
            const auto& ids = mesh.buffers[b].indices;
            const auto& a = mesh.positions[ids[off]];
            const auto& v = mesh.positions[ids[off + 1]];
            const auto& c = mesh.positions[ids[off + 2]];
            if (!((v[0] - a[0]) * (c[1] - a[1]) - (v[1] - a[1]) * (c[0] - a[0]) > 0))
                continue;
            RetailHeightPoint point{(a[0] + v[0] + c[0]) / 3.f, (a[1] + v[1] + c[1]) / 3.f,
                                    (a[2] + v[2] + c[2]) / 3.f};
            geometry.initializeSelected(0, {static_cast<uint16_t>(b), static_cast<uint16_t>(off)}, point);
            require(geometryWorld.GetProxyCount() == static_cast<int>(geometry.bodies(0).size()),
                    "Expanded fixture every native cliff has an actual proxy");
            require(geometryWorld.GetBodyCount() == 1 + static_cast<int>(geometry.bodies(0).size()),
                    "Expanded fixture ownership remains exact");
            fullShapes += geometry.bodies(0).size();
            fullMax = std::max(fullMax, geometry.bodies(0).size());
        }
    require(fullShapes == 17085 && fullMax == 12, "Complete supplied cliff geometry count");
    std::cout << "Test-only expanded-world cliff enumeration: " << fullShapes << " actual proxies, max "
              << fullMax << ".\n";
}
void outsideWorld() {
    Fixture f({{100, 0, 0}, {0, 0, 0}, {100, 0, 100}, {220100, 0, 0}, {220000, 0, 0}, {220100, 0, 100}},
              {0, 1, 2, 3, 4, 5});
    f.nav.cliffVertexKeys = {0, 1, 1, 1, 2, 1, 3, 1, 4, 1, 5, 1};
    f.nav.cliffTriangles = {0, 0, 0, 3, 3, 3};
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world);
    height.initializeSelected(0, {0, 0}, {50, 10, 0});
    require(height.bodies(0).size() == 1 && world.GetProxyCount() == 1,
            "Range fixture has initial real collider");
    auto* previous = height.bodies(0).front();
    bool rejected = false;
    try {
        height.initializeSelected(0, {0, 3}, {220050, 10, 0});
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("outside world proxy range") != std::string::npos;
    }
    require(rejected, "Outside native cliff reports explicit unsupported range");
    require(height.bodies(0).size() == 1 && height.bodies(0).front() == previous &&
                world.GetProxyCount() == 1,
            "Outside preflight preserves older bodies and real proxies");
    require(height.state(0).triangle.indexOffset == 3 && height.state(0).cached[0] == 220050,
            "Range failure explicitly retains partially changed query state");
    height.reset();
    require(world.GetBodyCount() == 1 && world.GetProxyCount() == 0,
            "Range failure reset releases preserved ownership");
}

void cliffs() {
    Fixture f({{100, 0, 0}, {0, 0, 0}, {100, 0, 100}}, {0, 1, 2});
    f.nav.cliffVertexKeys = {0, 1, 1, 1, 2, 1};
    f.nav.cliffTriangles = {0, 0, 0};
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world);
    height.initializeSelected(0, {0, 0}, {50, 10, 0});
    require(height.bodies(0).size() == 1, "One physical cliff strip after duplicate anchors");
    auto* body = height.bodies(0).front();
    require(body->IsStatic(), "Cliff must be static");
    auto* shape = static_cast<b2PolygonShape*>(body->GetShapeList());
    require(shape->GetVertexCount() == 4, "Four physical vertices");
    const auto* vertices = shape->GetVertices();
    const std::array<b2Vec2, 4> expected{b2Vec2(0, -.1f), b2Vec2(1, -.1f), b2Vec2(1, 0), b2Vec2(0, 0)};
    for (size_t i = 0; i < 4; ++i) {
        close(vertices[i].x, expected[i].x, "Strip X");
        close(vertices[i].y, expected[i].y, "Strip Y");
    }
    const auto filter = shape->GetFilterData();
    require(filter.categoryBits == 0 && filter.maskBits == 0xffff &&
                filter.groupIndex == height.groupIndex() && !shape->IsSensor(),
            "Actual cliff filter");
    close(shape->GetFriction(), 1, "Final friction");
    close(shape->GetRestitution(), 0, "Restitution");
    auto* metadata = static_cast<RetailPhysicsBodyData*>(body->GetUserData());
    require(metadata && !metadata->owner && !metadata->nodeBounds && !metadata->jumpSurface,
            "Typed owned terrain metadata");
    close(metadata->heightsCm[0], 0, "Face lower Z");
    close(metadata->heightsCm[1], 100, "Face upper Z");
    height.setGroundState(0, false);
    height.updateEdges(0);
    require(height.bodies(0).front() == body, "Airborne update retains bodies");
    height.setGroundState(0, true);
    height.updateEdges(0);
    require(height.bodies(0).size() == 1, "Landing rebuild");
    height.reset();
    require(height.bodies(0).empty(), "Reset destroys tracked bodies");
    RetailHeightController wide(terrain, world, 2);
    wide.initializeSelected(0, {0, 0}, {50, 10, 0});
    const auto* wideVertices =
        static_cast<b2PolygonShape*>(wide.bodies(0).front()->GetShapeList())->GetVertices();
    close(wideVertices[0].y, -1, "Multi-slot width is one metre");
    b2BodyDef actorDef;
    auto* supplied = world.CreateBody(&actorDef);
    supplied->SetLinearVelocity(b2Vec2(0, 1));
    RetailHeightController velocity(terrain, world, 1, supplied);
    velocity.initializeSelected(0, {0, 0}, {50, 10, 0});
    require(velocity.bodies(0).empty(), "Constructor body velocity gates cliff generation");
    RetailHeightController nullBody(terrain, world);
    nullBody.initializeSelected(0, {0, 0}, {50, 10, 0});
    require(nullBody.bodies(0).size() == 1, "Null constructor body is not a later actor body");
}
void translationAndPool() {
    Fixture f({{0, 0, 10}, {100, 0, 30}, {0, 100, 50}}, {0, 1, 2});
    RetailHeightTerrain terrain(f.mesh, f.nav, {10, 20, 30});
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController height(terrain, world);
    height.initializeSelected(0, {0, 0}, {35, 45, 0});
    close(height.height({35, 45, 900}), 55, "Translation-only vertices");
    std::vector<std::unique_ptr<RetailHeightController>> actors;
    for (size_t i = 0; i < 49; ++i)
        actors.push_back(std::make_unique<RetailHeightController>(terrain, world));
    bool exhausted = false;
    try {
        RetailHeightController tooMany(terrain, world);
    } catch (const std::runtime_error&) {
        exhausted = true;
    }
    require(exhausted, "Pool exhaustion is explicit");
    actors.pop_back();
    RetailHeightController reused(terrain, world);
    require(reused.groupIndex() == 50, "Released pool slot reused");
}
} // namespace
int wmain(int argc, wchar_t** argv) {
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    try {
        const auto baseline = b2_byteCount;
        basicHeight();
        sceneInitialization();
        liveSceneBoundsAndClassification();
        pointHeightAttributes();
        mixedSceneSources();
        sceneSeedBodyBoundary();
        neighborAndCache();
        overlapAndMixedReseed();
        originalOracleCases();
        originalWrapperReseed();
        allNeighborEdges();
        capacity();
        shortEdgeOracleCases();
        edgeGatesAndWrap();
        contactLifetime();
        cliffs();
        outsideWorld();
        translationAndPool();
        if (argc >= 2)
            retailAssets(argv[1], argc == 3 && std::wstring(argv[2]) == L"--oracle-dump",
                         argc == 3 && std::wstring(argv[2]) == L"--initialization-oracle-dump");
        require(b2_byteCount == baseline,
                "Terrain queries, probes and body ownership release all physics allocations");
        std::cout << "Retail height and actual cliff body fixtures passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
