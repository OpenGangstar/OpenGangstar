#include "retail_actor_graph.h"

#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>

RetailActorSceneNode &RetailActorGraphReference::root() const {
    if (actor)
        return actor->root();
    if (subtree)
        return subtree->root();
    throw std::logic_error("Empty retained actor graph reference.");
}
RetailActorSubtree::~RetailActorSubtree() {
    if (!rootNode)
        return;
    RetailActorGraph::detachOwnedRoot(*rootNode);
    for (auto &node : liveNodes)
        node->detachAnimators();
    liveMeshes.clear();
    liveNodes.clear();
}
RetailActorGraph::RetailActorGraph()
    : state(std::make_shared<RetailActorGraphState>()), rootNode(new RetailActorSceneNode) {
    rootNode->runtimeType = 0x72676d73;
    rootNode->manager = this;
    rootNode->managerState = state;
}
RetailActorGraph::~RetailActorGraph() {
    mutating = true;
    rootNode->detachAnimators();
    // Last edge references die while the real global parent/manager still exist.
    while (!rootNode->childNodes.empty())
        removeLink(*rootNode, rootNode->childNodes.front());
    ownership.clear();
}
void RetailActorGraph::checkMutation() const {
    if (framing || mutating)
        throw std::logic_error("Reentrant graph mutation during frame/ownership transfer is unsupported.");
}
RetailActorGraphReference RetailActorGraph::retain(RetailActorSceneNode &node) const {
    if (const auto found = ownership.find(&node); found != ownership.end())
        return found->second;
    if (node.parentNode) {
        auto *old = node.parentNode->sceneManager();
        if (old && node.parentNode == old->rootNode.get()) {
            if (const auto found = old->ownership.find(&node); found != old->ownership.end())
                return found->second;
        }
    }
    throw std::logic_error("Node is not an actual retained actor/subtree root; extract its ownership first.");
}
void RetailActorGraph::inheritManager(RetailActorSceneNode &node, RetailActorGraph *ignored) {
    if (!node.parentNode)
        throw std::logic_error("Original manager inheritance requires a real parent.");
    node.manager = node.parentNode->sceneManager();
    node.managerState = node.parentNode->managerState;
    for (auto *child : node.childNodes)
        inheritManager(*child, ignored);
    // All supported default Player/root/ordinary/skin nodes use literal empty
    // ISceneNode::onChangedSceneManager0x217311. Cameras are not constructed.
}
void RetailActorGraph::assignParent(RetailActorSceneNode &node, RetailActorSceneNode *parent) {
    for (auto *ancestor = parent; ancestor; ancestor = ancestor->parentNode)
        if (ancestor == &node)
            throw std::logic_error("Cyclic original actor hierarchy is unsupported.");
    if (node.parentNode)
        removeLink(*node.parentNode, &node);
    node.parentNode = parent;
    node.flags |= 0x80;
    if (parent && node.manager != parent->sceneManager())
        inheritManager(node, node.manager);
}
bool RetailActorGraph::removeLink(RetailActorSceneNode &parent, RetailActorSceneNode *child) {
    const auto found = std::find(parent.childNodes.begin(), parent.childNodes.end(), child);
    if (found == parent.childNodes.end())
        return false;
    auto *manager = parent.sceneManager();
    child->parentNode = nullptr;
    child->manager = nullptr;
    child->managerState.reset();
    // Original drop precedes linked-record erasure and hierarchy notification.
    // This may destroy the actual detached resources/actor before this erase.
    if (manager && &parent == manager->rootNode.get())
        manager->ownership.erase(child);
    parent.childNodes.erase(found);
    if (manager)
        manager->notifyHierarchyChanged();
    return true;
}
void RetailActorGraph::detachOwnedRoot(RetailActorSceneNode &root) noexcept {
    if (!root.parentNode)
        return;
    // An unlinked pointer-only setParent is a borrowed-parent domain. Avoid
    // dereferencing an expired manager during native failure cleanup.
    if (root.manager && root.managerState.expired()) {
        root.parentNode = nullptr;
        root.manager = nullptr;
        return;
    }
    try {
        removeLink(*root.parentNode, &root);
    } catch (...) {
        std::terminate();
    }
    root.parentNode = nullptr;
    root.manager = nullptr;
    root.managerState.reset();
}
void RetailActorGraph::append(const RetailActorGraphReference &reference) {
    if (!reference)
        return;
    auto retained = reference; // The original grab BEFORE remove protects identity.
    auto &child = retained.root();
    if (&child == rootNode.get())
        return;
    checkMutation();
    if (child.manager && child.managerState.expired())
        throw std::logic_error("Expired borrowed scene manager before reparent.");
    auto *old = child.parentNode ? child.parentNode->sceneManager() : nullptr;
    if (old && old != this)
        old->checkMutation();
    rootNode->childNodes.reserve(rootNode->childNodes.size() + 1);
    // Allocate the prospective owner record before destructive link changes.
    std::map<RetailActorSceneNode *, RetailActorGraphReference> pending;
    pending.emplace(&child, retained);
    mutating = true;
    if (old && old != this)
        old->mutating = true;
    try {
        if (child.parentNode)
            removeLink(*child.parentNode, &child);
        rootNode->childNodes.push_back(&child);
        ownership.insert(pending.extract(&child));
        assignParent(child, rootNode.get());
        notifyHierarchyChanged();
        child.notifyVisibilityChanged(rootNode->effectiveVisible);
    } catch (...) {
        mutating = false;
        if (old && old != this)
            old->mutating = false;
        throw;
    }
    mutating = false;
    if (old && old != this)
        old->mutating = false;
}
void RetailActorGraph::attachActor(std::shared_ptr<RetailActorScene> actor) {
    if (!actor)
        throw std::invalid_argument("Global actor registration requires an actual live owner.");
    RetailActorGraphReference reference;
    reference.actor = std::move(actor);
    append(reference);
}
RetailActorSubtree &RetailActorGraph::registerShadow(RetailActorScene &actor, RetailActorSceneNode &shadow) {
    checkMutation();
    const auto active = retain(actor.root());
    if (active.actor.get() != &actor)
        throw std::logic_error("Shadow requires its genuine active actor owner.");
    return registerShadow(active.actor, shadow);
}
RetailActorSubtree &RetailActorGraph::registerShadow(std::shared_ptr<RetailActorScene> actor,
                                                     RetailActorSceneNode &shadow) {
    checkMutation();
    if (!actor)
        throw std::invalid_argument("Shadow registration requires its genuine retained actor owner.");
    if (auto *previous = actor->root().sceneManager(); previous && previous != this)
        previous->checkMutation();
    if (shadow.childNodes.empty() || !shadow.childNodes.front()->runtimeMesh ||
        shadow.childNodes.front()->runtimeMesh->skin())
        throw std::logic_error("Authored ordinary shadow needs its genuine first mesh child.");
    auto owned = actor->extractOrdinarySubtree(shadow);
    owned->root().childNodes.front()->setRenderingLayer(1);
    RetailActorGraphReference reference;
    reference.subtree = std::move(owned);
    append(reference);
    return *reference.subtree;
}
void RetailActorGraph::addChild(RetailActorSceneNode *child) {
    if (!child || child == rootNode.get())
        return;
    append(retain(*child));
}
void RetailActorGraph::addChild(const RetailActorGraphReference &child) { append(child); }
bool RetailActorGraph::removeChild(RetailActorSceneNode *child) {
    checkMutation();
    mutating = true;
    try {
        const bool removed = removeLink(*rootNode, child);
        mutating = false;
        return removed;
    } catch (...) {
        mutating = false;
        throw;
    }
}
void RetailActorGraph::setParent(const RetailActorGraphReference &retained, RetailActorSceneNode *parent) {
    checkMutation();
    if (!retained)
        throw std::invalid_argument("setParent requires an actual retained owner.");
    if (parent && parent != rootNode.get())
        throw std::logic_error("Generic external borrowed parent is outside the global-root domain.");
    auto keepAlive = retained;
    if (keepAlive.root().parentNode) {
        if (auto *previous = keepAlive.root().parentNode->sceneManager(); previous && previous != this)
            previous->checkMutation();
    }
    assignParent(keepAlive.root(), parent);
}
void RetailActorGraph::inheritSceneManager(RetailActorSceneNode &node, RetailActorGraph *ignored) {
    checkMutation();
    inheritManager(node, ignored);
}
void RetailActorGraph::releaseActor(RetailActorScene &actor, RetailActorSceneNode *shadow,
                                    std::uint8_t poolId) {
    checkMutation();
    const auto found = ownership.find(&actor.root());
    if (found == ownership.end() || found->second.actor.get() != &actor)
        throw std::logic_error("Actor is not attached to this genuine global root.");
    if (shadow) {
        const auto registered = ownership.find(shadow);
        if (registered == ownership.end() || !registered->second.subtree)
            throw std::logic_error("Actor shadow must be its actual independently retained global subtree.");
    }
    if (shadow && poolId == 0xff)
        removeChild(shadow);
    // Do not retain/copy the actor owner here. The last actual parent edge may
    // free the remaining pose graph, independently of a pooled shadow edge.
    removeChild(&actor.root());
}
void RetailActorGraph::collect(RetailActorSceneNode &node) {
    const auto type = node.type();
    if (type != 0x79746d65 && type != 0x796d6d64 && type != 0x6e656164 && type != 0x72676d73)
        collected.push_back(&node);
    if (node.animatorCount())
        animated.push_back(&node);
    for (auto *child : node.childNodes)
        collect(*child);
}
void RetailActorGraph::collectAllNodes() {
    if (!state->hierarchyDirty)
        return;
    collected.clear();
    animated.clear();
    collect(*rootNode);
    state->hierarchyDirty = false;
}
void RetailActorGraph::lateFrame(std::uint32_t time) {
    checkMutation();
    collectAllNodes();
    framing = true;
    try {
        rootNode->onAnimate(time);
    } catch (...) {
        framing = false;
        throw;
    }
    framing = false;
}
