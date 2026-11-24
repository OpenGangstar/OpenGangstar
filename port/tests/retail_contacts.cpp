#include "retail_contacts.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void near(float actual, float expected, const char* message) { require(std::abs(actual - expected) < .001f, message); }
struct Record {
    bool npcBound, vehicleBound, rocketBound, characterBound;
    RetailContactNpcState npc{0, false, 0, {0,0,0}};
    RetailContactVehicleState vehicle{0, false, 0, nullptr, 0};
    RetailContactRocketState rocket{nullptr, false};
    RetailContactCharacterState character{nullptr, 0};
    RetailContactVector position{10,20,30}, front{1,0,0}, right{0,1,0}, destination{0,0,0};
    float speed = 30;
    bool dead = false, dying = false;
    int health = 100, metatype = 0;
};
struct Event {
    std::string name;
    RetailPhysicsOwnerState* actor;
    RetailContactVector vector;
    int integer;
    float amount;
};
struct Host : RetailContactHost {
    std::map<RetailPhysicsOwnerState*, Record> records;
    std::vector<Event> events;
    RetailPhysicsOwnerState* player = nullptr;
    RetailPhysicsOwnerState* currentVehicle = nullptr;
    RetailPhysicsOwnerState* mutateMetatypeOnCollision = nullptr;
    RetailPhysicsOwnerState* mutateCategoryOnCollision = nullptr;
    int healthAfterHit = 100;
    unsigned playerVehicleReads = 0;
    Record& state(RetailPhysicsOwnerState& owner) {
        auto found=records.find(&owner);
        if(found==records.end()) throw std::logic_error("Missing genuine contact-owner binding.");
        return found->second;
    }
    RetailContactNpcState& npc(RetailPhysicsOwnerState& owner) override {
        auto& value=state(owner); if(!value.npcBound) throw std::logic_error("Missing NPC role."); return value.npc;
    }
    RetailContactVehicleState& vehicle(RetailPhysicsOwnerState& owner) override {
        auto& value=state(owner); if(!value.vehicleBound) throw std::logic_error("Missing vehicle role."); return value.vehicle;
    }
    RetailContactRocketState& rocket(RetailPhysicsOwnerState& owner) override {
        auto& value=state(owner); if(!value.rocketBound) throw std::logic_error("Missing rocket role."); return value.rocket;
    }
    RetailContactCharacterState& character(RetailPhysicsOwnerState& owner) override {
        auto& value=state(owner); if(!value.characterBound) throw std::logic_error("Missing character role."); return value.character;
    }
    RetailPhysicsOwnerState* playerOwner() override { return player; }
    RetailPhysicsOwnerState* playerVehicle() override { ++playerVehicleReads; return currentVehicle; }
    RetailContactVector position(RetailPhysicsOwnerState& owner) override { return state(owner).position; }
    RetailContactVector front(RetailPhysicsOwnerState& owner) override { return state(owner).front; }
    RetailContactVector right(RetailPhysicsOwnerState& owner) override { return state(owner).right; }
    float speed(RetailPhysicsOwnerState& owner) override { return state(owner).speed; }
    bool dead(RetailPhysicsOwnerState& owner) override { return state(owner).dead; }
    bool dying(RetailPhysicsOwnerState& owner) override { return state(owner).dying; }
    int health(RetailPhysicsOwnerState& owner) override { return state(owner).health; }
    int metatype(RetailPhysicsOwnerState& owner) override { return state(owner).metatype; }
    RetailContactVector destination(RetailPhysicsOwnerState& owner) override { return state(owner).destination; }
    void add(std::string name, RetailPhysicsOwnerState* owner=nullptr, RetailContactVector vector={0,0,0}, int integer=0, float amount=0) {
        events.push_back({std::move(name),owner,vector,integer,amount});
    }
    void setDestination(RetailPhysicsOwnerState& owner, RetailContactVector value, int limit) override {
        state(owner).destination=value; add("destination",&owner,value,limit);
    }
    void runToLocationBase(RetailPhysicsOwnerState& owner, RetailContactVector value) override { add("base-run",&owner,value); }
    void setTarget(RetailPhysicsOwnerState& owner, RetailPhysicsOwnerState&) override { add("target",&owner); }
    void hitByCar(RetailPhysicsOwnerState& owner,int direction,float amount,RetailPhysicsOwnerState& vehicle) override {
        require(character(owner).lastHitVehicle==&vehicle,"hitByCar preceded the original lastHitVehicle store.");
        state(owner).health=healthAfterHit;add("hit",&owner,{0,0,0},direction,amount);
    }
    void lockPlayerControls() override { add("lock"); }
    void playerOnObjectKilled(RetailPhysicsOwnerState& owner) override { add("killed",&owner); }
    void playHorn(RetailPhysicsOwnerState& owner) override { add("horn",&owner); }
    void explode(RetailPhysicsOwnerState& owner,bool effect) override { rocket(owner).exploded92=true;add("explode",&owner,{0,0,0},effect); }
    void collision(RetailPhysicsOwnerState& owner,RetailContactVector vector,int other) override {
        add("collision",&owner,vector,other);
        if(mutateMetatypeOnCollision) { state(*mutateMetatypeOnCollision).metatype=38;mutateMetatypeOnCollision=nullptr; }
        if(mutateCategoryOnCollision) { mutateCategoryOnCollision->levelCategory=5;mutateCategoryOnCollision=nullptr; }
    }
    void raisePlayerEvent4() override { add("event22/4"); }
};
struct Owned {
    RetailPhysicsOwnerState state;
    RetailPhysicsBodyData data;
    explicit Owned(int category):state{category,0,0,0},data{&state,std::nullopt,{0,200,0,0},false} {}
};
b2AABB worldBounds() { b2AABB box;box.lowerBound.Set(-2000,-2000);box.upperBound.Set(2000,2000);return box; }
struct Fixture {
    Host host;
    std::list<Owned> owners;
    RetailContactListener listener{host};
    b2World world{worldBounds(),b2Vec2(0,0),true};
    Fixture() { world.SetContactListener(&listener);world.SetContinuousPhysics(false); }
    RetailPhysicsOwnerState& actor(int category) {
        owners.emplace_back(category);auto& owner=owners.back().state;
        host.records.emplace(&owner,Record{category==5,category==3,category==12,category==5||category==8});
        return owner;
    }
    b2Shape* shape(RetailPhysicsOwnerState* owner,bool sensor=false,bool dynamic=false,float x=0,float y=0) {
        b2BodyDef body;body.position.Set(x,y);body.allowSleep=false;
        if(owner) for(auto& entry:owners) if(&entry.state==owner) body.userData=&entry.data;
        auto* result=world.CreateBody(&body);b2CircleDef circle;circle.radius=.5f;circle.isSensor=sensor;circle.density=dynamic?1.f:0.f;
        auto* shape=result->CreateShape(&circle);if(dynamic)result->SetMassFromShapes();return shape;
    }
    b2ContactPoint point(b2Shape* a,b2Shape* b) {
        b2ContactPoint point{};point.shape1=a;point.shape2=b;point.normal.Set(1,0);point.velocity.Set(10,0);return point;
    }
};

