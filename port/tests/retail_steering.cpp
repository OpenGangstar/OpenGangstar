#include "retail_steering.h"

#include <cmath>
#include <iostream>
#include <list>
#include <stdexcept>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void near(float actual, float expected, const char* message, float tolerance = .02f)
{
    require(std::abs(actual - expected) < tolerance, message);
}

b2AABB bounds(float x1 = -2000, float y1 = -2000, float x2 = 2000, float y2 = 2000)
{
    b2AABB result;
    result.lowerBound.Set(x1, y1);
    result.upperBound.Set(x2, y2);
    return result;
}

struct Fixture {
    RetailPhysicsOwnerState actorOwner{5, 100, 0, 0};
    RetailSteeringActor actor{actorOwner, b2Vec2(0, 0), 2};
    RetailPhysicsOwnerState obstacleOwner{7, 0, 0, 0};
    std::list<RetailPhysicsBodyData> records;
    // Bodies die before all metadata, owners and actor references.
    b2World world{bounds(), b2Vec2(0, 0), true};

    b2Body* body(float x, float y)
    {
        records.push_back({&obstacleOwner, RetailPhysicsZBounds{-10000, 10000}, {0, 0, 0, 0}, false});
        b2BodyDef definition;
        definition.position.Set(x, y);
        definition.userData = &records.back();
        return world.CreateBody(&definition);
    }
    b2Shape* circle(float x, float y, float radius = .3f)
    {
        b2CircleDef circle;
        circle.radius = radius;
        return body(x, y)->CreateShape(&circle);
    }
    b2Shape* box(float x, float y, float halfX = .2f, float halfY = 2.f, float angle = 0)
    {
        auto* owner = body(x, y);
        require(owner->SetXForm(b2Vec2(x, y), angle), "Box fixture left the world.");
        b2PolygonDef definition;
        definition.SetAsBox(halfX, halfY);
        return owner->CreateShape(&definition);
    }
    RetailPhysicsBodyData& data(b2Shape* shape)
    {
        return *static_cast<RetailPhysicsBodyData*>(shape->GetBody()->GetUserData());
    }
    bool detailed(const RetailSteeringPoint& destination, RetailSteeringPoint& hit, RetailSteeringPoint& adjusted)
    {
        return retailCanGoToPoint(world, actor, destination, hit, adjusted);
    }
};

void checkClearAndOverloads()
{
    Fixture fixture;
    const RetailSteeringPoint destination{1000, 0, 100};
    RetailSteeringPoint hit{-1, -1, -1}, adjusted{17, 23, 29};
    require(fixture.detailed(destination, hit, adjusted), "An empty offset query was obstructed.");
    require(hit.x == 1000 && hit.y == 0 && hit.z == 100, "Clear hit output did not equal destination.");
    require(adjusted.x == 17 && adjusted.y == 23 && adjusted.z == 29, "Clear query modified unused adjusted output.");
    require(retailCanGoToPoint(fixture.world, fixture.actor, destination), "Empty point query failed its equal-axis padding.");
    require(retailCanGoToPoint(fixture.world, fixture.actor, {0, 0, 100}), "Zero-length point query changed its original zero normalization.");
    require(fixture.detailed({0, 0, 100}, hit, adjusted), "Zero-length character query fabricated an obstruction.");
    fixture.circle(5, .5f, .1f);
    require(!fixture.detailed(destination, hit, adjusted), "The offset ray ignored an off-centre obstacle.");
    require(retailCanGoToPoint(fixture.world, fixture.actor, destination), "The point overload incorrectly acquired offset rays.");
    fixture.actorOwner.levelCategory = 7;
    bool rejected = false;
    try { fixture.detailed(destination, hit, adjusted); }
    catch (const std::domain_error&) { rejected = true; }
    require(rejected, "A zero-margin degenerate AABB silently acquired fabricated padding.");
    require(fixture.detailed({1000, 200, 100}, hit, adjusted), "Nondegenerate zero-margin clear query failed.");
}

