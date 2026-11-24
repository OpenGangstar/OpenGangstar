#include "retail_physics.h"

#include <cmath>
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
}

int main()
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
        std::cout << "Retail native collision filter gates, height boundaries, jump callbacks, and world contacts passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
