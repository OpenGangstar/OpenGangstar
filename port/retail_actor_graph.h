#pragma once

#include "retail_actor_scene.h"

#include <map>

// Native lifetime guard for the original borrowed scene-manager pointer.
// This is the manager's actual hierarchy-dirty state, not a second graph.
struct RetailActorGraphState {
    bool hierarchyDirty = true;
};

// Actual moved records/resources, with no strong reference to their old actor.
class RetailActorSubtree {
  public:
    ~RetailActorSubtree();
    RetailActorSubtree(const RetailActorSubtree &) = delete;
    RetailActorSubtree &operator=(const RetailActorSubtree &) = delete;
    RetailActorSceneNode &root() const { return *rootNode; }
    const RetailActorSceneSource &source() const { return *decoded; }
    const std::vector<std::unique_ptr<RetailActorSceneNode>> &nodes() const { return liveNodes; }
    const std::vector<std::unique_ptr<RetailActorSceneMesh>> &meshes() const { return liveMeshes; }

  private:
    friend class RetailActorScene;
    friend class RetailActorGraph;
    RetailActorSubtree() = default;
    std::shared_ptr<const RetailActorSceneSource> decoded;
    std::vector<std::unique_ptr<RetailActorSceneNode>> liveNodes;
    std::vector<std::unique_ptr<RetailActorSceneMesh>> liveMeshes;
    RetailActorSceneNode *rootNode = nullptr;
};

// A genuine retained root reference, corresponding to an extra original grab.
// Shadow references own only the moved subtree and immutable source.
class RetailActorGraphReference {
  public:
    RetailActorGraphReference() = default;
    explicit operator bool() const { return bool(actor) || bool(subtree); }
    RetailActorSceneNode &root() const;
    RetailActorScene *actorScene() const { return actor.get(); }
    RetailActorSubtree *detachedSubtree() const { return subtree.get(); }

  private:
    friend class RetailActorGraph;
    std::shared_ptr<RetailActorScene> actor;
    std::shared_ptr<RetailActorSubtree> subtree;
};

// Bounded real global manager/root: actual ordered children and retained owners.
// Camera, culling, renderer and float-clock conversion remain separate hosts.
class RetailActorGraph {
  public:
    RetailActorGraph();
    ~RetailActorGraph();
    RetailActorGraph(const RetailActorGraph &) = delete;
    RetailActorGraph &operator=(const RetailActorGraph &) = delete;
    RetailActorSceneNode &root() const { return *rootNode; }
    void attachActor(std::shared_ptr<RetailActorScene>);
    RetailActorSubtree &registerShadow(RetailActorScene &, RetailActorSceneNode &actualShadow);
    // Character binds/registers shadow before Player attaches its actor root.
    // Caller supplies the genuine retained actor; this never appends that root.
    RetailActorSubtree &registerShadow(std::shared_ptr<RetailActorScene>, RetailActorSceneNode &actualShadow);
    // Character has already force-bound its actual plain pose before reparent.
    // Preserve that exact target/list ownership; no generic animator transfer.
    RetailActorSubtree &registerShadow(std::shared_ptr<RetailActorScene>, RetailActorSceneNode &actualShadow,
                                      RetailActorPose &);
    RetailActorGraphReference retain(RetailActorSceneNode &) const;
    void addChild(RetailActorSceneNode *);
    void addChild(const RetailActorGraphReference &);
    bool removeChild(RetailActorSceneNode *);
    // Literal pointer-only setParent: removes old link, never appends to new.
    // The explicit retained reference and new parent must stay alive.
    void setParent(const RetailActorGraphReference &, RetailActorSceneNode *);
    void inheritSceneManager(RetailActorSceneNode &, RetailActorGraph *ignoredArgument);
    void releaseActor(RetailActorScene &, RetailActorSceneNode *shadow, std::uint8_t originalNodePoolId);
    bool hierarchyChanged() const { return state->hierarchyDirty; }
    void notifyHierarchyChanged() { state->hierarchyDirty = true; }
    void collectAllNodes();
    const std::vector<RetailActorSceneNode *> &collectedNodes() const { return collected; }
    const std::vector<RetailActorSceneNode *> &ordinaryAnimatedNodes() const { return animated; }
    // Genuine late false-pass operation after the original caller's time conversion.
    void lateFrame(std::uint32_t timeMs);

  private:
    friend class RetailActorSceneNode;
    friend class RetailActorSubtree;
    friend class RetailActorScene;
    static bool removeLink(RetailActorSceneNode &, RetailActorSceneNode *);
    static void assignParent(RetailActorSceneNode &, RetailActorSceneNode *);
    static void inheritManager(RetailActorSceneNode &, RetailActorGraph *ignored);
    static void detachOwnedRoot(RetailActorSceneNode &) noexcept;
    void append(const RetailActorGraphReference &);
    RetailActorSubtree &registerShadowImpl(std::shared_ptr<RetailActorScene>, RetailActorSceneNode &,
                                         RetailActorPose *);
    void checkMutation() const;
    void collect(RetailActorSceneNode &);
    std::shared_ptr<RetailActorGraphState> state;
    std::unique_ptr<RetailActorSceneNode> rootNode;
    // Real parent-edge ownership, keyed only for retaining/dropping the owner.
    // Hierarchy, order, lookup and frame dispatch use rootNode->childNodes.
    std::map<RetailActorSceneNode *, RetailActorGraphReference> ownership;
    std::vector<RetailActorSceneNode *> collected, animated;
    bool framing = false, mutating = false;
};
