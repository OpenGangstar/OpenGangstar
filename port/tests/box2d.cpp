#include "Box2D.h"
#include "Common/b2BlockAllocator.h"
#include "Common/b2StackAllocator.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

static_assert(sizeof(void*) == 8, "The physics tests require native x86-64 pointers.");
static_assert(sizeof(float32) == 4 && sizeof(int32) == 4, "Keep original scalar widths.");

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

b2AABB worldBounds()
{
    b2AABB bounds;
    bounds.lowerBound.Set(-2000, -2000);
    bounds.upperBound.Set(2000, 2000);
    return bounds;
}

void verifyAllocationAlignment()
{
    const auto baseline = b2_byteCount;
    for (const int32 size : {1, 4, 8, 17, 640, 100001}) {
        void* memory = b2Alloc(size);
        require(reinterpret_cast<std::uintptr_t>(memory) % alignof(std::max_align_t) == 0,
                "The allocation header misaligned a native physics object.");
        std::memset(memory, 0x5a, size);
        b2Free(memory);
    }
    b2Free(nullptr);
    {
        b2StackAllocator stack;
        std::vector<void*> blocks;
        for (const int32 size : {1, 3, 7, 100000, 100001}) {
            void* memory = stack.Allocate(size);
            require(reinterpret_cast<std::uintptr_t>(memory) % alignof(std::max_align_t) == 0,
                    "Differently sized stack allocations lost native alignment.");
            std::memset(memory, 0xa5, size);
            blocks.push_back(memory);
        }
        for (auto block = blocks.rbegin(); block != blocks.rend(); ++block) stack.Free(*block);
    }
    {
        b2BlockAllocator pool;
        for (const int32 size : {1, 16, 17, 63, 160, 320, 640}) {
            std::vector<void*> blocks;
            for (int i = 0; i < 300; ++i) {
                void* memory = pool.Allocate(size);
                require(reinterpret_cast<std::uintptr_t>(memory) % alignof(std::max_align_t) == 0,
                        "A pooled allocation lost native alignment.");
                std::memset(memory, 0x3c, size);
                blocks.push_back(memory);
            }
            for (void* memory : blocks) pool.Free(memory, size);
        }
    }
    require(b2_byteCount == baseline, "Physics allocators did not release all backing memory.");
}

b2Body* dynamicCircle(b2World& world, b2Vec2 position, float32 radius, void* userData = nullptr, bool bullet = false)
{
    b2BodyDef body;
    body.position = position;
    body.userData = userData;
    body.fixedRotation = true;
    body.isBullet = bullet;
    auto* result = world.CreateBody(&body);
    b2CircleDef shape;
    shape.radius = radius;
    shape.density = 1;
    shape.friction = 0;
    result->CreateShape(&shape);
    result->SetMassFromShapes();
    return result;
}

void addWall(b2World& world, float32 x, float32 halfWidth)
{
    b2BodyDef body;
    body.position.Set(x, 0);
    auto* wall = world.CreateBody(&body);
    b2PolygonDef shape;
    shape.SetAsBox(halfWidth, 5);
    shape.friction = 0;
    wall->CreateShape(&shape);
}

struct Contacts : b2ContactListener {
    unsigned added = 0, persisted = 0, removed = 0, results = 0;
    void Add(const b2ContactPoint*) override { ++added; }
    void Persist(const b2ContactPoint*) override { ++persisted; }
    void Remove(const b2ContactPoint*) override { ++removed; }
    void Result(const b2ContactResult*) override { ++results; }
};

void verifyWorldSteppingAndContacts()
{
    Contacts contacts;
    b2World world(worldBounds(), b2Vec2(0, 0), true);
    world.SetPositionCorrection(true);
    world.SetContinuousPhysics(false);
    world.SetContactListener(&contacts);
    addWall(world, 5, 0.25f);
    std::array<std::uint64_t, 2> cookie{0x1234567887654321, 0xfedcba9876543210};
    auto* body = dynamicCircle(world, b2Vec2(0, 0), 0.5f, &cookie);
    require(body->GetUserData() == &cookie, "A native body truncated its user-data pointer.");
    body->SetLinearVelocity(b2Vec2(2, 0));
    for (int i = 0; i < 60; ++i) world.Step(1.f / 60, 10);
    require(std::abs(body->GetPosition().x - 2) < 0.001f && std::abs(body->GetPosition().y) < 0.001f,
            "The original fixed timestep did not integrate velocity in a zero-gravity world.");
    for (int i = 0; i < 180; ++i) world.Step(1.f / 60, 10);
    require(body->GetPosition().x > 4 && body->GetPosition().x < 4.3f &&
            contacts.added && contacts.persisted && contacts.results,
            "A dynamic shape crossed a wall or failed to generate original contact callbacks.");

    auto* shape = body->GetShapeList();
    auto filter = shape->GetFilterData();
    filter.maskBits = 0;
    shape->SetFilterData(filter);
    world.Refilter(shape);
    body->SetLinearVelocity(b2Vec2(2, 0));
    body->WakeUp();
    for (int i = 0; i < 120; ++i) world.Step(1.f / 60, 10);
    require(body->GetPosition().x > 7 && contacts.removed,
            "Refiltering did not remove the contact and permit movement through the masked wall.");
}

