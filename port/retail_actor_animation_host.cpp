#include "retail_actor_animation_host.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

RetailActorAnimationGlobals::RetailActorAnimationGlobals(FileSystemBase& resources)
    : RetailActorAnimationGlobals(
          std::make_shared<const RetailActorAnimationCatalog>(loadRetailActorAnimationCatalog(resources)),
          std::make_shared<const CityAnimationLibrary>(loadCityAnimationLibrary(resources, "anim_ingame.bdae"))) {}

RetailActorAnimationGlobals::RetailActorAnimationGlobals(
    std::shared_ptr<const RetailActorAnimationCatalog> clips,
    std::shared_ptr<const CityAnimationLibrary> library)
    : indexedClips(std::move(clips)), fullLibrary(std::move(library))
{
    if (!indexedClips || !fullLibrary)
        throw std::invalid_argument("Character animation globals require their genuine indexed/full source owners.");
}
void RetailActorAnimationGlobals::clearSharedTimeline()
{
    timeline.reset();
}

RetailActorAnimationHost::RetailActorAnimationHost(std::shared_ptr<RetailActorScene> scene,
    RetailActorGraph& manager, RetailActorAnimationGlobals& resources)
    : actor(std::move(scene)), graph(manager), globals(resources)
{
    if (!actor) throw std::invalid_argument("Character animation host requires its actual retained actor.");
}
const CityScene& RetailActorAnimationHost::sourceModel() const { return actor->source().model; }
const void* RetailActorAnimationHost::actorInstance() const { return &actor->root(); }
RetailActorPose& RetailActorAnimationHost::factoryAnimator() const
{
    if (!localAnimator) throw std::logic_error("No live local Character animator factory reference.");
    return *localAnimator;
}
void RetailActorAnimationHost::constructAnimator(std::string_view resource)
{
    if (resource != "./anim_ingame.bdae")
        throw std::invalid_argument("Character animation host supports the original anim_ingame factory resource.");
    if (localAnimator || !boundAnimator.expired())
        throw std::logic_error("Construct a fresh Character host for another complete binding session.");
    auto fresh = RetailActorPose::create(globals.fullLibrary);
    // Real original optional attachment on the sole actual plain animator.
    // Alias the catalogue's owner, without a copied or placeholder timeline.
    auto backing = std::shared_ptr<const RetailTimeline::Clips>(globals.indexedClips,
                                                                &globals.indexedClips->clips());
    fresh->setTimeline(std::make_shared<RetailTimeline>(std::move(backing)));
    localAnimator = std::move(fresh);
}
bool RetailActorAnimationHost::hasSharedTimeline() const { return bool(globals.timeline); }
void RetailActorAnimationHost::retainAnimatorTimeline(const std::array<std::int32_t, 7>& fists)
{
    const auto& attached = factoryAnimator().timeline();
    if (globals.timeline || !attached)
        throw std::logic_error("Only the first genuine attached timeline can become Character's shared timeline.");
    globals.timeline = attached;
    globals.fists = fists;
}
void RetailActorAnimationHost::clearAnimatorTimeline()
{
    factoryAnimator().setTimeline({});
}
void RetailActorAnimationHost::bindAnimatorToActor()
{
    auto& fresh = factoryAnimator();
    if (fresh.timeline())
        throw std::logic_error("Character must detach the actual timeline before binding its plain animator.");
    fresh.bind(actor);
    boundAnimator = localAnimator;
}
void RetailActorAnimationHost::releaseAnimatorReference()
{
    (void)factoryAnimator();
    localAnimator.reset();
}
std::shared_ptr<RetailActorPose> RetailActorAnimationHost::pose() const
{
    auto retained = localAnimator ? localAnimator : boundAnimator.lock();
    if (!retained || (!localAnimator && !retained->boundTo(*actor)))
        throw std::logic_error("Character's actual retained bound plain animator is unavailable.");
    return retained;
}
RetailActorSceneNode& RetailActorAnimationHost::node(const void* identity) const
{
    const auto found = std::find_if(actor->nodes().begin(), actor->nodes().end(),
        [&](const auto& owned) { return owned.get() == identity; });
    if (found == actor->nodes().end())
        throw std::invalid_argument("Character operation requires a genuine node in this actor's current hierarchy.");
    return **found;
}
const void* RetailActorAnimationHost::findNodeByUID(std::string_view uid)
{
    if (uid.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Embedded NUL in Character live-node UID.");
    return actor->root().findUid(uid);
}
void RetailActorAnimationHost::setFirstChildRenderingLayer(const void* identity, std::int32_t layer)
{
    auto& parent = node(identity);
    if (parent.children().empty())
        throw std::logic_error("Character layer operation requires the actual first child.");
    parent.children().front()->setRenderingLayer(layer);
}
void RetailActorAnimationHost::setColor(const void* identity, std::uint32_t color, std::uint32_t selector)
{
    setRetailActorColor(node(identity), color, selector);
}
void RetailActorAnimationHost::setMaterialFlag(const void* identity, std::uint32_t flag, bool value)
{
    setRetailActorMaterialFlag(node(identity), flag, value);
}
void RetailActorAnimationHost::registerShadow(const void* identity)
{
    auto& shadow = node(identity);
    const auto retained = pose();
    graph.registerShadow(actor, shadow, *retained);
}
const void* RetailActorAnimationHost::findMesh()
{
    return actor->root().findMesh();
}