void checkNpcRetargetAndPersist()
{
    for(bool reverse:{false,true}) {
        Fixture f;auto& npc=f.actor(5);auto& record=f.host.state(npc);record.npc.flags356=4;
        auto* a=f.shape(&npc);auto* b=f.shape(nullptr);auto p=f.point(reverse?b:a,reverse?a:b);p.normal.Set(1,0);
        f.listener.Add(&p);require(f.host.events.size()==2,"NPC Add failed to set destination then base-run.");
        near(record.destination.x,10,"NPC tangent changed X.");near(record.destination.y,-480,"NPC tangent changed Y.");
        require(f.host.events[0].integer==100,"NPC destination limit was fabricated.");
        record.front={0,1,0};f.host.events.clear();f.listener.Add(&p);
        near(record.destination.y,520,"Negative front dot failed to reverse NPC tangent.");
        f.host.events.clear();f.listener.Persist(&p);require(f.host.events.empty(),"First Persist cache write retargeted prematurely.");
        near(record.npc.accumulatedNormal.x,1,"Persist used velocity or negated second-shape normal.");
        f.listener.Persist(&p);near(record.destination.x,1010,"Persist did not accumulate and scale normal by500.");
        require(f.host.events.size()==2,"Subsequent Persist failed to reissue base run.");
        record.npc.hasPathToFollow=true;f.host.events.clear();f.listener.Add(&p);f.listener.Persist(&p);
        require(f.host.events.empty(),"A pathed NPC was redirected.");
        record.npc.hasPathToFollow=false;record.npc.state428=3;f.listener.Add(&p);f.listener.Persist(&p);
        require(f.host.events.empty(),"Ineligible NPC state was redirected.");
        record.npc.state428=2;record.npc.flags356=0;record.npc.accumulatedNormal={.000001f,-.000001f,0};
        f.listener.Persist(&p);require(f.host.events.empty(),"Inclusive epsilon-zero cache was treated as accumulated.");
        record.npc.accumulatedNormal={std::nextafter(.000001f,1.f),0,0};f.listener.Persist(&p);
        require(f.host.events.size()==1,"Beyond-epsilon cache failed to retarget without run flag.");
    }
    Fixture f;auto& player=f.actor(8);auto p=f.point(f.shape(&player),f.shape(nullptr));f.host.player=&player;
    f.listener.Add(&p);f.listener.Persist(&p);require(f.host.events.empty(),"Player category entered NPC retargeting.");
}

