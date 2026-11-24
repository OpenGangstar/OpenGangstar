#include "retail_actor_animation_host.h"
#include "filesystem.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F action)
{
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    require(failed, "Expected explicit unsupported Character host domain");
}
std::uint32_t bits(float value)
{
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
constexpr std::array<std::int32_t, 7> fists{144,94,92,93,95,90,91};

std::shared_ptr<RetailActorSceneSource> modelFixture()
{
    auto source = std::make_shared<RetailActorSceneSource>();
    auto& model = source->model;
    model.path = "host-fixture.bdae";
    model.materialCatalog.push_back({"material", {}});
    for (const char* uid : {"body", "shadow-mesh"}) {
        CityMesh mesh;
        mesh.name = uid;
        mesh.positions = {{0,0,0},{1,0,0},{0,1,0}};
        mesh.buffers.push_back({"",{0,1,2}});
        mesh.rawBoundsCm = RetailSkinBoundingBox{{{-1,-2,-3},{1,2,3}}};
        model.meshes.push_back(mesh);
    }
    const auto node = [](const char* uid, const char* scope, int parent) {
        CityNode n{};
        n.name = uid; n.jointName = scope; n.parent = parent;
        n.rotation = {0,0,0,1}; n.scale = {1,1,1};
        return n;
    };
    model.nodes = {node("body-container","",-1),node("Bip01-node","Bone",-1),node("shadow-node","",1)};
    model.nodes[0].geometry.push_back({"","body",{{"","material",0}}});
    model.nodes[0].instances.push_back({2,0,"","body-skin",false});
    model.nodes[1].translation = {7,8,9};
    model.nodes[2].translation = {11,12,13};
    model.nodes[2].geometry.push_back({"","shadow-mesh",{{"","material",0}}});
    model.nodes[2].instances.push_back({3,0,"","shadow-mesh",false});
    model.visualScenes.push_back({"visual","visual",0,model.nodes.size()});
    CitySkin skin{};
    skin.meshName = "body"; skin.controllerUid = "body-skin";
    skin.bindShape = retailSceneIdentity;
    skin.joints.push_back({1,retailSceneIdentity,"Bone"});
    skin.jointBoundsCm = std::vector<RetailSkinBoundingBox>{{{{-1,-2,-3},{1,2,3}}}};
    source->skins.push_back(skin);
    return source;
}
std::shared_ptr<CityAnimationLibrary> libraryFixture()
{
    auto library = std::make_shared<CityAnimationLibrary>();
    library->path = "anim_ingame.bdae"; library->commonCacheMetadata = 5;
    const auto track = [](std::string target, CityAnimationTransform transform,
                          std::array<float,4> first, std::array<float,4> last) {
        return CityAnimationLibraryTrack{0,"source",std::move(target),transform,1,0,1,{{0,first},{100,last}}};
    };
    library->tracks = {track("Bip01-node",CityAnimationTransform::translation,{0,0,0,0},{100,0,0,0}),
        track("Bip01-node",CityAnimationTransform::rotation,{0,0,0,1},{0,0,0,1}),
        track("shadow-node",CityAnimationTransform::translation,{1,2,3,0},{11,12,13,0})};
    for (std::size_t i = 0; i < library->tracks.size(); ++i)
        library->tracks[i].sourceIndex = static_cast<std::uint32_t>(i);
    return library;
}
std::shared_ptr<const RetailActorAnimationCatalog> catalogFixture()
{
    CityAnimationChannel root{{},CityAnimationTransform::translation,
        {{0,{0,0,0,0}},{100,{100,0,0,0}}},"Bip01-node"};
    return std::make_shared<const RetailActorAnimationCatalog>(
        std::vector<CityAnimationClip>{{"idle",100,{root},CityAnimationSource{0,0,100}}});
}

void stagesAndLifetime()
{
    RetailActorGraph graph;
    auto source = modelFixture();
    auto library = libraryFixture();
    RetailActorAnimationGlobals globals(catalogFixture(),library);
    auto actor = std::make_shared<RetailActorScene>(source);
    std::weak_ptr<RetailActorScene> sceneLifetime = actor;
    std::weak_ptr<RetailActorPose> poseLifetime;
    std::weak_ptr<RetailTimeline> firstTimeline;
    auto* shadow = actor->root().findUid("shadow-node");
    const auto cached = shadow->absolute();
    const auto* positionAddress = &shadow->position();
    {
        RetailActorAnimationHost host(actor,graph,globals);
        rejects([&]{host.clearAnimatorTimeline();});
        rejects([&]{host.constructAnimator("wrong.bdae");});
        host.constructAnimator("./anim_ingame.bdae");
        auto pose = host.pose();
        poseLifetime = pose;
        firstTimeline = pose->timeline();
        require(!host.hasSharedTimeline() && pose->timeline() && pose->timeline()->timeMs() == 0,
                "Actual fresh plain animator contains the sole factory timeline");
        rejects([&]{host.bindAnimatorToActor();});
        host.retainAnimatorTimeline(fists);
        require(globals.sharedTimeline() == pose->timeline() && globals.fistsCombatSequence() == fists,
                "First global retain is the exact attached mutable object");
        host.clearAnimatorTimeline();
        require(!pose->timeline() && !firstTimeline.expired(), "Actual detach leaves first shared timeline alive");
        host.bindAnimatorToActor();
        host.releaseAnimatorReference();
        pose.reset();
        require(poseLifetime.use_count() == 2 && shadow->boundAnimatorCount() == 1,
                "Only the two genuine bound node lists retain the three-track fixture pose");
        auto other = std::make_shared<RetailActorScene>(source);
        auto wrongPose = RetailActorPose::create(library); wrongPose->bind(other);
        rejects([&]{graph.registerShadow(actor,*shadow,*wrongPose);});
        rejects([&]{graph.registerShadow(actor,*shadow);}); // Legacy policy remains unchanged.
        auto extra = RetailActorPose::create(library); extra->bind(actor);
        auto genuine = host.pose();
        rejects([&]{graph.registerShadow(actor,*shadow,*genuine);});
        for (const auto& node : actor->nodes()) node->removeBoundAnimator(*extra);
        genuine.reset(); extra.reset();
        require(actor->root().findUid("shadow-node") == shadow && shadow->absolute() == cached,
                "Unsupported transfer fails before changing ownership/cache");
        host.registerShadow(shadow);
        require(graph.root().children() == std::vector<RetailActorSceneNode*>{shadow} && !actor->root().parent(),
                "Genuine bound shadow moves before the actor root attaches");
        require(shadow->absolute() == cached && &shadow->position() == positionAddress &&
                shadow->boundAnimatorCount() == 1 && poseLifetime.use_count() == 2,
                "Transfer preserves local references, cached matrix and exact node-held retention");
        auto retained = host.pose();
        require(retained->target(2) == shadow && retained->boundTo(*actor), "Moved source-order slot stays bound");
        retained->apply(50);
        require(shadow->position() == std::array<float,3>{6,7,8}, "Same current shadow local reference is still sampled");
        globals.fistsCombatSequence()[0] = 77;
        globals.clearSharedTimeline();
        require(firstTimeline.expired() && globals.fistsCombatSequence()[0] == 77 && !retained->timeline(),
                "Global timeline cleanup preserves fists and detached poses");
        retained->apply(60);
    }
    actor.reset();
    require(sceneLifetime.expired() && poseLifetime.use_count() == 1,
            "Pooled bound shadow retains its pose but never a hidden live actor");
    auto retained = poseLifetime.lock();
    require(retained && !retained->target(0) && !retained->target(1) && retained->target(2) == shadow,
            "Actor destruction invalidates actor targets while moved shadow target remains alive");
    rejects([&]{retained->apply(70);});
    retained.reset();
    graph.removeChild(shadow);
    require(poseLifetime.expired(), "Last shadow drop unbinds the final target before releasing the plain pose");

    // A subsequent factory owns a genuine new timeline; detaching it destroys
    // that exact object immediately, without replacing first global state.
    auto next = std::make_shared<RetailActorScene>(source);
    RetailActorAnimationHost host(next,graph,globals);
    host.constructAnimator("./anim_ingame.bdae");
    host.retainAnimatorTimeline(fists);
    host.clearAnimatorTimeline(); host.releaseAnimatorReference();
    require(globals.fistsCombatSequence() == fists, "First binding after global clean restores source fists indices");
    auto third = std::make_shared<RetailActorScene>(source);
    RetailActorAnimationHost later(third,graph,globals);
    later.constructAnimator("./anim_ingame.bdae");
    std::weak_ptr<RetailTimeline> subsequent = later.pose()->timeline();
    const auto* shared = globals.sharedTimeline().get();
    globals.fistsCombatSequence()[0] = 88;
    later.clearAnimatorTimeline();
    require(subsequent.expired() && globals.sharedTimeline().get() == shared && globals.fistsCombatSequence()[0] == 88,
            "Later attached timeline dies on clear; actual first timeline and fists remain untouched");
    later.releaseAnimatorReference();
}

void bindingAndShadowFirst()
{
    RetailActorAnimationGlobals globals(catalogFixture(),libraryFixture());
    RetailActorGraph graph;
    auto actor = std::make_shared<RetailActorScene>(modelFixture());
    auto* root = actor->root().findUid("Bip01-node");
    auto* shadow = actor->root().findUid("shadow-node");
    RetailAnimationClock clock;
    clock.rootHeight = 19;
    std::uint32_t flags = 0x100000;
    RetailActorAnimationHost host(actor,graph,globals);
    RetailActorAnimationBinding binding(globals.catalog(),host,clock,flags);
    auto pose = host.pose();
    require(binding.actorInstance() == &actor->root() && binding.rootInstance() == root &&
            binding.shadowInstance() == shadow && binding.meshInstance() == actor->root().findMesh() &&
            pose->target(2) == shadow && !pose->timeline(), "Concrete host composes the existing binding with real identities");
    require((root->transformFlags() & 7) == 6 && (shadow->transformFlags() & 7) == 4 &&
            clock.rootHeight == 19 && flags == 0x100000, "Binding preserves typed masks and canonical actor state");
    struct Weapon final : RetailActorAnimationWeaponHost {
        std::int32_t weaponType() override { throw std::runtime_error("Unexpected weapon query on first animation"); }
        void reloadRocket() override { throw std::runtime_error("Unexpected reload on first animation"); }
    } weapon;
    require(binding.select({0,1,true,true,false},false,weapon) == RetailAnimationSelection::accepted,
            "Existing selection uses the canonical actor clock");
    const auto step = binding.advance(25,actor->root().absolute(),false,{},RetailAnimationInterpolation::linear,pose.get());
    require(step.evaluated && step.absoluteMs == 25 && clock.cursor == 25 && step.sampledRoot == root->position(),
            "Existing clock evaluates this same full retained pose, with no host clock or second sampler");
    graph.attachActor(actor);
    require(graph.root().children() == std::vector<RetailActorSceneNode*>{shadow,&actor->root()},
            "Outer Player attachment retains original shadow-before-actor child order");
    graph.removeChild(shadow);
    require(!pose->target(2) && pose->target(0) == root && pose->target(1) == root,
            "Shadow-first destruction invalidates only its genuine source UID slots");
    pose->apply(26);
    require(root->position()[0] == 26, "Live actor pose remains usable after independent shadow destruction");
}

void snapshot(const char* name, const RetailActorAnimationBinding& binding,
              const std::shared_ptr<RetailActorScene>& actor, const std::shared_ptr<RetailActorPose>& pose,
              const RetailActorGraph& graph)
{
    const auto* shadow = static_cast<const RetailActorSceneNode*>(binding.shadowInstance());
    std::cout << "host {\"model\":" << std::quoted(name) << ",\"period\":" << pose->periodMs()
              << ",\"tracks\":" << pose->bindingCount() << ",\"targets\":[";
    for (std::size_t i = 0; i < pose->bindingCount(); ++i) {
        if (i) std::cout << ',';
        const auto* node = pose->target(i);
        std::cout << (node ? std::quoted(node->uid()) : std::quoted(std::string{}));
    }
    std::cout << "],\"nodes\":[";
    bool comma = false;
    const auto emit = [&](const RetailActorSceneNode& node) {
        if (comma) std::cout << ',';
        comma = true;
        std::cout << "{\"uid\":" << std::quoted(node.uid()) << ",\"mask\":" << node.transformFlags()
                  << ",\"ordinary\":" << node.animatorCount() << ",\"bound\":" << node.boundAnimatorCount()
                  << ",\"layer\":" << node.renderingLayer() << ",\"parent\":";
        if (node.parent() == &graph.root()) std::cout << "{\"global\":true}";
        else if (node.parent()) std::cout << std::quoted(node.parent()->uid());
        else std::cout << "null";
        std::cout << ",\"words\":[";
        bool wordComma = false;
        const auto words = [&](const auto& values) {
            for (auto value : values) {
                if (wordComma) std::cout << ',';
                wordComma = true;
                std::cout << bits(value);
            }
        };
        words(node.position()); words(node.rotation()); words(node.scale()); words(node.absolute());
        std::cout << "],\"children\":[";
        for (std::size_t i = 0; i < node.children().size(); ++i) {
            if (i) std::cout << ',';
            std::cout << std::quoted(node.children()[i]->uid());
        }
        std::cout << "],\"materials\":[";
        for (std::size_t i = 0; i < node.materialCount(); ++i) {
            if (i) std::cout << ',';
            const auto& state = node.materialOwner(i)->state();
            std::cout << '[' << state.vertexColorControl << ',' << state.flags << ',' << state.ambient << ','
                      << state.emissive << ',' << state.specular << ',' << state.diffuse << ',' << state.dirtyFlags << ']';
        }
        std::cout << "]}";
    };
    for (const auto& node : actor->nodes()) emit(*node);
    emit(*shadow);
    for (const auto* child : shadow->children()) emit(*child);
    std::cout << "]}\n";
}

void actualAssets(FileSystemBase& files)
{
    RetailActorAnimationGlobals globals(files);
    RetailActorGraph graph;
    std::weak_ptr<RetailTimeline> shared;
    std::vector<std::shared_ptr<RetailActorScene>> actors;
    std::vector<std::weak_ptr<RetailActorPose>> poses;
    for (const char* name : {"char_pthug.bdae","char_pthug_jail.bdae"}) {
        auto actor = std::make_shared<RetailActorScene>(loadRetailActorSceneSource(files,std::string("./") + name));
        RetailAnimationClock clock;
        clock.rootHeight = 52;
        std::uint32_t flags = 0x100000;
        RetailActorAnimationHost host(actor,graph,globals);
        RetailActorAnimationBinding binding(globals.catalog(),host,clock,flags);
        auto pose = host.pose();
        poses.push_back(pose);
        std::set<const RetailActorSceneNode*> targets;
        for (std::size_t i = 0; i < pose->bindingCount(); ++i) {
            require(pose->target(i), "All actual Player source tracks have real live target bindings");
            targets.insert(pose->target(i));
        }
        require(pose->bindingCount() == 24 && targets.size() == 23 && pose->periodMs() == 259066 &&
                !pose->timeline() && pose.use_count() == 24, "Actual factory24tracks/23nodes and local animator drop");
        require(clock.rootHeight == 52 && flags == 0x100000,
                "Binding uses canonical caller state without resetting rootHeight/flags");
        auto* shadow = static_cast<const RetailActorSceneNode*>(binding.shadowInstance());
        require(graph.root().children().back() == shadow && !actor->root().parent() &&
                !actor->root().findUid("shadow-node"), "Actual force-bound shadow is globally moved before actor attachment");
        const auto* mesh = static_cast<const RetailActorSceneNode*>(binding.meshInstance());
        require(mesh == actor->root().findMesh() && mesh->type() == retailSceneSkinnedMeshType && mesh->mesh()->skin(),
                "FindAMesh occurs after shadow removal and returns actual current skin node");
        const auto& material = shadow->children().front()->materialOwner(0)->state();
        require(shadow->children().front()->renderingLayer() == 1 && material.ambient == 0xffffffff &&
                material.emissive == 0xff000000 && material.specular == 0xffffffff && material.diffuse == 0xffffffff,
                "Original layer and color traversal updates genuine shared material owner");
        require(!(material.flags & 0x400) && !(material.flags & 0x200) && (material.flags & 16) &&
                (material.vertexColorControl & 1), "Original shadow material flag namespaces");
        for (const auto& node : actor->nodes())
            require(node->animatorCount() == 0, "Character never registers the pose in ordinary frame lists");
        snapshot(name,binding,actor,pose,graph);
        if (actors.empty()) {
            require(globals.fistsCombatSequence() == fists, "First actual Player initializes original fists sequence");
            shared = globals.sharedTimeline();
            globals.fistsCombatSequence() = {0,1,2,3,4,5,6};
        } else {
            require(globals.sharedTimeline().get() == shared.lock().get() &&
                    globals.fistsCombatSequence() == std::array<std::int32_t,7>{0,1,2,3,4,5,6},
                    "Second actual Player preserves first shared mutable timeline and current fists state");
        }
        graph.attachActor(actor); // Actual later outer Player operation.
        actors.push_back(actor);
    }
    globals.clearSharedTimeline();
    require(shared.expired(), "Actual global timeline dies while both detached full poses stay retained");
    for (std::size_t i = 0; i < actors.size(); ++i) {
        auto pose = poses[i].lock();
        pose->apply(241167);
        auto* shadow = const_cast<RetailActorSceneNode*>(pose->target(3));
        auto* raw = actors[i].get();
        std::weak_ptr<RetailActorScene> scene = actors[i];
        actors[i].reset();
        graph.releaseActor(*raw,shadow,17);
        require(scene.expired() && poses[i].use_count() == 2,
                "Real actor teardown leaves one moved-shadow pose reference plus explicit temporary grab");
        for (std::size_t j = 0; j < pose->bindingCount(); ++j)
            require(pose->target(j) == (j == 3 ? shadow : nullptr), "Exact original surviving shadow target after actor drop");
        rejects([&]{pose->apply(241168);});
        pose.reset();
        graph.removeChild(shadow);
        require(poses[i].expired(), "Final actual shadow drop destroys retained full pose without cycle");
    }
}
}

int main(int argc, char** argv)
{
    try {
        stagesAndLifetime();
        bindingAndShadowFirst();
        if (argc == 3 && std::string(argv[1]) == "--assets") {
            ResourceFileSystem files(argv[2]);
            actualAssets(files);
        }
        std::cout << "Character concrete factory/timeline/pose/material/shadow host and independent lifetimes passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
