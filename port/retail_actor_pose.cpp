#include "retail_actor_pose.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
std::string_view cString(const std::string& value)
{
    const std::string_view view(value);
    return view.substr(0, view.find('\0'));
}
}

std::shared_ptr<RetailActorPose> RetailActorPose::create(std::shared_ptr<const CityAnimationLibrary> source)
{
    return std::shared_ptr<RetailActorPose>(new RetailActorPose(std::move(source)));
}
RetailActorPose::RetailActorPose(std::shared_ptr<const CityAnimationLibrary> source) : library(std::move(source))
{
    if (!library || library->tracks.empty()) throw std::invalid_argument("Actor pose requires its original full source library.");
    for (const auto& track : library->tracks) {
        (void)selectRetailAnimationLibraryKey(track, 0);
        // addAnimationTrack converts BOTH endpoints before subtracting, then
        // compares the float-converted current integer length.
        const float span = float(track.keys.back().timeMs) - float(track.keys.front().timeMs);
        if (float(period) < span) {
            if (!std::isfinite(span) || double(span) > std::numeric_limits<int32_t>::max())
                throw std::runtime_error("Original animator length exceeds supported int32 conversion.");
            period = uint32_t(static_cast<int32_t>(span));
        }
    }
    if (!period) throw std::runtime_error("Detached actor animator has zero modulo period.");
    bindings.resize(library->tracks.size());
}
void RetailActorPose::bind(std::shared_ptr<RetailActorScene> scene)
{
    if (!scene) throw std::invalid_argument("Actor pose binding requires a genuine live scene owner.");
    if (root && owner.expired())
        throw std::logic_error("Create a fresh actor pose after its scene context expires.");
    owner = scene;
    onBind(scene->root());
}
void RetailActorPose::onBind(RetailActorSceneNode& node)
{
    const auto live = owner.lock();
    if (!live || &live->root() != &node)
        throw std::logic_error("Bind actor pose through its genuine shared root context.");
    if (root == &node) return; // Original same-root bind does not repair targets.
    root = &node;
    forceBind();
}
void RetailActorPose::forceBind()
{
    const auto live = owner.lock();
    if (!live || root != &live->root()) throw std::logic_error("Actor pose scene lifetime expired.");
    rootMotion = root->findUid("Bip01-node");
    if (!rootMotion) throw std::runtime_error("Actor pose requires the genuine live Bip01-node.");
    const auto retained = shared_from_this();
    for (size_t i = 0; i < bindings.size(); ++i) {
        auto& slot = bindings[i];
        const auto& track = library->tracks[i];
        slot.node = root->findUid(track.target);
        slot.vector = nullptr;
        slot.quaternion = nullptr;
        if (!slot.node) continue; // Keep original source-order NULL slots.
        switch (track.transform) {
        case CityAnimationTransform::translation:
        case CityAnimationTransform::translationZ: slot.vector = &slot.node->animationPosition(retained); break;
        case CityAnimationTransform::rotation: slot.quaternion = &slot.node->animationRotation(retained); break;
        case CityAnimationTransform::scale: slot.vector = &slot.node->animationScale(retained); break;
        }
    }
}
void RetailActorPose::onUnbind(RetailActorSceneNode& node) noexcept
{
    // Literal forceUnbind uses source target UID, including after cross-root
    // rebinding. It neither unregisters old nodes nor clears the root field.
    for (size_t i = 0; i < bindings.size(); ++i)
        if (cString(library->tracks[i].target) == cString(node.uid())) {
            bindings[i].node = nullptr;
            bindings[i].vector = nullptr;
            bindings[i].quaternion = nullptr;
        }
}
void RetailActorPose::animateNode(RetailActorSceneNode&, uint32_t time)
{
    // Original0x2af0a1 forwards the supplied time to virtual applyAnimationValues.
    // Character binds manually; this class never adds an ordinary list entry.
    apply(time);
}
void RetailActorPose::apply(uint32_t absoluteMs)
{
    const auto live = owner.lock();
    if (!live || root != &live->root()) throw std::logic_error("Actor pose scene lifetime expired.");
    const int32_t time = static_cast<int32_t>(absoluteMs % period);
    for (size_t i = 0; i < bindings.size(); ++i) {
        const auto& track = library->tracks[i];
        auto& slot = bindings[i];
        if (!slot.node) continue;
        auto& scratch = bindings[library->commonCacheMetadata ? 0 : i].keyIndex;
        if (weight != 1.f) {
            // The four supported original typed tracks inherit literally empty
            // WEIGHTED applyKeyBasedValue methods; search/scratch still occurs.
            scratch = selectRetailAnimationLibraryKey(track, time);
            continue;
        }
        const auto interpolation = mode == 1 || !track.interpolationSelector ?
            RetailAnimationInterpolation::step : RetailAnimationInterpolation::linear;
        const auto value = sampleRetailAnimationLibraryTrack(track, time, interpolation, &scratch);
        switch (track.transform) {
        case CityAnimationTransform::translation:
        case CityAnimationTransform::scale: *slot.vector = {value[0], value[1], value[2]}; break;
        case CityAnimationTransform::translationZ: (*slot.vector)[2] = value[0]; break;
        case CityAnimationTransform::rotation: *slot.quaternion = value; break;
        }
    }
}
std::array<float, 3> RetailActorPose::evaluate(int32_t, int32_t absoluteMs)
{
    const auto live = owner.lock();
    if (!live || root != &live->root() || !rootMotion)
        throw std::logic_error("Cannot evaluate an expired/unbound actor pose.");
    apply(static_cast<uint32_t>(absoluteMs));
    return rootMotion->position();
}
