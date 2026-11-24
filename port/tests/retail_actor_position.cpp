#include "retail_actor_position.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
uint32_t bits(float value)
{
    uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
b2AABB bounds()
{
    b2AABB result;
    result.lowerBound.Set(-2000, -2000); result.upperBound.Set(2000, 2000);
    return result;
}
std::shared_ptr<RetailActorSceneSource> source(bool translated)
{
    auto result = std::make_shared<RetailActorSceneSource>();
    result->model.path = "./position-fixture.bdae";
    CityNode node{};
    node.name = "parent"; node.parent = -1;
    if (translated) node.translation = {10, 20, 30};
    node.rotation = {0, 0, 0, 1}; node.scale = {1, 1, 1};
    result->model.nodes.push_back(node);
    node.name = "actor"; node.parent = 0; node.translation = {1, 2, 3};
    result->model.nodes.push_back(node);
    node.name = "shadow-node"; node.parent = -1; node.translation = {};
    result->model.nodes.push_back(node);
    result->model.visualScenes.push_back({"visual", "visual", 0, 3});
    return result;
}
struct Filter : b2ContactFilter {
    unsigned calls = 0;
    bool ShouldCollide(b2Shape* a, b2Shape* b) override
    {
        ++calls;
        return b2ContactFilter::ShouldCollide(a, b);
    }
};
b2Body* body(b2World& world, b2Vec2 position)
{
    b2BodyDef definition;
    definition.position = position;
    auto* result = world.CreateBody(&definition);
    b2PolygonDef shape;
    shape.SetAsBox(.1f, .1f); shape.density = 1;
    result->CreateShape(&shape); result->SetMassFromShapes();
    return result;
}
void placement(bool dump)
{
    for (bool parent : {false, true})
        for (bool present : {false, true}) {
            Filter filter;
            b2World world(bounds(), b2Vec2(0, 0), true);
            world.SetContactFilter(&filter);
            auto* actual = body(world, {1.25f, -2.5f});
            (void)body(world, {1.25f, -2.5f});
            actual->SetLinearVelocity({3, 4}); actual->SetAngularVelocity(.7f);
            RetailActorScene scene(source(parent));
            auto& actor = *scene.root().findUid("actor");
            auto& shadow = *scene.root().findUid("shadow-node");
            shadow.setRotation({.1f, .2f, .3f, .4f}); shadow.updateAbsolutePosition(false);
            const auto cached = shadow.absolute();
            const auto rotation = shadow.rotation();
            RetailActorFloorState floor; floor.heightCm = 40;
            RetailHeightState height;
            filter.calls = 0;
            updateRetailActorPosition(actor, shadow, present ? actual : nullptr, floor, height, {0, 0, .6f, .8f});
            const RetailHeightPoint expected = present ?
                RetailHeightPoint{125.f + (parent ? 10 : 0), -250.f + (parent ? 20 : 0), parent ? 63.f : 3.f} :
                RetailHeightPoint{parent ? 11.f : 1.f, parent ? 22.f : 2.f, parent ? 33.f : 3.f};
            require(actor.position() == expected && actor.absolute()[12] == expected[0] &&
                    actor.absolute()[13] == expected[1] && actor.absolute()[14] == expected[2],
                    "Actual body readback and qualified base cached/world-as-local order");
            require(actor.rotation() == RetailSceneQuaternion{0, 0, .6f, .8f} &&
                    shadow.position() == RetailHeightPoint{expected[0], expected[1], 43} &&
                    shadow.rotation() == rotation &&
                    std::memcmp(cached.data(), shadow.absolute().data(), sizeof cached) == 0,
                    "Actor setter is dirty, retained floor drives shadow, absolute shadow cache stays stale");
            require(actual->GetPosition().x == 1.25f && actual->GetPosition().y == -2.5f &&
                    actual->GetLinearVelocity().x == 3 && actual->GetLinearVelocity().y == 4 &&
                    actual->GetAngularVelocity() == .7f && filter.calls == 0,
                    "Readback does not move/refilter/zero the actual native body");
            if (dump) {
                std::cout << "position " << parent << ' ' << present;
                for (auto value : actor.position()) std::cout << ' ' << bits(value);
                for (size_t axis = 12; axis < 15; ++axis) std::cout << ' ' << bits(actor.absolute()[axis]);
                for (auto value : shadow.position()) std::cout << ' ' << bits(value);
                std::cout << '\n';
            }
        }
}
void postworld(bool dump)
{
    b2World world(bounds(), b2Vec2(0, 0), true);
    auto* player = body(world, {0, 0});
    for (uint32_t flags : {0u, 0x400u})
        for (int animation : {-1, 162, 202}) {
            player->PutToSleep();
            player->SetLinearVelocity({123, -456}); player->SetAngularVelocity(77);
            RetailAnimationClock clock; clock.animation = animation;
            const bool update = finishRetailPlayerPhysics(*player, flags, clock);
            const bool expected = !(flags & 0x400) || animation == 202;
            require(update == expected && bits(player->GetLinearVelocity().x) == 0 &&
                    bits(player->GetLinearVelocity().y) == 0 && player->GetAngularVelocity() == 77 && player->IsSleeping(),
                    "Actual player linear-only reset/down gate without steps, angular change or wake");
            if (dump) std::cout << "postworld " << flags << ' ' << animation << ' '
                << bits(player->GetLinearVelocity().x) << ' ' << bits(player->GetLinearVelocity().y) << ' '
                << bits(player->GetAngularVelocity()) << ' ' << update << '\n';
        }
    player->WakeUp(); player->SetLinearVelocity({2, -1}); player->SetAngularVelocity(0);
    world.Step(.025f, 10);
    const auto stepped = player->GetPosition();
    RetailActorScene scene(source(false));
    auto& actor = *scene.root().findUid("actor");
    RetailActorFloorState floor; floor.heightCm = 40;
    RetailHeightState height;
    (void)finishRetailPlayerPhysics(*player, 0, RetailAnimationClock{});
    updateRetailActorPosition(actor, *scene.root().findUid("shadow-node"), player, floor, height, actor.rotation());
    require(bits(actor.position()[0]) == bits(stepped.x * 100.f) &&
            bits(actor.position()[1]) == bits(stepped.y * 100.f) && actor.position()[2] == 3 && floor.heightCm == 40,
            "Real stepped native body readback uses retained pre-step floor without another height query");
}
}
int main(int argc, char** argv)
{
    try {
        const bool dump = argc > 1 && std::string_view(argv[1]) == "--dump";
        placement(dump); postworld(dump);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
