#pragma once

#include <array>
#include <cstdint>
#include <vector>

using RetailNpcPathPoint = std::array<float, 3>;

// Native ownership of decoded Structs::Path scalar storage. sourceIndex is a
// fixed-width catalogue index, never an object address. Common GameObjectBasic
// loading remains the required host's responsibility, keyed by this identity.
struct RetailNpcPathSource {
    std::uint32_t sourceIndex = 0;
    std::vector<std::int32_t> scalarCoordinates;
};

class RetailNpcPathLoadHost {
  public:
    virtual ~RetailNpcPathLoadHost() = default;
    // Path::Load calls LevelObject::Load with CGameFactory::getInstance(), not
    // the incoming factory, before reading/converting its coordinate array.
    virtual void loadLevelObject(const RetailNpcPathSource&) = 0;
};

class RetailNpcPath {
  public:
    // Takes ownership of source scalars; original deletes that temporary array
    // after conversion. Each full signed XYZ triple is converted directly to
    // floats. Common-object placement/rotation/scale do not transform points.
    RetailNpcPath(RetailNpcPathSource, RetailNpcPathLoadHost&);
    RetailNpcPath(const RetailNpcPath&) = delete;
    RetailNpcPath& operator=(const RetailNpcPath&) = delete;
    RetailNpcPath(RetailNpcPath&&) = delete;
    RetailNpcPath& operator=(RetailNpcPath&&) = delete;
    std::uint32_t sourceIndex() const { return index; }
    std::int32_t getSize() const;
    // At/past end returns false without changing output. Negative indices are
    // rejected: the original accesses before its allocation for that input.
    bool getWaypoint(std::int32_t, RetailNpcPathPoint&) const;

  private:
    std::uint32_t index;
    std::vector<RetailNpcPathPoint> points;
};

// Borrowed owners must outlive this state, including activePath after a change
// to pathToFollow. They have stable native addresses; source indices do not
// substitute for original pointer identity. Only these path fields are modeled.
struct RetailNpcPathState {
    const RetailNpcPath* pathToFollow = nullptr;
    const RetailNpcPath* activePath = nullptr;
    std::int32_t activeWaypoint = 0;
    std::int32_t currentWaypoint = 1; // NPC constructor +444; setPath resets -1.
};

class RetailNpcRandomPosition {
  public:
    virtual ~RetailNpcRandomPosition() = default;
    virtual RetailNpcPathPoint getNextRandomPos(std::int32_t, std::int32_t,
                                                const RetailNpcPathPoint&) = 0;
};

class RetailNpcPathHost {
  public:
    virtual ~RetailNpcPathHost() = default;
    // Capture the live PhysicAttributes provider before the virtual position
    // getter, as in 0x2fe058..0x2fe070. These are required genuine host operations.
    virtual RetailNpcRandomPosition& physicAttributes() = 0;
    virtual RetailNpcPathPoint getPosition() = 0;
    virtual void goToLocation(const RetailNpcPathPoint&) = 0;
    virtual void goToIdle() = 0;
};

void loopRetailNpcPath(std::uint32_t& flags356, bool loop);
bool isLoopingRetailNpcPath(std::uint32_t flags356);
void setRetailNpcPath(RetailNpcPathState&, std::uint32_t& flags356, const RetailNpcPath*);
RetailNpcPathPoint nextRetailNpcWaypoint(RetailNpcPathState&, std::uint32_t flags356);
bool nextRetailNpcPoint(RetailNpcPathState&, std::uint32_t& flags356,
                       RetailNpcPathHost&, RetailNpcPathPoint&);
void goToNextRetailNpcPoint(RetailNpcPathState&, std::uint32_t& flags356,
                            RetailNpcPathHost&, bool repeat);