void checkCircleAndMargins()
{
    for (float y : {.5f, -.5f}) {
        Fixture fixture;
        fixture.circle(5, y);
        RetailSteeringPoint hit{}, adjusted{};
        require(!fixture.detailed({1000, 0, 100}, hit, adjusted), "A circle failed to obstruct its offset ray.");
        near(hit.x, 470, "Circle intersection fraction was not propagated to hit X.");
        near(hit.z, 100, "Node-backed circle intersection lost its Z.");
        near(adjusted.x, 500, "Circle adjustment did not use body origin.");
        near(adjusted.y, y > 0 ? -75.f : 75.f, "Circle first/second-ray 75 cm adjustment changed.");
        near(adjusted.z, 100, "Circle adjustment did not preserve scratch Z.");
    }
    for (int category : {5, 8, 3}) {
        Fixture fixture;
        fixture.actorOwner.levelCategory = category;
        const float offset = category == 3 ? 1.5f : .5f;
        fixture.circle(5, offset);
        RetailSteeringPoint hit{}, adjusted{};
        require(!fixture.detailed({1000, 0, 100}, hit, adjusted), "Live actor category did not select original margin.");
        near(hit.x, 470, "Margin-selected ray hit the wrong circle coordinate.");
        near(adjusted.y, -75, "Category margin and circle centre adjustment were conflated.");
    }
    Fixture threeD;
    threeD.circle(5, .5f, .05f);
    RetailSteeringPoint hit{}, adjusted{};
    require(threeD.detailed({1000, 0, 1100}, hit, adjusted), "Offset normalization incorrectly discarded destination Z.");
    // Direction is (1/sqrt(2),0,1/sqrt(2)), so offset Y is 0.353553 m.
    near(hit.z, 1100, "Full 3D clear hit lost destination Z.");
}

void checkPolygonCorners()
{
    for (int category : {5, 3}) {
        Fixture fixture;
        fixture.actorOwner.levelCategory = category;
        fixture.box(5, 0);
        RetailSteeringPoint hit{}, adjusted{};
        require(!fixture.detailed({1000, 0, 100}, hit, adjusted), "A rectangle did not obstruct the first ray.");
        const float extension = category == 3 ? 200.f : 100.f;
        const float length = std::sqrt(20.f * 20.f + 200.f * 200.f);
        near(hit.x, 480, "Rectangle first-hit X differs from its left face.");
        near(adjusted.x, 480 - 20 / length * extension, "Polygon X did not extend the selected corner from body origin.");
        near(adjusted.y, 200 + 200 / length * extension, "Polygon Y did not extend the selected corner.");
        near(adjusted.z, 100, "Polygon adjustment lost scratch Z.");
    }
    Fixture second;
    second.box(5, -.5f, .2f, .1f);
    RetailSteeringPoint hit{}, adjusted{};
    require(!second.detailed({1000, 0, 100}, hit, adjusted), "Second-ray rectangle was missed.");
    const float length = std::sqrt(20.f * 20.f + 10.f * 10.f);
    near(adjusted.x, 480 - 20 / length * 100, "Second-ray corner X changed.");
    near(adjusted.y, -40 + 10 / length * 100, "Zero cross product must choose the original current corner.");

    Fixture rotated;
    rotated.box(5, 0, 2, .2f, b2_pi / 2);
    require(!rotated.detailed({1000, 0, 100}, hit, adjusted), "Rotated polygon normal matching failed.");
    // Upper-left world corner (4.8,2), extended from centre (5,0).
    const float rotatedLength = std::sqrt(20.f * 20.f + 200.f * 200.f);
    near(adjusted.x, 480 - 20 / rotatedLength * 100, "Rotated corner transformation lost world X.");
    near(adjusted.y, 200 + 200 / rotatedLength * 100, "Rotated corner transformation lost world Y.");

    Fixture positive;
    positive.box(5, .8f);
    require(!positive.detailed({1000, 0, 100}, hit, adjusted), "Positive-cross polygon was missed.");
    near(adjusted.x, 480 - 20 / rotatedLength * 100, "Positive cross selected the wrong polygon corner X.");
    near(adjusted.y, -120 - 200 / rotatedLength * 100, "Positive cross did not advance the corner index modulo four.");

    Fixture five;
    b2PolygonDef pentagon;
    pentagon.vertexCount = 5;
    pentagon.vertices[0].Set(-.2f, 1);
    pentagon.vertices[1].Set(-.2f, -1);
    pentagon.vertices[2].Set(.2f, -1);
    pentagon.vertices[3].Set(.3f, 0);
    pentagon.vertices[4].Set(.2f, 1);
    five.body(5, 0)->CreateShape(&pentagon);
    require(!five.detailed({1000, 0, 100}, hit, adjusted), "A valid first-four normal on a five-vertex polygon was rejected.");
    const float fiveLength = std::sqrt(20.f * 20.f + 100.f * 100.f);
    near(adjusted.x, 480 - 20 / fiveLength * 100, "Five-vertex polygon corner X changed.");
    near(adjusted.y, 100 + 100 / fiveLength * 100, "Five-vertex polygon corner Y changed.");
}

