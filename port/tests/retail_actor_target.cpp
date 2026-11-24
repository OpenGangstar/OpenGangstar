#include "retail_actor_target.h"
#include "filesystem.h"

#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Point = RetailActorHeadingPoint;
using Quaternion = RetailActorHeadingQuaternion;
using Events = std::vector<std::string>;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
uint32_t bits(float value) { uint32_t out; std::memcpy(&out, &value, 4); return out; }
float number(uint32_t value) { float out; std::memcpy(&out, &value, 4); return out; }
template<class F> void rejects(F operation) {
    bool failed = false;
    try { operation(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, "Invalid target must fail explicitly");
}
struct Target final : RetailActorTargetPosition {
    Point position{};
    RetailActorSceneNode* node = nullptr;
    Events* events = nullptr;
    std::function<void()> effect, destroyed;
    ~Target() override { if (destroyed) destroyed(); }
    Point getPosition() override {
        events->push_back("targetPosition");
        if (effect) effect();
        return node ? retailActorCachedPosition(*node) : position;
    }
};
struct Lookup final : RetailActorTargetLookup {
    Events& events;
    std::shared_ptr<Target> target;
    std::function<void(unsigned)> effect;
    unsigned calls = 0;
    explicit Lookup(Events& value) : events(value) {}
    std::shared_ptr<RetailActorTargetPosition> currentTarget() override {
        events.push_back("getTarget");
        if (effect) effect(++calls); else ++calls;
        return target;
    }
};
struct Heading final : RetailActorHeadingHost {
    Events& events;
    RetailActorSceneNode* node = nullptr;
    Point position{}, front{1, 0, 0};
    Quaternion rotation{0, 0, 0, 1};
    Point lastPosition{};
    std::function<void()> rotationEffect;
    explicit Heading(Events& value) : events(value) {}
    Point getPosition() override {
        events.push_back("actorPosition");
        lastPosition = node ? retailActorCachedPosition(*node) : position;
        return lastPosition;
    }
    Point getRight() override {
        events.push_back("actorRight");
        require(node != nullptr, "Unused synthetic Right is not a default getter");
        const auto& m = node->absolute(); return {-m[0], -m[1], -m[2]};
    }
    Point getFront() override {
        events.push_back("actorFront");
        if (!node) return front;
        const auto& m = node->absolute(); return {-m[4], -m[5], -m[6]};
    }
    Quaternion getRotation() override {
        events.push_back("actorRotation");
        const auto value = node ? node->rotation() : rotation;
        if (rotationEffect) rotationEffect();
        return value;
    }
    void setRotation(const Quaternion& value) override {
        events.push_back("setRotation");
        if (node) node->setRotation(value); else rotation = value;
    }
};
std::shared_ptr<Target> target(Events& events, Point position) {
    auto result = std::make_shared<Target>(); result->events = &events;
    result->position = position; return result;
}
void dumpEvents(const Events& events) { for (const auto& event : events) std::cout << ' ' << event; }
void predicate(bool dump) {
    const std::vector<std::array<Point, 3>> inputs{
        {{{0, 0, 900}, {1, 0, -900}, {1, 0, 4}}},
        {{{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}}},
        {{{3, 4, 5}, {3, 4, 100}, {1, 0, 0}}},
        {{{0, 0, 0}, {1, 0, 0}, {0, 0, 12}}},
        {{{-0.f, -0.f, 0}, {0, 0, 1}, {-0.f, 0, 0}}}
    };
    unsigned index = 0;
    for (const auto& input : inputs) {
        Events events; Heading heading(events); Lookup lookup(events);
        heading.position = input[0]; heading.front = input[2];
        lookup.target = target(events, input[1]);
        const auto previous = heading.rotation;
        const bool result = retailActorIsFacingTarget(heading, lookup);
        require(result == (index == 0), "Independent cardinal/zero/vertical-facing expectations");
        require(events == Events{"getTarget", "targetPosition", "actorPosition", "actorFront"} &&
                heading.rotation == previous, "Predicate getter order, no turning or rotation query");
        if (dump) { std::cout << "face " << index << ' ' << result; dumpEvents(events); std::cout << '\n'; }
        ++index;
    }
    // Actual original normalizer produces below, equal, above literal .9f.
    const uint32_t xy[][2]{{1063675488, 1054813429}, {1063675489, 1054813427}, {1063675488, 1054813427}};
    for (unsigned i = 0; i < 3; ++i) {
        Events events; Heading heading(events); Lookup lookup(events);
        heading.front = {number(xy[i][0]), number(xy[i][1]), -123};
        lookup.target = target(events, {1, 0, 999});
        const bool result = retailActorIsFacingTarget(heading, lookup);
        require(result == (i == 2), "Strict original float threshold below/equal/above");
        if (dump) { std::cout << "threshold " << xy[i][0] << ' ' << xy[i][1] << ' ' << result;
            dumpEvents(events); std::cout << '\n'; }
    }
    Events events; Heading heading(events); Lookup lookup(events);
    rejects([&] { retailActorIsFacingTarget(heading, lookup); });
    require(events == Events{"getTarget"}, "NULL predicate rejects before any getters");
    events.clear(); lookup.effect = [](unsigned) { throw std::runtime_error("Expired canonical entity"); };
    rejects([&] { retailActorIsFacingTarget(heading, lookup); });
    require(events == Events{"getTarget"}, "Unresolved entity cannot become known NULL");
    lookup.effect = {}; events.clear(); lookup.target = target(events, {1, 0, 0});
    std::weak_ptr<Target> weak = lookup.target; bool destroyed = false;
    lookup.target->destroyed = [&] { destroyed = true; };
    lookup.target->effect = [&] { lookup.target.reset(); require(!weak.expired(), "Lease survives producer clear"); };
    require(retailActorIsFacingTarget(heading, lookup) && weak.expired() && destroyed,
            "Only per-query canonical endpoint lease survives clear during target getter");
}
std::shared_ptr<RetailActorSceneSource> fixture() {
    auto source = std::make_shared<RetailActorSceneSource>();
    source->model.path = "./target-fixture.bdae";
    CityNode node{}; node.name = "actor"; node.parent = -1;
    node.rotation = {0, 0, 0, 1}; node.scale = {1, 1, 1};
    source->model.nodes.push_back(node);
    node.name = "shadow-node"; source->model.nodes.push_back(node);
    source->model.visualScenes.push_back({"visual", "visual", 0, 2});
    return source;
}
void wrappers(const std::shared_ptr<const RetailActorSceneSource>& source, const std::string& label, bool dump) {
    b2AABB bounds; bounds.lowerBound.Set(-2000, -2000); bounds.upperBound.Set(2000, 2000);
    b2World world(bounds, {0, 0}, true); b2BodyDef definition; definition.position.Set(50, 60);
    auto* body = world.CreateBody(&definition);
    for (unsigned mode = 0; mode < 6; ++mode) {
        RetailActorScene scene(source); auto& actor = scene.root();
        RetailActorScene targetScene(source);
        auto* shadow = scene.root().findUid("shadow-node"); require(shadow, "Real shadow node");
        actor.setPosition({10, 20, 30}); actor.setRotation({0, 0, 0, 1}); actor.updateAbsolutePosition(false);
        // Leave local rotation deliberately newer than the cached Front.
        const Quaternion expected{.1f, .2f, .3f, .4f}; actor.setRotation(expected);
        const auto before = actor.absolute(); const auto shadowBefore = shadow->absolute();
        Events events; Heading heading(events); heading.node = &actor; Lookup lookup(events);
        if (mode != 0) {
            lookup.target = target(events, {10, mode == 2 ? 100.f : -100.f, 800});
            targetScene.root().setPosition(lookup.target->position);
            targetScene.root().updateAbsolutePosition(false);
            lookup.target->node = &targetScene.root();
        }
        if (mode == 3) lookup.target->effect = [&] {
            actor.setPosition({-10, 0, 0}); actor.setRotation({.5f, .6f, .7f, .8f});
            actor.updateAbsolutePosition(false);
        };
        if (mode == 4) heading.rotationEffect = [&] { actor.setRotation({0, 0, 0, 0}); };
        RetailActorFloorState floor; floor.heightCm = 40; RetailHeightState height;
        if (mode == 5) updateRetailNpcSceneNode(heading, actor, *shadow, body, floor, height);
        else updateRetailPlayerSceneNode(heading, lookup, actor, *shadow, mode == 4 ? nullptr : body, floor, height);
        const Events expectedEvents = mode == 5 ? Events{"actorRotation"} : mode == 0 ?
            Events{"getTarget", "actorRotation"} : Events{"getTarget", "getTarget", "targetPosition",
                                                               "actorPosition", "actorFront", "actorRotation"};
        require(events == expectedEvents, "Exact Player/NPC outer/inner lookups and ignored predicate ordering");
        if (mode > 0 && mode < 5)
            require(heading.lastPosition == (mode == 3 ? Point{-10, 0, 0} : Point{10, 20, 30}),
                    "Facing reads old cached actor position after target effect, before body readback");
        const Quaternion wanted = mode == 3 ? Quaternion{.5f, .6f, .7f, .8f} : expected;
        require(actor.rotation() == wanted, "Rotation copied after facing and before genuine post-position");
        require(actor.position() == (mode == 4 ? Point{10, 20, 30} : Point{5000, 6000, mode == 3 ? 0.f : 30.f}),
                "Genuine original cached position/readback, no early body position facing");
        require(shadow->position() == Point{actor.position()[0], actor.position()[1], 43} &&
                std::memcmp(shadowBefore.data(), shadow->absolute().data(), sizeof shadowBefore) == 0,
                "Real shadow post-position preserves stale cache");
        require(body->GetPosition().x == 50 && body->GetPosition().y == 60 && world.GetBodyCount() == 2,
                "No body transform, stepping or synthetic body creation");
        if (mode == 4) require(std::memcmp(before.data(), actor.absolute().data(), sizeof before) == 0,
                               "Body-absent scene wrapper does not refresh cached matrix");
        if (dump) {
            std::cout << "wrapper " << label << ' ' << mode; dumpEvents(events);
            for (float value : actor.position()) std::cout << ' ' << bits(value);
            for (float value : actor.rotation()) std::cout << ' ' << bits(value);
            for (float value : shadow->position()) std::cout << ' ' << bits(value);
            std::cout << '\n';
        }
    }
    RetailActorScene scene(source); Events events; Heading heading(events); heading.node = &scene.root();
    Lookup lookup(events); lookup.target = target(events, {1, 0, 0});
    auto* shadow = scene.root().findUid("shadow-node");
    std::weak_ptr<Target> first = lookup.target;
    lookup.effect = [&](unsigned call) { if (call == 1) lookup.target.reset(); };
    updateRetailPlayerSceneNode(heading, lookup, scene.root(), *shadow, nullptr, {}, {});
    require(first.expired() && events == Events{"getTarget", "actorRotation"}, "Known NULL first check skips predicate");
    lookup.calls = 0; events.clear(); lookup.target = target(events, {1, 0, 0});
    lookup.effect = [&](unsigned call) { if (call == 2) lookup.target.reset(); };
    const auto rotation = scene.root().rotation();
    rejects([&] { updateRetailPlayerSceneNode(heading, lookup, scene.root(), *shadow, body, {}, {}); });
    require(events == Events{"getTarget", "getTarget"} && scene.root().rotation() == rotation,
            "Second lookup NULL rejects before position/Front/rotation/readback");
    lookup.calls = 0; events.clear(); lookup.target = target(events, {1, 0, 0});
    std::weak_ptr<Target> outer = lookup.target;
    lookup.effect = [&](unsigned call) {
        if (call == 2) {
            lookup.target.reset();
            require(outer.expired(), "Outer lease is gone before second lookup begins");
            lookup.target = target(events, {0, -1, 0});
        }
    };
    updateRetailPlayerSceneNode(heading, lookup, scene.root(), *shadow, nullptr, {}, {});
    require(events == Events{"getTarget", "getTarget", "targetPosition", "actorPosition", "actorFront", "actorRotation"},
            "Fresh replacement target endpoint used by predicate");
    // finishRetailPlayerPhysics remains the authoritative enclosing gate.
    RetailAnimationClock animation; animation.animation = -1;
    events.clear(); body->SetLinearVelocity({123, 456});
    if (finishRetailPlayerPhysics(*body, 0x400, animation))
        updateRetailPlayerSceneNode(heading, lookup, scene.root(), *shadow, body, {}, {});
    require(events.empty() && body->GetLinearVelocity().x == 0 && body->GetLinearVelocity().y == 0,
            "Rejected post-physics stage skips target/rotation/position but clears linearXY");
}
}
int main(int argc, char** argv) {
    try {
        const bool dump = argc > 1 && std::string(argv[argc - 1]) == "--oracle-dump";
        predicate(dump); wrappers(fixture(), "synthetic", dump);
        if (argc > 1 && std::string(argv[1]) != "--oracle-dump") {
            ResourceFileSystem files{std::filesystem::path(argv[1])};
            for (const char* name : {"char_pthug.bdae", "char_pthug_jail.bdae"})
                wrappers(loadRetailActorSceneSource(files, name), name, dump);
        }
        std::cout << "Actor target/facing and genuine scene readback checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
