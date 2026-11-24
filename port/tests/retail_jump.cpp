#include "retail_jump.h"
#include "retail_height.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Point = std::array<float, 3>;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void close(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < .001f, message);
}
uint32_t bits(float value) {
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
b2AABB bounds() {
    b2AABB value;
    value.lowerBound.Set(-2000, -2000);
    value.upperBound.Set(2000, 2000);
    return value;
}
struct Surface {
    RetailPhysicsBodyData data{nullptr, std::nullopt, {10, 50, 130, 900}, true, 0xa5};
    b2World& world;
    b2Body* body;
    b2Shape* shape;
    Surface(b2World& world, float angle = 0, b2Vec2 position = b2Vec2(0, 0)) : world(world) {
        b2BodyDef definition;
        definition.position = position;
        definition.angle = angle;
        definition.userData = &data;
        body = world.CreateBody(&definition);
        b2PolygonDef polygon;
        polygon.SetAsBox(1, 1);
        shape = body->CreateShape(&polygon);
    }
    ~Surface() { world.DestroyBody(body); }
};
void snapshotAndPlane() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    Surface surface(world);
    RetailPhysicsOwnerState owner{5, 0, 0, 17};
    RetailJumpController jump(owner);
    require(owner.currentJumpState == 0 && !jump.state().enabled,
            "Original constructor initializes live jump state");
    owner.currentJumpState = 7;
    jump.setCurrentShape(*surface.shape);
    require(owner.currentJumpState == 7, "Snapshot does not reset live jump count");
    const std::array<Point, 4> expected{
        {{-100, -100, 10}, {100, -100, 50}, {100, 100, 130}, {-100, 100, 900}}};
    require(jump.state().quadCm == expected,
            "Real polygon vertex order, metres to cm, metadata Z correspondence");
    require(jump.state().centreCm == Point{0, 0, 272.5f} && jump.state().opaqueJumpFlag == 0xa5,
            "Four-point centre and opaque byte copied without bool conversion");
    close(jump.height({0, 0, 0}), 70, "ABC plane at centre independently z=70+0.2x+0.4y");
    require(owner.currentJumpState == 8, "Accepted direct query increments original count");
    const std::array<float, 4> cornerHeights{10, 50, 130, 90};
    for (size_t i = 0; i < expected.size(); ++i)
        close(jump.height({expected[i][0], expected[i][1], 0}), cornerHeights[i],
              "Each original quad boundary vertex is included in the ABC plane");
    close(jump.height({-100, 100, 0}, 999), 90,
          "D boundary included but nonplanar D height does not replace ABC plane");
    close(jump.height({25, -25, 1234}), 65, "Query Z is proximity only, slot ignored");
    owner.currentJumpState = std::numeric_limits<int32_t>::max();
    close(jump.height({0, 0, 0}), 70, "Counter overflow leaves height calculation intact");
    require(owner.currentJumpState == std::numeric_limits<int32_t>::min(),
            "Original counter addition wraps full32bits");
    const auto copied = jump.state().quadCm;
    surface.data.heightsCm = {300, 300, 300, 300};
    surface.data.opaqueJumpFlag = 3;
    surface.body->SetXForm(b2Vec2(5, 7), .5f);
    require(jump.state().quadCm == copied && jump.state().opaqueJumpFlag == 0xa5,
            "Snapshot retains no dependency on later shape or metadata mutation");
    close(jump.height({0, 0, 0}), 70, "Snapshot survives live shape movement");
}
void boundariesAndDisabled() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    Surface surface(world);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailJumpController jump(owner);
    jump.setCurrentShape(*surface.shape);
    owner.currentJumpState = 11;
    require(jump.height({101, 0, 272.5f}) == RetailHeightController::noHeight,
            "Outside XY quad returns sentinel");
    require(jump.state().enabled && owner.currentJumpState == 11,
            "Outside edge neither disables nor resets current jump state");
    for (const Point p : {Point{-101, 0, 272.5f}, Point{0, 101, 272.5f}, Point{0, -101, 272.5f}})
        require(jump.height(p) == RetailHeightController::noHeight && jump.state().enabled &&
                    owner.currentJumpState == 11,
                "Each remaining negative quad-edge side preserves enabled/count");
    close(jump.height({0, 0, std::nextafter(1772.5f, 0.f)}), 70, "Strict distance just below1500cm accepted");
    require(jump.height({0, 0, 1772.5f}) == RetailHeightController::noHeight,
            "Exact1500cm 3D radius is rejected");
    require(!jump.state().enabled && owner.currentJumpState == 0 && jump.state().opaqueJumpFlag == 0xa5,
            "Distance failure clears enabled/count but retains geometry/opaque flag");
    close(jump.height({0, 0, 0}), 70, "Direct GetJumpHeight is not gated by enabled");
    require(!jump.state().enabled && owner.currentJumpState == 1,
            "Direct disabled query increments without enabling");
    jump.setCurrentShape(*surface.shape);
    owner.currentJumpState = 9;
    require(jump.height({0, 0, std::numeric_limits<float>::quiet_NaN()}) == RetailHeightController::noHeight,
            "Unordered radius comparison follows original failed predicate");
    require(!jump.state().enabled && owner.currentJumpState == 0, "NaN radius clears original state");
    RetailPhysicsOwnerState freshOwner{5, 0, 0, 0};
    RetailJumpController fresh(freshOwner);
    require(std::isnan(fresh.height({0, 0, 0})) && freshOwner.currentJumpState == 1 && !fresh.state().enabled,
            "Zero constructor quad preserves direct-query degenerate NaN instead of invented epsilon");
}
void snapshotLifetimeAndValidation() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailJumpController jump(owner);
    {
        Surface surface(world, 1.5707963267948966f, b2Vec2(5, -7));
        jump.setCurrentShape(*surface.shape);
        close(jump.state().centreCm[0], 500, "Translated rotated quad centre X");
        close(jump.state().centreCm[1], -700, "Translated rotated quad centre Y");
        close(jump.height({500, -700, 0}), 70, "Rotated native polygon uses real transform");
    }
    require(world.GetBodyCount() == 1, "Jump snapshot owns no shape/body and does not retain them");
    close(jump.height({500, -700, 0}), 70, "Snapshot valid after original body and metadata destruction");
    b2BodyDef bd;
    auto* body = world.CreateBody(&bd);
    b2CircleDef circle;
    circle.radius = 1;
    auto* shape = body->CreateShape(&circle);
    bool rejected = false;
    try {
        jump.setCurrentShape(*shape);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Nonpolygon unsupported binding is explicit");
    b2PolygonDef polygon;
    polygon.SetAsBox(1, 1);
    shape = body->CreateShape(&polygon);
    rejected = false;
    try {
        jump.setCurrentShape(*shape);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Missing native metadata cannot silently become empty handler");
    RetailPhysicsBodyData data{nullptr, std::nullopt, {0, 0, 0, 0}, true};
    body->SetUserData(&data);
    polygon.vertexCount = 3;
    polygon.vertices[0].Set(-1, -1);
    polygon.vertices[1].Set(1, -1);
    polygon.vertices[2].Set(0, 1);
    shape = body->CreateShape(&polygon);
    rejected = false;
    try {
        jump.setCurrentShape(*shape);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Threevertex polygon does not read nonexistent fourth vertex");
    world.DestroyBody(body);
}
struct Ground {
    CityMesh mesh;
    CityNavigation nav;
    Ground(float z) {
        mesh.positions = {{-200, -200, z}, {200, -200, z}, {-200, 200, z}};
        mesh.buffers.push_back({"", {0, 1, 2}});
        nav.buffers.resize(1);
        for (size_t i = 0; i < 8; ++i)
            nav.buffers[0].neighbors[i] = {uint16_t(i < 6 ? UINT16_MAX : 0)};
        nav.buffers[0].roadFlags = {0};
        nav.buffers[0].overlaps.resize(1);
        nav.materialBuffers.fill(0);
        nav.cliffVertexKeys.resize(6);
    }
};
void wrapperComposition() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    Surface surface(world);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailJumpController jump(owner);
    Ground ground(20);
    RetailHeightTerrain terrain(ground.mesh, ground.nav);
    RetailHeightController height(terrain, world, 2, nullptr, &jump);
    height.initializeSelected(0, {0, 0}, {0, 0, 77});
    height.initializeSelected(1, {0, 0}, {0, 0, 88});
    jump.setCurrentShape(*surface.shape);
    owner.currentJumpState = 17;
    close(height.height({0, 0, 2000}), 20, "Far query returns successful ordinary ground");
    require(!jump.state().enabled && owner.currentJumpState == 0,
            "Successful wrapper query runs distance reset");
    jump.setCurrentShape(*surface.shape);
    owner.currentJumpState = 17;
    close(height.height({0, 0, 0}), 70, "Higher valid jump plane wins over ordinary height");
    require(owner.currentJumpState == 1 && height.state(0).cached == Point{0, 0, 70},
            "Slotzero resets just before accepted jump, then writes chosen cached Z");
    close(height.height({0, 0, 0}, 1), 70, "Nonzero slot jump selection");
    require(owner.currentJumpState == 2 && height.state(1).cached == Point{0, 0, 70},
            "Nonzero slot accumulates same live state without reset");
    Ground highGround(200);
    RetailHeightTerrain highTerrain(highGround.mesh, highGround.nav);
    // Separate terrain context is used in a distinct world, preserving the pool invariant.
    b2World highWorld(bounds(), b2Vec2(0, 0), true);
    RetailHeightController high(highTerrain, highWorld, 2, nullptr, &jump);
    high.initializeSelected(0, {0, 0}, {0, 0, 77});
    high.initializeSelected(1, {0, 0}, {0, 0, 88});
    owner.currentJumpState = 22;
    close(high.height({0, 0, 0}), 200, "Lower jump plane does not replace successful terrain");
    require(owner.currentJumpState == 1, "Nonwinning jump query still increments after slotzero reset");
    close(high.height({0, 0, 0}, 1), 200, "Lower nonzero-slot jump also does not win");
    require(owner.currentJumpState == 2, "Nonwinning nonzero query still accumulates");
    owner.currentJumpState = 31;
    close(height.height({250, 0, 0}, 1, true), 70, "Core miss returns previous chosen cache");
    require(owner.currentJumpState == 31 && jump.state().enabled,
            "Core miss does not reset or evaluate jump");
    owner.currentJumpState = 33;
    close(height.height({250, 0, 1772.5f}), 70, "Slotzero core miss returns prior jump cache");
    require(owner.currentJumpState == 33 && jump.state().enabled,
            "Slotzero core miss does not reset or query jump");
    owner.currentJumpState = 32;
    close(height.height({101, -101, 0}), 20,
          "Inside ordinary terrain but outside jump quad keeps ordinary result");
    require(owner.currentJumpState == 0 && jump.state().enabled,
            "Slotzero outside-edge query resets count without disabling");
    RetailHeightController unselected(terrain, world, 1, nullptr, &jump);
    owner.currentJumpState = 41;
    close(unselected.height({5000, 5000, 123}), 123,
          "Unselected terrain initializes sentinel cache without core");
    require(owner.currentJumpState == 41 && jump.state().enabled,
            "No selected node means no jump or count reset");
    jump.height({0, 0, 1772.5f});
    owner.currentJumpState = 51;
    close(height.height({0, 0, 0}), 20, "Disabled jump does not alter ordinary height");
    require(owner.currentJumpState == 51, "Disabled branch does not reset original current state");
}
void cacheMutatingCoreMiss() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    Surface surface(world, 0, b2Vec2(2.25f, .25f));
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailJumpController jump(owner);
    jump.setCurrentShape(*surface.shape);
    Ground ground(0);
    ground.mesh.positions = {{0, 0, 10},    {100, 0, 30},  {0, 100, 50},
                             {200, 0, 100}, {300, 0, 100}, {200, 100, 200}};
    ground.mesh.buffers[0].indices = {0, 1, 2, 3, 4, 5};
    for (size_t i = 0; i < 8; ++i)
        ground.nav.buffers[0].neighbors[i].assign(2, i < 6 ? UINT16_MAX : 0);
    ground.nav.buffers[0].roadFlags.assign(2, 0);
    ground.nav.buffers[0].overlaps.resize(2);
    ground.nav.cliffVertexKeys.resize(12);
    RetailHeightTerrain terrain(ground.mesh, ground.nav);
    RetailHeightController height(terrain, world, 2, nullptr, &jump);
    height.initializeSelected(1, {0, 0}, {225, 25, 77});
    owner.currentJumpState = 17;
    close(height.height({225, 25, 400}, 1, false), 400,
          "Core miss retains reseeded query Z rather than older cache");
    require(height.state(1).triangle.indexOffset == 3 && height.state(1).changed &&
                owner.currentJumpState == 17 && jump.state().enabled,
            "Cache-mutating core miss bypasses otherwise eligible jump query and preserves original state");
    close(jump.height({225, 25, 400}, 1), 70,
          "Jump would have been valid if incorrectly called after core miss");
}