void checkQueryFilters()
{
    for (int mode = 0; mode < 7; ++mode) {
        Fixture fixture;
        auto* shape = fixture.circle(5, 0, 1);
        auto filter = shape->GetFilterData();
        filter.categoryBits = mode < 4 ? 0 : 1;
        filter.groupIndex = mode == 0 ? 9 : 3;
        filter.maskBits = mode == 1 || mode == 4 ? 0 : 0xffff;
        shape->SetFilterData(filter);
        if (mode == 3) {
            // Real sensor geometry, not a mutated sensor flag.
            fixture.world.DestroyBody(shape->GetBody());
            auto* owner = fixture.body(5, 0);
            b2CircleDef definition;
            definition.radius = 1;
            definition.isSensor = true;
            shape = owner->CreateShape(&definition);
        }
        if (mode == 5) fixture.data(shape).owner = &fixture.actorOwner;
        if (mode == 6) {
            fixture.data(shape).owner = nullptr;
            fixture.data(shape).nodeBounds.reset();
            fixture.data(shape).heightsCm = {900, 900, 0, 0};
        }
        RetailSteeringPoint hit{}, adjusted{};
        const bool skipped = mode == 0 || mode == 1 || mode == 3 || mode == 5;
        require(fixture.detailed({1000, 0, 100}, hit, adjusted) == (skipped || mode == 6),
                "Detailed category/group/mask/sensor/self/ownerless gate changed.");
        require(retailCanGoToPoint(fixture.world, fixture.actor, {1000, 0, 100}) == skipped,
                "Point overload's distinct candidate filtering changed.");
    }
}

void checkHeightAndScratch()
{
    Fixture fixture;
    auto* circle = fixture.circle(5, 0, 1);
    auto& data = fixture.data(circle);
    RetailSteeringPoint hit{}, adjusted{};
    data.nodeBounds = RetailPhysicsZBounds{100, 100};
    require(!fixture.detailed({1000, 0, 100}, hit, adjusted), "Inclusive node-height endpoints were rejected.");
    data.nodeBounds = RetailPhysicsZBounds{101, 200};
    require(fixture.detailed({1000, 0, 100}, hit, adjusted), "Height-rejected offset hit still obstructed the detailed overload.");
    require(!retailCanGoToPoint(fixture.world, fixture.actor, {1000, 0, 100}),
            "Point overload lost its retained fraction after height rejection.");
    data.nodeBounds.reset();
    data.heightsCm = {101, 200, -10000, 10000};
    data.jumpSurface = true;
    require(fixture.detailed({1000, 0, 100}, hit, adjusted), "Steering invented four-height or jump-flag interval semantics.");
    data.heightsCm = {900, 900, -10000, 10000};
    require(!fixture.detailed({1000, 0, 100}, hit, adjusted), "Equal node-less heights did not bypass Z rejection.");
    near(adjusted.z, 0, "Equal-height query fabricated scratch intersection Z.");
    fixture.obstacleOwner.levelCategory = 3;
    require(!fixture.detailed({1000, 0, 100}, hit, adjusted), "Equal-height vehicle circle was missed.");
    near(adjusted.x, 0, "Equal-height vehicle query fabricated scratch X.");
    near(adjusted.y, 100, "Equal-height vehicle query changed zero-cross side adjustment.");
    near(adjusted.z, 0, "Equal-height vehicle query fabricated scratch Z.");

    Fixture firstRay;
    auto* polygon = firstRay.box(5, 0, .2f, 2, b2_pi / 4);
    firstRay.data(polygon).nodeBounds = RetailPhysicsZBounds{600, 650};
    // First (+Y) ray hits near x4.217, Z521.7; second hits near x5.217,
    // Z621.7. Height rejection of the first hit must not try the second ray.
    require(firstRay.detailed({1000, 0, 1100}, hit, adjusted),
            "A height-rejected first-ray hit incorrectly fell through to its second ray.");
}

