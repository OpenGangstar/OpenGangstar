#include "retail_actor_animation.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

RetailActorAnimationCatalog::RetailActorAnimationCatalog(std::vector<CityAnimationClip> clips)
    : entries(std::move(clips)) {
    if (entries.empty() || entries.size() > size_t(std::numeric_limits<int32_t>::max()))
        throw std::invalid_argument("Original actor animation catalog is empty or too large.");
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& c = entries[i];
        if (!c.source || c.source->index != i || c.name.find('\0') != std::string::npos ||
            int64_t(c.source->endMs) - c.source->startMs != c.durationMs ||
            c.durationMs == 0 || c.durationMs > uint32_t(std::numeric_limits<int32_t>::max()))
            throw std::invalid_argument("Actor animation catalog requires indexed original timeline metadata.");
    }
}
const CityAnimationClip& RetailActorAnimationCatalog::at(int32_t animation) const {
    if (animation < 0 || size_t(animation) >= entries.size())
        throw std::out_of_range("Original actor animation index is unavailable.");
    return entries[size_t(animation)];
}
int32_t RetailActorAnimationCatalog::find(std::string_view name) const {
    if (name.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Embedded NUL in original animation name.");
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == name) return static_cast<int32_t>(i);
    return -1;
}
RetailActorAnimationCatalog loadRetailActorAnimationCatalog(FileSystemBase& resources) {
    return RetailActorAnimationCatalog(loadCityAnimationCatalog(resources, "anim_ingame.bdae"));
}

RetailActorAnimationBinding::RetailActorAnimationBinding(const RetailActorAnimationCatalog& catalog,
    RetailActorAnimationSceneHost& scene, RetailAnimationClock& clock, uint32_t& flags, std::string_view rootUID)
    : catalog(catalog), model(scene.sourceModel()), clock(clock), flags(flags), boundClips(catalog.clips()) {
    actor = scene.actorInstance();
    if (!actor || rootUID.empty() || rootUID.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Actual actor instance and exact animation root UID are required.");
    // Original constructs a fresh animator but retains its first timeline as
    // the shared catalog. Detach timeline before onBind, then release the local
    // animator reference; the real bound scene retains its required ownership.
    scene.constructAnimator("./anim_ingame.bdae");
    if (!scene.hasSharedTimeline()) scene.retainAnimatorTimeline({144,94,92,93,95,90,91});
    scene.clearAnimatorTimeline();
    scene.bindAnimatorToActor();
    scene.releaseAnimatorReference();
    root = scene.findNodeByUID(rootUID);
    const auto source = std::find_if(model.nodes.begin(), model.nodes.end(),
        [&](const auto& node) { return node.name == rootUID; });
    if (!root || source == model.nodes.end())
        throw std::invalid_argument("Actual loaded actor animation root binding is unavailable.");
    rootNode = static_cast<size_t>(source - model.nodes.begin());
    shadow = scene.findNodeByUID("shadow-node");
    if (!shadow) throw std::invalid_argument("Actual loaded actor shadow binding is unavailable.");
    scene.setFirstChildRenderingLayer(shadow, 1);
    scene.setColor(shadow, 0xff000000u, 3);
    for (uint32_t type = 0; type < 3; ++type) scene.setColor(shadow, 0xffffffffu, type);
    scene.setMaterialFlag(shadow, 0x400, false);
    scene.setMaterialFlag(shadow, 0x200, false);
    scene.setMaterialFlag(shadow, 4, true);
    scene.setMaterialFlag(shadow, 16, true);
    scene.registerShadow(shadow);
    mesh = scene.findMesh();
    if (!mesh) throw std::invalid_argument("Actual loaded actor skin/mesh binding is unavailable.");
    bindCityAnimationTracks(boundClips, model);
}
const CityAnimationClip& RetailActorAnimationBinding::clip(int32_t animation) const {
    catalog.at(animation); // Explicit indexed native error instead of an ARM invalid read.
    return boundClips.at(size_t(animation));
}
const CityAnimationChannel& RetailActorAnimationBinding::rootChannel(int32_t animation) const {
    const CityAnimationChannel* result = nullptr;
    for (const auto& channel : clip(animation).channels) {
        if (channel.node != rootNode || channel.transform != CityAnimationTransform::translation) continue;
        if (result) throw std::runtime_error("Multiple actor root translation writers require original dispatch.");
        result = &channel;
    }
    if (!result || result->keys.empty() || result->keys.front().timeMs != 0 ||
        result->keys.back().timeMs < clip(animation).durationMs - 1)
        throw std::runtime_error("Actor root track does not cover the original clip interval.");
    return *result;
}
RetailAnimationSelection RetailActorAnimationBinding::select(const RetailAnimationRequest& request,
    bool cinematic, RetailActorAnimationWeaponHost& weapon) {
    // Guards do not compute a rate: reload can change its live multiplier.
    const auto decision = guardRetailAnimationSelection(clock, request, flags, cinematic);
    if (decision != RetailAnimationSelection::accepted) return decision;
    if (clock.animation == 67 && weapon.weaponType() == 7) weapon.reloadRocket();
    const auto& selected = catalog.at(request.animation);
    commitAcceptedRetailAnimation(clock, request, selected.source->startMs, selected.source->endMs, flags);
    return RetailAnimationSelection::accepted;
}
RetailAnimationStep RetailActorAnimationBinding::advance(float elapsed,
    const std::array<float, 16>& matrix, bool body, std::optional<std::array<float, 2>> direction,
    RetailAnimationInterpolation interpolation) {
    return advanceRetailAnimation(clock, elapsed, clock.animation == -1 ? nullptr : &rootChannel(clock.animation),
                                  matrix, body, direction, interpolation);
}
