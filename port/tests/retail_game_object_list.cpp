#include "retail_game_object_list.h"
#include "retail_physics.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Entity = RetailGameObjectListEntity;
using List = RetailGameObjectList;
using Manager = RetailGameObjectManagerLists;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> bool rejected(F operation) {
    try { operation(); } catch (const std::runtime_error&) { return true; }
    return false;
}
struct Owner;
struct Object final : Entity {
    Owner& owner;
    int id, category = 5, meta = 41;
    bool named = false, dead = false;
    std::string storedName;
    std::function<void()> updating, metatypeEffect;
    b2World* world = nullptr;
    b2Body* body = nullptr;
    Object(Owner& owner, int id) : owner(owner), id(id) {}
    ~Object() override;
    void update(int32_t) override;
    const char* name() const override;
    int32_t levelCategory() override;
    bool isDead() override;
    int32_t metatype() override;
    void setName(std::string_view) override;
};
struct Owner final : RetailGameObjectLifetime {
    std::vector<std::unique_ptr<Object>> objects;
    std::vector<std::string> events;
    std::function<void(Object&)> destroying;
    int nextId = 100;
    bool createNull = false;
    ~Owner() override { objects.clear(); } // Destructors still use the live event sink.
    Object& make(int id) { objects.push_back(std::make_unique<Object>(*this, id)); return *objects.back(); }
    void destroy(Entity& entity) noexcept override {
        auto& object = static_cast<Object&>(entity);
        events.push_back("destroy:" + std::to_string(object.id) + ":" + std::to_string(object.linked()));
        if (destroying) destroying(object);
        const auto found = std::find_if(objects.begin(), objects.end(), [&](const auto& value) { return value.get() == &entity; });
        if (found == objects.end()) std::terminate();
        objects.erase(found); // Actual canonical test object destruction, not deferred shared reset.
    }
    Entity* createFromMetatype(int32_t type) override {
        events.push_back("create:" + std::to_string(type));
        if (createNull) return nullptr;
        auto& object = make(nextId++); object.category = 3; object.meta = type; return &object;
    }
};
Object::~Object() {
    if (body) { world->DestroyBody(body); body = nullptr; }
    owner.events.push_back("destructor:" + std::to_string(id));
}
void Object::update(int32_t elapsed) { owner.events.push_back("update:" + std::to_string(id) + ":" + std::to_string(elapsed)); if (updating) updating(); }
const char* Object::name() const { owner.events.push_back("name:" + std::to_string(id)); return named ? storedName.c_str() : nullptr; }
int32_t Object::levelCategory() { owner.events.push_back("category:" + std::to_string(id) + ":" + std::to_string(category)); return category; }
bool Object::isDead() { owner.events.push_back("dead:" + std::to_string(id)); return dead; }
int32_t Object::metatype() { owner.events.push_back("metatype:" + std::to_string(id)); if (metatypeEffect) metatypeEffect(); return meta; }
void Object::setName(std::string_view value) { owner.events.push_back("setName:" + std::to_string(id)); storedName = value; named = true; }
std::vector<int> order(List& list) {
    std::vector<int> result;
    for (auto* object = list.first(); object; object = object->next()) result.push_back(static_cast<Object*>(object)->id);
    return result;
}
void row(const std::string& name, List& list, const Owner& owner, bool dump) {
    if (!dump) return;
    std::cout << "list " << name << " order";
    for (int id : order(list)) std::cout << ' ' << id;
    std::cout << " events";
    for (const auto& event : owner.events) std::cout << ' ' << event;
    std::cout << '\n';
}
void links(bool dump) {
    Owner owner; List list(owner), foreign(owner);
    auto& a = owner.make(1); auto& b = owner.make(2); auto& c = owner.make(3); auto& d = owner.make(4);
    a.preselectList(&foreign); require(!a.linked() && a.objectList() == &foreign, "Load preselection is not list membership");
    list.add(a, true); list.add(b, false); list.add(c, true);
    require(order(list) == std::vector<int>{1,2,3} && list.size() == 3 && !a.previous() && b.previous() == &a && c.previous() == &b && !c.next(), "Genuine append links");
    row("append", list, owner, dump);
    list.insertAfter(d, a); require(order(list) == std::vector<int>{1,4,2,3} && b.previous() == &d, "Middle insertion repairs both directions");
    row("insert-middle", list, owner, dump);
    require(list.remove(a) == &d && !a.linked() && !a.objectList() && !a.previous() && !a.next(), "Removal returns original next and clears entire hook");
    row("remove-head", list, owner, dump);
    list.remove(c); row("remove-tail", list, owner, dump);
    require(order(list) == std::vector<int>{4,2} && !b.next(), "Tail removal changes last anchor");
    require(rejected([&] { list.add(b, false); }) && rejected([&] { foreign.remove(b); }) &&
                rejected([&] { list.insertAfter(a, c); }) && rejected([&] { b.preselectList(&foreign); }), "Invalid membership never corrupts links");
    list.remove(d); list.remove(b); require(!list.first() && !list.size(), "Final removal resets both endpoints");
    list.add(a, false); list.remove(a); // Tail anchor is actually reusable.
}
void iteration(bool dump) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        Owner owner; List list(owner); auto& a=owner.make(1); auto& b=owner.make(2); auto& c=owner.make(3);
        list.add(a,false);list.add(b,false);list.add(c,false);
        if (mode == 0) { a.updating=[&] { list.remove(a); };list.updateGameObjects(123,false); }
        if (mode == 1) list.forEach([&](Entity& object,int32_t context) { owner.events.push_back("apply:"+std::to_string(static_cast<Object&>(object).id)+":"+std::to_string(context));list.remove(object); },true);
        if (mode == 2) list.updateGameObjects(123,true);
        require(owner.events == (mode == 0 ? std::vector<std::string>{"update:1:123","update:2:123","update:3:123"} : mode == 1 ? std::vector<std::string>{"apply:1:0"} : std::vector<std::string>{}), "Update caches next, ForEach rereads and skip emits no callback");
        row(mode==0?"update-captures-next":mode==1?"foreach-rereads-next":"skip-update",list,owner,dump);
    }
    for (bool update : {false, true}) {
        Owner owner;List list(owner);auto& a=owner.make(1);auto& b=owner.make(2);auto& c=owner.make(3);auto& d=owner.make(4);
        list.add(a,false);list.add(b,false);list.add(c,false);
        if (update) { c.updating=[&]{list.add(d,true);};list.updateGameObjects(123,false); }
        else list.forEach([&](Entity& entity,int32_t input){auto& object=static_cast<Object&>(entity);owner.events.push_back("apply:"+std::to_string(object.id)+":"+std::to_string(input));if(&object==&c)list.add(d,true);},true);
        require(owner.events.size()==(update?3u:4u)&&order(list)==std::vector<int>{1,2,3,4},"Update retains cached final next while ForEach visits callback-appended next");
        row(update?"update-append-cached-tail":"foreach-append-live-tail",list,owner,dump);
    }
    for (bool conditional : {false,true}) {
        Owner owner;List list(owner);auto& a=owner.make(1);auto& b=owner.make(2);auto& c=owner.make(3);
        list.add(a,false);list.add(b,false);list.add(c,false);int32_t context=0x12345678;
        const List::Condition condition=[&](Entity& entity,int32_t input) {
            auto& object=static_cast<Object&>(entity);owner.events.push_back("condition:"+std::to_string(object.id)+":"+std::to_string(input));
            if (!conditional && &object==&b) { list.remove(object);return false; }
            return object.id%2==0;
        };
        if (!conditional) require(!list.next(condition,a,true,&context), "Rejected removed candidate's current next is reread");
        else list.forEachIf([&](Entity& object,int32_t input) { owner.events.push_back("apply:2:"+std::to_string(input));list.remove(object); },condition,true);
        row(conditional?"foreachif-local-zero":"condition-rereads-removed-candidate",list,owner,dump);
        require(order(list)==std::vector<int>{1,3},"Callback mutation changed actual list");
    }
    for (int32_t capacity : {0,1,3}) {
        Owner owner;List list(owner);for(int i=1;i<=3;++i)list.add(owner.make(i),false);
        Entity* output[3]{};int32_t count=-1;
        const bool complete=list.fillList([&](Entity& entity,int32_t context) {auto& object=static_cast<Object&>(entity);owner.events.push_back("condition:"+std::to_string(object.id)+":"+std::to_string(context));return object.id%2==0;},capacity,output,count,true,0x13579bdf);
        require(complete==(capacity!=0) && count==(capacity?1:0) && (!capacity || static_cast<Object*>(output[0])->id==2), "Explicit nonzero FillList context/capacity boundary");
        row("fill-seeded-"+std::to_string(capacity),list,owner,dump);
    }
    Owner owner;List list(owner);auto& a=owner.make(1);auto& b=owner.make(2);auto& c=owner.make(3);list.add(a,false);list.add(b,false);list.add(c,false);
    int32_t context=7;
    auto* result=list.first([&](Entity& object,int32_t input) { if(&object==&a){require(input==7,"First context");context=9;return false;}require(input==9,"Reread context after callback");return true; },false,&context);
    require(result==&b,"First/next context reads stay live");
    int32_t count=-1;
    require(!list.fillList([](Entity&,int32_t){return true;},2,nullptr,count,false,77)&&count==2,"Count-only FillList can fill to capacity before detecting another match");
    require(rejected([&]{list.fillList([](Entity&,int32_t){return true;},-1,nullptr,count,false,0);})&&
                rejected([&]{list.forEach({},false);})&&rejected([&]{list.first({},false);}),"Required endpoints and capacity are checked");
    // Actual GetFirst rereads the head after a condition, including the original skip of new head.
    require(!list.first([&](Entity& object,int32_t){if(&object==&a){list.remove(a);return false;}return &object==&b;},false),"Rejected first condition follows CURRENT head's next, skipping replacement head");
    require(list.first([&](Entity& object,int32_t){list.remove(object);return true;},false)==&c,
            "Accepted first condition returns CURRENT replacement head, not the old argument");
    require(rejected([&]{list.first([&](Entity& object,int32_t){list.remove(object);return false;},false);})&&!list.first(),
            "Checked native error replaces original final-head NULL dereference after rejected condition");
}
void destruction(bool dump) {
    for (bool clearing : {false,true}) {
        Owner owner;List list(owner);auto& a=owner.make(1);auto& b=owner.make(2);auto& c=owner.make(3);list.add(a,false);list.add(b,false);list.add(c,false);
        unsigned guards=0;
        owner.destroying=[&](Object& object){require(object.linked()==clearing && (object.objectList()==&list)==clearing,"Exact unlink-before-delete versus still-linked clear identity");if(clearing){guards+=rejected([&]{list.first();});guards+=rejected([&]{list.remove(object);});guards+=rejected([&]{object.next();});}};
        if(clearing)list.clear();else require(list.removeAndDestroy(b)==&c,"Destroy returns cached next");
        require(!clearing||guards==9,"No reentrant clear walk/query/mutation through dead anchors");
        row(clearing?"clear-still-linked":"remove-before-destroy",list,owner,dump);
        owner.destroying={};
    }
}
void manager(bool dump) {
    Owner owner;Manager manager(owner);
    auto& a=owner.make(1);auto& b=owner.make(2);auto& c=owner.make(3);
    manager.add(a,0);manager.add(b,1);manager.add(c,2);
    for(int selector:{-3,0,1,2,9}) {
        const int first=static_cast<Object*>(manager.first(selector))->id;
        const int selected=&manager.getObjectList(selector)==&manager.getObjectList(0)?0:&manager.getObjectList(selector)==&manager.getObjectList(1)?1:2;
        require(first==(selector==0?1:selector==1?2:3)&&selected==(selector==0?0:selector==2?2:1),"Manager selector families remain different");
        if(dump)std::cout<<"selector "<<selector<<' '<<first<<' '<<selected<<'\n';
    }
    for(int category=0;category<17;++category)require(manager.typeCount(category)==0,"Original17 counts initialize zero");
    for(int i=0;i<65537;++i)manager.addGameObjectOfType(7);
    require(manager.typeCount(7)==65537,"Category counts retain full32-bit width");
    for(int i=0;i<65537;++i)manager.removeGameObjectOfType(7);
    manager.removeGameObjectOfType(5);require(manager.typeCount(5)==-1,"Negative original counts are not clamped");
    manager.addGameObjectOfType(5);manager.addGameObjectOfType(5);
    require(manager.removeAndDestroy(nullptr)==nullptr&&manager.typeCount(5)==1,"NULL remove does not touch counts");
    owner.destroying=[&](Object& object){require(manager.typeCount(5)==0&&!object.linked(),"Category decrement precedes unlink/destruction");};
    manager.removeAndDestroy(&b);owner.destroying={};
    require(rejected([&]{manager.typeCount(17);})&&rejected([&]{manager.addGameObjectOfType(-1);}),"Invalid category does not index native storage");
    a.setName("same");c.setName("same");owner.events.clear();
    require(manager.getGameObjectByName("same",false)==&a&&owner.events==std::vector<std::string>{"name:1"},"Exact case-sensitive name and one getter, list0 first");
    require(manager.getGameObjectByName("SAME",false)==nullptr,"No guessed case-insensitive entity names");
    manager.remove(a);require(manager.getGameObjectByName("same",false)==&c,"Manager name order list0 then2 then1");
    require(rejected([&]{manager.getGameObjectByName(std::string("x\0y",3),false);}),"Checked query C-string domain");
    manager.remove(c);
    std::vector<int> deaths;owner.destroying=[&](Object& object){deaths.push_back(object.id);};
    manager.add(a,0);manager.add(c,2);manager.add(owner.make(4),1);manager.clearLists();
    require(deaths==std::vector<int>{1,3,4},"Explicit manager clear order0,2,1");owner.destroying={};
    {
        Manager scope(owner);scope.add(owner.make(5),0);scope.add(owner.make(6),2);scope.add(owner.make(7),1);
        owner.destroying=[&](Object& object){deaths.push_back(object.id);};
    }
    require(deaths==std::vector<int>{1,3,4,7,6,5},"Embedded manager list destruction order1,2,0");owner.destroying={};
}
void respawn() {
    Owner owner;Manager manager(owner);auto& old=owner.make(1);old.category=3;old.dead=true;old.meta=44;old.setName("vehicle");manager.add(old,2);manager.addGameObjectOfType(3);owner.events.clear();
    // Borrowed input aliases old name; native owns query value before destruction.
    auto* result=manager.getGameObjectByName(old.storedName,true);
    require(result&&static_cast<Object*>(result)->id==100&&result->objectList()==&manager.getObjectList(0)&&manager.typeCount(3)==0,"Real respawn sequence returns new canonical entity, add0 does not increment counts");
    require(owner.events==std::vector<std::string>{"name:1","category:1:3","dead:1","metatype:1","category:1:3","destroy:1:0","destructor:1","create:44","setName:100"},"Exact category/death/metatype/decrement/unlink/teardown/create/setName/add order");
    auto& live=static_cast<Object&>(*result);owner.events.clear();require(manager.getGameObjectByName("vehicle",true)==&live&&owner.events==std::vector<std::string>{"name:100","category:100:3","dead:100"},"Alive category3 is returned without respawn");
    live.category=5;live.dead=true;owner.events.clear();require(manager.getGameObjectByName("vehicle",true)==&live&&owner.events==std::vector<std::string>{"name:100","category:100:5"},"Only actual category3 queries death");
    live.category=3;live.metatypeEffect=[&]{live.category=5;};manager.addGameObjectOfType(5);owner.events.clear();owner.createNull=true;
    require(rejected([&]{manager.getGameObjectByName("vehicle",true);})&&manager.typeCount(5)==0&&!manager.first(0),"Fresh category after metatype and checked NULL factory preserve partial source order, no rollback/fallback");
}
void respawnTrace(bool dump) {
    for(bool changed:{false,true}) {
        Owner owner;Manager manager(owner);auto& old=owner.make(1);old.category=3;old.dead=true;old.meta=44;old.setName("vehicle");manager.add(old,2);manager.addGameObjectOfType(changed?5:3);
        if(changed)old.metatypeEffect=[&]{old.category=5;};
        owner.events.clear();auto* result=manager.getGameObjectByName("vehicle",true);
        require(result&&static_cast<Object*>(result)->id==100&&manager.typeCount(changed?5:3)==0,"Original respawn final identity and fresh loaded category decrement");
        if(dump){std::cout<<"respawn "<<changed<<" result 100 count 0 events";for(const auto& event:owner.events)std::cout<<' '<<event;std::cout<<'\n';}
    }
}
struct ShapeListener final : b2DestructionListener, b2ContactListener {
    std::vector<bool> linked, contactLinked;
    void SayGoodbye(b2Joint*) override { std::terminate(); }
    void SayGoodbye(b2Shape* shape) override {linked.push_back(static_cast<Object*>(shape->GetBody()->GetUserData())->linked());}
    void Remove(const b2ContactPoint* point) override {
        auto* object=static_cast<Object*>(point->shape1->GetBody()->GetUserData());
        if(!object)object=static_cast<Object*>(point->shape2->GetBody()->GetUserData());
        if(!object)std::terminate();
        contactLinked.push_back(object->linked());
    }
};
struct Filter final : b2ContactFilter {
    unsigned calls=0;
    bool ShouldCollide(b2Shape* a,b2Shape* b) override {++calls;return b2ContactFilter::ShouldCollide(a,b);}
};
void bodyLifetime() {
    b2AABB bounds;bounds.lowerBound.Set(-100,-100);bounds.upperBound.Set(100,100);
    ShapeListener listener;Filter filter;b2World world(bounds,{0,0},true);world.SetDestructionListener(&listener);world.SetContactListener(&listener);world.SetContactFilter(&filter);
    b2BodyDef obstacleDefinition;auto* obstacle=world.CreateBody(&obstacleDefinition);b2CircleDef obstacleShape;obstacleShape.radius=1;obstacle->CreateShape(&obstacleShape);
    Owner owner;List list(owner);
    for(bool clearing:{false,true}) {
        auto& object=owner.make(clearing?2:1);object.world=&world;b2BodyDef body;object.body=world.CreateBody(&body);object.body->SetUserData(&object);b2CircleDef shape;shape.radius=.5f;shape.density=1;object.body->CreateShape(&shape);object.body->SetMassFromShapes();list.add(object,false);
        world.Step(.025f,10);require(world.GetContactCount()!=0,"Actual live Box2D contact before canonical teardown");
        const auto calls=filter.calls;
        if(!clearing){list.remove(object);require(world.GetBodyCount()==3&&filter.calls==calls,"Plain list removal does not refilter or destroy genuine body");list.add(object,false);require(filter.calls==calls,"List append does not invent a refilter");}
        if(clearing)list.clear();else list.removeAndDestroy(object);
        require(world.GetBodyCount()==2,"Actual native body is destroyed inside canonical teardown");
    }
    require(listener.linked==std::vector<bool>{false,true}&&listener.contactLinked==std::vector<bool>{false,true},"Real Box2D contact/shape destruction callbacks see source-correct hook lifetime before actual object free");
    world.SetDestructionListener(nullptr);world.SetContactListener(nullptr);world.DestroyBody(obstacle);
}
}
int main(int argc,char** argv) {
    try {
        const bool dump=argc>1&&std::string(argv[1])=="--dump";
        links(dump);iteration(dump);destruction(dump);manager(dump);respawn();respawnTrace(dump);bodyLifetime();
        std::cout<<"Actual intrusive list/callback/lifetime/selector tests passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