void sentinelAndEquality() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    Surface surface(world);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailJumpController jump(owner);
    Ground ground(70);
    RetailHeightTerrain terrain(ground.mesh, ground.nav);
    RetailHeightController height(terrain, world, 1, nullptr, &jump);
    height.initializeSelected(0, {0, 0}, {0, 0, 77});
    jump.setCurrentShape(*surface.shape);
    owner.currentJumpState = 17;
    close(height.height({0, 0, 0}), 70, "Equal jump height retains ordinary result");
    require(owner.currentJumpState == 1, "Equal nonwinning plane still increments");
    surface.data.heightsCm.fill(RetailHeightController::noHeight);
    jump.setCurrentShape(*surface.shape);
    close(height.height({0, 0, RetailHeightController::noHeight}), 70,
          "Jump sentinel cannot win despite greater numeric value");
    require(owner.currentJumpState == 1 && jump.state().enabled,
            "Accepted sentinel plane increments but does not disable");
}

void actualFilterBinding() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    world.SetContinuousPhysics(false);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailJumpController jump(owner);
    unsigned calls = 0;
    RetailContactFilter filter(&owner, [&](RetailPhysicsOwnerState& supplied, b2Shape& shape) {
        require(&supplied == &owner, "Filter must dispatch to exact native owner");
        ++calls;
        jump.setCurrentShape(shape);
    });
    world.SetContactFilter(&filter);
    Surface surface(world);
    RetailPhysicsBodyData actor{&owner, RetailPhysicsZBounds{-25, 150}, {}, false};
    b2BodyDef bd;
    bd.userData = &actor;
    auto* body = world.CreateBody(&bd);
    body->SetLinearVelocity(b2Vec2(0, 1));
    b2CircleDef circle;
    circle.radius = .2f;
    circle.density = 1;
    body->CreateShape(&circle);
    body->SetMassFromShapes();
    world.Step(1.f / 60, 10);
    require(calls > 0 && jump.state().enabled && jump.state().opaqueJumpFlag == 0xa5,
            "Actual native broadphase dispatches real filter to jump snapshot");
    close(jump.height({0, 0, 0}), 70, "Snapshot from actual contact filter produces original jump height");
    require(owner.currentJumpState == 1,
            "Real contact filter and height share one authoritative owner state");
    world.DestroyBody(body);
    world.SetContactFilter(nullptr);
}
void dumpOracleCases() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    size_t count = 0;
    for (float angle : {0.f, .125f, 1.5707963267948966f, -.625f, 2.1f}) {
        Surface surface(world, angle, b2Vec2(2.75f, -4.125f));
        for (const auto heights :
             {std::array<float, 4>{10, 50, 130, 900}, std::array<float, 4>{-10, -10, -10, -10},
              std::array<float, 4>{125.125f, -339.5f, 271.75f, -870.25f}}) {
            surface.data.heightsCm = heights;
            for (size_t which = 0; which < 9; ++which) {
                RetailPhysicsOwnerState owner{5, 0, 0, 0};
                RetailJumpController jump(owner);
                jump.setCurrentShape(*surface.shape);
                owner.currentJumpState = which == 8 ? std::numeric_limits<int32_t>::max() : 17;
                const int32_t before = owner.currentJumpState;
                const std::array<b2Vec2, 6> local{
                    {{0, 0}, {.25f, -.25f}, {-1, 1}, {1, 1}, {1.01f, 0}, {-1.01f, 0}}};
                auto xy = b2Mul(surface.body->GetXForm(), local[which % 6]);
                Point p{xy.x * 100.f, xy.y * 100.f, 0};
                if (which == 6)
                    p = {jump.state().centreCm[0], jump.state().centreCm[1],
                         jump.state().centreCm[2] + 1500.f};
                if (which == 7)
                    p = {jump.state().centreCm[0], jump.state().centreCm[1],
                         std::nextafter(jump.state().centreCm[2] + 1500.f, jump.state().centreCm[2])};
                const size_t slot = which % 3;
                const float result = jump.height(p, slot);
                std::cout << "jump {\"vertices\":[";
                const auto* v = static_cast<b2PolygonShape*>(surface.shape)->GetVertices();
                for (size_t i = 0; i < 4; ++i) {
                    if (i)
                        std::cout << ',';
                    std::cout << '[' << bits(v[i].x) << ',' << bits(v[i].y) << ']';
                }
                const auto& xf = surface.body->GetXForm();
                std::cout << "],\"transform\":[" << bits(xf.position.x) << ',' << bits(xf.position.y) << ','
                          << bits(xf.R.col1.x) << ',' << bits(xf.R.col1.y) << ',' << bits(xf.R.col2.x) << ','
                          << bits(xf.R.col2.y) << "],\"heights\":[";
                for (size_t i = 0; i < 4; ++i) {
                    if (i)
                        std::cout << ',';
                    std::cout << bits(heights[i]);
                }
                std::cout << "],\"flag\":" << unsigned(surface.data.opaqueJumpFlag) << ",\"point\":["
                          << bits(p[0]) << ',' << bits(p[1]) << ',' << bits(p[2]) << "],\"slot\":" << slot
                          << ",\"before\":" << before << ",\"result\":" << bits(result)
                          << ",\"after\":" << owner.currentJumpState
                          << ",\"enabled\":" << int(jump.state().enabled) << ",\"quad\":[";
                for (size_t i = 0; i < 4; ++i)
                    for (size_t k = 0; k < 3; ++k) {
                        if (i || k)
                            std::cout << ',';
                        std::cout << bits(jump.state().quadCm[i][k]);
                    }
                std::cout << "],\"centre\":[" << bits(jump.state().centreCm[0]) << ','
                          << bits(jump.state().centreCm[1]) << ',' << bits(jump.state().centreCm[2])
                          << "]}\n";
                ++count;
            }
        }
    }
    require(count == 135, "Complete deterministic original-oracle fixture set");
}
void dumpWrapperCases() {
    struct Case {
        float ground;
        Point point;
        size_t slot;
        bool selected;
        bool enabled;
        int32_t before;
        bool sentinel;
    };
    const std::array<Case, 12> cases{{{20, {0, 0, 0}, 0, true, true, 17, false},
                                      {20, {0, 0, 0}, 1, true, true, 17, false},
                                      {200, {0, 0, 0}, 0, true, true, 17, false},
                                      {70, {0, 0, 0}, 0, true, true, 17, false},
                                      {20, {101, -101, 0}, 0, true, true, 17, false},
                                      {20, {101, -101, 0}, 1, true, true, 17, false},
                                      {20, {0, 0, 2000}, 0, true, true, 17, false},
                                      {20, {250, 0, 1772.5f}, 0, true, true, 17, false},
                                      {20, {250, 0, 1772.5f}, 1, true, true, 17, false},
                                      {20, {5000, 5000, 123}, 0, false, true, 17, false},
                                      {20, {0, 0, 0}, 0, true, false, 17, false},
                                      {20, {0, 0, 999999}, 0, true, true, 17, true}}};
    for (const auto& test : cases) {
        b2World world(bounds(), b2Vec2(0, 0), true);
        Surface surface(world);
        if (test.sentinel)
            surface.data.heightsCm.fill(999999.f);
        RetailPhysicsOwnerState owner{5, 0, 0, 0};
        RetailJumpController jump(owner);
        jump.setCurrentShape(*surface.shape);
        if (!test.enabled)
            jump.height({0, 0, 10000000});
        owner.currentJumpState = test.before;
        Ground ground(test.ground);
        RetailHeightTerrain terrain(ground.mesh, ground.nav);
        RetailHeightController height(terrain, world, 2, nullptr, &jump);
        if (test.selected)
            height.initializeSelected(test.slot, {0, 0}, {0, 0, 77});
        const float result = height.height(test.point, test.slot, true);
        std::cout << "wrapper {\"ground\":" << bits(test.ground) << ",\"point\":[" << bits(test.point[0])
                  << ',' << bits(test.point[1]) << ',' << bits(test.point[2]) << "],\"slot\":" << test.slot
                  << ",\"selected\":" << int(test.selected) << ",\"enabledBefore\":" << int(test.enabled)
                  << ",\"before\":" << test.before << ",\"sentinel\":" << int(test.sentinel)
                  << ",\"result\":" << bits(result) << ",\"after\":" << owner.currentJumpState
                  << ",\"enabled\":" << int(jump.state().enabled) << ",\"cache\":["
                  << bits(height.state(test.slot).cached[0]) << ',' << bits(height.state(test.slot).cached[1])
                  << ',' << bits(height.state(test.slot).cached[2])
                  << "],\"changed\":" << int(height.state(test.slot).changed)
                  << ",\"generation\":" << terrain.generation() << "}\n";
    }
}

} // namespace
int main(int argc, char** argv) {
    try {
        const auto baseline = b2_byteCount;
        snapshotAndPlane();
        boundariesAndDisabled();
        snapshotLifetimeAndValidation();
        wrapperComposition();
        cacheMutatingCoreMiss();
        sentinelAndEquality();
        actualFilterBinding();
        if (argc == 2 && std::string(argv[1]) == "--oracle-dump") {
            dumpOracleCases();
            dumpWrapperCases();
        }
        require(b2_byteCount == baseline,
                "Jump snapshots/native bodies and terrain composition release allocations");
        std::cout << "Retail jump snapshots, original plane/state, wrapper composition and actual filter "
                     "fixtures passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
