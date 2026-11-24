#include "retail_actor_physics.h"
#include "retail_actor_scene.h"
#include "retail_animation.h"

#include "Common/b2BlockAllocator.h"

#include <cmath>
#include <stdexcept>

namespace {
// Obtain the actual native circle's AABB without inserting a proxy. A derived
// access shim exposes only the historical protected standalone shape factory.
struct ShapeProbe : b2Shape {
    using b2Shape::Create;
    using b2Shape::Destroy;
};
} // namespace

RetailActorCollisionBinding::RetailActorCollisionBinding(RetailPhysicsOwnerState& owner,
    const RetailAnimationClock& clock, std::shared_ptr<RetailActorScene> scene,
    RetailActorSceneNode& actor, RetailActorSceneNode& skin)
    : owner(owner), clock(clock), scene(std::move(scene)), actor(actor), skin(skin) {
    if (!this->scene)
        throw std::invalid_argument("Live actor collision requires a retained scene.");
    const auto contains = [&](const RetailActorSceneNode& node) {
        for (const auto& candidate : this->scene->nodes())
            if (candidate.get() == &node) return true;
        return false;
    };
    if (!contains(actor) || !contains(skin))
        throw std::invalid_argument("Live actor collision nodes belong to a different instance.");
    if (skin.type() != retailSceneSkinnedMeshType || !skin.mesh() || !skin.mesh()->skin() ||
        !skin.mesh()->skin()->jointBoundsCm)
        throw std::invalid_argument("Live collision requires the supported genuine skin transformed getter.");
    auto* ancestor = &skin;
    while (ancestor && ancestor != &actor) ancestor = ancestor->parent();
    if (!ancestor)
        throw std::invalid_argument("getSkinMesh binding is outside the actual actor subtree.");
}
const RetailPhysicsOwnerState& RetailActorCollisionBinding::ownerIdentity() const { return owner; }
float RetailActorCollisionBinding::getPositionZCm() const { return actor.absolute()[14]; }
float RetailActorCollisionBinding::storedRootHeightCm() const { return clock.rootHeight; }
RetailPhysicsZBounds RetailActorCollisionBinding::transformedBoundingBoxCm() const {
    const auto& box = skin.transformedBoundingBox();
    return {box[0][2], box[1][2]};
}

RetailActorPhysics::RetailActorPhysics(RetailHeightTerrain& terrain, b2World& world,
                                       RetailPhysicsOwnerState& owner, RetailHeightController* existingHeight)
    : world(world), owner(owner), height(existingHeight) {
    if (!height) {
        ownedHeight = std::make_unique<RetailHeightController>(terrain, world, 1, nullptr);
        height = ownedHeight.get();
        owner.currentJumpState = 0;
    }
}

RetailActorPhysics::~RetailActorPhysics() {
    // Native lifetime correction: contact Remove sees the complete live metadata.
    // Actor body precedes the owned height/controller's local bodies, as original.
    if (actorBody)
        world.DestroyBody(actorBody);
    actorBody = nullptr; // Owned-cliff destruction callbacks must not observe freed actor storage.
    bodyData.reset();
    ownedHeight.reset();
}

void RetailActorPhysics::initialize(const RetailHeightPoint& position,
                                    std::optional<RetailPhysicsZBounds> skinNodeBounds,
                                    const RetailPhysicsOwnerState* player) {
    initializeBody(position, skinNodeBounds, player, {});
}

void RetailActorPhysics::initializeLive(const RetailHeightPoint& position,
    std::shared_ptr<const RetailActorCollisionBinding> binding, const RetailPhysicsOwnerState* player) {
    if (actorBody) return;
    if (!binding || &binding->ownerIdentity() != &owner)
        throw std::invalid_argument("Live actor body requires its canonical owner binding.");
    initializeBody(position, std::nullopt, player, std::move(binding));
}

