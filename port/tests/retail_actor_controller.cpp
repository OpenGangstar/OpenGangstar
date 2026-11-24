#include "retail_actor_controller.h"
#include "retail_world.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool close(float actual, float expected) { return std::abs(actual-expected)<.0001f; }
b2AABB bounds() {
    b2AABB result;
    result.lowerBound.Set(-2000, -2000);
    result.upperBound.Set(2000, 2000);
    return result;
}
struct Host final : RetailDirectPlayerHost {
    RetailDirectPlayerState& state;
    bool dead = false, down = false, vehicle = false, dieOnSecond = false;
    int context = 0, weapon = 0, deaths = 0;
    bool twoHanded = false;
    RetailHeightPoint position{0, 0, 10};
    std::vector<std::string> calls;
    std::vector<RetailAnimationRequest> requests;
    std::function<void()> onReload, onClear, onTurn, onSelect, onWeaponType;
    std::function<void(const RetailHeightPoint&)> onRoot;
    explicit Host(RetailDirectPlayerState& state) : state(state) {}
    bool isDead() override { calls.push_back("dead"); ++deaths; return dead || (dieOnSecond && deaths==2); }
    bool isDown() override { calls.push_back("down"); return down; }
    bool hasVehicle() override { return vehicle; }
    int animationContext() override { calls.push_back("context"); return context; }
    int weaponType() override { calls.push_back("weapon"); if (onWeaponType) onWeaponType(); return weapon; }
    bool weaponTwoHanded() override { calls.push_back("two"); return twoHanded; }
    RetailHeightPoint positionCm() override { calls.push_back("position"); return position; }
    void turnToward(const RetailHeightPoint& point) override {
        calls.push_back("turn");
        require(point == state.adjustedDestinationCm || point == state.destinationCm,
                "Turn uses the requested/original adjusted point");
        if (onTurn) onTurn();
    }
    void selectAnimation(const RetailAnimationRequest& request) override {
        calls.push_back("select"); requests.push_back(request);
        if (onSelect) onSelect();
        // Synthetic intervals test the real phase/guard primitive. Production
        // must supply the genuine catalog and original reload host, separately.
        const auto result = selectRetailAnimation(state.animation, request, 0, 100,
                                                  state.movementFlags, false, weapon);
        require(!result.reloadRocket, "Fixture must not hide a required rocket reload");
    }
    void checkWeaponReload() override { calls.push_back("reload"); if (onReload) onReload(); }
    void clearTarget() override { calls.push_back("clear"); if (onClear) onClear(); }
    void setAnimatedRootTranslation(const RetailHeightPoint& value) override {
        calls.push_back("root"); if (onRoot) onRoot(value);
    }
};
struct Fixture {
    CityMesh mesh;
    CityNavigation nav;
    RetailPhysicsOwnerState owner{8, 10, 0, 0};
    RetailContactFilter filter{&owner, {}};
    b2World world{bounds(), b2Vec2(0, 0), true};
    RetailDirectPlayerState state;
    Host host{state};
    std::unique_ptr<RetailHeightTerrain> terrain;
    std::unique_ptr<RetailActorPhysics> body;
    std::unique_ptr<RetailDirectPlayerController> controller;
    Fixture() {
        world.SetContactFilter(&filter);
        world.SetPositionCorrection(true);
        world.SetContinuousPhysics(false);
        mesh.positions = {{-1000,-1000,10},{1000,-1000,10},{0,1000,10}};
        mesh.buffers.push_back({"", {0,1,2}});
        nav.buffers.resize(1);
        for (size_t i=0;i<8;++i)
            nav.buffers[0].neighbors[i] = {uint16_t(i<6 ? UINT16_MAX : 0)};
        nav.buffers[0].roadFlags = {0};
        nav.buffers[0].overlaps.resize(1);
        nav.cliffVertexKeys.resize(6);
        nav.materialBuffers.fill(0);
        terrain = std::make_unique<RetailHeightTerrain>(mesh,nav);
        body = std::make_unique<RetailActorPhysics>(*terrain,world,owner);
        body->initialize({0,0,10}, RetailPhysicsZBounds{0,180}, &owner);
        controller = std::make_unique<RetailDirectPlayerController>(world,*body,owner,state,host,&owner);
    }
};
void commands() {
    Fixture f;
    f.state.movementFlags = 0xa56000f1;
    f.controller->walkTo({500,200,77});
    require(f.state.movementFlags == 0xa5608002 && f.state.destinationLimitSquared == 10000 &&
            f.state.destinationCm == RetailHeightPoint{500,200,77}, "Walk preserves upper flags and sets real destination");
    require(f.host.calls == std::vector<std::string>{"dead","dead","turn","context","weapon","two","select"},
            "Original wrapper/base/turn/animation order");
    require(f.host.requests.back().animation==202 && f.host.requests.back().loop &&
            f.host.requests.back().rootMotion && !f.host.requests.back().directionalMotion,
            "Walk original animation options");
    f.state.animation.cursor=47.5f; f.state.animation.cachedRoot={1,2,3};
    f.host.calls.clear();
    f.controller->walkTo({600,400,99});
    require(f.state.animation.cursor==47.5f && f.state.animation.cachedRoot==RetailHeightPoint{1,2,3},
            "Reissued same clip retains original phase/root cache");
    f.host.dead=true;
    const auto old=f.state.destinationCm; const auto flags=f.state.movementFlags;
    f.controller->runTo({900,900,900});
    require(f.state.destinationCm==old && f.state.movementFlags==flags, "Dead wrapper has no movement mutation");
    f.host.dead=false; f.host.down=true;
    f.controller->runTo({700,800,123});
    require(f.state.destinationCm==RetailHeightPoint{700,800,123} &&
            f.state.movementFlags==flags, "Down run commits wrapper destination before base guard");
    f.host.down=false;
    f.controller->runTo({800,800,123});
    require((f.state.movementFlags&255)==4 && f.host.requests.back().animation==188,
            "Run uses original low-byte4 and specific188");
    f.body->body()->SetLinearVelocity(b2Vec2(2,-3)); f.body->body()->SetAngularVelocity(4);
    f.host.onClear=[&] {
        require(f.body->body()->GetLinearVelocity().LengthSquared()==0 &&
                f.body->body()->GetAngularVelocity()==0, "Target callback follows actual body velocity zeroing");
    };
    f.controller->stop();
    require((f.state.movementFlags&255)==1 && (f.state.movementFlags&0x8000) &&
            f.host.requests.back().animation==212 && !f.host.requests.back().loop &&
            !f.host.requests.back().rootMotion, "Stop preserves completion bit and disables animation movement");
}
void mappingsAndGuards() {
    Fixture f;
    const int expected[5][3]={{188,202,212},{189,203,204},{192,206,207},{39,40,41},{44,48,49}};
    for (int mode=0;mode<5;++mode) {
        f.host.context=mode>=3?mode-2:0; f.host.weapon=mode==2?7:0; f.host.twoHanded=mode==1;
        for (int i=0;i<3;++i)
            require(f.controller->specificAnimation(i==0?188:i==1?202:212)==expected[mode][i],
                    "Original movement-specific context/weapon mapping");
    }
    for (int context : {-1,3,100}) {
        f.host.context=context; f.host.calls.clear();
        require(f.controller->specificAnimation(202)==-1 &&
                f.host.calls==std::vector<std::string>{"context"}, "Unknown context returns-1 without weapon access");
    }
    f.host.context=0; f.host.weapon=0; f.host.twoHanded=false;
    f.state.movementFlags=0x800;
    f.controller->walkTo({10,20,30});
    require((f.state.movementFlags&0x800) && (f.state.movementFlags&255)==2 &&
            f.state.animation.animation==-1, "Animation rejection does not roll back original movement flags");
    bool threw=false; f.host.vehicle=true;
    try { f.controller->stop(); } catch(const std::logic_error&) { threw=true; }
    require(threw, "A real vehicle cannot be silently omitted");
    threw=false;
    try { RetailDirectPlayerController bad(f.world,*f.body,f.owner,f.state,f.host,nullptr); }
    catch(const std::invalid_argument&) { threw=true; }
    require(threw, "Real player identity is mandatory");
    for (bool run : {false,true}) {
        Fixture secondDeath;
        secondDeath.host.dieOnSecond=true;
        if (run) secondDeath.controller->runTo({123,456,789});
        else secondDeath.controller->walkTo({123,456,789});
        require(secondDeath.state.movementFlags==0x8000 &&
                secondDeath.state.destinationCm==RetailHeightPoint{123,456,789} &&
                secondDeath.host.requests.empty(), "Second live death guard preserves wrapper-only mutation");
    }
}
void destination() {
    Fixture f;
    f.controller->walkTo({100,0,999});
    f.state.destinationReachable=false;
    f.controller->checkDestination(true);
    require(!f.state.destinationReachable && f.state.adjustedDestinationCm==f.state.destinationCm,
            "Cinematic bypass copies destination without changing reachability");
    f.controller->checkDestination(false);
    require(f.state.destinationReachable && f.state.adjustedDestinationCm==f.state.destinationCm,
            "Real Box2D steering clear path composes with original CheckDestination");
    require(!f.controller->updateScriptedDestination() && f.state.animation.rootMotion &&
            (f.state.movementFlags&0x8000), "Exactly100cm does not complete; destination Z is unrelated");
    f.host.position={.25f,0,-9000};
    f.state.turnDirection={0,0,0};
    f.host.calls.clear();
    require(f.controller->updateScriptedDestination() && !(f.state.movementFlags&0x8000) &&
            (f.state.movementFlags&255)==1 && f.host.position==RetailHeightPoint{.25f,0,-9000},
            "Strictly inside100cm completes without teleport or height gate");
    require(f.host.calls==std::vector<std::string>{"reload","turn","position","context","weapon","two","select","clear"},
            "Completion preserves original reload/turn/stop/target order");
    f.state.movementFlags|=0x8000; f.state.turnDirection={1,0,0};
    f.host.calls.clear();
    require(f.controller->updateScriptedDestination() && f.state.turning && f.state.turnSpeed==8 &&
            f.host.requests.back().animation==201 && f.host.requests.back().loop &&
            !f.host.requests.back().rootMotion, "Nonzero saved direction starts exact8 turn after Stop");
    f.state.movementFlags=0;
    f.host.onReload=[&]{f.state.movementFlags=0x8004;};
    f.host.position={-1000,0,10};
    f.host.calls.clear();
    require(!f.controller->updateScriptedDestination() && f.host.calls[1]=="turn",
            "Reload callback changes live flags before scripted test");
    f.host.onReload={}; f.state.destinationCm[2]=std::numeric_limits<float>::infinity();
    f.host.position={100,0,10};
    require(!f.controller->updateScriptedDestination(), "Original inf-inf Z becomes NaN, never unconditional zero");
    f.state.turning=false; f.state.turnDirection={std::numeric_limits<float>::quiet_NaN(),0,0};
    f.controller->startTurn(99);
    require(!f.state.turning, "NaN turn length fails original strict positive branch");
}
void specificSelections(bool dump) {
    Fixture fixture;
    auto& host = fixture.host;
    const auto select = [&](int request, int context, int weapon, bool two) {
        host.context = context; host.weapon = weapon; host.twoHanded = two; host.calls.clear();
        return fixture.controller->specificAnimation(request);
    };
    require(select(0, 0, 0, true) == 205 && select(0, 0, 7, false) == 208,
            "Original zero request selects weapon-specific205/208");
    require(select(179, 2, 7, true) == 179 && host.calls == std::vector<std::string>{"context"},
            "Context2 preserves179 without weapon reads");
    require(select(198, 1, 0, true) == 198 && select(200, 2, 7, false) == 200,
            "Original alternate contexts preserve198/200 requests");
    require(select(165, 1, 7, true) == 36 && select(180, 2, 0, false) == 43,
            "Original additional context-specific mappings");
    require(select(180, 0, 7, true) == 171 && host.calls == std::vector<std::string>{"context", "weapon"},
            "Rocket branch returns before two-handed query");
    require(select(200, -1, 7, true) == -1 && host.calls == std::vector<std::string>{"context"},
            "Invalid context never reads weapon");
    require(select(166, 0, 7, true) == -1 && host.calls.empty(),
            "Unrecognized request returns original sentinel before context access");
    host.onWeaponType = [&] { host.twoHanded = true; };
    require(select(198, 0, 0, false) == 190 && host.calls == std::vector<std::string>{"context", "weapon", "two"},
            "Two-handed query observes current weapon state after type operation");
    host.onWeaponType = {};
    if (!dump) return;
    std::vector<int> requests;
    for (int request = -4; request <= 220; ++request) requests.push_back(request);
    requests.push_back(std::numeric_limits<int>::min()); requests.push_back(std::numeric_limits<int>::max());
    for (int context : {-2, -1, 0, 1, 2, 3, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
        for (int weapon : {0, 7, 8})
            for (bool two : {false, true})
                for (int request : requests) {
                    const auto result = select(request, context, weapon, two);
                    std::cout << "selection " << request << ' ' << context << ' ' << weapon << ' ' << two << ' ' << result;
                    for (const auto& call : host.calls) std::cout << ' ' << call;
                    std::cout << '\n';
                }
}
void animationBodyComposition() {
    Fixture f;
    f.controller->walkTo({1000,0,10});
    CityAnimationChannel root;
    root.transform = CityAnimationTransform::translation;
    root.keys = {{0, {0,0,0}}, {99, {99,198,297}}};
    const std::array<float,16> absolute{0,2,0,0, -3,0,0,0, 0,0,1,0, 9000,8000,7000,1};
    f.host.onRoot=[&](const RetailHeightPoint& value) {
        require(value==RetailHeightPoint{0,0,0} && close(f.owner.rootZCm,150) &&
                close(f.body->body()->GetLinearVelocity().x,-60) &&
                close(f.body->body()->GetLinearVelocity().y,20),
                "Root scene callback observes sampled rootZ and actual matrix-transformed body velocity");
    };
    const auto step=advanceRetailAnimation(f.state.animation,50,&root,absolute,true);
    f.controller->applyAnimationStep(step);
    require(f.state.animation.cursor==50 && close(f.state.animation.cachedRoot[0],50) &&
            close(f.state.animation.cachedRoot[1],100) && close(f.state.animation.cachedRoot[2],150),
            "Controller consumes genuine sampled phase/cache rather than estimated speed");
    struct NoVehicles final : RetailWorldVehicles {
        std::size_t size() const override { return 0; }
        void updateFrame(std::size_t, std::int32_t) override { throw std::logic_error("Fixture has no vehicle"); }
        void updatePhysics(std::size_t, float) override { throw std::logic_error("Fixture has no vehicle"); }
        void synchronizePhysics(std::size_t) override { throw std::logic_error("Fixture has no vehicle"); }
    } vehicles;
    RetailWorldStepState worldState;
    RetailWorldUpdate update(f.world,worldState);
    update.advance(17,vehicles);
    require(worldState.completedSteps==1 && close(f.body->body()->GetPosition().x,-1) &&
            close(f.body->body()->GetPosition().y,1.f/3.f),
            "Sampled root velocity drives actual body displacement through the single original fixed-step clock");
    f.host.onRoot={};
    f.body->body()->SetLinearVelocity(b2Vec2(7,8));
    f.state.animation.rootMotion=false;
    const auto held=advanceRetailAnimation(f.state.animation,1,&root,absolute,true);
    f.controller->applyAnimationStep(held);
    require(f.body->body()->GetLinearVelocity().x==7 && f.body->body()->GetLinearVelocity().y==8 &&
            close(f.owner.rootZCm,153), "Absent velocity intent preserves actual body velocity while root sample advances");
    f.host.calls.clear();
    f.controller->applyAnimationStep(RetailAnimationStep{});
    require(f.host.calls.empty(), "Unevaluated root does not synthesize scene or rootZ update");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--specific-dump") { specificSelections(true); return 0; }
        commands(); mappingsAndGuards(); destination(); specificSelections(false); animationBodyComposition();
        std::cout << "Direct player command/completion/native-body/steering checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