void checkRocketPrecedence()
{
    for(bool reverse:{false,true}) {
        Fixture f;auto& rocket=f.actor(12);auto& car=f.actor(3);
        auto* r=f.shape(&rocket);auto* c=f.shape(&car);auto p=f.point(reverse?c:r,reverse?r:c);
        f.listener.Add(&p);require(f.host.events.size()==1&&f.host.events[0].name=="explode"&&f.host.events[0].integer==0,
                "Rocket/live-vehicle branch fell through to ordinary contacts.");
        require(f.host.vehicle(car).rocketContactWord924==5000,"Rocket vehicle contact word was not set before explosion.");
        f.host.events.clear();f.host.rocket(rocket).exploded92=false;f.host.rocket(rocket).launcher=&car;
        f.listener.Add(&p);require(f.host.events.empty(),"Rocket collided with its own launcher.");
        f.host.rocket(rocket).launcher=nullptr;f.host.state(car).dead=true;f.listener.Add(&p);
        require(f.host.events.size()==1&&f.host.events[0].integer==1,"Dead vehicle failed to select explosion effect.");
        f.host.vehicle(car).sensorContactCount=3;
        auto* sensor=f.shape(&car,true);p.shape1=reverse?sensor:r;p.shape2=reverse?r:sensor;f.listener.Remove(&p);
        require(f.host.vehicle(car).sensorContactCount==3,"Category12 failed to suppress Remove sensor handling.");
        p.shape1=reverse?c:r;p.shape2=reverse?r:c;f.host.events.clear();f.host.state(car).dead=false;
        f.listener.Add(&p);require(f.host.events.size()==1&&f.host.events[0].name=="collision"&&f.host.events[0].integer==12,
                "Already exploded rocket incorrectly suppressed the later vehicle collision branch.");
    }
    Fixture f;auto& rocket=f.actor(12);auto& car=f.actor(3);
    auto p=f.point(f.shape(&rocket),f.shape(&car,true));f.listener.Add(&p);
    require(f.host.events.empty()&&f.host.vehicle(car).sensorContactCount==0,"Rocket precedence lost owned-sensor exclusion.");
    p.shape2=f.shape(nullptr,true);f.listener.Add(&p);
    require(f.host.events.size()==1&&f.host.events[0].integer==1,"Ownerless sensor incorrectly suppressed rocket explosion.");
    Fixture pair;auto& a=pair.actor(12);auto& b=pair.actor(12);auto both=pair.point(pair.shape(&a),pair.shape(&b));
    pair.listener.Add(&both);require(pair.host.events.size()==1&&pair.host.events[0].actor==&a,"Second rocket overrode first-rocket precedence.");
    pair.host.events.clear();pair.listener.Add(&both);require(pair.host.events.size()==1&&pair.host.events[0].actor==&b,"Exploded first rocket failed to continue into second-rocket branch.");
}

