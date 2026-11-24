#include "filesystem.h"
#include "retail_height.h"
#include "retail_scene.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Point = RetailHeightPoint;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::uint32_t bits(float value) {
    std::uint32_t out;
    std::memcpy(&out, &value, 4);
    return out;
}
b2AABB bounds() {
    b2AABB b;
    b.lowerBound.Set(-2000, -2000);
    b.upperBound.Set(2000, 2000);
    return b;
}
// Test-only arrangement of the actual nonconst controller storage. No mutable
// production state API or original-layout/pointer cast is introduced.
RetailHeightState &arrange(RetailHeightController &c, std::size_t slot) {
    return const_cast<RetailHeightState &>(c.state(slot));
}
struct Geometry {
    CityMesh mesh;
    CityNavigation nav;
    Geometry() {
        mesh.positions = {{0, 0, 10},    {100, 0, 30},  {0, 100, 50},
                          {200, 0, 100}, {300, 0, 100}, {200, 100, 200}};
        mesh.buffers.push_back({"", {0, 1, 2, 3, 4, 5}});
        nav.buffers.resize(1);
        nav.materialBuffers.fill(0);
        auto &b = nav.buffers[0];
        b.roadFlags.resize(2);
        b.overlaps.resize(2);
        for (std::size_t n = 0; n < 8; ++n)
            b.neighbors[n].assign(2, n < 6 ? UINT16_MAX : 0);
        nav.cliffVertexKeys.resize(mesh.positions.size() * 2);
    }
};
template <class T> void array(const T &values) {
    std::cout << '[';
    bool first = true;
    for (auto v : values) {
        if (!first)
            std::cout << ',';
        first = false;
        std::cout << +v;
    }
    std::cout << ']';
}
void point(const Point &p) {
    array(std::array{bits(p[0]), bits(p[1]), bits(p[2])});
}
void state(const RetailHeightState &s, const RetailHeightSceneNode &global,
           const RetailHeightSceneNode *other) {
    std::cout << "{\"triangle\":";
    array(std::array{s.triangle.buffer, s.triangle.indexOffset});
    std::cout << ",\"node\":"
              << (s.selectedNode == &global          ? 1
                  : s.selectedNode == other && other ? 2
                                                     : 0)
              << ",\"vertices\":[";
    for (std::size_t i = 0; i < 3; ++i) {
        if (i)
            std::cout << ',';
        point(s.vertices[i]);
    }
    std::cout << "],\"normal\":";
    point(s.normal);
    std::cout << ",\"changed\":" << s.changed << ",\"cache\":";
    point(s.cached);
    std::cout << ",\"ground\":";
    array(std::array{s.onGround, s.wasOnGround});
    const auto &r = s.randomEdge;
    std::cout << ",\"random\":";
    array(std::array<std::uint16_t, 5>{r.edge, r.triangle.indexOffset, r.triangle.buffer, r.laneOrdinal,
                                       std::uint16_t(r.intersectionSeen)});
    std::cout << '}';
}
void geometry(const CityMesh &mesh, const CityNavigation &nav, const Point &translation) {
    std::cout << "geometry {\"vertices\":[";
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        if (i)
            std::cout << ',';
        point(mesh.positions[i]);
    }
    std::cout << "],\"indices\":[";
    for (std::size_t i = 0; i < mesh.buffers.size(); ++i) {
        if (i)
            std::cout << ',';
        array(mesh.buffers[i].indices);
    }
    std::cout << "],\"lanes\":[";
    for (std::size_t b = 0; b < nav.buffers.size(); ++b) {
        if (b)
            std::cout << ',';
        std::cout << '[';
        for (std::size_t e = 0; e < 8; ++e) {
            if (e)
                std::cout << ',';
            array(nav.buffers[b].neighbors[e]);
        }
        std::cout << ']';
    }
    std::cout << "],\"overlaps\":[";
    for (std::size_t b = 0; b < nav.buffers.size(); ++b) {
        if (b)
            std::cout << ',';
        std::cout << '[';
        for (std::size_t t = 0; t < nav.buffers[b].overlaps.size(); ++t) {
            if (t)
                std::cout << ',';
            std::cout << '[';
            bool first = true;
            for (auto ref : nav.buffers[b].overlaps[t]) {
                if (!first)
                    std::cout << ',';
                first = false;
                array(std::array{ref.buffer, ref.indexOffset});
            }
            std::cout << ']';
        }
        std::cout << ']';
    }
    std::cout << "],\"translation\":";
    point(translation);
    std::cout << "}\n";
}
float run(RetailHeightController &c, RetailHeightTerrain &terrain, std::size_t from, std::size_t to,
          const Point &p, bool dump, int aliasSlot = -1, int aliasField = -1,
          const RetailHeightSceneNode *other = nullptr, bool continuous = false) {
    const std::array<RetailHeightState, 2> before{c.state(0), c.state(1)};
    const auto generation = terrain.generation();
    const auto initialVisits = terrain.visitWords();
    const float result = c.heightFrom(from, p, to);
    if (dump) {
        std::cout << "case {\"source\":" << from << ",\"target\":" << to << ",\"point\":";
        point(p);
        std::cout << ",\"alias\":";
        array(std::array{aliasSlot, aliasField});
        std::cout << ",\"before\":[";
        state(before[0], terrain.heightmapNode(), other);
        std::cout << ',';
        state(before[1], terrain.heightmapNode(), other);
        std::cout << "],\"after\":[";
        state(c.state(0), terrain.heightmapNode(), other);
        std::cout << ',';
        state(c.state(1), terrain.heightmapNode(), other);
        std::cout << "],\"generationBefore\":" << generation
                  << ",\"generationAfter\":" << terrain.generation() << ",\"result\":" << bits(result)
                  << ",\"visitsBefore\":";
        if (continuous)
            std::cout << "null";
        else {
            std::cout << '[';
            for (std::size_t b = 0; b < initialVisits.size(); ++b) {
                if (b)
                    std::cout << ',';
                array(initialVisits[b]);
            }
            std::cout << ']';
        }
        std::cout << ",\"visitChanges\":[";
        bool first = true;
        for (std::size_t b = 0; b < initialVisits.size(); ++b)
            for (std::size_t t = 0; t < initialVisits[b].size(); ++t)
                if (initialVisits[b][t] != terrain.visitWords()[b][t]) {
                    if (!first)
                        std::cout << ',';
                    first = false;
                    array(std::array<std::size_t, 4>{b, t, initialVisits[b][t], terrain.visitWords()[b][t]});
                }
        std::cout << "]}\n";
    }
    return result;
}
void synthetic(bool dump) {
    Geometry g;
    if (dump)
        geometry(g.mesh, g.nav, {});
    for (int sourceNode : {0, 1, 2})
        for (int targetNode : {0, 1})
            for (int same : {0, 1})
                for (int alias : {-1, 0, 1, 2, 3}) {
                    RetailHeightTerrain terrain(g.mesh, g.nav);
                    b2World world(bounds(), b2Vec2(0, 0), true);
                    RetailHeightController c(terrain, world, 2);
                    c.initializeSelected(0, {0, 0}, {25, 25, 77});
                    c.initializeSelected(1, {0, 3}, {225, 25, 188});
                    RetailHeightSceneNode secondary = terrain.heightmapNode();
                    auto &a = arrange(c, 0);
                    auto &b = arrange(c, 1);
                    a.selectedNode =
                        sourceNode ? sourceNode == 1 ? &terrain.heightmapNode() : &secondary : nullptr;
                    a.hasHeightmap = sourceNode != 0;
                    b.selectedNode = targetNode ? &terrain.heightmapNode() : nullptr;
                    b.hasHeightmap = targetNode != 0;
                    a.randomEdge = {2, {0, 3}, 3, true};
                    b.randomEdge = {1, {0, 0}, 2, false};
                    a.onGround = a.wasOnGround = false;
                    b.onGround = b.wasOnGround = true;
                    b.normal = {3, 4, 5};
                    a.changed = true;
                    b.changed = true;
                    const auto to = std::size_t(same ? 0 : 1);
                    const Point external{25, 25, 900};
                    const Point &p = alias < 0    ? external
                                     : alias == 0 ? c.state(to).cached
                                                  : c.state(to).vertices[std::size_t(alias - 1)];
                    const float result =
                        run(c, terrain, 0, to, p, dump, alias < 0 ? -1 : int(to), alias, &secondary);
                    const auto &after = c.state(to);
                    require(after.randomEdge.laneOrdinal == (same ? 3 : 2) &&
                                after.randomEdge.intersectionSeen == bool(same),
                            "Transfer preserves target lane ordinal/intersection state");
                    require(after.onGround == !bool(same) && after.wasOnGround == !bool(same),
                            "Transfer preserves target ground flags");
                    if (sourceNode && !same) {
                        require(after.randomEdge.edge == 2 && after.randomEdge.triangle.indexOffset == 3,
                                "Only three source random identities transfer");
                        require(after.normal == Point{3, 4, 5},
                                "Copied geometry does not copy source normal");
                        if (alias == 0)
                            require(result == 25 && after.cached == Point{25, 25, 25},
                                    "Cache alias observes the source cache copied before query");
                        if (alias == 1)
                            require(result == 10 && after.cached == Point{0, 0, 10},
                                    "Vertex alias observes copied source A before query");
                    }
                }
    for (float sourceZ : {77.f, RetailHeightController::noHeight}) {
        RetailHeightTerrain terrain(g.mesh, g.nav);
        b2World world(bounds(), b2Vec2(0, 0), true);
        RetailHeightController c(terrain, world, 2);
        c.initializeSelected(0, {0, 0}, {25, 25, sourceZ});
        c.initializeSelected(1, {0, 3}, {225, 25, 188});
        const float result = run(c, terrain, 0, 1, {225, 25, 900}, dump);
        require(result == (sourceZ == 77 ? 77 : 900) && c.state(1).triangle.indexOffset == 0 &&
                    !c.state(1).changed,
                "Detailed slot1 failure preserves copied geometry/cache rather than forcing a reseed");
    }
    Geometry crossed;
    crossed.mesh.buffers[0].indices = {0, 2, 1, 3, 4, 5};
    crossed.nav.buffers[0].neighbors[1][0] = 3;
    crossed.nav.buffers[0].neighbors[4][0] = 0;
    if (dump)
        geometry(crossed.mesh, crossed.nav, {});
    for (bool aliased : {false, true}) {
        RetailHeightTerrain terrain(crossed.mesh, crossed.nav);
        b2World world(bounds(), b2Vec2(0, 0), true);
        RetailHeightController c(terrain, world, 2);
        c.initializeSelected(0, {0, 0}, {225, 25, 77});
        c.initializeSelected(1, {0, 3}, {225, 25, 188});
        c.setGroundState(1, false);
        c.setGroundState(1, false);
        const Point external{0, 0, 10};
        const Point &p = aliased ? c.state(1).vertices[0] : external;
        const auto result = run(c, terrain, 0, 1, p, dump, aliased ? 1 : -1, aliased ? 1 : -1);
        require(result == (aliased ? 100 : 77), "Live aliased vertex changes again during the delegated "
                                                "neighbor transition; an external point stays fixed");
    }
}
void bodyOwnership() {
    Geometry g;
    g.mesh.positions = {{100, 0, 0}, {0, 0, 0}, {100, 0, 100}, {0, 0, 10}, {100, 0, 30}, {0, 100, 50}};
    g.nav.cliffVertexKeys = {0, 1, 1, 1, 2, 1, 0, 0, 0, 0, 0, 0};
    g.nav.cliffTriangles = {0, 0, 0};
    RetailHeightTerrain terrain(g.mesh, g.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController c(terrain, world, 2);
    c.setGroundState(1, true);
    c.setGroundState(1, true);
    c.initializeSelected(1, {0, 0}, {50, 10, 0});
    c.initializeSelected(0, {0, 3}, {25, 25, 77});
    const auto bodies = c.bodies(1);
    require(!bodies.empty(), "Actual native target cliff bodies exist before partial transfer");
    require(c.heightFrom(0, {25, 25, 900}, 1) == 25 && c.bodies(1) == bodies && c.bodies(0).empty(),
            "Partial transfer neither clones source bodies nor eagerly refreshes existing target bodies");
    c.setGroundState(1, false);
    c.setGroundState(1, true);
    require(c.heightFrom(0, {25, 25, 900}, 1) == 25 && c.bodies(1).empty() && world.GetBodyCount() == 1,
            "Existing detailed query still performs genuine ground-transition cliff destruction");
}
void retail(const std::filesystem::path &path, bool dump) {
    ResourceFileSystem files(path);
    const auto city = loadCityAssets(files);
    auto opening = loadCityOpening(files, city);
    RetailHeightSceneBinding scene(std::move(opening.heightMap));
    const auto &node = scene.globalHeightmap();
    RetailHeightTerrain terrain(node, opening.navigation);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailHeightController c(terrain, world, 2);
    for (std::size_t s = 0; s < 2; ++s) {
        c.setGroundState(s, false);
        c.setGroundState(s, false);
    }
    if (dump)
        geometry(*node.mesh, opening.navigation, node.absoluteTranslationCm);
    std::size_t count = 0, enumerated = 0;
    for (std::size_t b = 0; b < node.mesh->buffers.size(); ++b)
        for (std::size_t off = 0; off < node.mesh->buffers[b].indices.size(); off += 3) {
            const auto &ids = node.mesh->buffers[b].indices;
            const auto &a = node.mesh->positions[ids[off]], &v = node.mesh->positions[ids[off + 1]],
                       &d = node.mesh->positions[ids[off + 2]];
            ++enumerated;
            const float area = (v[0] - a[0]) * (d[1] - a[1]) - (v[1] - a[1]) * (d[0] - a[0]);
            if (!(area > 0))
                continue; // Same interior-height query domain as existing retail_height tests.
            Point p;
            for (std::size_t k = 0; k < 3; ++k)
                p[k] = (d[k] + (a[k] + v[k])) * (1.f / 3.f) + node.absoluteTranslationCm[k];
            c.initializeSelected(0, {std::uint16_t(b), std::uint16_t(off)}, p);
            c.initializeSelected(1, {std::uint16_t(b), std::uint16_t(off)}, {p[0], p[1], 777});
            const auto result = run(c, terrain, 0, 1, p, dump, -1, -1, nullptr, true);
            require(c.bodies(0).empty() && c.bodies(1).empty() && world.GetBodyCount() == 1,
                    "Copied-height query does not manufacture or clone local cliff bodies");
            (void)result;
            ++count;
        }
    std::cout << "Retail copied-height interior triangles " << count << ", enumerated " << enumerated
              << ".\n";
    require(enumerated == 14576 && count > 10000,
            "Complete supplied triangle enumeration and eligible interior query corpus");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        const bool dump = argc >= 2 && std::wstring(argv[argc - 1]) == L"--oracle-dump";
        synthetic(dump);
        bodyOwnership();
        if (argc >= 2 && std::wstring(argv[1]) != L"--oracle-dump")
            retail(argv[1], dump);
        std::cout << "Original conditional height transfer and reference-alias fixtures passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
