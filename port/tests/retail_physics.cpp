#include "retail_physics.h"

#include <cmath>
#include <functional>
#include <iomanip>
#include <memory>
#include <string>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

b2AABB bounds()
{
    b2AABB result;
    result.lowerBound.Set(-2000, -2000);
    result.upperBound.Set(2000, 2000);
    return result;
}

b2FilterData shapeFilter(uint16 category, uint16 mask, int16 group)
{
    b2FilterData result;
    result.categoryBits = category;
    result.maskBits = mask;
    result.groupIndex = group;
    return result;
}

struct Pair {
    RetailPhysicsOwnerState ownerA{7, 0, 0, 0}, ownerB{7, 0, 0, 0};
    RetailPhysicsBodyData dataA{nullptr, std::nullopt, {0, 200, 0, 0}, false};
    RetailPhysicsBodyData dataB{nullptr, std::nullopt, {0, 200, 0, 0}, false};
    // The world is destroyed before the metadata and owner records it references.
    b2World world{bounds(), b2Vec2(0, 0), true};
    b2Shape* a;
    b2Shape* b;

    Pair()
    {
        a = create(&dataA);
        b = create(&dataB);
    }
    Pair(const Pair&) = delete;
    Pair& operator=(const Pair&) = delete;

    b2Shape* create(RetailPhysicsBodyData* data)
    {
        b2BodyDef definition;
        definition.userData = data;
        definition.position.Set(-430, -500);
        auto* body = world.CreateBody(&definition);
        b2CircleDef circle;
        circle.radius = 0.3f;
        circle.density = 1;
        auto* shape = body->CreateShape(&circle);
        body->SetMassFromShapes();
        return shape;
    }

    void filters(b2FilterData first, b2FilterData second)
    {
        a->SetFilterData(first);
        b->SetFilterData(second);
    }

    void both(RetailContactFilter& filter, bool expected, const char* message)
    {
        require(filter.ShouldCollide(a, b) == expected && filter.ShouldCollide(b, a) == expected, message);
    }
};

void checkMasksAndGroups()
{
    Pair pair;
    RetailContactFilter filter(nullptr, {});
    struct Case { b2FilterData first, second; bool collides; };
    const std::vector<Case> cases{
        {shapeFilter(1, 0xffff, 0), shapeFilter(2, 0xffff, 0), true},
        {shapeFilter(1, 0, 0), shapeFilter(2, 0xffff, 0), false},
        {shapeFilter(1, 0xffff, 0), shapeFilter(2, 0, 0), false},
        {shapeFilter(1, 2, 0), shapeFilter(2, 1, 0), true},
        {shapeFilter(1, 4, 0), shapeFilter(2, 1, 0), false},
        {shapeFilter(1, 0, 7), shapeFilter(2, 0, 7), false},
        {shapeFilter(1, 0xffff, -7), shapeFilter(2, 0xffff, -7), true},
        {shapeFilter(0, 0, 1), shapeFilter(4, 0, 1), true},
        {shapeFilter(0, 0, -1), shapeFilter(4, 0, -1), true},
        {shapeFilter(0, 0xffff, 0), shapeFilter(4, 0xffff, 0), true},
        {shapeFilter(0, 0, 0), shapeFilter(4, 0xffff, 0), false},
        {shapeFilter(0, 0, 1), shapeFilter(4, 0xffff, 2), false},
        {shapeFilter(0, 0xffff, 2), shapeFilter(4, 0xffff, 1), false},
        {shapeFilter(0, 0xffff, 2), shapeFilter(0, 0xffff, 2), true},
        {shapeFilter(0, 0xffff, 2), shapeFilter(0, 0xffff, 3), false}
    };
    for (const auto& entry : cases) {
        pair.filters(entry.first, entry.second);
        pair.both(filter, entry.collides, "Retail category/mask/group filtering changed in one shape order.");
    }
}