void checkSensorsAndRemove()
{
    for(bool reverse:{false,true}) {
        Fixture f;auto& car=f.actor(3);auto& player=f.actor(8);f.host.player=&player;
        f.host.vehicle(car).flags344=0x800;
        auto* sensor=f.shape(&car,true);auto* playerShape=f.shape(&player);auto p=f.point(reverse?playerShape:sensor,reverse?sensor:playerShape);
        f.listener.Add(&p);require(f.host.vehicle(car).sensorContactCount==1&&f.host.events.size()==1&&f.host.events[0].name=="horn",
                "Vehicle sensor failed to increment and horn for genuine player.");
        f.listener.Remove(&p);require(f.host.vehicle(car).sensorContactCount==0,"Sensor Remove failed to balance count.");
        f.host.events.clear();f.host.vehicle(car).turnSoundPlaying=true;f.listener.Add(&p);
        require(f.host.vehicle(car).sensorContactCount==1&&f.host.events.empty(),"Turn-sound horn guard failed or skipped count.");
        f.host.vehicle(car).turnSoundPlaying=false;f.host.vehicle(car).flags344=0;f.listener.Add(&p);
        require(f.host.events.empty(),"Missing vehicle flag did not suppress horn.");
        f.host.vehicle(car).flags344=0x800;f.host.state(car).dead=true;f.listener.Add(&p);require(f.host.events.empty(),"Dead vehicle played horn.");
    }
    Fixture f;auto& car=f.actor(3);auto& other=f.actor(8);auto& actual=f.actor(8);f.host.player=&actual;
    auto p=f.point(f.shape(&car,true),f.shape(&other));f.host.vehicle(car).flags344=0x800;f.listener.Add(&p);
    require(f.host.events.empty(),"Category8 was substituted for genuine player identity.");
    f.host.currentVehicle=&other;f.listener.Add(&p);require(f.host.events.size()==1,"Player's current vehicle did not trigger horn.");
    p.shape2=f.shape(&other,true);const auto old=f.host.vehicle(car).sensorContactCount;f.host.events.clear();f.listener.Add(&p);f.listener.Remove(&p);
    require(f.host.vehicle(car).sensorContactCount==old&&f.host.events.empty(),"Two sensors changed contact count.");
    // Remove relies on constructor-provided vehicle ownership, not a new category3 gate.
    car.levelCategory=7;p.shape2=f.shape(&other);f.listener.Remove(&p);
    require(f.host.vehicle(car).sensorContactCount==old-1,"Remove invented a category3 prerequisite.");
}