void RetailActorPhysics::initializeBody(const RetailHeightPoint& position,
    std::optional<RetailPhysicsZBounds> skinNodeBounds, const RetailPhysicsOwnerState* player,
    std::shared_ptr<const RetailActorCollisionBinding> binding) {
    if (actorBody) return;
    if (!std::isfinite(position[0]) || !std::isfinite(position[1]))
        throw std::invalid_argument("Retail actor body requires finite initial XY.");
    b2BodyDef definition;
    definition.position.Set(position[0] / 100.f, position[1] / 100.f);
    definition.allowSleep = false;
    auto* created = world.CreateBody(&definition);
    if (!created)
        throw std::runtime_error("Could not allocate retail actor body.");
    try {
        b2CircleDef circle;
        circle.localPosition.Set(0, 0);
        circle.radius = .5f;
        circle.density = 1.f;
        circle.friction = .3f;
        circle.restitution = 0.f;
        circle.isSensor = false;
        circle.filter.categoryBits = &owner == player ? 2 : 4;
        circle.filter.maskBits = 0xffff;
        circle.filter.groupIndex = static_cast<int16>(height->groupIndex());
        // Original CreateShape outside the world asserts in Debug but silently
        // loses the proxy in Release. Report unsupported initial geometry instead.
        b2BlockAllocator allocator;
        auto* probe = ShapeProbe::Create(&circle, &allocator);
        if (!probe)
            throw std::runtime_error("Could not allocate retail actor circle probe.");
        b2AABB aabb;
        probe->ComputeAABB(&aabb, created->GetXForm());
        ShapeProbe::Destroy(probe, &allocator);
        if (!world.InRange(aabb))
            throw std::runtime_error("Retail actor initial circle outside world proxy range.");
        originalFilter = circle.filter;
        if (!created->CreateShape(&circle))
            throw std::runtime_error("Could not allocate retail actor circle.");
        created->SetMassFromShapes();
        auto metadata = std::make_unique<RetailPhysicsBodyData>(
            RetailPhysicsBodyData{&owner, skinNodeBounds, {0, 0, 0, 0}, false});
        metadata->liveOwner = binding;
        metadata->liveNode = std::move(binding);
        created->SetUserData(metadata.get());
        bodyData = std::move(metadata);
        actorBody = created;
    } catch (...) {
        world.DestroyBody(created);
        throw;
    }
}

void RetailActorPhysics::setBodyPositionAfterSceneUpdate(const RetailHeightPoint& requested) {
    if (!actorBody)
        return;
    actorBody->SetXForm(b2Vec2(requested[0] / 100.f, requested[1] / 100.f), actorBody->GetAngle());
    for (auto* shape = actorBody->GetShapeList(); shape; shape = shape->GetNext())
        world.Refilter(shape);
}

void RetailActorPhysics::synchronizeSkinNodeBounds(std::optional<RetailPhysicsZBounds> bounds) {
    if (!bodyData)
        throw std::logic_error("Retail actor skin bounds require an initialized body.");
    if (bodyData->liveNode || bodyData->liveOwner)
        throw std::logic_error("Live actor collision uses retained getters, not synchronized box snapshots.");
    bodyData->nodeBounds = bounds;
}

void RetailActorPhysics::removeCollisions() {
    if (!actorBody)
        return;
    for (auto* shape = actorBody->GetShapeList(); shape; shape = shape->GetNext()) {
        auto filter = shape->GetFilterData();
        if (filter.maskBits) {
            filter.groupIndex = 0;
            filter.categoryBits = 0;
            filter.maskBits = 0;
            shape->SetFilterData(filter);
            world.Refilter(shape);
        }
    }
}

void RetailActorPhysics::resetCollisions() {
    if (!actorBody)
        return;
    for (auto* shape = actorBody->GetShapeList(); shape; shape = shape->GetNext()) {
        shape->SetFilterData(originalFilter);
        world.Refilter(shape);
    }
}