void checkMissingMetadata()
{
    Pair pair;
    RetailContactFilter filter(nullptr, {});
    pair.dataA.heightsCm = {0, 1, 0, 0};
    pair.dataB.heightsCm = {10, 20, 0, 0};
    pair.both(filter, false, "Disjoint metadata intervals should not collide.");
    pair.b->GetBody()->SetUserData(nullptr);
    pair.both(filter, true, "Missing metadata did not preserve the original fallback.");
    pair.dataA.nodeBounds = RetailPhysicsZBounds{0, 1};
    pair.both(filter, true, "Missing metadata fallback must precede inspecting the other owner.");
    pair.a->SetFilterData(shapeFilter(0, 0, 0));
    pair.both(filter, false, "Missing metadata incorrectly bypassed the original shape-filter gate.");
    pair.a->SetFilterData(shapeFilter(1, 0xffff, 0));
    pair.a->GetBody()->SetUserData(nullptr);
    pair.b->GetBody()->SetUserData(&pair.dataB);
    pair.both(filter, true, "First-body missing metadata did not preserve the fallback.");
}

void checkIntervalsAndPlayerIdentity()
{
    Pair pair;
    RetailContactFilter filter(nullptr, {});
    pair.filters(shapeFilter(1, 0xffff, 1), shapeFilter(0, 0xffff, 1));
    pair.dataA.owner = &pair.ownerA;
    pair.dataA.nodeBounds = RetailPhysicsZBounds{100, 200};
    pair.dataA.heightsCm = {-10000, 10000, -10000, 10000};
    pair.dataB.heightsCm = {200, 300, -10000, 10000};
    pair.both(filter, false, "Touching upper Z endpoints must not collide.");
    pair.dataB.heightsCm = {0, 100, -10000, 10000};
    pair.both(filter, false, "Touching lower Z endpoints must not collide.");
    pair.dataB.heightsCm = {99, 101, 0, 0};
    pair.both(filter, true, "Overlapping node bounds did not collide.");

    pair.ownerA = {5, 500, 450, 0};
    pair.dataA.nodeBounds = RetailPhysicsZBounds{900, 1000};
    pair.dataB.heightsCm = {199, 201, 0, 0};
    pair.both(filter, true, "An NPC must use scene Z minus animation root Z, not its bounding box.");
    pair.dataB.heightsCm = {200, 250, 0, 0};
    pair.both(filter, false, "NPC upper endpoint must remain strict.");
    pair.dataB.heightsCm = {-10, 25, 0, 0};
    pair.both(filter, false, "NPC lower endpoint must remain strict.");
    pair.dataB.heightsCm = {199, 201, 0, 0};
    pair.ownerA.rootZCm = 451;
    pair.both(filter, false, "A changed root-height state was not observed by the filter.");
    pair.ownerA.rootZCm = 450;
    pair.ownerA.levelCategory = 8;
    pair.both(filter, false, "A category value alone must not identify the genuine player.");
    filter.setPlayer(&pair.ownerA);
    pair.both(filter, true, "The explicitly bound player did not use its character interval.");
    filter.setPlayer(&pair.ownerB);
    pair.both(filter, false, "Changing genuine player identity did not restore ordinary node bounds.");
}

void checkFourHeightBounds()
{
    Pair pair;
    RetailContactFilter filter(nullptr, {});
    pair.dataA.jumpSurface = true;
    pair.dataB.owner = &pair.ownerB;
    pair.dataB.nodeBounds = RetailPhysicsZBounds{-50, 0};
    for (size_t i = 0; i < 4; ++i) {
        pair.dataA.heightsCm.fill(100);
        pair.dataA.heightsCm[i] = -100;
        pair.dataB.nodeBounds = RetailPhysicsZBounds{-50, 0};
        pair.both(filter, true, "One of the four lower jump-surface heights was ignored.");
        pair.dataA.heightsCm.fill(100);
        pair.dataA.heightsCm[i] = 300;
        pair.dataB.nodeBounds = RetailPhysicsZBounds{200, 250};
        pair.both(filter, true, "One of the four upper jump-surface heights was ignored.");
    }
    pair.dataA.heightsCm = {200, 220, -100, 350};
    pair.dataB.nodeBounds = RetailPhysicsZBounds{350, 360};
    pair.both(filter, false, "Four-height upper endpoint must remain strict.");
    pair.dataB.nodeBounds = RetailPhysicsZBounds{-120, -100};
    pair.both(filter, false, "Four-height lower endpoint must remain strict.");
}

