#include "retail_actor_graph.h"
#include "filesystem.h"
#include "retail_actor_pose.h"
#include "retail_actor_shadow.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected unsupported graph domain rejection");
}
std::shared_ptr<RetailActorSceneSource> sourceFixture(bool secondShadowMesh = false) {
    auto source = std::make_shared<RetailActorSceneSource>();
    auto &model = source->model;
    model.path = "graph.bdae";
    model.materialCatalog.push_back({"fixture-material", {}});
    for (const char *uid : {"body", "shadow-mesh"}) {
        CityMesh m;
        m.name = uid;
        m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        m.buffers.push_back({"", {0, 1, 2}});
        m.rawBoundsCm = RetailSkinBoundingBox{{{-1, -2, -3}, {1, 2, 3}}};
        model.meshes.push_back(m);
    }
    auto node = [](const char *uid, const char *scope, int parent) {
        CityNode n{};
        n.name = uid;
        n.jointName = scope;
        n.parent = parent;
        n.rotation = {0, 0, 0, 1};
        n.scale = {1, 1, 1};
        return n;
    };
    model.nodes = {node("body-container", "", -1), node("Bip01-node", "Bone", -1),
                   node("shadow-node", "", 1)};
    model.nodes[0].geometry.push_back({"", "body", {{"", "fixture-material", 0}}});
    model.nodes[0].instances.push_back({2, 0, "", "body-skin", false});
    model.nodes[1].translation = {2, 3, 4};
    model.nodes[2].translation = {.5f, -1.5f, 2.5f};
    model.nodes[2].scale = {.75f, .75f, .75f};
    model.nodes[2].geometry.push_back({"", "shadow-mesh", {{"", "fixture-material", 0}}});
    model.nodes[2].instances.push_back({3, 0, "", "shadow-mesh", false});
    if (secondShadowMesh) {
        model.nodes[2].geometry.push_back({"", "shadow-mesh", {{"", "fixture-material", 0}}});
        model.nodes[2].instances.push_back({3, 1, "", "shadow-mesh", false});
    }
    model.visualScenes.push_back({"visual", "visual", 0, model.nodes.size()});
    CitySkin skin{};
    skin.meshName = "body";
    skin.controllerUid = "body-skin";
    skin.bindShape = retailSceneIdentity;
    skin.joints.push_back({1, retailSceneIdentity, "Bone"});
    skin.jointBoundsCm = std::vector<RetailSkinBoundingBox>{{{{-1, -2, -3}, {1, 2, 3}}}};
    source->skins.push_back(skin);
    return source;
}
struct Probe final : RetailActorSceneAnimator {
    std::vector<std::string> &events;
    std::string name;
    std::function<void(RetailActorSceneNode &)> bind, unbind, frame;
    Probe(std::vector<std::string> &events, std::string name) : events(events), name(std::move(name)) {}
    void onBind(RetailActorSceneNode &node) override {
        events.push_back(name + " bind");
        if (bind)
            bind(node);
    }
    void onUnbind(RetailActorSceneNode &n) noexcept override {
        events.push_back(name + " unbind");
        if (unbind)
            unbind(n);
    }
    void animateNode(RetailActorSceneNode &n, uint32_t) override {
        events.push_back(name + " frame");
        if (frame)
            frame(n);
    }
};
void unit() {
    auto source = sourceFixture(true);
    auto actor = std::make_shared<RetailActorScene>(source);
    RetailActorGraph graph;
    auto *shadow = actor->root().findUid("shadow-node");
    auto *first = shadow->children()[0];
    auto *second = shadow->children()[1];
    auto *mesh = &first->mesh()->geometry();
    const auto cached = shadow->absolute();
    const auto local = shadow->position();
    const auto id = shadow->index();
    graph.attachActor(actor);
    auto &moved = graph.registerShadow(*actor, *shadow);
    require(graph.root().type() == 0x72676d73 && shadow->parent() == &graph.root() &&
                shadow->sceneManager() == &graph,
            "Actual global root and inherited manager");
    require(actor->root().findUid("shadow-node") == nullptr && graph.root().findUid("shadow-node") == shadow,
            "UID scope follows real reparenting");
    require(moved.nodes().size() == 3 && moved.meshes().size() == 2 && shadow->index() == id &&
                &first->mesh()->geometry() == mesh,
            "Actual node/mesh ownership transfer preserves identities");
    require(shadow->position() == local && shadow->absolute() == cached,
            "Reparent retains local TRS and cached absolute");
    require(first->renderingLayer() == 1 && second->renderingLayer() == 0 && shadow->renderingLayer() == 0,
            "Only actual first generated child receives layer1");
    graph.collectAllNodes();
    require(!graph.hierarchyChanged(), "Actual hierarchy collection clears dirty");
    const auto kept = graph.retain(*shadow);
    graph.addChild(kept);
    require(graph.root().children() == std::vector<RetailActorSceneNode *>{&actor->root(), shadow},
            "Same-node reappend without duplicate");
    require(graph.removeChild(shadow) && !graph.removeChild(shadow), "Matched then absent removal");
    require(!shadow->parent() && !shadow->sceneManager() && first->sceneManager() == &graph,
            "Removal clears only detached root manager");
    graph.addChild(kept);
    actor->root().setVisible(false);
    require(shadow->visible(), "Detached shadow visibility independent of actor");
    shadow->setVisible(false);
    actor->root().setVisible(true);
    require(!shadow->visible() && !first->visible(),
            "Actor show does not override independently hidden shadow");
    shadow->setVisible(true);
    RetailHeightState height{};
    height.changed = false;
    updateRetailActorShadow(*shadow, {100, 200, 300}, 10, height);
    require(shadow->absolute() == cached, "Projection keeps old cached transform until real late frame");
    graph.lateFrame(42);
    require(shadow->absolute()[12] == 100 && shadow->absolute()[13] == 200 && shadow->absolute()[14] == 13,
            "Actual late root dispatch updates globally parented shadow");
    std::vector<std::string> events;
    auto ordinary = std::make_shared<Probe>(events, "ordinary");
    bool boundBeforeNotification = false, unboundBeforeErase = false;
    ordinary->bind = [&](auto &n) {
        boundBeforeNotification = !graph.hierarchyChanged() && n.animatorCount() == 1;
    };
    ordinary->unbind = [&](auto &n) {
        unboundBeforeErase = !graph.hierarchyChanged() && n.animatorCount() == 1;
    };
    graph.collectAllNodes();
    shadow->addAnimator(ordinary);
    require(boundBeforeNotification, "Actual onBind observes list append before hierarchy notification");
    require(graph.hierarchyChanged(), "Ordinary add invalidates hierarchy after onBind");
    graph.collectAllNodes();
    require(std::find(graph.ordinaryAnimatedNodes().begin(), graph.ordinaryAnimatedNodes().end(), shadow) !=
                graph.ordinaryAnimatedNodes().end(),
            "Collection uses ordinary list");
    auto bound = std::make_shared<Probe>(events, "bound");
    first->addBoundAnimator(bound);
    require(!graph.hierarchyChanged(), "Bound retention does not invalidate ordinary hierarchy");
    first->removeBoundAnimator(*bound);
    require(!graph.hierarchyChanged(), "Bound removal does not invalidate ordinary hierarchy");
    shadow->removeAnimator(*ordinary);
    require(unboundBeforeErase, "Actual onUnbind observes live ordinary entry before erase/notification");
    require(graph.hierarchyChanged(), "Matched ordinary removal invalidates hierarchy");
    graph.collectAllNodes();
    shadow->removeAnimator(*ordinary);
    require(!graph.hierarchyChanged(), "Absent ordinary removal does not notify");
}
void lifetime(uint8_t pool, bool present) {
    auto source = sourceFixture();
    std::weak_ptr<const RetailActorSceneSource> sourceWeak = source;
    auto actor = std::make_shared<RetailActorScene>(source);
    auto *raw = actor.get();
    std::weak_ptr<RetailActorScene> actorWeak = actor;
    RetailActorGraph graph;
    graph.attachActor(actor);
    auto *shadow = actor->root().findUid("shadow-node");
    graph.registerShadow(*actor, *shadow);
    std::vector<std::string> events;
    auto actorProbe = std::make_shared<Probe>(events, "actor");
    auto shadowProbe = std::make_shared<Probe>(events, "shadow");
    actor->root().addBoundAnimator(actorProbe);
    shadow->addBoundAnimator(shadowProbe);
    actorProbe.reset();
    shadowProbe.reset();
    actor.reset();
    source.reset();
    require(!actorWeak.expired(), "Actual global parent retains active actor owner");
    graph.releaseActor(*raw, present ? shadow : nullptr, pool);
    require(actorWeak.expired(), "Moved pooled shadow never retains a dead actor pose graph");
    if (pool != 255 || !present) {
        require(events == std::vector<std::string>{"actor unbind"} && !sourceWeak.expired() &&
                    shadow->parent() == &graph.root(),
                "Pool keeps only actual global shadow/source");
        graph.removeChild(shadow);
        require(events.back() == "shadow unbind" && sourceWeak.expired(),
                "Final actual shadow drop frees resources/source");
    } else
        require(events == std::vector<std::string>{"shadow unbind", "actor unbind"} && sourceWeak.expired(),
                "Unpooled shadow destruction precedes actor destruction");
}
void managerControls() {
    auto actor = std::make_shared<RetailActorScene>(sourceFixture());
    RetailActorGraph a, b;
    a.attachActor(actor);
    auto *shadow = actor->root().findUid("shadow-node");
    auto &moved = a.registerShadow(*actor, *shadow);
    auto *child = shadow->children()[0];
    auto retained = a.retain(*shadow);
    const auto matrix = shadow->absolute();
    const auto local = shadow->position();
    a.collectAllNodes();
    b.collectAllNodes();
    b.root().setVisible(false);
    b.addChild(shadow);
    require(a.hierarchyChanged() && b.hierarchyChanged() && shadow->sceneManager() == &b &&
                child->sceneManager() == &b,
            "Cross-manager transfer notifies both real managers and propagates from parent");
    require(shadow->parent() == &b.root() && !shadow->visible() && shadow->locallyVisible() &&
                !shadow->parentVisible() && shadow->absolute() == matrix && shadow->position() == local,
            "Transfer preserves stale cache/TRS and uses new inherited visibility");
    b.collectAllNodes();
    b.inheritSceneManager(*shadow, &a);
    require(shadow->sceneManager() == &b && child->sceneManager() == &b && !b.hierarchyChanged(),
            "setSceneManager ignores supplied manager for parent-derived identity");
    require(b.removeChild(shadow) && !shadow->parent() && !shadow->sceneManager() &&
                child->sceneManager() == &b,
            "Only removed subtree root loses manager");
    a.collectAllNodes();
    a.setParent(retained, &a.root());
    require(shadow->parent() == &a.root() && shadow->sceneManager() == &a && child->sceneManager() == &a &&
                !a.hierarchyChanged() &&
                std::find(a.root().children().begin(), a.root().children().end(), shadow) ==
                    a.root().children().end() &&
                !shadow->parentVisible(),
            "Pointer-only parent assignment neither appends, notifies new parent nor updates inherited "
            "visibility");
    a.addChild(retained);
    require(shadow->parent() == &a.root() && std::find(a.root().children().begin(), a.root().children().end(),
                                                       shadow) == a.root().children().end(),
            "Original nested setParent removes just-appended record after pointer-only same-parent state");
    a.setParent(retained, nullptr);
    a.addChild(retained);
    require(shadow->parent() == &a.root() && shadow->visible() && &moved.root() == shadow,
            "Explicit null then append restores actual link");
}
void clearAndFrame() {
    std::vector<std::string> events;
    bool firstSawOldList = false, secondSawFirstDestroyed = false;
    auto actor = std::make_shared<RetailActorScene>(sourceFixture());
    RetailActorGraph graph;
    graph.attachActor(actor);
    auto *shadow = actor->root().findUid("shadow-node");
    graph.registerShadow(*actor, *shadow);
    graph.collectAllNodes();
    shadow->removeAnimators();
    require(graph.hierarchyChanged(), "Empty remove-all still notifies the actual manager");
    auto first = std::make_shared<Probe>(events, "first");
    auto second = std::make_shared<Probe>(events, "second");
    std::weak_ptr<Probe> firstWeak = first, secondWeak = second;
    first->unbind = [&](auto &n) {
        firstSawOldList = n.animatorCount() == 2 && !graph.hierarchyChanged();
    };
    second->unbind = [&](auto &n) {
        secondSawFirstDestroyed = n.animatorCount() == 2 && !graph.hierarchyChanged() && firstWeak.expired();
    };
    shadow->addAnimator(first);
    shadow->addAnimator(second);
    first.reset();
    second.reset();
    graph.collectAllNodes();
    events.clear();
    shadow->removeAnimators();
    require(firstSawOldList && secondSawFirstDestroyed && firstWeak.expired() && secondWeak.expired() &&
                shadow->animatorCount() == 0 && graph.hierarchyChanged() &&
                events == std::vector<std::string>{"first unbind", "second unbind"},
            "Remove-all unbinds/drops in order before clearing records and notifying");
    auto root = std::make_shared<Probe>(events, "root");
    auto actorFrame = std::make_shared<Probe>(events, "actor");
    auto shadowFrame = std::make_shared<Probe>(events, "shadow");
    auto bound = std::make_shared<Probe>(events, "bound");
    graph.root().addAnimator(root);
    actor->root().addAnimator(actorFrame);
    shadow->addAnimator(shadowFrame);
    shadow->children()[0]->addBoundAnimator(bound);
    graph.collectAllNodes();
    require(graph.ordinaryAnimatedNodes() ==
                std::vector<RetailActorSceneNode *>{&graph.root(), &actor->root(), shadow},
            "Ordinary collection includes manager animators, with no bound-only entry");
    events.clear();
    graph.lateFrame(100);
    require(events == std::vector<std::string>{"root frame", "actor frame", "shadow frame"},
            "Actual late root dispatch follows live child order once, excluding bound retention");
    graph.attachActor(actor);
    events.clear();
    graph.lateFrame(101);
    require(events == std::vector<std::string>{"root frame", "shadow frame", "actor frame"},
            "Same-node append changes actual late dispatch order");
    actor->root().setVisible(false);
    shadow->setVisible(false);
    const auto oldAbsolute = actor->root().absolute();
    actor->root().setPosition({8, 9, 10});
    const auto retainedShadow = graph.retain(*shadow);
    graph.releaseActor(*actor, shadow, 17);
    require(actor->root().parent() == nullptr && actor->root().absolute() == oldAbsolute &&
                shadow->parent() == &graph.root(),
            "Explicit caller pool owner retains real removed actor and stale cache independently");
    events.clear();
    graph.lateFrame(102);
    require(events == std::vector<std::string>{"root frame"}, "Hidden pooled shadow does not animate");
    graph.attachActor(actor);
    actor->root().setVisible(true);
    shadow->setVisible(true);
    graph.addChild(retainedShadow);
    events.clear();
    graph.lateFrame(103);
    require(events == std::vector<std::string>{"root frame", "actor frame", "shadow frame"} &&
                actor->root().absolute()[12] == 8 && graph.root().findUid("shadow-node") == shadow,
            "Explicit retained pool reuse preserves actual nodes and updates on the real next frame");
    // Clear callbacks while their observations and event storage are alive.
    graph.root().removeAnimators();
    actor->root().removeAnimators();
    shadow->removeAnimators();
    shadow->children()[0]->removeBoundAnimator(*bound);
}
void unsupported() {
    auto source = sourceFixture();
    auto actor = std::make_shared<RetailActorScene>(source);
    RetailActorGraph graph;
    graph.attachActor(actor);
    auto *shadow = actor->root().findUid("shadow-node");
    std::vector<std::string> events;
    auto animator = std::make_shared<Probe>(events, "bound");
    shadow->addBoundAnimator(animator);
    const auto count = actor->nodes().size();
    rejects([&] { graph.registerShadow(*actor, *shadow); });
    require(actor->nodes().size() == count && actor->root().findUid("shadow-node") == shadow &&
                shadow->children()[0]->renderingLayer() == 0,
            "Rejected bound extraction leaves actual graph/ownership/layer unchanged");
    shadow->removeBoundAnimator(*animator);
    rejects([&] { graph.registerShadow(*actor, *actor->root().findMesh()->parent()); });
    auto borrowed = std::make_shared<RetailActorScene>(source);
    rejects([&] { graph.registerShadow(*actor, *borrowed->root().findUid("shadow-node")); });
    source = sourceFixture();
    source->model.nodes[2].jointName = "BorrowedShadow";
    source->skins[0].joints[0].node = 2;
    source->skins[0].joints[0].scope = "BorrowedShadow";
    auto dependent = std::make_shared<RetailActorScene>(source);
    graph.attachActor(dependent);
    rejects([&] { graph.registerShadow(*dependent, *dependent->root().findUid("shadow-node")); });
    auto safe = std::make_shared<RetailActorScene>(sourceFixture());
    {
        RetailActorGraph temporary;
        temporary.attachActor(safe);
    }
    require(!safe->root().sceneManager(), "Destroyed manager detached actual retained root");
    rejects([&] { safe->root().findUid("Bip01-node")->sceneManager(); });
    graph.attachActor(safe);
    require(safe->root().findUid("Bip01-node")->sceneManager() == &graph,
            "Reattach replaces stale borrowed descendant manager safely");
}
uint32_t bits(float value) {
    uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
void snapshot(const char *model, unsigned phase, const std::vector<RetailActorSceneNode *> &nodes,
              RetailActorGraph &a, RetailActorGraph &b, bool emit) {
    if (!emit)
        return;
    std::cout << "graph {\"model\":" << std::quoted(model) << ",\"phase\":" << phase << ",\"dirty\":["
              << a.hierarchyChanged() << ',' << b.hierarchyChanged() << "],\"childrenA\":[";
    for (size_t i = 0; i < a.root().children().size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << a.root().children()[i]->index();
    }
    std::cout << "],\"childrenB\":[";
    for (size_t i = 0; i < b.root().children().size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << b.root().children()[i]->index();
    }
    const auto cached = [&](const char *key, const auto &entries) {
        std::cout << "],\"" << key << "\":[";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << entries[i]->index();
        }
    };
    cached("collectedA", a.collectedNodes());
    cached("animatedA", a.ordinaryAnimatedNodes());
    cached("collectedB", b.collectedNodes());
    cached("animatedB", b.ordinaryAnimatedNodes());
    std::cout << "],\"nodes\":[";
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (index)
            std::cout << ',';
        auto &n = *nodes[index];
        auto *manager = n.sceneManager();
        const int64_t parent = n.parent() == &a.root()   ? -2
                               : n.parent() == &b.root() ? -3
                               : n.parent()              ? static_cast<int64_t>(n.parent()->index())
                                                         : -1;
        std::cout << "{\"index\":" << n.index() << ",\"parent\":" << parent << ",\"manager\":"
                  << (manager == &a   ? 1
                      : manager == &b ? 2
                                      : 0)
                  << ",\"layer\":" << n.renderingLayer() << ",\"flags\":" << n.transformFlags()
                  << ",\"visibility\":[" << n.visible() << ',' << n.locallyVisible() << ','
                  << n.parentVisible() << "],\"local\":[";
        bool comma = false;
        for (const auto *part : {&n.position(), &n.scale()}) {
            if (part == &n.scale()) {
                for (float q : n.rotation()) {
                    if (comma)
                        std::cout << ',';
                    std::cout << bits(q);
                    comma = true;
                }
            }
            for (float component : *part) {
                if (comma)
                    std::cout << ',';
                std::cout << bits(component);
                comma = true;
            }
        }
        std::cout << "],\"absolute\":[";
        for (size_t i = 0; i < 16; ++i) {
            if (i)
                std::cout << ',';
            std::cout << bits(n.absolute()[i]);
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
void retail(const std::filesystem::path &directory, bool emit) {
    ResourceFileSystem assets(directory);
    auto library =
        std::make_shared<CityAnimationLibrary>(loadCityAnimationLibrary(assets, "anim_ingame.bdae"));
    for (const char *name : {"char_pthug.bdae", "char_pthug_jail.bdae"}) {
        auto source = loadRetailActorSceneSource(assets, name);
        auto actor = std::make_shared<RetailActorScene>(source);
        RetailActorGraph a, b;
        std::vector<RetailActorSceneNode *> nodes;
        for (const auto &n : actor->nodes())
            nodes.push_back(n.get());
        require(nodes.size() == 36, "Both actual Player graphs");
        a.attachActor(actor);
        snapshot(name, 0, nodes, a, b, emit);
        auto *shadow = actor->root().findUid("shadow-node");
        auto *mesh = shadow->children()[0]->mesh();
        auto *vertices = mesh->geometry().positions.data();
        auto &owned = a.registerShadow(*actor, *shadow);
        require(actor->nodes().size() == 34 && owned.nodes().size() == 2 && owned.meshes().size() == 1 &&
                    shadow->children()[0]->mesh() == mesh && mesh->geometry().positions.data() == vertices,
                "Actual supplied shadow record/resource transfer");
        snapshot(name, 1, nodes, a, b, emit);
        a.root().setVisible(false);
        snapshot(name, 2, nodes, a, b, emit);
        a.root().setVisible(true);
        actor->root().setVisible(false);
        snapshot(name, 3, nodes, a, b, emit);
        shadow->setVisible(false);
        actor->root().setVisible(true);
        snapshot(name, 4, nodes, a, b, emit);
        shadow->setVisible(true);
        a.attachActor(actor);
        snapshot(name, 5, nodes, a, b, emit);
        RetailHeightState height{};
        height.changed = false;
        updateRetailActorShadow(*shadow, {100, 200, 300}, 10, height);
        snapshot(name, 6, nodes, a, b, emit);
        a.lateFrame(1234);
        snapshot(name, 7, nodes, a, b, emit);
        a.collectAllNodes();
        b.collectAllNodes();
        b.root().setVisible(false);
        b.addChild(shadow);
        snapshot(name, 8, nodes, a, b, emit);
        b.collectAllNodes();
        b.inheritSceneManager(*shadow, &a);
        snapshot(name, 9, nodes, a, b, emit);
        auto retained = b.retain(*shadow);
        b.removeChild(shadow);
        snapshot(name, 10, nodes, a, b, emit);
        a.addChild(retained);
        snapshot(name, 11, nodes, a, b, emit);
        a.setParent(retained, &a.root());
        snapshot(name, 12, nodes, a, b, emit);
        a.addChild(retained);
        snapshot(name, 13, nodes, a, b, emit);
        a.setParent(retained, nullptr);
        snapshot(name, 14, nodes, a, b, emit);
        a.addChild(retained);
        snapshot(name, 15, nodes, a, b, emit);
        a.removeChild(shadow);
        a.collectAllNodes();
        require(!a.removeChild(shadow) && !a.hierarchyChanged(),
                "Actual absent removal has no new notification");
        snapshot(name, 16, nodes, a, b, emit);
        a.addChild(retained);
        a.lateFrame(1235);
        snapshot(name, 17, nodes, a, b, emit);
        // Genuine retained pose remains manually sampled; late graph frame must not
        // apply it again or change its track cursor/local target values.
        auto pose = RetailActorPose::create(library);
        pose->bind(actor);
        pose->apply(111);
        std::vector<int32_t> cursors;
        for (size_t i = 0; i < pose->bindingCount(); ++i)
            cursors.push_back(pose->keyIndex(i));
        const auto rootPosition = actor->root().findUid("Bip01-node")->position();
        a.lateFrame(2000);
        for (size_t i = 0; i < cursors.size(); ++i)
            require(pose->keyIndex(i) == cursors[i], "Late root frame never samples bound pose again");
        require(actor->root().findUid("Bip01-node")->position() == rootPosition,
                "Late root frame preserves manually sampled local pose");
        const auto *originalBoundRoot = pose->boundRoot();
        retained = {};
        std::weak_ptr<RetailActorScene> weak = actor;
        auto *raw = actor.get();
        actor.reset();
        source.reset();
        a.releaseActor(*raw, shadow, 17);
        require(weak.expired() && pose->boundRoot() == originalBoundRoot && shadow->parent() == &a.root(),
                "Actual pooled shadow survives while weak pose scene expires; original opaque root field "
                "remains");
        for (size_t i = 0; i < pose->bindingCount(); ++i)
            require(!pose->target(i), "Actual node destruction invalidates every bound pose target");
        rejects([&] { pose->apply(112); });
        a.removeChild(shadow);
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        unit();
        for (uint8_t pool : {0, 17, 254, 255})
            for (bool present : {false, true})
                lifetime(pool, present);
        managerControls();
        clearAndFrame();
        unsupported();
        if (argc > 1)
            retail(argv[1], argc > 2 && std::string(argv[2]) == "--dump");
        std::cout << "Actual actor graph ownership checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
