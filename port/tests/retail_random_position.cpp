#include "retail_random_position.h"
#include "filesystem.h"
#include "retail_scene.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Point = RetailHeightPoint;
using Ref = CityNavigation::TriangleRef;
using Cursor = RetailHeightState::RandomEdge;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::uint32_t bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
struct Random final : RetailRandomPositionSource {
    std::vector<std::int32_t> values;
    std::size_t consumed = 0;
    std::int32_t lrand48() override {
        if (consumed == values.size())
            throw std::runtime_error("Unexpected original random consumption.");
        return values[consumed++];
    }
};
struct Fixture {
    CityMesh mesh;
    CityNavigation nav;
    Fixture() {
        mesh.positions = {{0, 0, 0},    {100, 0, 10}, {0, 100, 20},   {100, 100, 30},
                          {300, 0, 40}, {400, 0, 50}, {300, 100, 60}, {400, 100, 70}};
        const std::vector<std::uint16_t> indices{0, 1, 2, 1, 3, 2, 4, 5, 6, 5, 7, 6};
        mesh.buffers.resize(8);
        nav.buffers.resize(8);
        nav.materialBuffers = {1, 0, 5, 2, 4, 3, 6, 7};
        nav.cliffVertexKeys.resize(mesh.positions.size() * 2);
        for (std::size_t b = 0; b < 8; ++b) {
            mesh.buffers[b].indices = indices;
            auto &data = nav.buffers[b];
            data.roadFlags.resize(4);
            data.overlaps.resize(4);
            for (std::size_t e = 0; e < 8; ++e)
                data.neighbors[e].assign(4, e < 6 ? UINT16_MAX : 0);
        }
    }
    void link(Ref from, std::size_t edge, Ref to) {
        auto &data = nav.buffers[from.buffer];
        data.neighbors[edge][from.indexOffset / 3] = to.indexOffset;
        data.neighbors[edge + 3][from.indexOffset / 3] = to.buffer;
    }
};
template <class T> void array(const T &values) {
    std::cout << '[';
    bool first = true;
    for (const auto &value : values) {
        if (!first)
            std::cout << ',';
        first = false;
        std::cout << +value;
    }
    std::cout << ']';
}
void pointBits(const Point &point) {
    array(std::array{bits(point[0]), bits(point[1]), bits(point[2])});
}
void dumpCase(const Fixture &f, const Point &translation, Ref selected, const Cursor &before, int mode,
              const Point &position, const Random &random, const Point &point, const Cursor &after) {
    std::cout << "{\"vertices\":[";
    for (std::size_t i = 0; i < f.mesh.positions.size(); ++i) {
        if (i)
            std::cout << ',';
        pointBits(f.mesh.positions[i]);
    }
    std::cout << "],\"indices\":[";
    for (std::size_t i = 0; i < 8; ++i) {
        if (i)
            std::cout << ',';
        array(f.mesh.buffers[i].indices);
    }
    std::cout << "],\"lanes\":[";
    for (std::size_t b = 0; b < 8; ++b) {
        if (b)
            std::cout << ',';
        std::cout << '[';
        for (std::size_t e = 0; e < 8; ++e) {
            if (e)
                std::cout << ',';
            array(f.nav.buffers[b].neighbors[e]);
        }
        std::cout << ']';
    }
    std::cout << "],\"flags\":[";
    for (std::size_t b = 0; b < 8; ++b) {
        if (b)
            std::cout << ',';
        array(f.nav.buffers[b].roadFlags);
    }
    std::cout << "],\"materials\":";
    array(f.nav.materialBuffers);
    std::cout << ",\"translation\":";
    pointBits(translation);
    std::cout << ",\"selected\":";
    array(std::array{selected.buffer, selected.indexOffset});
    const auto cursor = [](const Cursor &c) {
        array(std::array<std::uint16_t, 5>{c.edge, c.triangle.indexOffset, c.triangle.buffer, c.laneOrdinal,
                                           std::uint16_t(c.intersectionSeen)});
    };
    std::cout << ",\"state\":";
    cursor(before);
    std::cout << ",\"mode\":" << mode << ",\"position\":";
    pointBits(position);
    std::cout << ",\"rolls\":";
    array(random.values);
    std::cout << ",\"point\":";
    pointBits(point);
    std::cout << ",\"after\":";
    cursor(after);
    std::cout << ",\"consumed\":" << random.consumed << "}\n";
}
Point execute(Fixture &f, Ref selected, Cursor &state, int mode, Point position,
              std::vector<std::int32_t> rolls, bool dump, Point translation = {}) {
    RetailHeightTerrain terrain(f.mesh, f.nav, translation);
    Random random;
    random.values = std::move(rolls);
    RetailRandomPosition query(terrain, random);
    RetailHeightState slot;
    slot.triangle = selected;
    slot.randomEdge = state;
    const auto visits = terrain.visitWords();
    const auto before = state;
    const auto point = query.next(slot, mode, position);
    state = slot.randomEdge;
    require(terrain.visitWords() == visits && terrain.generation() == 0,
            "Random-position search does not modify ordinary height visit generations");
    if (dump)
        dumpCase(f, translation, selected, before, mode, position, random, point, state);
    return point;
}
void synthetic(bool dump) {
    Fixture f;
    f.link({0, 0}, 0, {0, 3});
    f.link({0, 3}, 2, {0, 0});
    Cursor state;
    auto point = execute(f, {0, 0}, state, 0, {0, 0, 0}, {0}, dump);
    require(point == Point{50, 0, 5} && state.edge == 0 && state.triangle.indexOffset == 0,
            "First pedestrian random edge midpoint");
    point = execute(f, {0, 0}, state, 0, {0, 0, 0}, {0}, dump);
    require(point == Point{50, 50, 15} && state.edge == 2 && state.triangle.indexOffset == 3,
            "Next call crosses cached edge and retains sole backtracking edge");
    for (auto ordinal : {0, 1, 2, int(UINT16_MAX)})
        for (auto flags : {0, 64, 128, 192})
            for (auto roll : {0, 1, 2, 2147483647}) {
                f.nav.buffers[0].roadFlags.assign(4, std::uint8_t(flags));
                state = {};
                state.laneOrdinal = std::uint16_t(ordinal);
                execute(f, {0, 0}, state, 0, {3, -5, 77}, {roll}, dump, {13, -29, 79});
                require(state.laneOrdinal <= std::uint16_t(flags ? flags / 64 : 1),
                        "Original ordinal clamps to encoded lane count or one");
            }
    // All three candidates, cross-buffer backtracking comparison and wrong-way fields.
    for (int mode : {0, 1})
        for (int raw : {0, 1, 2, 3, 4, 8, 12, 16, 32, 48, 64, 255})
            for (int roll : {0, 1, 2}) {
                Fixture all;
                const std::uint16_t b = std::uint16_t(mode ? 1 : 0);
                for (std::size_t t = 0; t < 4; ++t)
                    for (std::size_t e = 0; e < 3; ++e)
                        all.link({b, std::uint16_t(t * 3)}, e, {b, std::uint16_t(((t + e + 1) % 4) * 3)});
                all.nav.buffers[b].roadFlags.assign(4, std::uint8_t(raw));
                state = {};
                execute(all, {b, 0}, state, mode, {77, 11, -50}, {roll, 0}, dump);
            }
    // No permitted neighbor invokes real sidewalk fallback and consumes twice.
    for (int roll : {0, 1, 2, 3, 5, 6, 8, 9, 11, 2147483647}) {
        Fixture none;
        state = {};
        execute(none, {2, 0}, state, 0, {350, 50, 60}, {0, roll}, dump);
        require(state.edge == UINT16_MAX && state.triangle.buffer == 0,
                "Fallback uses authored sidewalk material and clears edge");
    }
    Fixture omitted;
    const Point exactCenter{200.f * (1.f / 3.f), 200.f * (1.f / 3.f), 20};
    state = {};
    execute(omitted, {2, 0}, state, 0, exactCenter, {0, 6}, dump);
    require(state.triangle.indexOffset == 0,
            "Start2 fallback omits predecessor1 even when it is the exact nearest center");
    state = {};
    point = execute(omitted, {2, 0}, state, 0, exactCenter, {0, 3}, dump);
    require(state.triangle.indexOffset == 3 && point == exactCenter,
            "Start1 fallback visits the complete ascending scan including exact center");
    // Previous offset comparison deliberately ignores the previous buffer.
    Fixture crossing;
    crossing.link({0, 0}, 0, {5, 3});
    crossing.link({5, 3}, 0, {0, 0});
    crossing.link({5, 3}, 1, {5, 0});
    state.edge = 0;
    state.triangle = {0, 0};
    state.laneOrdinal = 1;
    execute(crossing, {0, 0}, state, 0, {0, 0, 0}, {1}, dump);
    Fixture road;
    road.link({1, 0}, 0, {4, 3});
    road.link({4, 3}, 2, {1, 0});
    state = {};
    execute(road, {1, 0}, state, 1, {0, 0, 0}, {0}, dump);
    require(state.intersectionSeen, "Road cursor with intersection neighbor sets persistent marker");
    Fixture noVehicleChoice;
    state = {};
    point = execute(noVehicleChoice, {1, 0}, state, 1, {0, 0, 0}, {0}, dump);
    require(state.edge == 0 && point == Point{50, 0, 5},
            "Vehicle with no candidate retains original edge0 default without pedestrian fallback");
    Fixture noReciprocal;
    noReciprocal.link({1, 0}, 0, {1, 3});
    noReciprocal.nav.buffers[1].roadFlags[0] = 16;
    state = {};
    point = execute(noReciprocal, {1, 0}, state, 1, {0, 0, 0}, {0}, dump);
    require(state.edge == 0 && state.triangle.indexOffset == 3 && point == Point{100, 50, 20},
            "Vehicle hop without reciprocal lane preserves prior edge instead of inventing a replacement");
    for (int mode : {0, 1}) {
        Fixture blocked;
        const auto b = std::uint16_t(mode ? 1 : 0);
        blocked.link({b, 0}, 0, {2, 3});
        blocked.link({b, 0}, 1, {b, 6});
        state = {};
        point = execute(blocked, {b, 0}, state, mode, {0, 0, 0}, {0}, dump);
        require(
            state.edge == 1 && point == Point{50, 50, 15},
            "Initial and random scans reject authored blocked material costs before selecting the next edge");
    }
    state = {};
    execute(f, {0, UINT16_MAX}, state, 99, {13, -29, 79}, {}, dump);
}
void ownershipAndErrors() {
    Fixture f;
    f.link({0, 0}, 0, {0, 3});
    f.link({0, 3}, 2, {0, 0});
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2AABB bounds;
    bounds.lowerBound.Set(-2000, -2000);
    bounds.upperBound.Set(2000, 2000);
    b2World world(bounds, b2Vec2(0, 0), true);
    RetailHeightController controller(terrain, world, 2);
    Random random;
    random.values = {0, 0, 0};
    RetailRandomPosition query(terrain, random);
    controller.initializeSelected(1, {0, 0}, {10, 10, 0});
    query.next(controller, 1, 0, {10, 10, 0});
    require(controller.state(0).randomEdge.edge == UINT16_MAX && controller.state(1).randomEdge.edge == 0,
            "Each real native slot owns its cursor");
    controller.initializeSelected(1, {0, 0}, {10, 10, 0});
    require(controller.state(1).randomEdge.edge == 0,
            "Explicit height initialization preserves random cursor");
    controller.reset();
    const auto &state = controller.state(1).randomEdge;
    require(state.edge == UINT16_MAX && state.triangle.buffer == UINT16_MAX &&
                state.triangle.indexOffset == UINT16_MAX && state.laneOrdinal == 1 && !state.intersectionSeen,
            "Real height reset restores original random cursor defaults");
    RetailHeightTerrain otherTerrain(f.mesh, f.nav);
    RetailHeightController other(otherTerrain, world);
    bool failed = false;
    try {
        query.next(other, 0, 0, {});
    } catch (const std::invalid_argument &) {
        failed = true;
    }
    require(failed, "Different native terrain/controller bindings reject explicitly");
    RetailHeightState slot;
    slot.triangle = {0, 0};
    failed = false;
    try {
        query.next(slot, 2, {});
    } catch (const std::invalid_argument &) {
        failed = true;
    }
    require(failed, "Out of range original cost mode rejects");
    Random bad;
    bad.values = {-1};
    RetailRandomPosition invalid(terrain, bad);
    failed = false;
    try {
        invalid.next(slot, 0, {});
    } catch (const std::runtime_error &) {
        failed = true;
    }
    require(failed, "Negative lrand48 result is a binding error");
    slot.randomEdge.edge = 1;
    slot.randomEdge.triangle = {0, 0};
    failed = false;
    try {
        query.next(slot, 0, {});
    } catch (const std::runtime_error &) {
        failed = true;
    }
    require(failed, "Absent cached neighbor is explicit original undefined binding");
    Fixture none;
    RetailHeightTerrain noNeighbors(none.mesh, none.nav);
    Random far;
    far.values = {0, 0};
    RetailRandomPosition fallback(noNeighbors, far);
    slot.randomEdge = {};
    slot.triangle = {2, 0};
    failed = false;
    try {
        fallback.next(slot, 0, {1000000, 1000000, 1000000});
    } catch (const std::runtime_error &) {
        failed = true;
    }
    require(failed && far.consumed == 2,
            "No initialized fallback triangle is explicit after exact two RNG calls");
    RetailHeightSceneNode live;
    live.type = retailSceneMeshType;
    live.mesh = &f.mesh;
    RetailHeightTerrain liveTerrain(live, f.nav);
    Random liveRandom;
    liveRandom.values = {0, 0};
    RetailRandomPosition liveQuery(liveTerrain, liveRandom);
    RetailHeightState liveSlot;
    liveSlot.triangle = {0, 0};
    const auto first = liveQuery.next(liveSlot, 0, {});
    live.absoluteTranslationCm = {13, -29, 79};
    liveSlot.randomEdge = {};
    const auto second = liveQuery.next(liveSlot, 0, {});
    require(second == Point{first[0] + 13, first[1] - 29, first[2] + 79},
            "Live global height translation is reread between serial queries");
}
void retail(const std::filesystem::path &path, bool dump) {
    ResourceFileSystem files(path);
    const auto city = loadCityAssets(files);
    auto opening = loadCityOpening(files, city);
    RetailHeightSceneBinding scene(std::move(opening.heightMap));
    const auto &node = scene.globalHeightmap();
    RetailHeightTerrain terrain(node, opening.navigation);
    Random random;
    RetailRandomPosition query(terrain, random);
    std::size_t checked = 0, emitted = 0;
    bool geometry = false;
    for (int mode : {0, 1})
        for (std::size_t b = 0; b < node.mesh->buffers.size(); ++b) {
            const auto &materials = opening.navigation.materialBuffers;
            if (mode == 0 && b != materials[1] && b != materials[2])
                continue;
            if (mode == 1 && b != materials[0] && b != materials[4])
                continue;
            const auto &indices = node.mesh->buffers[b].indices;
            for (std::size_t off = 0; off < indices.size(); off += 3) {
                const auto &a = node.mesh->positions[indices[off]],
                           &v = node.mesh->positions[indices[off + 1]],
                           &c = node.mesh->positions[indices[off + 2]];
                Point position;
                for (std::size_t k = 0; k < 3; ++k)
                    position[k] = (c[k] + (a[k] + v[k])) * (1.f / 3.f) + node.absoluteTranslationCm[k];
                RetailHeightState state;
                state.triangle = {std::uint16_t(b), std::uint16_t(off)};
                random.values = {std::int32_t((off + b) % 3), std::int32_t(off * 13 + b)};
                random.consumed = 0;
                const auto result = query.next(state, mode, position);
                ++checked;
                if (dump) {
                    if (!geometry) {
                        Fixture exportData;
                        exportData.mesh = *node.mesh;
                        exportData.nav = opening.navigation;
                        std::cout << "retail-data ";
                        dumpCase(exportData, node.absoluteTranslationCm,
                                 {std::uint16_t(b), std::uint16_t(off)}, {}, mode, position, random, result,
                                 state.randomEdge);
                        geometry = true;
                    } else {
                        std::cout << "retail {\"selected\":";
                        array(std::array<std::uint16_t, 2>{std::uint16_t(b), std::uint16_t(off)});
                        std::cout << ",\"state\":[65535,65535,65535,1,0],\"mode\":" << mode
                                  << ",\"position\":";
                        pointBits(position);
                        std::cout << ",\"rolls\":";
                        array(random.values);
                        std::cout << ",\"point\":";
                        pointBits(result);
                        const auto &s = state.randomEdge;
                        std::cout << ",\"after\":";
                        array(std::array<std::uint16_t, 5>{s.edge, s.triangle.indexOffset, s.triangle.buffer,
                                                           s.laneOrdinal, std::uint16_t(s.intersectionSeen)});
                        std::cout << ",\"consumed\":" << random.consumed << "}\n";
                    }
                    ++emitted;
                }
            }
        }
    std::cout << "Retail random-position queries " << checked << ", original-reference samples " << emitted
              << ".\n";
    require(checked > 1000, "Actual supplied pedestrian/vehicle navigation enumeration");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        const bool dump = argc >= 2 && std::wstring(argv[argc - 1]) == L"--oracle-dump";
        synthetic(dump);
        ownershipAndErrors();
        if (argc >= 2 && std::wstring(argv[1]) != L"--oracle-dump")
            retail(argv[1], dump);
        std::cout << "Original random-position cursor/candidate/fallback fixtures passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