void checkAuthoredRectangle()
{
    Pair pair;
    RetailContactFilter filter(nullptr, {});
    pair.dataA.heightsCm = {100, 200, -10000, 10000};
    pair.dataB.owner = &pair.ownerB;
    pair.dataB.nodeBounds = RetailPhysicsZBounds{-50, 0};
    const float infinity = std::numeric_limits<float>::infinity();
    struct Case { float x, y; bool extended; };
    const std::vector<Case> cases{
        {-430, -500, false},
        {-450, -500, true}, {std::nextafter(-450.f, infinity), -500, false},
        {std::nextafter(-450.f, -infinity), -500, true},
        {-405, -500, true}, {std::nextafter(-405.f, -infinity), -500, false},
        {std::nextafter(-405.f, infinity), -500, true},
        {-430, -560, true}, {-430, std::nextafter(-560.f, infinity), false},
        {-430, std::nextafter(-560.f, -infinity), true},
        {-430, -420, true}, {-430, std::nextafter(-420.f, -infinity), false},
        {-430, std::nextafter(-420.f, infinity), true}
    };
    for (const auto& entry : cases) {
        require(pair.a->GetBody()->SetXForm(b2Vec2(entry.x, entry.y), 0), "Rectangle fixture left the world.");
        pair.both(filter, entry.extended, "The authored rectangle boundary or shape-order handling changed.");
    }
    pair.filters(shapeFilter(0, 0xffff, 1), shapeFilter(4, 0xffff, 1));
    pair.both(filter, false, "A zero category must retain its ordinary stored lower bound outside the rectangle.");
}

void checkVehicleJumpGate()
{
    Pair pair;
    RetailContactFilter filter(nullptr, {});
    pair.dataA.owner = &pair.ownerA;
    pair.dataA.nodeBounds = RetailPhysicsZBounds{0, 200};
    pair.ownerA.levelCategory = 3;
    pair.ownerA.currentJumpState = 1;
    pair.both(filter, false, "A vehicle's positive jump state must suppress node-less collisions.");
    pair.ownerA.currentJumpState = 0;
    pair.both(filter, true, "A zero jump state incorrectly suppressed collision.");
    pair.ownerA.currentJumpState = -1;
    pair.both(filter, true, "A negative jump state incorrectly suppressed collision.");
    pair.ownerA.currentJumpState = 1;
    pair.dataB.owner = &pair.ownerB;
    pair.dataB.nodeBounds = RetailPhysicsZBounds{0, 200};
    pair.both(filter, true, "A positive vehicle jump state must not suppress another node-backed object.");
}