void checkStrictFractionAndOrder()
{
    Fixture endpoint;
    endpoint.box(10.2f, 0);
    RetailSteeringPoint hit{}, adjusted{7, 11, 13};
    require(endpoint.detailed({1000, 0, 100}, hit, adjusted), "Fraction exactly one replaced the no-hit state.");
    require(adjusted.x == 7 && adjusted.y == 11 && adjusted.z == 13, "Endpoint hit changed unused adjustment.");
    require(retailCanGoToPoint(endpoint.world, endpoint.actor, {1000, 0, 100}), "Point endpoint fraction was treated as obstructed.");

    Fixture ties;
    auto* upper = ties.circle(5, .5f);
    auto* lower = ties.circle(5, -.5f);
    b2Shape* ordered[100]{};
    const auto count = ties.world.Query(bounds(-.5f, -.5f, 10.5f, .5f), ordered, 100);
    require(count == 2 && ((ordered[0] == upper && ordered[1] == lower) ||
                          (ordered[0] == lower && ordered[1] == upper)), "Tie fixture query changed shape identities.");
    require(!ties.detailed({1000, 0, 100}, hit, adjusted), "Equal-fraction obstacles were missed.");
    near(adjusted.y, ordered[0] == upper ? -75.f : 75.f, "Equal fraction overwrote the first query-order hit.");

    Fixture firstAccepted;
    auto* left = firstAccepted.circle(3, 0, 1);
    firstAccepted.circle(7, 0, 1);
    b2Shape* initial[100]{};
    require(firstAccepted.world.Query(bounds(0, 0, 10, .01f), initial, 100) == 2,
            "First-accepted fixture did not return both shapes.");
    const bool firstIsLeft = initial[0] == left;
    firstAccepted.actor.positionXYCm.x = firstIsLeft ? 1000.f : 0.f;
    const RetailSteeringPoint end{firstIsLeft ? 400.f : 600.f, 0, 100};
    b2Shape* finalOrder[100]{};
    require(firstAccepted.world.Query(bounds(firstIsLeft ? 4.f : 0.f, 0, firstIsLeft ? 10.f : 6.f, .01f),
                                     finalOrder, 100) == 2 && finalOrder[0] == initial[0],
            "First-accepted fixture lost its native broad-phase order.");
    require(retailCanGoToPoint(firstAccepted.world, firstAccepted.actor, end),
            "Point overload sorted by fraction instead of returning its first accepted endpoint hit.");
    require(!firstAccepted.detailed(end, hit, adjusted), "Detailed overload failed to retain its nearer offset hit.");

    Fixture cap;
    for (int i = 0; i < 101; ++i) cap.circle(5, 0, 1);
    b2Shape* limited[100]{};
    b2Shape* all[102]{};
    const auto query = bounds(-.5f, -.5f, 10.5f, .5f);
    require(cap.world.Query(query, limited, 100) == 100 && cap.world.Query(query, all, 102) == 101,
            "The real broad phase did not produce the 101-shape cap fixture.");
    for (auto* shape : limited) cap.data(shape).owner = &cap.actorOwner;
    require(cap.detailed({1000, 0, 100}, hit, adjusted), "Detailed steering exceeded its original 100-shape query cap.");
    require(retailCanGoToPoint(cap.world, cap.actor, {1000, 0, 100}), "Point steering exceeded its original 100-shape query cap.");
}

