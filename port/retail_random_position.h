#pragma once

#include "retail_height.h"

// The original function uses the application's shared libc lrand48 stream.
// Supply that genuine stream, including its seed/state and consumption by other
// systems. No per-actor/private default stream is created by this component.
class RetailRandomPositionSource {
  public:
    virtual ~RetailRandomPositionSource() = default;
    virtual std::int32_t lrand48() = 0;
};

class RetailRandomPosition {
  public:
    // Borrow the existing immutable decoded navigation and live global-heightmap
    // node. Terrain, scene and random source outlive this serial query object.
    RetailRandomPosition(const RetailHeightTerrain&, RetailRandomPositionSource&);
    RetailHeightPoint next(RetailHeightController&, std::size_t slot, int mode,
                           RetailHeightPoint position);
    // Original scalar/state leaf for explicit native state bindings and isolated
    // tests. Production height controllers use the overload above, preserving
    // one actual PhysicInfo-equivalent state per slot.
    RetailHeightPoint next(RetailHeightState&, int mode, RetailHeightPoint position);

  private:
    const RetailHeightTerrain& terrain;
    RetailRandomPositionSource& random;
    std::array<std::vector<float>, 2> costs;
    std::uint32_t roll();
    CityNavigation::TriangleRef neighbor(CityNavigation::TriangleRef, std::size_t edge) const;
    std::uint8_t wrongWay(CityNavigation::TriangleRef) const;
    RetailHeightPoint vertex(CityNavigation::TriangleRef, std::size_t corner) const;
    RetailHeightPoint center(CityNavigation::TriangleRef) const;
    RetailHeightPoint randomDestination(std::uint16_t buffer, std::uint16_t& offset,
                                        const RetailHeightPoint&, float minimum, float maximum);
};