void checkDirectionalJumpBinding()
{
    Pair pair;
    pair.dataA.owner = &pair.ownerA;
    pair.dataA.jumpSurface = true;
    pair.dataA.heightsCm = {0, 400, 100, 200};
    pair.dataB.owner = &pair.ownerB;
    pair.dataB.nodeBounds = RetailPhysicsZBounds{0, 200};
    pair.ownerB = {5, 100, 0, 0};
    RetailPhysicsOwnerState* observedOwner = nullptr;
    b2Shape* observedShape = nullptr;
    unsigned calls = 0;
    RetailContactFilter filter(nullptr, [&](auto& owner, auto& shape) {
        observedOwner = &owner;
        observedShape = &shape;
        ++calls;
    });
    for (const auto velocity : {b2Vec2(0, 0), b2Vec2(0, -1), b2Vec2(1, 0)}) {
        pair.b->GetBody()->SetLinearVelocity(velocity);
        pair.both(filter, true, "Nonpositive second-column velocity must retain normal overlap.");
        require(calls == 0, "A zero, negative, or orthogonal velocity triggered a jump callback.");
    }
    for (const float speed : {std::numeric_limits<float>::min(), 1.f}) {
        pair.b->GetBody()->SetLinearVelocity(b2Vec2(0, speed));
        pair.both(filter, false, "A strictly positive jump-surface direction must suppress collision.");
        require(observedOwner == &pair.ownerB && observedShape == pair.a,
                "Directional filtering dispatched the wrong owner or jump shape.");
    }
    require(calls == 4, "Directional jump callbacks were skipped or repeated within a filter call.");
    pair.ownerB.positionZCm = 1000;
    pair.both(filter, false, "Disjoint vertical intervals should remain noncolliding.");
    require(calls == 6, "The directional callback must still run after a disjoint interval test.");
    pair.ownerB.positionZCm = 100;

    pair.dataB.jumpSurface = true;
    pair.a->GetBody()->SetLinearVelocity(b2Vec2(0, 1));
    pair.b->GetBody()->SetLinearVelocity(b2Vec2(0, -1));
    require(filter.ShouldCollide(pair.a, pair.b) && calls == 6,
            "The second jump flag incorrectly overrode a nonpositive first jump flag.");
    require(!filter.ShouldCollide(pair.b, pair.a) && calls == 7 &&
            observedOwner == &pair.ownerA && observedShape == pair.b,
            "Reversed jump-flag precedence did not preserve the original owner/shape side effect.");

    pair.dataB.jumpSurface = false;
    pair.b->GetBody()->SetLinearVelocity(b2Vec2(0, 1));
    RetailContactFilter missing(nullptr, {});
    bool rejected = false;
    try { missing.ShouldCollide(pair.a, pair.b); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected, "A required SetCurrentJumpShape binding was silently replaced by a no-op.");
    pair.b->GetBody()->SetLinearVelocity(b2Vec2(0, 0));
    pair.both(missing, true, "A nonpositive gate must not require an unused jump binding.");

    require(pair.a->GetBody()->SetXForm(pair.a->GetBody()->GetPosition(), b2_pi / 2),
            "Rotated jump fixture left the world.");
    const auto direction = pair.a->GetBody()->GetXForm().R.col2;
    pair.b->GetBody()->SetLinearVelocity(direction);
    pair.both(filter, false, "The jump gate ignored the surface body's rotated second column.");
    require(calls == 9 && observedOwner == &pair.ownerB && observedShape == pair.a,
            "A rotated surface dispatched the wrong jump binding.");
    pair.b->GetBody()->SetLinearVelocity(-direction);
    pair.both(filter, true, "A negative rotated directional dot must retain ordinary overlap.");
    require(calls == 9, "A negative rotated dot unexpectedly dispatched a jump binding.");

    pair.dataB.owner = nullptr;
    rejected = false;
    try { filter.ShouldCollide(pair.a, pair.b); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected, "Node-backed metadata without its required owner was silently accepted.");
    pair.dataB.nodeBounds.reset();
    pair.b->GetBody()->SetLinearVelocity(direction);
    rejected = false;
    try { filter.ShouldCollide(pair.a, pair.b); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected, "A positive jump gate without its recipient owner was silently accepted.");
}

