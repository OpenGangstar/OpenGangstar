#include "retail_world.h"

#include <cmath>
#include <cstring>
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
uint32_t bits(float value) { uint32_t result; std::memcpy(&result, &value, 4); return result; }
float value(uint32_t bits) { float result; std::memcpy(&result, &bits, 4); return result; }
b2AABB bounds() { b2AABB result; result.lowerBound.Set(-100, -100); result.upperBound.Set(100, 100); return result; }
struct Event {
    char kind;
    int id;
    uint32_t argument, accumulator;
    int count;
};
struct Vehicles final : RetailWorldVehicles {
    RetailWorldStepState& state;
    std::vector<int> ids{0, 1};
    std::vector<Event> events;
    std::function<void(char, int)> change;
    explicit Vehicles(RetailWorldStepState& state) : state(state) {}
    size_t size() const override { return ids.size(); }
    void record(char kind, size_t index, uint32_t argument = 0) {
        const auto id = ids.at(index);
        events.push_back({kind, id, argument, bits(state.accumulatorSeconds), state.completedSteps});
        if (change) change(kind, id);
    }
    void updateFrame(size_t index, int32_t elapsed) override {
        uint32_t raw; std::memcpy(&raw, &elapsed, 4); record('f', index, raw);
    }
    void updatePhysics(size_t index, float seconds) override { record('p', index, bits(seconds)); }
    void synchronizePhysics(size_t index) override { record('s', index); }
};

