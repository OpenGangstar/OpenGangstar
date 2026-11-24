#pragma once

#include "retail_actor_animation.h"
#include "retail_actor_graph.h"
#include "retail_actor_pose.h"
#include "retail_timeline.h"

// Character's actual application-scoped animation resources/state. The first
// mutable timeline is separate from the immutable clips and detached clock.
class RetailActorAnimationGlobals {
public:
    explicit RetailActorAnimationGlobals(FileSystemBase&);
    RetailActorAnimationGlobals(std::shared_ptr<const RetailActorAnimationCatalog>,
                                std::shared_ptr<const CityAnimationLibrary>);
    RetailActorAnimationGlobals(const RetailActorAnimationGlobals&) = delete;
    RetailActorAnimationGlobals& operator=(const RetailActorAnimationGlobals&) = delete;
    const RetailActorAnimationCatalog& catalog() const { return *indexedClips; }
    const std::shared_ptr<const CityAnimationLibrary>& library() const { return fullLibrary; }
    const std::shared_ptr<RetailTimeline>& sharedTimeline() const { return timeline; }
    std::array<std::int32_t, 7>& fistsCombatSequence() { return fists; }
    const std::array<std::int32_t, 7>& fistsCombatSequence() const { return fists; }
    void clearSharedTimeline(); // Original cleanTimelineController: fists survive.

private:
    friend class RetailActorAnimationHost;
    std::shared_ptr<const RetailActorAnimationCatalog> indexedClips;
    std::shared_ptr<const CityAnimationLibrary> fullLibrary;
    std::shared_ptr<RetailTimeline> timeline;
    std::array<std::int32_t, 7> fists{};
};

// Concrete Character scene host, using the genuine retained actor and global
// graph. The owner constructs this before the existing Binding, which borrows
// its canonical clock/flags. No clock, clipped-track copy or binding is added.
// Globals and graph outlive this host; the host owns its explicit actor reference.
class RetailActorAnimationHost final : public RetailActorAnimationSceneHost {
public:
    RetailActorAnimationHost(std::shared_ptr<RetailActorScene>, RetailActorGraph&,
                             RetailActorAnimationGlobals&);
    RetailActorAnimationHost(const RetailActorAnimationHost&) = delete;
    RetailActorAnimationHost& operator=(const RetailActorAnimationHost&) = delete;
    const CityScene& sourceModel() const override;
    const void* actorInstance() const override;
    void constructAnimator(std::string_view resource) override;
    bool hasSharedTimeline() const override;
    void retainAnimatorTimeline(const std::array<std::int32_t, 7>& fists) override;
    void clearAnimatorTimeline() override;
    void bindAnimatorToActor() override;
    void releaseAnimatorReference() override;
    const void* findNodeByUID(std::string_view) override;
    void setFirstChildRenderingLayer(const void*, std::int32_t) override;
    void setColor(const void*, std::uint32_t color, std::uint32_t selector) override;
    void setMaterialFlag(const void*, std::uint32_t flag, bool) override;
    void registerShadow(const void*) override;
    const void* findMesh() override;
    // Explicit temporary grab of the actual current factory/bound animator.
    // After the local drop, nodes supply retention; existing Binding.advance
    // receives this same object as its pose evaluator.
    std::shared_ptr<RetailActorPose> pose() const;

private:
    RetailActorSceneNode& node(const void*) const;
    RetailActorPose& factoryAnimator() const;
    std::shared_ptr<RetailActorScene> actor;
    RetailActorGraph& graph;
    RetailActorAnimationGlobals& globals;
    std::shared_ptr<RetailActorPose> localAnimator;
    std::weak_ptr<RetailActorPose> boundAnimator;
};
