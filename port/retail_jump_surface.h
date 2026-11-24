#pragma once

#include "city_assets.h"
#include "retail_physics.h"

#include <array>
#include <memory>
#include <vector>

using RetailJumpMatrix = std::array<float, 16>;

struct RetailJumpSurfaceSource {
    std::string identity, ownerIdentity, uid;
    std::vector<std::array<float, 3>> verticesCm;
    RetailJumpMatrix currentAbsolute, placementAbsolute;
};

// Original function-local static uint8 ID persists across worlds and reloads.
// The host supplies one shared sequence for that process lifetime, not one per
// catalog/world. Fixture construction can explicitly supply an initial value.
class RetailJumpSurfaceSequence {
  public:
    explicit RetailJumpSurfaceSequence(std::uint8_t initial = 0) : next(initial) {}
    RetailJumpSurfaceSequence(const RetailJumpSurfaceSequence&) = delete;
    RetailJumpSurfaceSequence& operator=(const RetailJumpSurfaceSequence&) = delete;
    std::uint8_t nextID() const { return next; }

  private:
    friend class RetailJumpSurfaceCatalog;
    std::uint8_t consume();
    std::uint8_t next;
};

struct RetailJumpSurfaceDefinition {
    std::string identity, ownerIdentity;
    b2BodyDef body;
    b2PolygonDef polygon;
    RetailPhysicsBodyData metadata{nullptr, std::nullopt, {}, true};
};

// Pure decoded-scene binding for the evidenced AddXRef external mesh path.
// It preserves distinct Jump-node and first visible-mesh transforms and source
// order, including the generated identity-child multiplication and its signed zeros.
// Nested/LOD/billboard bindings requiring unported scene-factory behavior
// report an unsupported binding rather than selecting an approximate ramp.
std::vector<RetailJumpSurfaceSource> collectRetailJumpSurfaceSources(const CityAssets&, const CityScene&);

// One original CollisionBodyDef scene owner per catalog. Definitions and metadata persist while
// visibility activation/deactivation creates/destroys genuine static bodies.
// No SetMassFromShapes: the original density1 polygon remains on a mass0 body.
// World must outlive active catalog; operations/callbacks are serial and outside
// world.Step, and callbacks must not throw. Metadata stays alive in destruction.
class RetailJumpSurfaceCatalog {
  public:
    explicit RetailJumpSurfaceCatalog(RetailJumpSurfaceSequence&);
    ~RetailJumpSurfaceCatalog();
    RetailJumpSurfaceCatalog(const RetailJumpSurfaceCatalog&) = delete;
    RetailJumpSurfaceCatalog& operator=(const RetailJumpSurfaceCatalog&) = delete;
    // Non-Jump UIDs and non-four-vertex buffers follow the original skip branch,
    // without consuming an ID. Sources are appended only while inactive.
    bool append(const RetailJumpSurfaceSource&);
    void activate(b2World&);
    void deactivate();
    std::size_t size() const { return entries.size(); }
    const RetailJumpSurfaceDefinition& definition(std::size_t index) const { return *entries.at(index); }
    const std::vector<b2Body*>& bodies() const { return liveBodies; }

  private:
    RetailJumpSurfaceSequence& sequence;
    std::string ownerIdentity;
    std::vector<std::unique_ptr<RetailJumpSurfaceDefinition>> entries;
    b2World* world = nullptr;
    std::vector<b2Body*> liveBodies;
};
