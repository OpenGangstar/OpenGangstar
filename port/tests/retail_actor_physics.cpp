#include "retail_actor_physics.h"
#include "retail_actor_scene.h"
#include "retail_animation.h"
#include "filesystem.h"
#include <iomanip>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void close(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < .0001f, message);
}
b2AABB bounds() {
    b2AABB value;
    value.lowerBound.Set(-2000, -2000);
    value.upperBound.Set(2000, 2000);
    return value;
}
struct Fixture {
    CityMesh mesh;
    CityNavigation nav;
    Fixture() {
        mesh.positions = {{0, 0, 10}, {100, 0, 10}, {0, 100, 10}};
        mesh.buffers.push_back({"", {0, 1, 2}});
        nav.buffers.resize(1);
        for (size_t i = 0; i < 8; ++i)
            nav.buffers[0].neighbors[i] = {uint16_t(i < 6 ? UINT16_MAX : 0)};
        nav.buffers[0].roadFlags = {0};
        nav.buffers[0].overlaps.resize(1);
        nav.cliffVertexKeys.resize(6);
        nav.materialBuffers.fill(0);
    }
    void cliff() {
        mesh.positions = {{100, 0, 0}, {0, 0, 0}, {100, 0, 100}};
        nav.cliffVertexKeys = {0, 1, 1, 1, 2, 1};
        nav.cliffTriangles = {0, 0, 0};
    }
};
void defaultsAndIdentity() {
    Fixture f;
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailPhysicsOwnerState owner{5, 300, 12, 17};
    RetailActorPhysics actor(terrain, world, owner);
    require(!actor.body() && actor.heightController().groupIndex() == 1 && owner.currentJumpState == 0,
            "Fresh one-slot attributes precede body and initialize live count");
    actor.setBodyPositionAfterSceneUpdate({1, 2, 3});
    actor.removeCollisions();
    actor.resetCollisions();
    actor.initialize({123.5f, -234.5f, 300}, RetailPhysicsZBounds{-40, 60}, &owner);
    auto* body = actor.body();
    auto* shape = body->GetShapeList();
    require(world.GetBodyCount() == 2 && world.GetProxyCount() == 1 && body->IsDynamic(),
            "Actual dynamic body and live proxy");
    close(body->GetPosition().x, 1.235f, "Initial GetPosition X centimetres conversion");
    close(body->GetPosition().y, -2.345f, "Initial GetPosition Y centimetres conversion");
    require(body->GetAngle() == 0 && body->GetLinearVelocity().x == 0 && body->GetLinearVelocity().y == 0 &&
                body->GetAngularVelocity() == 0,
            "Original initial angle and velocities are zero");
    require(!body->IsBullet() && !body->IsSleeping(), "Body is initially awake and nonbullet");
    require(shape && !shape->GetNext() && shape->GetType() == e_circleShape && !shape->IsSensor(),
            "Exactly one nonsensor circle");
    const auto* circle = static_cast<const b2CircleShape*>(shape);
    require(circle->GetRadius() == .5f && circle->GetLocalPosition().x == 0 &&
                circle->GetLocalPosition().y == 0,
            "Original final circle radius and zero local centre");
    require(shape->GetFriction() == .3f && shape->GetRestitution() == 0, "Original circle material");
    const auto& filter = shape->GetFilterData();
    require(filter.categoryBits == 2 && filter.maskBits == 0xffff &&
                filter.groupIndex == actor.heightController().groupIndex(),
            "Explicit player identity and shared height-pool group");
    const float expectedMass = b2_pi * .5f * .5f;
    close(body->GetMass(), expectedMass, "Circle density1 computes analytic pi*r*r mass");
    close(body->GetInertia(), .5f * expectedMass * .5f * .5f,
          "Circle density1 computes analytic rotational inertia");
    const auto* data = actor.metadata();
    require(body->GetUserData() == data && data->owner == &owner && data->nodeBounds &&
                data->nodeBounds->lowerCm == -40 && data->nodeBounds->upperCm == 60,
            "Full native owner pointer and raw untransformed skin bbox");
    require(data->heightsCm == std::array<float, 4>{0, 0, 0, 0} && !data->jumpSurface,
            "Ordinary actor metadata heights/flag clear");
    body->SetLinearVelocity(b2Vec2(1, 2));
    body->SetAngularVelocity(.25f);
    world.Step(1.f / 60, 10);
    close(body->GetLinearVelocity().x, 1, "Zero linear damping");
    close(body->GetAngularVelocity(), .25f, "Zero angular damping");
    require(body->GetAngle() > 0, "Rotation is not fixed");
    body->SetLinearVelocity(b2Vec2(0, 0));
    body->SetAngularVelocity(0);
    for (size_t i = 0; i < 180; ++i)
        world.Step(1.f / 60, 10);
    require(!body->IsSleeping(), "Actor body disables sleep even though world permits it");
    const auto old = body->GetPosition();
    actor.initialize({std::numeric_limits<float>::quiet_NaN(), 5000, 999}, std::nullopt, nullptr);
    require(actor.body() == body && body->GetPosition().x == old.x && body->GetPosition().y == old.y &&
                actor.metadata() == data && actor.savedFilter().categoryBits == 2,
            "Existing-body early return precedes all new initialization inputs");
    RetailPhysicsOwnerState other{5, 0, 0, 0};
    RetailActorPhysics npc(terrain, world, other);
    npc.initialize({500, 500, 0}, std::nullopt, &owner);
    require(npc.savedFilter().categoryBits == 4 && npc.savedFilter().groupIndex == 2 &&
                !npc.metadata()->nodeBounds,
            "Other character receives category4 and absent skin remains absent");
}
void placementAndSavedFilters() {
    Fixture f;
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailPhysicsOwnerState owner{5, 10, 2, 0};
    RetailActorPhysics actor(terrain, world, owner);
    actor.initialize({0, 0, 10}, RetailPhysicsZBounds{-40, 60}, &owner);
    auto& height = actor.heightController();
    height.initializeSelected(0, {0, 0}, {10, 10, 77});
    const auto before = height.state(0);
    const auto generation = terrain.generation();
    auto* body = actor.body();
    body->SetXForm(b2Vec2(0, 0), .75f);
    body->SetLinearVelocity(b2Vec2(3, -4));
    body->SetAngularVelocity(2);
    owner.positionZCm = 300;
    owner.currentJumpState = 17;
    actor.setBodyPositionAfterSceneUpdate({1234, -5678, 300});
    close(body->GetPosition().x, 12.34f, "Placement uses requested X/100");
    close(body->GetPosition().y, -56.78f, "Placement uses requested Y/100");
    require(body->GetAngle() == .75f && body->GetLinearVelocity().x == 3 &&
                body->GetLinearVelocity().y == -4 && body->GetAngularVelocity() == 2,
            "Valid placement preserves body angle and velocities");
    require(height.state(0).triangle.indexOffset == before.triangle.indexOffset &&
                height.state(0).cached == before.cached && terrain.generation() == generation,
            "Placement leaves selected height/cache/generation unchanged");
    require(owner.positionZCm == 300 && owner.rootZCm == 2 && owner.currentJumpState == 17 &&
                actor.metadata()->nodeBounds->lowerCm == -40,
            "Live owner synchronization belongs to caller; raw node bounds are unchanged");
    b2CircleDef extra;
    extra.radius = .1f;
    extra.localPosition.Set(3, 0);
    extra.filter.categoryBits = 0x80;
    extra.filter.maskBits = 0;
    extra.filter.groupIndex = 23;
    auto* initiallyMasked = body->CreateShape(&extra);
    actor.removeCollisions();
    require(initiallyMasked->GetFilterData().categoryBits == 0x80 &&
                initiallyMasked->GetFilterData().groupIndex == 23,
            "removeCollisions skips a preexisting zero-mask shape");
    for (auto* shape = body->GetShapeList(); shape; shape = shape->GetNext())
        if (shape != initiallyMasked)
            require(!shape->GetFilterData().categoryBits && !shape->GetFilterData().maskBits &&
                        !shape->GetFilterData().groupIndex,
                    "removeCollisions zeros all three filter fields for active masks");
    actor.resetCollisions();
    for (auto* shape = body->GetShapeList(); shape; shape = shape->GetNext())
        require(shape->GetFilterData().categoryBits == 2 && shape->GetFilterData().maskBits == 0xffff &&
                    shape->GetFilterData().groupIndex == height.groupIndex(),
                "resetCollisions restores original saved filter to every shape");
    require(actor.savedFilter().categoryBits == 2 && actor.savedFilter().maskBits == 0xffff &&
                actor.savedFilter().groupIndex == height.groupIndex(),
            "Disabling/restoring preserves original saved filter");
}
struct MetadataContacts : b2ContactListener {
    RetailPhysicsOwnerState* expected;
    size_t adds = 0, removes = 0, liveRemoves = 0;
    explicit MetadataContacts(RetailPhysicsOwnerState& owner) : expected(&owner) {}
    void Add(const b2ContactPoint*) override { ++adds; }
    void Remove(const b2ContactPoint* point) override {
        ++removes;
        for (auto* shape : {point->shape1, point->shape2}) {
            const auto* data = static_cast<RetailPhysicsBodyData*>(shape->GetBody()->GetUserData());
            if (data && data->owner == expected && data->nodeBounds) {
                require(std::isfinite(data->nodeBounds->lowerCm), "Metadata bounds remain live in Remove");
                ++liveRemoves;
            }
        }
    }
};
void contactsAndMetadataLifetime(bool useRawNodeBounds) {
    Fixture f;
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    world.SetContinuousPhysics(false);
    RetailPhysicsOwnerState owner{useRawNodeBounds ? 8 : 5, 0, 0, 0};
    MetadataContacts contacts(owner);
    world.SetContactListener(&contacts);
    RetailContactFilter filter(useRawNodeBounds ? nullptr : &owner, {});
    world.SetContactFilter(&filter);
    auto actor = std::make_unique<RetailActorPhysics>(terrain, world, owner);
    actor->initialize({0, 0, 0}, RetailPhysicsZBounds{0, 100}, useRawNodeBounds ? nullptr : &owner);
    RetailPhysicsBodyData wallData{nullptr, std::nullopt, {0, 100, 0, 0}, false};
    b2BodyDef wallDef;
    wallDef.position.Set(.5f, 0);
    wallDef.userData = &wallData;
    auto* wall = world.CreateBody(&wallDef);
    b2CircleDef circle;
    circle.radius = .5f;
    circle.filter.categoryBits = 0;
    circle.filter.groupIndex = actor->heightController().groupIndex();
    wall->CreateShape(&circle);
    world.Step(1.f / 60, 10);
    require(contacts.adds > 0 && world.GetContactCount() > 0,
            "Real original custom filter creates actor contact");
    const auto previousRemoves = contacts.removes;
    if (useRawNodeBounds)
        actor->synchronizeSkinNodeBounds(RetailPhysicsZBounds{1000, 1100});
    else
        owner.positionZCm = 1000;
    require(world.GetContactCount() > 0, "Owner/bounds mutation alone does not invent refilter timing");
    const auto p = actor->body()->GetPosition();
    actor->setBodyPositionAfterSceneUpdate({p.x * 100, p.y * 100, owner.positionZCm});
    world.Step(1.f / 60, 10);
    require(contacts.removes > previousRemoves && world.GetContactCount() == 0,
            "Same-XY SetPosition refilters changed vertical owner/node state");
    if (useRawNodeBounds)
        actor->synchronizeSkinNodeBounds(RetailPhysicsZBounds{0, 100});
    else
        owner.positionZCm = 0;
    actor->setBodyPositionAfterSceneUpdate({0, 0, 0});
    world.Step(1.f / 60, 10);
    require(world.GetContactCount() > 0, "Returning live Z bounds recreates real contact");
    const auto beforeCleanup = contacts.liveRemoves;
    actor.reset();
    require(contacts.liveRemoves > beforeCleanup && world.GetBodyCount() == 2 && world.GetProxyCount() == 1,
            "DestroyBody Remove callbacks see live metadata before ownership release");
    world.DestroyBody(wall);
    world.SetContactListener(nullptr);
    world.SetContactFilter(nullptr);
}
struct RecordingFilter : b2ContactFilter {
    std::set<b2Shape*> seen;
    bool ShouldCollide(b2Shape* a, b2Shape* b) override {
        seen.insert(a);
        seen.insert(b);
        return false;
    }
};
void refiltersEveryShape() {
    Fixture f;
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RecordingFilter filter;
    world.SetContactFilter(&filter);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailActorPhysics actor(terrain, world, owner);
    actor.initialize({0, 0, 0}, RetailPhysicsZBounds{0, 100}, &owner);
    auto* first = actor.body()->GetShapeList();
    b2CircleDef extra;
    extra.radius = .1f;
    extra.localPosition.Set(.1f, 0);
    extra.filter.maskBits = 0;
    auto* second = actor.body()->CreateShape(&extra);
    b2BodyDef def;
    auto* other = world.CreateBody(&def);
    b2CircleDef large;
    large.radius = 2;
    other->CreateShape(&large);
    world.Step(1.f / 60, 10);
    filter.seen.clear();
    actor.setBodyPositionAfterSceneUpdate({0, 0, 0});
    world.Step(1.f / 60, 10);
    require(filter.seen.count(first) && filter.seen.count(second),
            "SetPosition refilters every shape including zero mask");
    world.DestroyBody(other);
    world.SetContactFilter(nullptr);
}
void suppliedBodyAndBorrowedHeight() {
    Fixture f;
    f.cliff();
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    {
        RetailActorPhysics actor(terrain, world, owner);
        actor.initialize({50, 10, 0}, RetailPhysicsZBounds{0, 100}, &owner);
        actor.body()->SetLinearVelocity(b2Vec2(0, 1));
        actor.heightController().initializeSelected(0, {0, 0}, {50, 10, 0});
        require(actor.heightController().bodies(0).size() == 1,
                "Later actor body does not replace constructor-supplied NULL");
        require(actor.heightController().bodies(0)[0]->GetShapeList()->GetFilterData().groupIndex ==
                    actor.savedFilter().groupIndex,
                "Actual local cliff and actor share original pool identity");
    }
    require(world.GetBodyCount() == 1 && world.GetProxyCount() == 0,
            "Owned actor and local terrain bodies release together");
    b2BodyDef definition;
    auto* supplied = world.CreateBody(&definition);
    supplied->SetLinearVelocity(b2Vec2(0, 1));
    {
        RetailHeightController existing(terrain, world, 1, supplied);
        owner.currentJumpState = 17;
        {
            RetailActorPhysics actor(terrain, world, owner, &existing);
            require(owner.currentJumpState == 17, "Borrowed attributes do not reset live counter");
            actor.initialize({50, 10, 0}, RetailPhysicsZBounds{0, 100}, &owner);
            actor.body()->SetLinearVelocity(b2Vec2(0, -1));
            existing.initializeSelected(0, {0, 0}, {50, 10, 0});
            require(existing.bodies(0).empty(),
                    "Borrowed constructor body remains the height velocity source");
            supplied->SetLinearVelocity(b2Vec2(0, 0));
            actor.body()->SetLinearVelocity(b2Vec2(0, 1));
            existing.initializeSelected(0, {0, 0}, {50, 10, 0});
            require(existing.bodies(0).size() == 1,
                    "Actor velocity is not silently rebound into borrowed height state");
        }
        require(existing.bodies(0).size() == 1 && world.GetBodyCount() == 3,
                "Borrowed controller and local bodies outlive actor backend without double ownership");
    }
    world.DestroyBody(supplied);
}
void rangeFailureAndFrozenPlacement() {
    Fixture f;
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    RetailActorPhysics actor(terrain, world, owner);
    bool failed = false;
    try {
        actor.initialize({220000, 0, 0}, std::nullopt, &owner);
    } catch (const std::runtime_error& error) {
        failed = std::string(error.what()).find("outside world proxy range") != std::string::npos;
    }
    require(failed && !actor.body() && !actor.metadata() && world.GetBodyCount() == 1 &&
                world.GetProxyCount() == 0,
            "Initial outside circle fails explicitly with complete partial-body cleanup");
    const auto group = actor.heightController().groupIndex();
    failed = false;
    try {
        actor.initialize({std::numeric_limits<float>::infinity(), 0, 0}, std::nullopt, &owner);
    } catch (const std::invalid_argument&) {
        failed = true;
    }
    require(failed && world.GetBodyCount() == 1 && actor.heightController().groupIndex() == group,
            "Initial invalid XY keeps attributes reserved for retry");
    {
        RetailPhysicsOwnerState partialOwner{5, 0, 0, 0};
        RetailActorPhysics partial(terrain, world, partialOwner);
        failed = false;
        try {
            partial.initialize({200050, 0, 0}, std::nullopt, &partialOwner);
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed && !partial.body() && world.GetProxyCount() == 0,
                "Initial circle merely touching world boundary is rejected");
        partial.initialize({200025, 0, 0}, std::nullopt, &partialOwner);
        require(partial.body() && world.GetProxyCount() == 1,
                "Initial circle strict partial world overlap is accepted by real predicate");
    }
    actor.initialize({0, 0, 0}, RetailPhysicsZBounds{0, 100}, &owner);
    auto* body = actor.body();
    body->SetXForm(b2Vec2(0, 0), .75f);
    body->SetLinearVelocity(b2Vec2(1, 2));
    body->SetAngularVelocity(.5f);
    actor.setBodyPositionAfterSceneUpdate({220000, 0, 0});
    require(body->IsFrozen() && world.GetProxyCount() == 0 && body->GetPosition().x == 2200 &&
                body->GetAngle() == .75f && body->GetLinearVelocity().x == 0 &&
                body->GetLinearVelocity().y == 0 && body->GetAngularVelocity() == 0,
            "Original finite out-of-world placement freezes after transform and zeros velocities");
    actor.setBodyPositionAfterSceneUpdate({0, 0, 0});
    require(body->IsFrozen() && body->GetPosition().x == 2200 && world.GetProxyCount() == 0,
            "Subsequent placement of original frozen body does not unfreeze or move it");
}
struct CleanupObserver : b2DestructionListener {
    RetailActorPhysics* actor = nullptr;
    b2Body* originalActorBody = nullptr;
    b2Body* localCliff = nullptr;
    const RetailPhysicsBodyData* originalMetadata = nullptr;
    bool actorShapeSeen = false, cliffShapeSeen = false;
    bool actorBodyLiveDuringOwnShape = false, metadataLiveDuringOwnShape = false;
    b2Body* bodyDuringCliff = nullptr;
    void SayGoodbye(b2Joint*) override {}
    void SayGoodbye(b2Shape* shape) override {
        // Recording only: callbacks never throw or dereference a freed body.
        if (shape->GetBody() == originalActorBody) {
            actorShapeSeen = true;
            actorBodyLiveDuringOwnShape = actor->body() == originalActorBody;
            metadataLiveDuringOwnShape =
                shape->GetBody()->GetUserData() == originalMetadata && originalMetadata->owner != nullptr;
        }
        if (shape->GetBody() == localCliff) {
            cliffShapeSeen = true;
            bodyDuringCliff = actor->body();
        }
    }
};
void cleanupPointerOrdering() {
    Fixture f;
    f.cliff();
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailPhysicsOwnerState owner{5, 0, 0, 0};
    auto actor = std::make_unique<RetailActorPhysics>(terrain, world, owner);
    actor->initialize({50, 10, 0}, RetailPhysicsZBounds{0, 100}, &owner);
    actor->heightController().initializeSelected(0, {0, 0}, {50, 10, 0});
    CleanupObserver observer;
    observer.actor = actor.get();
    observer.originalActorBody = actor->body();
    observer.originalMetadata = actor->metadata();
    observer.localCliff = actor->heightController().bodies(0).at(0);
    world.SetDestructionListener(&observer);
    actor.reset();
    world.SetDestructionListener(nullptr);
    require(observer.actorShapeSeen && observer.actorBodyLiveDuringOwnShape &&
                observer.metadataLiveDuringOwnShape,
            "Actor body and metadata stay observable during its own destruction callback");
    require(observer.cliffShapeSeen && observer.bodyDuringCliff == nullptr,
            "Owned cliff destruction must observe NULL after actor DestroyBody returned");
    require(world.GetBodyCount() == 1 && world.GetProxyCount() == 0,
            "Actor cleanup ordering preserves complete native body/proxy ownership release");
}