void checkLaterRejectedScratch()
{
    Fixture fixture;
    fixture.obstacleOwner.levelCategory = 3;
    auto* left = fixture.circle(3, 0, 1);
    fixture.circle(7, 0, 1);
    b2Shape* order[100]{};
    require(fixture.world.Query(bounds(-.5f, -.5f, 10.5f, .5f), order, 100) == 2,
            "Retained scratch fixture did not return both shapes.");
    const bool forward = order[0] == left;
    fixture.actor.positionXYCm.x = forward ? 0.f : 1000.f;
    fixture.data(order[1]).nodeBounds = RetailPhysicsZBounds{5000, 6000};
    RetailSteeringPoint hit{}, adjusted{};
    require(!fixture.detailed({forward ? 1000.f : 0.f, 0, 1100}, hit, adjusted),
            "Nearest accepted obstacle was lost after a later height rejection.");
    // Full 3D direction gives |offsetY|=0.5/sqrt(2). The first and second
    // circle entry distances are respectively 3-sqrt(0.875),7-sqrt(0.875) m.
    const float halfChord = std::sqrt(.875f);
    const float nearestDistanceCm = (3.f - halfChord) * 100.f;
    const float laterDistanceCm = (7.f - halfChord) * 100.f;
    near(hit.x, forward ? nearestDistanceCm : 1000.f - nearestDistanceCm,
         "Closest hit fraction was replaced by later rejected scratch.");
    near(adjusted.x, forward ? laterDistanceCm : 1000.f - laterDistanceCm,
         "Vehicle adjustment failed to retain later rejected scratch X.");
    near(adjusted.z, 100.f + laterDistanceCm,
         "Adjusted scratch Z was incorrectly saved with the closest accepted hit.");
}

void checkVehicleAndFailures()
{
    for (float y : {.4f, -.4f}) {
        Fixture fixture;
        fixture.obstacleOwner.levelCategory = 3;
        fixture.circle(5, y);
        RetailSteeringPoint hit{}, adjusted{};
        require(!fixture.detailed({1000, 0, 100}, hit, adjusted), "Vehicle circle was missed.");
        near(adjusted.x, 500.f - std::sqrt(.3f * .3f - .1f * .1f) * 100.f,
             "Vehicle adjustment did not use retained intersection scratch X.");
        near(adjusted.y, y > 0 ? 100.f : -100.f, "Vehicle-relative cross-product sign or 50 cm offset changed.");
    }
    for (int mode = 0; mode < 3; ++mode) {
        Fixture fixture;
        b2Shape* shape;
        if (mode == 0) {
            shape = fixture.circle(5, 0, 1);
            shape->GetBody()->SetUserData(nullptr);
        } else {
            b2PolygonDef definition;
            if (mode == 1) {
                definition.vertexCount = 3;
                definition.vertices[0].Set(-1, -1);
                definition.vertices[1].Set(1, 0);
                definition.vertices[2].Set(-1, 1);
            } else {
                definition.vertexCount = 5;
                definition.vertices[0].Set(-.2f, -1);
                definition.vertices[1].Set(.2f, -1);
                definition.vertices[2].Set(.3f, 0);
                definition.vertices[3].Set(.2f, 1);
                definition.vertices[4].Set(-.2f, 1);
            }
            shape = fixture.body(5, 0)->CreateShape(&definition);
        }
        RetailSteeringPoint hit{}, adjusted{};
        bool rejected = false;
        try { fixture.detailed({1000, 0, 100}, hit, adjusted); }
        catch (const std::logic_error&) { rejected = true; }
        require(rejected, "Unsupported native metadata or polygon entries silently acquired approximate steering.");
        if (mode == 0) {
            rejected = false;
            try { retailCanGoToPoint(fixture.world, fixture.actor, {1000, 0, 100}); }
            catch (const std::logic_error&) { rejected = true; }
            require(rejected, "Point query accepted intersected missing metadata.");
        }
    }
}
}

int main()
{
    try {
        checkClearAndOverloads();
        checkCircleAndMargins();
        checkPolygonCorners();
        checkQueryFilters();
        checkHeightAndScratch();
        checkStrictFractionAndOrder();
        checkLaterRejectedScratch();
        checkVehicleAndFailures();
        std::cout << "Retail native steering geometry, overloads, query ordering, heights and binding errors passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
