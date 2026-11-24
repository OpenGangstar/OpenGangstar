#include "retail_actor_animation.h"
#include "filesystem.h"

#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
uint32_t bits(float x) { uint32_t result; std::memcpy(&result, &x, 4); return result; }
const std::array<float, 16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
CityScene model() {
    CityScene result; result.path = "char_pthug.bdae";
    CityNode root{}; root.name = "Bip01-node"; root.parent = -1; root.translation = {7,8,9};
    result.nodes.push_back(root); root.name = "shadow-node"; result.nodes.push_back(root); return result;
}
std::vector<CityAnimationClip> clips() {
    std::vector<CityAnimationClip> result;
    for (uint32_t i = 0; i < 213; ++i) {
        CityAnimationClip clip{"clip" + std::to_string(i), 101, {}};
        clip.source = CityAnimationSource{i, int32_t(i*1000), int32_t(i*1000+101)};
        clip.channels.push_back({{}, CityAnimationTransform::translation,
            {{0, {float(i),2,3,0}}, {101, {float(i+101),204,306,0}}}, "Bip01-node"});
        result.push_back(std::move(clip));
    }
    return result;
}
struct Scene final : RetailActorAnimationSceneHost {
    const CityScene& source;
    struct Node { int value; } actor{1}, root{2}, shadow{3}, mesh{4};
    std::vector<std::string> calls;
    bool missingActor = false, missingRoot = false, missingShadow = false, missingMesh = false;
    bool sharedTimeline = false;
    explicit Scene(const CityScene& source) : source(source) {}
    const CityScene& sourceModel() const override { return source; }
    const void* actorInstance() const override { return missingActor ? nullptr : &actor; }
    void constructAnimator(std::string_view path) override {
        require(path == "./anim_ingame.bdae", "Original animator resource"); calls.push_back("construct");
    }
    void clearAnimatorTimeline() override { calls.push_back("detach"); }
    bool hasSharedTimeline() const override { return sharedTimeline; }
    void retainAnimatorTimeline(const std::array<int32_t,7>& sequence) override {
        require(!sharedTimeline && sequence==std::array<int32_t,7>{144,94,92,93,95,90,91},
                "Original first timeline retains identity and initializes fists sequence once");
        sharedTimeline=true;calls.push_back("retain");
    }
    void bindAnimatorToActor() override { calls.push_back("bind"); }
    void releaseAnimatorReference() override { calls.push_back("release"); }
    const void* findNodeByUID(std::string_view uid) override {
        calls.push_back(std::string(uid));
        if (uid == "Bip01-node") return missingRoot ? nullptr : &root;
        if (uid == "shadow-node") return missingShadow ? nullptr : &shadow;
        return nullptr;
    }
    void setFirstChildRenderingLayer(const void* node, int32_t layer) override {
        require(node == &shadow && layer == 1, "Shadow child rendering layer, not inferred visibility"); calls.push_back("layer1");
    }
    void setColor(const void* node, uint32_t color, uint32_t type) override {
        require(node == &shadow && type <= 3 && color == (type == 3 ? 0xff000000u : 0xffffffffu),
                "Original literal shadow color/type"); calls.push_back("color"+std::to_string(type));
    }
    void setMaterialFlag(const void* node, uint32_t flag, bool value) override {
        require(node == &shadow && ((flag == 0x400 || flag == 0x200) ? !value : value) &&
                    (flag == 0x400 || flag == 0x200 || flag == 4 || flag == 16), "Original literal shadow material flags");
        calls.push_back("flag"+std::to_string(flag));
    }
    void registerShadow(const void* node) override {
        require(node == &shadow, "Register actual bound shadow"); calls.push_back("register");
    }
    const void* findMesh() override { calls.push_back("mesh"); return missingMesh ? nullptr : &mesh; }
};
struct Weapon final : RetailActorAnimationWeaponHost {
    int type = 0, queries = 0, reloads = 0;
    std::function<void()> callback;
    int32_t weaponType() override { ++queries; return type; }
    void reloadRocket() override { ++reloads; if (callback) callback(); }
};
void catalogIdentity() {
    auto entries = clips(); entries[0].name = "move"; entries[1].name = "MOVE"; entries[2].name = "move";
    entries[1].source->startMs = -101; entries[1].source->endMs = 0;
    RetailActorAnimationCatalog catalog(std::move(entries));
    require(catalog.find("move") == 0 && catalog.find("MOVE") == 1 && catalog.find("Move") == -1,
            "Original exact strcmp/first-match lookup preserves duplicate indexed entries");
    require(catalog.at(2).source->index == 2 && catalog.at(1).source->startMs == -101,
            "Signed interval and duplicate-name indexed identity survive");
    auto bad = clips(); bad[0].source.reset(); bool failed = false;
    try { RetailActorAnimationCatalog invalid(std::move(bad)); } catch (const std::invalid_argument&) { failed = true; }
    require(failed, "Missing original metadata is explicit");
    bad = clips(); bad[1].source->index = 0; failed = false;
    try { RetailActorAnimationCatalog invalid(std::move(bad)); } catch (const std::invalid_argument&) { failed = true; }
    require(failed, "Decoded catalogue must preserve source index order");
}
void bindingAndCache() {
    RetailActorAnimationCatalog catalog(clips()); auto source = model(); Scene scene(source);
    RetailAnimationClock clock; clock.cachedRoot = {11,13,17}; clock.rootHeight = 19; clock.cursor = 23;
    uint32_t flags = 0;
    RetailActorAnimationBinding binding(catalog, scene, clock, flags);
    require(scene.calls == std::vector<std::string>{"construct","retain","detach","bind","release","Bip01-node","shadow-node",
                "layer1","color3","color0","color1","color2","flag1024","flag512","flag4","flag16","register","mesh"},
            "Original bindAnimation required operation sequence");
    require(binding.actorInstance() == &scene.actor && binding.rootInstance() == &scene.root &&
                binding.rootInstance() != &source.nodes[0] && binding.meshInstance() == &scene.mesh &&
                binding.rootSourceNode() == 0, "Live identities remain distinct from source CityNode identity/index");
    require(clock.cachedRoot == std::array<float,3>{11,13,17} && clock.rootHeight == 19 && clock.cursor == 23,
            "Binding does not establish a guessed first-frame cache or reset the clock");
    scene.calls.clear();
    { RetailActorAnimationBinding next(catalog,scene,clock,flags); }
    require(scene.calls.front()=="construct" && scene.calls[1]=="detach" && scene.sharedTimeline,
            "Later binding reuses the application's retained first timeline without reinitializing combat sequence");
    Weapon weapon;
    require(binding.select({202,1,true,true,false}, false, weapon) == RetailAnimationSelection::accepted,
            "Genuine indexed interval selected");
    require(clock.startMs == 202000 && clock.endMs == 202101 && clock.cachedRoot[0] == 11,
            "Accepted clip interval comes from source metadata while retained cache survives");
    const auto first = binding.advance(0, identity, true);
    require(first.evaluated && first.absoluteMs == 202000 && first.sampledRoot == std::array<float,3>{202,2,3} &&
                clock.cachedRoot == first.sampledRoot && clock.rootHeight == 3 && !first.bodyVelocity,
            "First sample uses verified sampler and real indexed start without guessed speed");
    clock.cursor = 17; clock.nextAnimation = 99;
    require(binding.select({202,99,false,false,true}, false, weapon) == RetailAnimationSelection::repeated &&
                clock.cursor == 17 && clock.rate == 1 && clock.rootMotion && clock.nextAnimation == 99,
            "Repeated ID preserves phase/options/cache/deferred state");
    const auto cache = clock.cachedRoot;
    binding.select({-1,1,false,false,false}, false, weapon);
    const auto cleared = binding.advance(100, identity, true);
    require(!cleared.evaluated && clock.cachedRoot == cache && clock.nextAnimation == 99,
            "Clear/unbound clock does not advance or clear retained cache/deferred state");
    clock.animation=1;clock.startMs=1000;clock.endMs=1101;clock.cursor=0;clock.rate=2;clock.loop=false;
    clock.nextAnimation=-1;
    require(binding.select({2,std::numeric_limits<float>::max(),true,true,false},false,weapon)==RetailAnimationSelection::deferred &&
                clock.animation==1 && clock.cursor==0 && clock.rate==2 && clock.nextAnimation==2 && clock.cachedRoot==cache,
            "Guard-only deferred selection mutates only the pending ID, without accepted rate validation");
    require(binding.select({202,1,true,true,false},false,weapon)==RetailAnimationSelection::rejected && clock.nextAnimation==2,
            "Protected rejection preserves the guard's preceding deferred state");
    for (int missing = 0; missing < 4; ++missing) {
        Scene invalid(source); invalid.missingActor=missing==0; invalid.missingRoot=missing==1;
        invalid.missingShadow=missing==2; invalid.missingMesh=missing==3;
        bool failed=false;
        try { RetailActorAnimationBinding unavailable(catalog,invalid,clock,flags); }
        catch(const std::invalid_argument&) { failed=true; }
        require(failed,"Missing actual runtime binding is explicit, never a fabricated source pointer");
    }
}
void liveReload(bool dump) {
    RetailActorAnimationCatalog catalog(clips()); auto source = model(); Scene scene(source);
    RetailAnimationClock clock; uint32_t flags = 5; RetailActorAnimationBinding binding(catalog,scene,clock,flags);
    clock.animation=67;clock.startMs=700;clock.endMs=800;clock.cursor=44;clock.rate=2;clock.loop=true;
    clock.speedMultiplier=2;clock.cachedRoot={1,2,3};clock.rootHeight=4;
    Weapon weapon;weapon.type=7;
    weapon.callback=[&] {
        require(clock.animation==67 && clock.cursor==44 && flags==5 && clock.cachedRoot[0]==1,
                "Reload observes LIVE old actor before accepted clock/flag commit");
        clock.animation=86;clock.cursor=12;clock.loop=false;clock.speedMultiplier=3;
        clock.cachedRoot={7,8,9};clock.rootHeight=77;clock.nextAnimation=999;flags|=0x820;
    };
    const auto selection=binding.select({164,.25f,true,true,false},false,weapon);
    require(selection==RetailAnimationSelection::accepted && weapon.queries==1 && weapon.reloads==1,
            "Required rocket reload consumed exactly once after guard acceptance");
    require(clock.animation==164 && clock.startMs==164000 && clock.endMs==164101 && clock.cursor==0 &&
                clock.rate==.75f && clock.speedMultiplier==3 && clock.nextAnimation==-1 && clock.loop && clock.rootMotion &&
                !clock.directionalMotion && clock.cachedRoot==std::array<float,3>{7,8,9} && clock.rootHeight==77 && flags==0x80000825,
            "Commit reads live multiplier/preserves cache and flag effects; newly set blocker does not rerun guards");
    if(dump) std::cout<<"reload "<<clock.animation<<' '<<clock.startMs<<' '<<clock.endMs<<' '<<bits(clock.cursor)<<' '
        <<bits(clock.rate)<<' '<<clock.speedMultiplier<<' '<<clock.nextAnimation<<' '<<clock.loop<<' '<<clock.rootMotion<<' '
        <<clock.directionalMotion<<' '<<bits(clock.cachedRoot[0])<<' '<<bits(clock.cachedRoot[1])<<' '<<bits(clock.cachedRoot[2])
        <<' '<<bits(clock.rootHeight)<<' '<<flags<<' '<<weapon.reloads<<'\n';
    // Guard rejection must not even look up an unavailable clip or query weapon.
    const auto priorQueries=weapon.queries;
    require(binding.select({999,1,true,true,false},false,weapon)==RetailAnimationSelection::rejected && weapon.queries==priorQueries,
            "Selection guards precede catalogue getters and live weapon operations");
}
void reloadBeforeRateValidation(bool dump) {
    RetailActorAnimationCatalog catalog(clips()); auto source=model(); Scene scene(source);
    RetailAnimationClock clock; uint32_t flags=5; RetailActorAnimationBinding binding(catalog,scene,clock,flags);
    clock.animation=67;clock.startMs=700;clock.endMs=800;clock.cursor=44;clock.rate=2;clock.loop=true;
    clock.speedMultiplier=2;clock.cachedRoot={1,2,3};clock.rootHeight=4;
    Weapon weapon;weapon.type=7;
    weapon.callback=[&] {
        require(clock.animation==67 && clock.speedMultiplier==2 && clock.cursor==44,
                "Overflow regression reload must observe unchanged live pre-commit state");
        clock.speedMultiplier=1;
    };
    const auto maximum=std::numeric_limits<float>::max();
    RetailAnimationSelection selected;
    try { selected=binding.select({164,maximum,true,true,false},false,weapon); }
    catch(const std::runtime_error&) {
        require(weapon.reloads==1,"Accepted rate validation ran before required live reload");
        throw;
    }
    require(selected==RetailAnimationSelection::accepted && weapon.reloads==1 && weapon.queries==1 &&
                clock.animation==164 && clock.startMs==164000 && clock.endMs==164101 && clock.cursor==0 &&
                bits(clock.rate)==0x7f7fffff && clock.speedMultiplier==1 && clock.nextAnimation==-1 &&
                clock.loop && clock.rootMotion && !clock.directionalMotion &&
                clock.cachedRoot==std::array<float,3>{1,2,3} && clock.rootHeight==4 && flags==0x80000005,
            "Old multiplier overflow must not block a finite accepted rate after live reload");
    if(dump) std::cout<<"reload_overflow "<<clock.animation<<' '<<clock.startMs<<' '<<clock.endMs<<' '<<bits(clock.cursor)<<' '
        <<bits(clock.rate)<<' '<<clock.speedMultiplier<<' '<<clock.nextAnimation<<' '<<clock.loop<<' '<<clock.rootMotion<<' '
        <<clock.directionalMotion<<' '<<bits(clock.cachedRoot[0])<<' '<<bits(clock.cachedRoot[1])<<' '<<bits(clock.cachedRoot[2])
        <<' '<<bits(clock.rootHeight)<<' '<<flags<<' '<<weapon.reloads<<'\n';
    clock.animation=67;clock.cursor=44;clock.loop=true;clock.speedMultiplier=2;flags=5;
    weapon.reloads=0;weapon.queries=0;weapon.callback=[] {};
    bool failed=false;
    try { binding.select({164,maximum,true,true,false},false,weapon); }
    catch(const std::runtime_error& error) { failed=std::string_view(error.what())=="Animation rate overflow."; }
    require(failed && weapon.reloads==1 && weapon.queries==1,
            "Finite-rate checks remain enforced AFTER live reload when the actual multiplier still overflows");
}
void supplied(const std::filesystem::path& directory,bool dump) {
    ResourceFileSystem assets(directory); const auto catalog=loadRetailActorAnimationCatalog(assets);
    require(catalog.clips().size()==213 && catalog.find("move_run")==188 && catalog.find("move_walk")==202 &&
                catalog.find("move_turn")==201 && catalog.find("move_walk_to_idle")==212 &&
                catalog.find("move_idle")==165 && catalog.find("MOVE_WALK")==-1,
            "Actual player IDs are source catalogue facts, not hardcoded runtime mapping");
    for(const auto& name:{"char_pthug.bdae","char_pthug_jail.bdae"}) {
        const auto source=loadCityScene(assets,name);Scene scene(source);RetailAnimationClock clock;uint32_t flags=0;
        RetailActorAnimationBinding binding(catalog,scene,clock,flags);Weapon weapon;size_t keys=0,checked=0;
        for(size_t i=0;i<catalog.clips().size();++i) {
            const auto& c=catalog.at(static_cast<int32_t>(i));const auto& root=binding.rootChannel(static_cast<int32_t>(i));
            keys+=root.keys.size(); require(root.node==binding.rootSourceNode() && root.target=="Bip01-node",
                "Every actual catalogue root resolves to the loaded player source UID and separate live root");
            clock=RetailAnimationClock{};
            binding.select({static_cast<int32_t>(i),1,true,true,false},false,weapon);
            require(clock.startMs==c.source->startMs && clock.endMs==c.source->endMs && clock.cachedRoot==std::array<float,3>{},
                    "Constructor zero cache and each original interval retained across all supplied IDs");
            if(dump && std::string_view(name)=="char_pthug.bdae")
                std::cout<<"clip "<<i<<' '<<c.source->startMs<<' '<<c.source->endMs<<' '<<c.name<<'\n';
            for(uint32_t t:{0u,1u,c.durationMs-1}) {
                const auto sampled=sampleRetailAnimationRoot(root,static_cast<int32_t>(t),RetailAnimationInterpolation::linear);
                if(dump && std::string_view(name)=="char_pthug.bdae")
                    std::cout<<"root "<<i<<' '<<t<<' '<<bits(sampled[0])<<' '<<bits(sampled[1])<<' '<<bits(sampled[2])<<'\n';
                ++checked;
            }
        }
        std::cout<<name<<": "<<catalog.clips().size()<<" indexed roots, "<<keys<<" root keys, "<<checked<<" samples; root source node "
                 <<binding.rootSourceNode()<<".\n";
    }
}
}
int main(int argc,char** argv) {
    try {
        const bool dump=argc>=2 && std::string_view(argv[argc-1])=="--oracle-dump";
        catalogIdentity();bindingAndCache();liveReload(dump);reloadBeforeRateValidation(dump);
        if(argc>=2 && std::string_view(argv[1])!="--oracle-dump") supplied(argv[1],dump);
        std::cout<<"Retail actor animation binding fixtures passed.\n";return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