// Direct execution of the original 0x2e08a8..0x2e09b8 instruction window,
// intercepting AEABI helpers/vehicle calls/Step, produced these exact values.
// These are bit fixtures, not a second native accumulator implementation.
struct OriginalCase { uint32_t before, elapsed, after; int steps; };
const OriginalCase originalCases[] = {
    {0, 0, 0, 0}, {0, 1, 0x3a83126f, 0}, {0, 16, 0x3c83126f, 0},
    {0, 17, 0x39aec340, 1}, {0, 33, 0x3c85cd7b, 1}, {0, 50, 0x3c888887, 2},
    {0, 249, 0x3c805767, 14}, {0, 250, 0x32400000, 15},
    {0, 251, 0x32400000, 15}, {0, 1000, 0x32400000, 15},
    {0, 0x7fffffff, 0x32400000, 15}, {0, 0x80000000, 0, 0}, {0, 0xffffffff, 0, 0},
    {0x3c888888, 0, 0x3c888888, 0}, {0x3c888888, 1, 0x3a831260, 1},
    {0x3c888888, 16, 0x3c83126f, 1}, {0x3c888888, 250, 0x3c888887, 15},
    {0x3c888889, 0, 0, 1}, {0x3c888889, 1, 0x3a831270, 1},
    {0x3c888889, 16, 0x3c83126f, 1}, {0x3c888889, 250, 0x32400000, 16},
    {0x3c88888a, 0, 0x31000000, 1}, {0x3c88888a, 1, 0x3a831280, 1},
    {0x3c88888a, 16, 0x3c83126f, 1}, {0x3c88888a, 250, 0x32400000, 16},
    {0x80000000, 0, 0, 0}, {0x80000000, 1, 0x3a83126f, 0},
    {0x80000000, 16, 0x3c83126f, 0}, {0x80000000, 250, 0x32400000, 15},
    {0x3a83126f, 0, 0x3a83126f, 0}, {0x3a83126f, 1, 0x3b03126f, 0},
    {0x3a83126f, 16, 0x39aec340, 1}, {0x3a83126f, 250, 0x3a831260, 15},
};
void originalBoundaries(bool dump) {
    require(bits(RetailWorldUpdate::fixedStepSeconds) == 0x3c888889, "Original binary32 timestep");
    for (const auto& c : originalCases) {
        b2World world(bounds(), b2Vec2(0, 0), true);
        RetailWorldStepState state{value(c.before), 99};
        RetailWorldUpdate update(world, state); Vehicles vehicles(state);
        update.advance(c.elapsed, vehicles);
        require(bits(state.accumulatorSeconds) == c.after && state.completedSteps == c.steps,
                "Original signed clamp/division/accumulation/subtraction/count bits");
        require(vehicles.events.size() == 2 + size_t(c.steps) * 4, "One vehicle update per frame and pre/post per actual step");
        for (size_t i = 0; i < vehicles.events.size(); ++i) {
            const auto& e = vehicles.events[i];
            if (i < 2) {
                require(e.kind == 'f' && e.id == int(i) && e.argument == c.elapsed && e.count == 0,
                        "Raw elapsed survives clamp; step counter reset precedes frame callbacks");
            } else {
                const auto lane = (i - 2) % 4;
                require(e.kind == (lane < 2 ? 'p' : 's') && e.id == int(lane % 2) &&
                            e.count == int((i - 2) / 4), "Complete ordered vehicle passes precede subtraction/counter increment");
                if (lane < 2) require(e.argument == 0x3c888889, "Vehicle physics uses exact original seconds");
            }
            if (dump) std::cout << "event " << c.before << ' ' << c.elapsed << ' ' << e.kind << ' '
                                << e.id << ' ' << e.argument << ' ' << e.accumulator << ' ' << e.count << '\n';
        }
        if (dump) std::cout << "case " << c.before << ' ' << c.elapsed << ' ' << bits(state.accumulatorSeconds)
                            << ' ' << state.completedSteps << '\n';
    }
}
void liveVehicles() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailWorldStepState state; RetailWorldUpdate update(world, state); Vehicles vehicles(state);
    vehicles.ids = {0};
    vehicles.change = [&](char phase, int id) {
        if (phase == 'f' && id == 0) vehicles.ids.push_back(1);
        if (phase == 'p' && id == 0) vehicles.ids.erase(vehicles.ids.begin());
        if (phase == 's' && id == 1) vehicles.ids.push_back(2);
    };
    update.advance(17, vehicles);
    const std::vector<std::pair<char, int>> expected{{'f',0},{'f',1},{'p',0},{'s',1},{'s',2}};
    require(vehicles.events.size() == expected.size(), "Original mutable vector pass lengths");
    for (size_t i = 0; i < expected.size(); ++i)
        require(vehicles.events[i].kind == expected[i].first && vehicles.events[i].id == expected[i].second,
                "Live append/removal uses original current index and new size, not a snapshot");
}
b2Body* movingBody(b2World& world, float x = 0) {
    b2BodyDef def; def.position.Set(x, 0); def.linearDamping = .1f; def.angularDamping = .2f;
    auto* body = world.CreateBody(&def);
    b2CircleDef circle; circle.radius = .5f; circle.density = 1.f; circle.friction = .3f;
    body->CreateShape(&circle); body->SetMassFromShapes(); body->SetAngularVelocity(.7f);
    body->SetLinearVelocity(b2Vec2(2, 1)); return body;
}
void actualWorldAndRemainder() {
    b2World actual(bounds(), b2Vec2(0, -9.8f), false), reference(bounds(), b2Vec2(0, -9.8f), false);
    auto* body = movingBody(actual); auto* expected = movingBody(reference);
    auto* second = movingBody(actual, .7f); auto* secondExpected = movingBody(reference, .7f);
    RetailWorldStepState state; RetailWorldUpdate update(actual, state); Vehicles vehicles(state);
    vehicles.ids.clear();
    // Original sequential oracle: 16ms none, next1ms one, next250ms fifteen.
    update.advance(16, vehicles); require(body->GetPosition().LengthSquared() == 0, "No early fractional physics step");
    update.advance(1, vehicles); reference.Step(value(0x3c888889), 10);
    update.advance(250, vehicles);
    for (int i = 0; i < 15; ++i) reference.Step(value(0x3c888889), 10);
    require(bits(body->GetPosition().x) == bits(expected->GetPosition().x) &&
            bits(body->GetPosition().y) == bits(expected->GetPosition().y) &&
            bits(body->GetLinearVelocity().x) == bits(expected->GetLinearVelocity().x) &&
            bits(body->GetLinearVelocity().y) == bits(expected->GetLinearVelocity().y) &&
            bits(body->GetAngle()) == bits(expected->GetAngle()) &&
            bits(body->GetAngularVelocity()) == bits(expected->GetAngularVelocity()) &&
            bits(second->GetPosition().x) == bits(secondExpected->GetPosition().x) &&
            bits(second->GetPosition().y) == bits(secondExpected->GetPosition().y) &&
            bits(second->GetLinearVelocity().x) == bits(secondExpected->GetLinearVelocity().x) &&
            bits(second->GetLinearVelocity().y) == bits(secondExpected->GetLinearVelocity().y),
            "Actual borrowed world steps match independent direct native Box2D integration");
    require(state.completedSteps == 15 && state.accumulatorSeconds > 0 &&
            state.accumulatorSeconds < RetailWorldUpdate::fixedStepSeconds, "Remainder retained; counter is per frame");
    // The raw native timestamp subtraction wraps before signed interpretation.
    RetailWorldStepState wrap; RetailWorldUpdate wrapped(actual, wrap); Vehicles empty(wrap); empty.ids.clear();
    wrapped.advance(uint32_t(9) - uint32_t(0xfffffff8), empty);
    require(wrap.completedSteps == 1 && bits(wrap.accumulatorSeconds) == 0x39aec340,
            "Device-millisecond wrap preserves17ms delta");
}
void sequentialOracle(bool dump) {
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailWorldStepState state; RetailWorldUpdate update(world, state); Vehicles vehicles(state); vehicles.ids.clear();
    uint32_t random = 0x71a9bc42;
    for (int i = 0; i < 1024; ++i) {
        random = random * 1664525u + 1013904223u;
        const auto elapsed = i % 5 == 0 ? random : random % 300;
        update.advance(elapsed, vehicles);
        require(state.accumulatorSeconds >= 0 && state.accumulatorSeconds < RetailWorldUpdate::fixedStepSeconds,
                "Sequential original remainder retains canonical finite range");
        if (dump) std::cout << "sequence " << elapsed << ' ' << bits(state.accumulatorSeconds) << ' '
                            << state.completedSteps << '\n';
    }
}
void actualContactMutation() {
    b2World world(bounds(), b2Vec2(0, 0), false);
    movingBody(world, 0); movingBody(world, .7f);
    RetailWorldStepState state; RetailWorldUpdate update(world, state); Vehicles vehicles(state);
    struct Listener final : b2ContactListener {
        Vehicles& vehicles; RetailWorldStepState& state; int calls = 0;
        Listener(Vehicles& v, RetailWorldStepState& s) : vehicles(v), state(s) {}
        void Add(const b2ContactPoint*) override {
            require(vehicles.events.size() == 4 && vehicles.events.back().kind == 'p' && state.completedSteps == 0,
                    "Real contact callback occurs after complete pre-pass and before post-pass");
            vehicles.ids = {2}; ++calls;
        }
    } listener(vehicles, state);
    world.SetContactListener(&listener); update.advance(17, vehicles); world.SetContactListener(nullptr);
    require(listener.calls > 0 && vehicles.events.size() == 5 && vehicles.events.back().kind == 's' &&
                vehicles.events.back().id == 2 && vehicles.events.back().count == 0,
            "Actual Step contact mutation changes the live post-physics vehicle pass before count commit");
}
void unsupportedState() {
    b2World world(bounds(), b2Vec2(0, 0), true);
    for (float bad : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        RetailWorldStepState state{bad, 99}; RetailWorldUpdate update(world, state); Vehicles vehicles(state);
        bool failed = false; try { update.advance(17, vehicles); } catch (const std::invalid_argument&) { failed = true; }
        require(failed && vehicles.events.empty() && state.completedSteps == 99, "Nonfinite imported accumulator is explicit unsupported state");
    }
    RetailWorldStepState state{std::numeric_limits<float>::max(), 99};
    RetailWorldUpdate update(world, state); Vehicles vehicles(state); vehicles.ids.clear();
    bool failed = false; try { update.advance(0, vehicles); } catch (const std::runtime_error&) { failed = true; }
    require(failed && state.completedSteps == 0, "Nonprogressing original repeated subtraction raises after actual step, without invented count/height");
}
} // namespace
int main(int argc, char** argv) {
    try {
        const auto before = b2_byteCount;
        const bool dump = argc == 2 && std::string(argv[1]) == "--oracle-dump";
        originalBoundaries(dump); sequentialOracle(dump);
        liveVehicles(); actualWorldAndRemainder(); actualContactMutation(); unsupportedState();
        require(b2_byteCount == before, "World binding and hosts release all native physics allocations");
        std::cout << "Retail world fixed-step and genuine shared Box2D fixtures passed.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