void checkCharacterVehicle()
{
    const b2Vec2 velocities[]={{10,0},{-10,0},{0,10},{0,-10},{10,10}};
    const int expected[]={0,1,2,3,0};
    for(bool reverse:{false,true}) for(int side=0;side<5;++side) {
        Fixture f;auto& npc=f.actor(5);auto& car=f.actor(3);auto& player=f.actor(8);f.host.player=&player;
        f.host.vehicle(car).driver=&player;f.host.healthAfterHit=-1;
        auto* n=f.shape(&npc);auto* c=f.shape(&car);auto p=f.point(reverse?c:n,reverse?n:c);p.velocity=velocities[side];f.listener.Add(&p);
        require(f.host.events.size()==4&&f.host.events[0].name=="target"&&f.host.events[1].name=="hit"&&
                f.host.events[2].name=="killed"&&f.host.events[3].name=="collision", "Character/vehicle operation order changed.");
        require(f.host.events[1].integer==expected[side],"Strongest contact-direction projection or tie order changed.");
        near(f.host.events[1].amount,(std::sqrt(p.velocity.x*p.velocity.x+p.velocity.y*p.velocity.y)/1000.f)*3600.f,"Hit amount changed conversion order.");
        f.host.events.clear();f.listener.Add(&p);require(f.host.events.size()==1&&f.host.events[0].name=="collision","Existing last-hit vehicle did not suppress repeated damage.");
    }
    for(int guard=0;guard<7;++guard) {
        Fixture f;auto& npc=f.actor(5);auto& car=f.actor(3);auto p=f.point(f.shape(&npc),f.shape(&car));
        auto& record=f.host.state(npc);
        if(guard==0)f.host.state(car).speed=18;
        if(guard==1)f.host.state(car).speed=-30;
        if(guard==2)p.velocity.Set(5,0);
        if(guard==3)record.dying=true;
        if(guard==4)record.dead=true;
        if(guard==5){npc.levelCategory=8;record.character.playerWord372=1;}
        if(guard==6)record.character.lastHitVehicle=&car;
        f.listener.Add(&p);require(f.host.events.size()==1&&f.host.events[0].name=="collision","Strict18/signed-speed or character-state guard failed.");
    }
    Fixture f;auto& player=f.actor(8);auto& car=f.actor(3);f.host.player=&player;auto p=f.point(f.shape(&player),f.shape(&car));
    f.host.state(car).speed=std::nextafter(18.f,100.f);p.velocity.Set(std::nextafter(5.f,100.f),0);f.listener.Add(&p);
    require(f.host.events.size()==3&&f.host.events[0].name=="lock"&&f.host.events[1].name=="hit"&&f.host.events[2].name=="collision",
            "Player controls or positive threshold boundary used NPC damage actions.");
    Fixture unrelated;auto& npc=unrelated.actor(5);auto& otherCar=unrelated.actor(3);auto& genuine=unrelated.actor(8);auto& fake=unrelated.actor(8);
    unrelated.host.player=&genuine;unrelated.host.vehicle(otherCar).driver=&fake;unrelated.host.healthAfterHit=-1;
    auto q=unrelated.point(unrelated.shape(&npc),unrelated.shape(&otherCar));unrelated.listener.Add(&q);
    require(unrelated.host.events.size()==3&&unrelated.host.events[1].name=="hit"&&unrelated.host.events[2].name=="collision",
            "Non-player driver incorrectly issued genuine player's killed notification.");
}

