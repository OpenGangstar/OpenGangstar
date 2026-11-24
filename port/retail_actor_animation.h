#pragma once

#include "retail_animation.h"

#include <string_view>

// Owns decoded clips in ORIGINAL indexed timeline order. Exact name lookup is
// first-match strcmp semantics; duplicate names never replace indexed entries.
// The authoritative city_assets decoder supplies the original signed intervals.
class RetailActorAnimationCatalog {
  public:
    explicit RetailActorAnimationCatalog(std::vector<CityAnimationClip>);
    const CityAnimationClip& at(std::int32_t animation) const;
    std::int32_t find(std::string_view exactName) const;
    const std::vector<CityAnimationClip>& clips() const { return entries; }
  private:
    std::vector<CityAnimationClip> entries;
};
RetailActorAnimationCatalog loadRetailActorAnimationCatalog(FileSystemBase&);

// Required original scene operations, not a source CityNode masquerading as a
// live actor. Nonnull identities are genuine caller-owned runtime instances.
// The host/model, catalog, live clock/flags, nodes and mesh outlive the binding.
// These operations must implement the named original effects, not empty hooks.
// Original allocation/refcount/scene-manager ownership remains the host's.
class RetailActorAnimationSceneHost {
  public:
    virtual ~RetailActorAnimationSceneHost() = default;
    virtual const CityScene& sourceModel() const = 0;
    virtual const void* actorInstance() const = 0;
    virtual void constructAnimator(std::string_view resource) = 0;
    virtual bool hasSharedTimeline() const = 0;
    virtual void retainAnimatorTimeline(const std::array<std::int32_t, 7>& fistsCombatSequence) = 0;
    virtual void clearAnimatorTimeline() = 0;
    virtual void bindAnimatorToActor() = 0;
    virtual void releaseAnimatorReference() = 0;
    virtual const void* findNodeByUID(std::string_view exactUID) = 0;
    virtual void setFirstChildRenderingLayer(const void* node, std::int32_t layer) = 0;
    virtual void setColor(const void* node, std::uint32_t color, std::uint32_t type) = 0;
    virtual void setMaterialFlag(const void* node, std::uint32_t flag, bool value) = 0;
    virtual void registerShadow(const void* node) = 0;
    virtual const void* findMesh() = 0;
};

class RetailActorAnimationWeaponHost {
  public:
    virtual ~RetailActorAnimationWeaponHost() = default;
    virtual std::int32_t weaponType() = 0;
    // Called against the LIVE actor before accepted clock/flag commit. Mutated
    // cache, retained root height, speed multiplier and unrelated flags survive.
    virtual void reloadRocket() = 0;
};

// The original Player passes Bip01-node; NPC callers may pass their proved UID.
// Binding does not clear the constructor's cached root, reset the clock, or
// select an initial animation. It preserves the host's setup side effects.
class RetailActorAnimationBinding {
  public:
    RetailActorAnimationBinding(const RetailActorAnimationCatalog&,
        RetailActorAnimationSceneHost&, RetailAnimationClock&, std::uint32_t& movementFlags,
        std::string_view rootUID = "Bip01-node");
    RetailActorAnimationBinding(const RetailActorAnimationBinding&) = delete;
    RetailActorAnimationBinding& operator=(const RetailActorAnimationBinding&) = delete;
    RetailAnimationSelection select(const RetailAnimationRequest&, bool inCinematic,
                                    RetailActorAnimationWeaponHost&);
    const CityAnimationClip& clip(std::int32_t animation) const;
    const CityAnimationChannel& rootChannel(std::int32_t animation) const;
    RetailAnimationStep advance(float elapsedMs, const std::array<float, 16>& absoluteActorMatrix,
        bool bodyPresent, std::optional<std::array<float, 2>> direction = {},
        RetailAnimationInterpolation = RetailAnimationInterpolation::linear);
    const void* actorInstance() const { return actor; }
    const void* rootInstance() const { return root; }
    const void* shadowInstance() const { return shadow; }
    const void* meshInstance() const { return mesh; }
    std::size_t rootSourceNode() const { return rootNode; }
    const CityScene& sourceModel() const { return model; }

  private:
    const RetailActorAnimationCatalog& catalog;
    const CityScene& model;
    RetailAnimationClock& clock;
    std::uint32_t& flags;
    std::vector<CityAnimationClip> boundClips;
    const void* actor = nullptr;
    const void* root = nullptr;
    const void* shadow = nullptr;
    const void* mesh = nullptr;
    std::size_t rootNode = 0;
};
