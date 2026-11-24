#pragma once

#include "city_assets.h"
#include "retail_physics.h"

#include <array>
#include <memory>
#include <vector>

using RetailHeightPoint = std::array<float, 3>;

struct RetailHeightState {
    CityNavigation::TriangleRef triangle{UINT16_MAX, UINT16_MAX};
    bool hasHeightmap = false;
    std::array<RetailHeightPoint, 3> vertices{};
    RetailHeightPoint normal{};
    bool changed = false;
    RetailHeightPoint cached{0, 0, 999999.f};
    bool onGround = true;
    bool wasOnGround = true;
};

// Immutable decoded inputs must outlive this shared terrain context. Visitation
// words are copied from lane 7; neither asset bytes nor roadFlags are mutated.
// Queries are serial, as in the original shared terrain generation protocol.
// The caller must use exactly one shared terrain context per native world and
// keep its controllers in that world. Group indices are unique within this
// context only; separate contexts in the same world would duplicate groups.
class RetailHeightTerrain {
  public:
    RetailHeightTerrain(const CityMesh&, const CityNavigation&, RetailHeightPoint translation = {});
    RetailHeightTerrain(const RetailHeightTerrain&) = delete;
    RetailHeightTerrain& operator=(const RetailHeightTerrain&) = delete;
    std::uint16_t generation() const { return currentUsed; }
    const std::vector<std::vector<std::uint16_t>>& visitWords() const { return visits; }

  private:
    friend class RetailHeightController;
    const CityMesh& mesh;
    const CityNavigation& navigation;
    RetailHeightPoint translation;
    std::array<RetailHeightPoint, 2> bounds;
    std::vector<std::vector<std::uint16_t>> visits;
    std::array<bool, 50> pool{};
    std::uint16_t currentUsed = 0;
    void validate(CityNavigation::TriangleRef) const;
    std::array<std::uint16_t, 3> indices(CityNavigation::TriangleRef) const;
    std::array<RetailHeightPoint, 3> vertices(CityNavigation::TriangleRef) const;
    void loadGeometry(RetailHeightState&, bool normalAndMarker) const;
    bool firstSeed(RetailHeightState&, const RetailHeightPoint&) const;
};

// Ordinary terrain only: no jump quads, steering, actor clock or VM binding.
// World and terrain must outlive this object; destroy outside world.Step.
// suppliedBody is exactly the constructor-supplied PhysicAttributes body. A
// later actor body is not implicitly substituted. Its lifetime is the caller's.
// More than twelve eligible strips, or a polygon outside the actual world proxy
// range, throws before replacing existing bodies. Bounds use the real native
// polygon OBB and authoritative world predicate; no proxy is silently omitted.
// Slot geometry/cache and shared query markers may already have changed: this
// is an explicit unsupported geometry/capacity error, not a resumable transaction. Reset
// or reinitialize from known state before another query. World callbacks must
// not throw. Metadata remains alive through native DestroyBody callbacks.
class RetailHeightController {
  public:
    static constexpr float noHeight = 999999.f;
    RetailHeightController(RetailHeightTerrain&, b2World&, std::size_t slots = 1,
                           b2Body* suppliedBody = nullptr);
    ~RetailHeightController();
    RetailHeightController(const RetailHeightController&) = delete;
    RetailHeightController& operator=(const RetailHeightController&) = delete;
    // Explicit selected-triangle initialization; this is not a full actor Init.
    void initializeSelected(std::size_t slot, CityNavigation::TriangleRef, const RetailHeightPoint&);
    float height(const RetailHeightPoint&, std::size_t slot = 0, bool detailed = true);
    void setGroundState(std::size_t slot, bool value);
    void updateEdges(std::size_t slot);
    void reset();
    const RetailHeightState& state(std::size_t slot) const { return states.at(slot); }
    const std::vector<b2Body*>& bodies(std::size_t slot) const { return owned.at(slot).bodies; }
    int groupIndex() const { return poolIndex + 1; }

  private:
    struct Owned {
        std::vector<std::unique_ptr<RetailPhysicsBodyData>> metadata;
        std::vector<b2Body*> bodies;
    };
    RetailHeightTerrain& terrain;
    b2World& world;
    b2Body* const suppliedBody;
    std::vector<RetailHeightState> states;
    std::vector<Owned> owned;
    int poolIndex = -1;
    float core(std::size_t, const RetailHeightPoint&, bool crossing, bool detailed);
    void destroy(std::size_t);
};