void verifyWorldRange()
{
    b2World world(worldBounds(), b2Vec2(0, 0), true);
    b2AABB candidate;
    candidate.lowerBound.Set(2000, -1);
    candidate.upperBound.Set(2001, 1);
    require(!world.InRange(candidate), "Touching the world boundary is outside proxy range.");
    candidate.lowerBound.x = std::nextafter(2000.f, 0.f);
    require(world.InRange(candidate), "Any strict world overlap is in proxy range, even if not contained.");
    candidate.lowerBound.Set(2200, -1);
    candidate.upperBound.Set(2201, 1);
    require(!world.InRange(candidate), "A wholly outside proxy must be rejected.");
    candidate.lowerBound.Set(-2001, -1);
    candidate.upperBound.Set(-2000, 1);
    require(!world.InRange(candidate), "Touching the lower boundary is outside proxy range.");
    candidate.upperBound.x = std::nextafter(-2000.f, 0.f);
    require(world.InRange(candidate), "Strict lower-bound overlap must be accepted.");
    candidate.lowerBound.Set(-1, -1);
    candidate.upperBound.Set(1, 1);
    require(world.InRange(candidate), "Contained proxy is in range.");
}

void verifyRetailProxyCapacity()
{
    require(b2_maxProxies == 1024 && b2_maxPairs == 8192,
            "The native broad phase does not retain the verified retail capacities.");
    b2World world(worldBounds(), b2Vec2(0, 0), true);
    world.SetContinuousPhysics(false);
    for (int y = 0; y < 25; ++y)
        for (int x = 0; x < 32; ++x) dynamicCircle(world, b2Vec2(float32(x * 3), float32(y * 3)), 0.2f);
    world.Step(1.f / 60, 10);
    std::array<b2Shape*, 1024> shapes{};
    const auto count = world.Query(worldBounds(), shapes.data(), static_cast<int32>(shapes.size()));
    require(world.GetProxyCount() == 800 && count == 800,
            "The native broad phase lost shapes above the upstream 512-proxy capacity.");
}

void verifyContinuousCollision()
{
    b2World world(worldBounds(), b2Vec2(0, 0), true);
    world.SetContinuousPhysics(true);
    // Polygon cores shrink by b2_toiSlop (0.04 m), so each half extent must exceed it.
    addWall(world, 1, 0.05f);
    auto* body = dynamicCircle(world, b2Vec2(0, 0), 0.1f, nullptr, true);
    body->SetLinearVelocity(b2Vec2(100, 0));
    for (int i = 0; i < 3; ++i) world.Step(1.f / 60, 10);
    require(body->GetPosition().x > 0.7f && body->GetPosition().x < 1,
            "Continuous collision failed to keep a fast bullet from tunneling through a thin wall.");
}

void verifyDistanceJoint()
{
    b2World world(worldBounds(), b2Vec2(0, 0), true);
    world.SetContinuousPhysics(false);
    auto* first = dynamicCircle(world, b2Vec2(-3, 0), 0.2f);
    auto* second = dynamicCircle(world, b2Vec2(3, 0), 0.2f);
    b2DistanceJointDef definition;
    definition.Initialize(first, second, first->GetPosition(), second->GetPosition());
    world.CreateJoint(&definition);
    first->SetLinearVelocity(b2Vec2(-2, 1));
    second->SetLinearVelocity(b2Vec2(2, -1));
    for (int i = 0; i < 120; ++i) world.Step(1.f / 60, 10);
    require(std::abs((second->GetPosition() - first->GetPosition()).Length() - 6) < 0.02f,
            "The native joint solver did not preserve its authored distance.");
}
}

int main()
{
    try {
        require(b2_version.major == 2 && b2_version.minor == 0 && b2_version.revision == 1,
                "The linked engine does not match the retail Box2D version.");
        const auto baseline = b2_byteCount;
        verifyAllocationAlignment();
        verifyWorldSteppingAndContacts();
        verifyWorldRange();
        verifyRetailProxyCapacity();
        verifyContinuousCollision();
        verifyDistanceJoint();
        require(b2_byteCount == baseline, "Native worlds leaked physics backing allocations.");
        std::cout << "Box2D 2.0.1 x64 allocation, stepping, contacts, filtering, capacity, CCD, and joint checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