void poolReuse() {
    Fixture f;
    RetailHeightTerrain terrain(f.mesh, f.nav);
    b2World world(bounds(), b2Vec2(0, 0), true);
    std::array<RetailPhysicsOwnerState, 51> owners{};
    std::vector<std::unique_ptr<RetailActorPhysics>> actors;
    for (size_t i = 0; i < 50; ++i) {
        actors.push_back(std::make_unique<RetailActorPhysics>(terrain, world, owners[i]));
        require(actors.back()->heightController().groupIndex() == int(i + 1),
                "Actor uses real shared50slot pool");
    }
    bool failed = false;
    try {
        RetailActorPhysics overflow(terrain, world, owners[50]);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed && world.GetBodyCount() == 1, "Pool exhaustion creates no stray actor body");
    actors.clear();
    RetailActorPhysics reused(terrain, world, owners[0]);
    require(reused.heightController().groupIndex() == 1, "Actor cleanup releases owned height pool identity");
}
std::shared_ptr<RetailActorSceneSource> collisionSceneSource() {
    auto source = std::make_shared<RetailActorSceneSource>();
    source->model.materialCatalog.push_back({"collision-material", {}});
    auto& model = source->model;
    model.path = "./actor.bdae";
    CityMesh mesh{};
    mesh.name = "body";
    mesh.positions = {{0,0,0},{100,0,0},{0,100,0}};
    mesh.buffers.push_back({"", {0,1,2}});
    mesh.rawBoundsCm = RetailSkinBoundingBox{{{-1,-2,-3},{111,222,333}}};
    model.meshes.push_back(mesh);
    CityNode body{}, bone{};
    body.name = "body-node"; body.parent = -1; body.rotation = {0,0,0,1}; body.scale = {1,1,1};
    body.instances.push_back({2,0,"","body-skin",false});
    body.geometry.push_back({"", "body", {{"", "collision-material", 0}}});
    bone.name = "Bip01-node"; bone.jointName = "Bone1"; bone.parent = -1;
    bone.rotation = {0,0,0,1}; bone.scale = {1,1,1}; bone.translation = {2,3,4};
    model.nodes = {body,bone};
    model.visualScenes.push_back({"visual-uid","visual-display",0,2});
    CitySkin skin{};
    skin.controllerUid = "body-skin"; skin.meshName = "body";
    skin.bindShape = retailSceneIdentity;
    skin.joints.push_back({1,retailSceneIdentity,"Bone1"});
    skin.jointBoundsCm = std::vector<RetailSkinBoundingBox>{}; // Original known-empty branch.
    source->skins.push_back(skin);
    return source;
}
RetailActorSceneNode& actualSkin(RetailActorScene& scene) {
    for (const auto& node : scene.nodes())
        if (node->type() == retailSceneSkinnedMeshType) return *node;
    throw std::runtime_error("Fixture requires its actual retained skin node");
}
struct ObservedReads final : RetailPhysicsOwnerReads, RetailPhysicsNodeReads {
    std::shared_ptr<const RetailActorCollisionBinding> actual;
    std::vector<std::string>& events;
    mutable RetailPhysicsZBounds lastBox{};
    std::function<void()> beforePosition;
    ObservedReads(std::shared_ptr<const RetailActorCollisionBinding> actual, std::vector<std::string>& events)
        : actual(std::move(actual)), events(events) {}
    const RetailPhysicsOwnerState& ownerIdentity() const override { return actual->ownerIdentity(); }
    float getPositionZCm() const override {
        events.push_back("GetPosition");
        if (beforePosition) beforePosition();
        return actual->getPositionZCm();
    }
    float storedRootHeightCm() const override { return actual->storedRootHeightCm(); }
    RetailPhysicsZBounds transformedBoundingBoxCm() const override {
        events.push_back("skinTransformedBox"); lastBox = actual->transformedBoundingBoxCm(); return lastBox;
    }
};
void liveScenePhases(std::shared_ptr<const RetailActorSceneSource> source, const std::string& model = "") {
    Fixture terrainFixture;
    RetailHeightTerrain terrain(terrainFixture.mesh, terrainFixture.nav);
    RetailPhysicsOwnerState owner{2,9999,8888,0};
    RetailAnimationClock clock{}; clock.rootHeight = 10;
    auto scene = std::make_shared<RetailActorScene>(source);
    auto& skin = actualSkin(*scene);
    auto binding = std::make_shared<RetailActorCollisionBinding>(owner,clock,scene,scene->root(),skin);
    std::vector<std::string> events;
    const RetailPhysicsOwnerState* player = nullptr;
    RetailContactFilter filter(RetailContactFilter::PlayerLookup([&] {events.push_back("GetPlayer");return player;}), {});
    RetailPhysicsBodyData wall{nullptr,std::nullopt,{0,1,0,1},false};
    b2World world(bounds(),b2Vec2(0,0),true);
    world.SetContactFilter(&filter);
    RetailActorPhysics actor(terrain,world,owner);
    actor.initializeLive({0,0,0},binding,nullptr);
    auto* data = static_cast<RetailPhysicsBodyData*>(actor.body()->GetUserData());
    require(data == actor.metadata() && !data->nodeBounds && data->liveNode && data->liveOwner &&
                &data->liveOwner->ownerIdentity() == &owner && skin.mesh()->skinCacheDirty(),
            "Live metadata retains the actual skin without eager bbox/palette reads");
    auto observed = std::make_shared<ObservedReads>(binding,events);
    data->liveOwner = observed; data->liveNode = observed;
    b2BodyDef wallDef; wallDef.userData = &wall;
    auto* wallBody = world.CreateBody(&wallDef);
    b2CircleDef shapeDef; shapeDef.radius = .5f;
    shapeDef.filter.categoryBits = 0; shapeDef.filter.groupIndex = actor.heightController().groupIndex();
    auto* wallShape = wallBody->CreateShape(&shapeDef);
    auto* actorShape = actor.body()->GetShapeList();
    const auto run = [&](const char* phase, bool reversed = false) {
        events.clear();
        const bool result = filter.ShouldCollide(reversed ? wallShape : actorShape, reversed ? actorShape : wallShape);
        if (!model.empty()) {
            std::cout << "livecollision {\"name\":" << std::quoted(model+" "+phase)
                      << ",\"result\":" << result << ",\"dirty\":" << skin.mesh()->skinCacheDirty() << ",\"events\":[";
            for (size_t i=0;i<events.size();++i) {if(i)std::cout<<',';std::cout<<std::quoted(events[i]);}
            std::cout << "]";
            if (std::find(events.begin(),events.end(),"skinTransformedBox") != events.end())
                std::cout << ",\"bounds\":[" << std::setprecision(10) << observed->lastBox.lowerCm << ',' << observed->lastBox.upperCm << ']';
            if (!skin.mesh()->skinCacheDirty()) {
                std::cout << ",\"palette\":[";
                const auto& matrices=skin.mesh()->skinMatrices();
                for(size_t i=0;i<matrices.size();++i) {
                    if(i)std::cout<<',';std::cout<<'[';
                    for(size_t j=0;j<16;++j) {if(j)std::cout<<',';uint32_t value;std::memcpy(&value,&matrices[i][j],4);std::cout<<value;}
                    std::cout<<']';
                }
                std::cout<<']';
            }
            std::cout << "}\n";
        }
        return result;
    };
    run("dirty skin getter inside filter");
    auto box = observed->lastBox;
    require(!skin.mesh()->skinCacheDirty(),"Original dirty getter prepares palette and box");
    const auto palette = skin.mesh()->skinMatrices();
    scene->root().setPosition({0,0,1000}); scene->root().updateAbsolutePosition(true);
    run("absolute update alone retains clean stale skin box");
    require(observed->lastBox.lowerCm==box.lowerCm && observed->lastBox.upperCm==box.upperCm &&
                palette==skin.mesh()->skinMatrices(),"No eager recompute from absolute changes");
    skin.mesh()->computeBoundingBox();
    run("forced compute prior to filter changes visible box");
    box=observed->lastBox;
    scene->root().setPosition({0,0,2000});scene->root().onAnimate(20);
    require(skin.mesh()->skinCacheDirty(),"Real original onAnimate invalidates skin cache");
    skin.mesh()->skinMatrices();
    run("palette-first clears shared dirty so filter sees stale box");
    require(observed->lastBox.lowerCm==box.lowerCm && observed->lastBox.upperCm==box.upperCm,"Shared palette dirty preserves older box");
    scene->root().onAnimate(21);
    run("dirty frame filter recomputes bounds and palette");
    require(observed->lastBox.lowerCm!=box.lowerCm && !skin.mesh()->skinCacheDirty(),"Filter owns genuine lazy recomputation timing");
    owner.levelCategory=5;scene->root().onAnimate(22);
    run("NPC interval bypasses dirty skin getter");
    require(events==std::vector<std::string>{"GetPosition"} && skin.mesh()->skinCacheDirty(),"NPC does not run player/box getter");
    owner.levelCategory=2;player=&owner;
    run("genuine player interval also bypasses dirty skin getter");
    require(events==std::vector<std::string>{"GetPlayer","GetPosition"} && skin.mesh()->skinCacheDirty(),"Player bypass preserves dirty");
    run("reversed genuine player interval",true);
    require(events==std::vector<std::string>{"GetPlayer","GetPosition"} && skin.mesh()->skinCacheDirty(),"Reverse body order preserves actor getter path");
    scene->root().setPosition({0,0,9000});clock.rootHeight=77;
    require(binding->getPositionZCm()==2000 && binding->storedRootHeightCm()==77,"Live binding uses cached absolute and actual clock, not poisoned owner snapshots/local TRS");
    observed->beforePosition=[&]{clock.rootHeight=77;};
    clock.rootHeight=10;wall.heightsCm={1900,1901,1900,1901};
    require(filter.ShouldCollide(actorShape,wallShape),
            "Actual cached GetPosition precedes reading mutated canonical clock rootHeight");
    observed->beforePosition={};
    bool rejected=false;
    try {actor.synchronizeSkinNodeBounds(RetailPhysicsZBounds{0,1});}catch(const std::logic_error&){rejected=true;}
    require(rejected,"Live body forbids eager snapshot synchronization");
    actor.initializeLive({std::numeric_limits<float>::quiet_NaN(),0,0},{},nullptr);
    events.clear();actor.body()->SetUserData(nullptr);
    require(filter.ShouldCollide(actorShape,wallShape) && events.empty(),"Detached body metadata preserves original early fallback without getter calls");
    actor.body()->SetUserData(data);
}
struct LiveContactAudit final : b2ContactListener {
    const RetailPhysicsOwnerState& owner;
    std::weak_ptr<RetailActorScene> scene;
    unsigned added=0,removed=0;
    bool metadataAlive=true;
    LiveContactAudit(const RetailPhysicsOwnerState& owner, std::weak_ptr<RetailActorScene> scene)
        : owner(owner),scene(std::move(scene)) {}
    void Add(const b2ContactPoint*) override { ++added; }
    void Remove(const b2ContactPoint* point) override {
        ++removed;
        for (auto* shape : {point->shape1,point->shape2}) {
            const auto* data=static_cast<const RetailPhysicsBodyData*>(shape->GetBody()->GetUserData());
            if(data && data->owner==&owner)
                metadataAlive &= data->liveOwner && data->liveNode && !scene.expired() &&
                    &data->liveOwner->ownerIdentity()==&owner && data->liveOwner->getPositionZCm()==0;
        }
    }
};
void liveBindingSetupAndLifetime() {
    auto source=collisionSceneSource();
    auto scene=std::make_shared<RetailActorScene>(source);
    auto other=std::make_shared<RetailActorScene>(source);
    RetailPhysicsOwnerState owner{8,0,0,0};RetailAnimationClock clock{};
    auto rejects=[&](RetailActorSceneNode& actor,RetailActorSceneNode& skin) {
        bool rejected=false;
        try {RetailActorCollisionBinding invalid(owner,clock,scene,actor,skin);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Unsupported/root-guessed/foreign getSkinMesh binding rejected at setup");
    };
    rejects(scene->root(),scene->root());
    rejects(scene->root(),actualSkin(*other));
    rejects(*scene->root().findUid("Bip01-node"),actualSkin(*scene));
    auto missingSource=collisionSceneSource();missingSource->skins[0].jointBoundsCm.reset();
    auto missing=std::make_shared<RetailActorScene>(missingSource);
    bool rejected=false;
    try {RetailActorCollisionBinding invalid(owner,clock,missing,missing->root(),actualSkin(*missing));}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Unknown required dynamic bbox metadata rejected without a box getter");
    Fixture f;RetailHeightTerrain terrain(f.mesh,f.nav);
    RetailPhysicsBodyData wall{nullptr,std::nullopt,{0,1,0,1},false};
    RetailContactFilter filter(RetailContactFilter::PlayerLookup([&]{return &owner;}),{});
    LiveContactAudit listener(owner,scene);
    b2World world(bounds(),b2Vec2(0,0),true);
    world.SetContactFilter(&filter);world.SetContactListener(&listener);
    std::weak_ptr<RetailActorScene> retainedScene=scene;
    std::weak_ptr<const RetailActorCollisionBinding> retainedBinding;
    {
        auto binding=std::make_shared<RetailActorCollisionBinding>(owner,clock,scene,scene->root(),actualSkin(*scene));
        retainedBinding=binding;
        auto actor=std::make_unique<RetailActorPhysics>(terrain,world,owner);
        bool failed=false;
        try {actor->initializeLive({0,0,0},{},nullptr);}catch(const std::invalid_argument&){failed=true;}
        require(failed&&!actor->body()&&world.GetBodyCount()==1,"Missing live binding fails before body creation");
        RetailPhysicsOwnerState differentOwner{5,0,0,0};
        auto wrongOwner=std::make_shared<RetailActorCollisionBinding>(differentOwner,clock,scene,scene->root(),actualSkin(*scene));
        failed=false;
        try {actor->initializeLive({0,0,0},wrongOwner,nullptr);}catch(const std::invalid_argument&){failed=true;}
        require(failed&&!actor->body()&&world.GetBodyCount()==1,"Adapter address never substitutes for canonical owner identity");
        wrongOwner.reset();
        actor->initializeLive({0,0,0},binding,&owner);
        binding.reset();scene.reset();
        require(!retainedScene.expired()&&!retainedBinding.expired(),"Actual body metadata retains genuine scene/node binding");
        const auto* data=actor->metadata();
        require(data->liveOwner->getPositionZCm()==0,"Retained node remains callable after external scene release");
        b2BodyDef wallDef;wallDef.userData=&wall;auto* wallBody=world.CreateBody(&wallDef);
        b2CircleDef circle;circle.radius=.5f;circle.filter.categoryBits=0;
        circle.filter.groupIndex=actor->heightController().groupIndex();
        auto* wallShape=wallBody->CreateShape(&circle);
        world.Refilter(actor->body()->GetShapeList());world.Refilter(wallShape);
        world.Step(1.f/60,10);
        require(listener.added>0,"Live filter installed in the genuine world permits actual Box2D contacts");
        actor.reset();
        require(listener.removed>0&&listener.metadataAlive,"Actual destruction Remove sees retained node/owner/clock binding until body is gone");
    }
    require(retainedScene.expired()&&retainedBinding.expired(),"Body destruction releases retained adapter/scene without a cycle");
}

} // namespace
int main(int argc, char** argv) {
    try {
        const auto baseline = b2_byteCount;
        defaultsAndIdentity();
        placementAndSavedFilters();
        contactsAndMetadataLifetime(false);
        contactsAndMetadataLifetime(true);
        refiltersEveryShape();
        suppliedBodyAndBorrowedHeight();
        rangeFailureAndFrozenPlacement();
        cleanupPointerOrdering();
        poolReuse();
        liveScenePhases(collisionSceneSource());
        liveBindingSetupAndLifetime();
        if (argc == 3 && std::string(argv[1]) == "--assets") {
            ResourceFileSystem files(argv[2]);
            for (const auto* name : {"char_pthug.bdae", "char_pthug_jail.bdae"})
                liveScenePhases(loadRetailActorSceneSource(files,name),name);
        }
        require(b2_byteCount == baseline,
                "Native actor/probe/metadata/height ownership releases all physics allocations");
        std::cout
            << "Retail actor body, original placement/refilter, saved filter and lifetime fixtures passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