void checkNativeWorldIntegration()
{
    RetailPhysicsOwnerState actorState{5, 0, 0, 0};
    RetailPhysicsBodyData wallData{nullptr, std::nullopt, {500, 600, 0, 0}, false};
    RetailPhysicsBodyData actorData{&actorState, RetailPhysicsZBounds{0, 150}, {0, 0, 0, 0}, false};
    RetailContactFilter filter(nullptr, {});
    struct Listener : b2ContactListener {
        unsigned added = 0;
        void Add(const b2ContactPoint*) override { ++added; }
    } listener;
    b2World world(bounds(), b2Vec2(0, 0), true);
    world.SetPositionCorrection(true);
    world.SetContinuousPhysics(false);
    world.SetContactFilter(&filter);
    world.SetContactListener(&listener);
    b2BodyDef wall;
    wall.userData = &wallData;
    auto* wallBody = world.CreateBody(&wall);
    b2PolygonDef polygon;
    polygon.SetAsBox(0.1f, 5);
    polygon.filter = shapeFilter(0, 0xffff, 1);
    auto* wallShape = wallBody->CreateShape(&polygon);
    b2BodyDef actor;
    actor.position.Set(-2, 0);
    actor.userData = &actorData;
    actor.allowSleep = false;
    auto* body = world.CreateBody(&actor);
    b2CircleDef circle;
    circle.radius = 0.3f;
    circle.density = 1;
    circle.filter = shapeFilter(4, 0xffff, 1);
    auto* actorShape = body->CreateShape(&circle);
    body->SetMassFromShapes();
    body->SetLinearVelocity(b2Vec2(2, 0));
    for (int i = 0; i < 90; ++i) world.Step(1.f / 60, 10);
    require(body->GetPosition().x > 0.8f && listener.added == 0,
            "The installed retail filter did not reject a vertically disjoint real contact.");
    wallData.heightsCm = {0, 100, 0, 0};
    require(body->SetXForm(b2Vec2(-2, 0), 0), "Actor integration fixture left the world.");
    world.Refilter(wallShape);
    world.Refilter(actorShape);
    body->SetLinearVelocity(b2Vec2(2, 0));
    for (int i = 0; i < 90; ++i) world.Step(1.f / 60, 10);
    require(body->GetPosition().x > -0.5f && body->GetPosition().x < -0.2f && listener.added > 0,
            "The installed filter did not observe changed native height state and permit a real contact.");
    require(body->GetUserData() == &actorData && wallBody->GetUserData() == &wallData,
            "Native body user-data identity was truncated or replaced.");
}
// Controlled original virtual-call seam. Concrete retained-scene/body tests live
// in retail_actor_physics_test; this fixture isolates dispatch timing on real shapes.
struct LiveReads final : RetailPhysicsOwnerReads, RetailPhysicsNodeReads {
    RetailPhysicsOwnerState& owner;
    std::vector<std::string>& calls;
    std::string id;
    RetailPhysicsZBounds box{1, 337};
    float position = 0, root = 10;
    std::function<void()> beforePosition, beforeBox;
    LiveReads(RetailPhysicsOwnerState& owner, std::vector<std::string>& calls, std::string id)
        : owner(owner), calls(calls), id(std::move(id)) {}
    const RetailPhysicsOwnerState& ownerIdentity() const override { return owner; }
    float getPositionZCm() const override {
        calls.push_back(id + "position");
        if (beforePosition) beforePosition();
        return position;
    }
    float storedRootHeightCm() const override { calls.push_back(id + "root"); return root; }
    RetailPhysicsZBounds transformedBoundingBoxCm() const override {
        calls.push_back(id + "box");
        if (beforeBox) beforeBox();
        return box;
    }
};
struct LivePair : Pair {
    std::vector<std::string> calls;
    std::shared_ptr<LiveReads> readsA = std::make_shared<LiveReads>(ownerA, calls, "A.");
    std::shared_ptr<LiveReads> readsB = std::make_shared<LiveReads>(ownerB, calls, "B.");
    const RetailPhysicsOwnerState* player = nullptr;
    RetailContactFilter filter{RetailContactFilter::PlayerLookup([this] {
        calls.push_back("player"); return player;
    }), [this](RetailPhysicsOwnerState& owner, b2Shape& shape) {
        require(&owner == &ownerB && &shape == a, "Genuine late jump recipient/shape");
        calls.push_back("jump");
    }};
    LivePair() {
        dataA.owner = &ownerA; dataB.owner = &ownerB;
        dataA.liveOwner = readsA; dataA.liveNode = readsA;
        dataB.liveOwner = readsB; dataB.liveNode = readsB;
        filters(shapeFilter(4, 0xffff, 1), shapeFilter(4, 0xffff, 1));
        a->GetBody()->SetXForm(b2Vec2(0, 0), 0);
        b->GetBody()->SetXForm(b2Vec2(0, 0), 0);
    }
    void terrainB(float lo = 100, float hi = 101, uint16 category = 0) {
        dataB.liveNode.reset(); dataB.heightsCm = {lo, hi, lo, hi};
        b->SetFilterData(shapeFilter(category, 0xffff, 1));
    }
};
void checkLiveDispatch() {
    {
        LivePair p;
        p.ownerA.levelCategory = 5;
        p.readsA->position = 100;
        p.ownerA.positionZCm = 9999; p.ownerA.rootZCm = 8888; // Poison snapshots.
        p.terrainB();
        require(p.filter.ShouldCollide(p.a, p.b), "Live character reads canonical inputs");
        require(p.calls == std::vector<std::string>{"A.position", "A.root"}, "NPC bypasses player and node getter");
        p.calls.clear();
        p.readsA->beforePosition = [&] { p.readsA->root = 77; };
        p.terrainB(200, 201);
        require(!p.filter.ShouldCollide(p.a, p.b) && p.calls == std::vector<std::string>{"A.position", "A.root"},
                "Root height is read AFTER GetPosition mutation: upper173 instead of stale upper240");
        p.readsA->root = 10; p.readsA->beforePosition = {};
        p.terrainB(240, 241);
        require(!p.filter.ShouldCollide(p.a, p.b), "Strict live upper endpoint remains nonoverlap");
    }
    {
        LivePair p;
        p.player = &p.ownerA; p.ownerA.levelCategory = 3; p.ownerA.currentJumpState = 1;
        p.terrainB();
        require(!p.filter.ShouldCollide(p.a, p.b) && p.calls.empty(), "Vehicle positive gate precedes even genuine player lookup");
        p.ownerA.currentJumpState = 0;
        require(p.filter.ShouldCollide(p.a, p.b), "Nonpositive vehicle continues genuine player");
        require(p.calls == std::vector<std::string>{"player", "A.position", "A.root"}, "Player lookup then position then stored root");
    }
    {
        LivePair p;
        p.readsA->beforeBox = [&] { p.ownerB.levelCategory = 5; };
        require(p.filter.ShouldCollide(p.a, p.b), "Second classification is live after first getter");
        require(p.calls == std::vector<std::string>{"player", "A.box", "B.position", "B.root"}, "First interval before second dispatch");
    }
    {
        LivePair p;
        p.readsA->beforeBox = [&] { p.player = &p.ownerB; };
        require(p.filter.ShouldCollide(p.a, p.b), "Second lookup observes changed genuine player");
        require(p.calls == std::vector<std::string>{"player", "A.box", "player", "B.position", "B.root"}, "Player lookup is not cached per pair");
    }
    {
        LivePair p;
        p.readsA->beforeBox = [&] { p.dataB.liveNode.reset(); };
        require(p.filter.ShouldCollide(p.a, p.b), "Second node-null uses actual metadata heights");
        require(p.calls == std::vector<std::string>{"player", "A.box"}, "Second node presence read after first getter");
    }
    {
        LivePair p;
        p.readsA->beforeBox = [&] { p.b->GetBody()->SetUserData(nullptr); };
        require(p.filter.ShouldCollide(p.a, p.b), "Second captured metadata pointer survives body userData replacement");
        require(p.calls == std::vector<std::string>{"player", "A.box", "player", "B.box"}, "No late userData reload/fallback");
        p.b->GetBody()->SetUserData(&p.dataB);
    }
    for (bool reverse : {false, true}) {
        LivePair p;
        p.terrainB(400, 500, 4);
        p.readsA->beforeBox = [&] { p.a->SetFilterData(shapeFilter(0, 0xffff, 1)); };
        require(p.filter.ShouldCollide(reverse ? p.b : p.a, reverse ? p.a : p.b) == reverse,
                "Second categories are reread; an already captured first terrain interval stays unchanged");
    }
    {
        LivePair p;
        p.terrainB();
        p.readsA->beforeBox = [&] {
            p.dataA.jumpSurface = true;
            p.b->GetBody()->SetLinearVelocity(b2Vec2(0, 1));
        };
        require(!p.filter.ShouldCollide(p.a, p.b) && p.calls.back() == "jump", "Late flags and genuine velocity dispatch jump after getter");
    }
    {
        LivePair p;
        p.a->SetFilterData(shapeFilter(4, 0, 1));
        require(!p.filter.ShouldCollide(p.a, p.b) && p.calls.empty(), "Mask rejection invokes no live operations");
        p.a->SetFilterData(shapeFilter(4, 0xffff, 1));
        p.b->GetBody()->SetUserData(nullptr);
        require(p.filter.ShouldCollide(p.a, p.b) && p.calls.empty(), "Absent metadata fallback invokes no live operations");
        p.b->GetBody()->SetUserData(&p.dataB);
    }
}
void checkLiveBindingErrors() {
    bool rejected = false;
    try { RetailContactFilter missing(RetailContactFilter::PlayerLookup{}, {}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Live setup requires a player lookup even when no current player exists");
    LivePair p;
    auto rejects = [&](const std::function<void()>& action) {
        bool failed = false;
        try { action(); } catch (const std::logic_error&) { failed = true; }
        require(failed, "Invalid live binding is fatal, never a silent snapshot fallback");
    };
    rejects([&] { p.filter.setPlayer(nullptr); });
    p.dataA.nodeBounds = RetailPhysicsZBounds{0, 100};
    rejects([&] { p.filter.ShouldCollide(p.a, p.b); });
    p.dataA.nodeBounds.reset();
    RetailContactFilter fixture(nullptr, {});
    rejects([&] { fixture.ShouldCollide(p.a, p.b); });
    p.ownerA.levelCategory = 5; p.dataA.liveOwner.reset();
    rejects([&] { p.filter.ShouldCollide(p.a, p.b); });
    p.dataA.liveOwner = p.readsB;
    rejects([&] { p.filter.ShouldCollide(p.a, p.b); });
    p.dataA.liveOwner = p.readsA; p.ownerA.levelCategory = 7; p.dataA.liveNode = p.readsB;
    rejects([&] { p.filter.ShouldCollide(p.a, p.b); });
}

void dumpLivePolicyReference() {
    const auto dump = [](const char* name, LivePair& p, bool reverse = false) {
        p.calls.clear();
        const bool result = p.filter.ShouldCollide(reverse ? p.b : p.a, reverse ? p.a : p.b);
        std::cout << "livepolicy {\"name\":" << std::quoted(name) << ",\"result\":" << result << ",\"events\":[";
        bool first=true;
        for (const auto& call : p.calls) {
            std::string label;
            if (call=="player") label="GetPlayer";
            else if(call=="jump") label="SetCurrentJumpShape";
            else if(call.find("position")!=std::string::npos) label="GetPosition";
            else if(call.find("box")!=std::string::npos) label="baseTransformedBox";
            else continue; // Stored field read is not an original virtual call.
            if(!first)std::cout<<','; first=false;std::cout<<std::quoted(label);
        }
        std::cout << "]}\n";
    };
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.ownerA.levelCategory=5;p.terrainB();p.readsA->position=100;
        dump("NPC cached100 vs local900 Bip01local400 rootHeight10 floor-999",p);
        p.readsA->position=900;dump("NPC after explicit actual absolute update",p);
        p.readsA->position=100;p.readsA->beforePosition=[&]{p.readsA->root=77;};
        dump("rootHeight read AFTER virtual GetPosition mutation",p);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.player=&p.ownerA;p.terrainB();
        dump("actual player identity selects character interval",p);
        p.ownerA.levelCategory=3;p.ownerA.currentJumpState=1;
        dump("vehicle jump rejects BEFORE player getter even genuine player",p);
        p.ownerA.currentJumpState=0;dump("vehicle nonpositive jump continues actual player getter",p);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.terrainB(400,401);p.readsA->box={1001,1337};
        dump("ordinary genuine transformed box after absolute update",p);
        dump("ordinary setter without absolute update retains cached transformed box",p);
        p.readsA->box={-999,-663};dump("ordinary explicit absolute update refreshes transformed box",p);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.readsA->beforeBox=[&]{p.ownerB.levelCategory=5;};
        dump("first actual box getter mutates second classification to NPC",p);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.terrainB(400,500,4);p.readsA->beforeBox=[&]{p.a->SetFilterData(shapeFilter(0,0xffff,1));};
        dump("second terrain category reread after first getter",p);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.terrainB();p.readsA->beforeBox=[&]{p.dataA.jumpSurface=true;p.b->GetBody()->SetLinearVelocity(b2Vec2(0,1));};
        dump("late jump flag and velocity after getter",p);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.a->SetFilterData(shapeFilter(4,0,1));dump("mask rejection no getter",p);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.b->GetBody()->SetUserData(nullptr);dump("absent metadata fallback no getter",p);
        p.b->GetBody()->SetUserData(&p.dataB);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.terrainB(400,500,4);p.readsA->beforeBox=[&]{p.a->SetFilterData(shapeFilter(0,0xffff,1));};
        dump("reverse order captures terrain categories BEFORE later box mutation",p,true);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.readsA->beforeBox=[&]{p.dataB.liveNode.reset();};
        dump("second live node binding changed by first getter",p);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.readsA->beforeBox=[&]{p.player=&p.ownerB;};
        dump("second genuine GetPlayer sees global changed by first box getter",p);
    }
    {
        auto owned=std::make_unique<LivePair>();auto& p=*owned;p.readsA->beforeBox=[&]{p.b->GetBody()->SetUserData(nullptr);};
        dump("captured second metadata survives live body userData replacement",p);
        p.b->GetBody()->SetUserData(&p.dataB);
    }
}

}

int main(int argc, char** argv)
{
    try {
        checkMasksAndGroups();
        checkMissingMetadata();
        checkIntervalsAndPlayerIdentity();
        checkFourHeightBounds();
        checkAuthoredRectangle();
        checkVehicleJumpGate();
        checkDirectionalJumpBinding();
        checkNativeWorldIntegration();
        checkLiveDispatch();
        checkLiveBindingErrors();
        if (argc == 2 && std::string(argv[1]) == "--live-dump") dumpLivePolicyReference();
        std::cout << "Retail native collision filter gates, height boundaries, jump callbacks, and world contacts passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