void checkVehicleProjectionAndEvents()
{
    Fixture f;auto& first=f.actor(3);auto& second=f.actor(3);auto p=f.point(f.shape(&first),f.shape(&second));p.velocity.Set(3,4);p.normal.Set(.6f,.8f);
    f.host.currentVehicle=&second;f.host.state(first).metatype=38;f.listener.Add(&p);
    require(f.host.events.size()==3&&f.host.events[0].name=="collision"&&f.host.events[1].name=="event22/4"&&f.host.events[2].name=="collision",
            "Vehicle collision/event precedence changed.");
    near(f.host.events[0].vector.x,-3,"First vehicle normal projection sign changed.");near(f.host.events[0].vector.y,-4,"First vehicle projection Y changed.");
    near(f.host.events[2].vector.x,3,"Second vehicle normal projection sign changed.");near(f.host.events[2].vector.y,4,"Second vehicle projection Y changed.");
    require(f.host.events[0].integer==3&&f.host.events[2].integer==3,"Vehicle collision lost live other s_type category.");
    f.host.events.clear();f.host.state(first).metatype=0;f.host.currentVehicle=&first;f.host.mutateMetatypeOnCollision=&second;f.listener.Add(&p);
    require(f.host.events.size()==3&&f.host.events[1].name=="event22/4","Earlier collision side effects were replaced by a stale metadata snapshot.");
    f.host.events.clear();f.host.state(second).metatype=0;const auto reads=f.host.playerVehicleReads;f.listener.Add(&p);
    require(f.host.events.size()==2&&f.host.playerVehicleReads==reads,"Ordinary vehicle pair required an unrelated player-vehicle binding.");

    Fixture mutation;auto& a=mutation.actor(3);auto& b=mutation.actor(3);
    mutation.host.state(a).metatype=38;mutation.host.currentVehicle=&b;mutation.host.mutateCategoryOnCollision=&a;
    auto pair=mutation.point(mutation.shape(&a),mutation.shape(&b));pair.velocity.Set(3,4);pair.normal.Set(.6f,.8f);
    mutation.listener.Add(&pair);
    require(a.levelCategory==5&&mutation.host.events.size()==3&&
            mutation.host.events[0].name=="collision"&&mutation.host.events[0].integer==3&&
            mutation.host.events[1].name=="event22/4"&&
            mutation.host.events[2].name=="collision"&&mutation.host.events[2].integer==5,
            "A callback's live category change discarded first-vehicle dispatch history or its live second-collision category.");
}

void checkRealWorldCallbacksAndErrors()
{
    Fixture f;auto& npc=f.actor(5);f.host.npc(npc).flags356=4;
    auto* body=f.shape(&npc,false,true,-.8f,0)->GetBody();f.shape(nullptr,false,false,0,0);body->SetLinearVelocity(b2Vec2(1,0));
    for(int i=0;i<15;++i)f.world.Step(1.f/60,10);
    require(!f.host.events.empty()&&f.host.events[0].name=="destination","Real Box2D contacts did not invoke the installed Add policy.");
    require(f.host.npc(npc).accumulatedNormal.x!=0,"Real Box2D Persist did not populate native collision cache.");
    Fixture sensors;auto& car=sensors.actor(3);auto& player=sensors.actor(8);sensors.host.player=&player;
    auto* sensor=sensors.shape(&car,true);auto* actor=sensors.shape(&player,false,true,.2f,0);sensors.world.Step(1.f/60,10);
    require(sensors.host.vehicle(car).sensorContactCount==1,"Real sensor Add did not increment count.");
    require(actor->GetBody()->SetXForm(b2Vec2(10,0),0),"Sensor fixture left native world.");sensors.world.Step(1.f/60,10);
    require(sensors.host.vehicle(car).sensorContactCount==0,"Real sensor Remove did not balance count.");
    (void)sensor;
    Fixture missing;auto& actorOwner=missing.actor(5);auto p=missing.point(missing.shape(&actorOwner),missing.shape(nullptr));missing.host.records.erase(&actorOwner);
    bool rejected=false;try{missing.listener.Add(&p);}catch(const std::logic_error&){rejected=true;}
    require(rejected,"Missing mandatory native NPC binding silently consumed Add.");
    missing.listener.Result(nullptr); // This original method is genuinely empty.
}
}

int main()
{
    try {
        checkNpcRetargetAndPersist();checkRocketPrecedence();checkSensorsAndRemove();checkCharacterVehicle();
        checkVehicleProjectionAndEvents();checkRealWorldCallbacksAndErrors();
        std::cout<<"Retail native contact policy precedence, roles, damage, sensors, NPC state and real callbacks passed.\n";
    }catch(const std::exception&error){std::cerr<<error.what()<<'\n';return 1;}
}
