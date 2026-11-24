#pragma once

#include "city_assets.h"
#include "retail_physics.h"

#include <array>
#include <memory>
#include <vector>

class RetailJumpController;

using RetailHeightPoint = std::array<float, 3>;

// A native host view, not an ARM scene-node layout. type is the actual runtime
// node tag; the two original mesh tags are 0x6d656164 and 0x73656164. Other types
// traverse children without inspecting mesh/bounds. A recognized type needs mesh.
// Supply the node's raw bbox and actual absolute translation, not a transformed
// bbox or a guessed scene-matrix conversion. Query Z does not rank candidates.
// Nodes, child pointers and meshes outlive their terrain/controllers. Type, mesh
// and child topology stay fixed; bounds and absolute translation may be updated
// by the host between serial queries, and are read afresh at original query sites.
// The complete view is read-only during each query.
struct RetailHeightSceneNode {
    std::uint32_t type = 0;
    const CityMesh* mesh = nullptr;
    std::array<RetailHeightPoint, 2> boundsCm{};
    RetailHeightPoint absoluteTranslationCm{};
    std::vector<const RetailHeightSceneNode*> children;
};

struct RetailHeightState {
    CityNavigation::TriangleRef triangle{UINT16_MAX, UINT16_MAX};
    bool hasHeightmap = false;
    std::array<RetailHeightPoint, 3> vertices{};
    RetailHeightPoint normal{};
    bool changed = false;
    RetailHeightPoint cached{0, 0, 999999.f};
    bool onGround = true;
    bool wasOnGround = true;
    const RetailHeightSceneNode* selectedNode = nullptr;
};

// Immutable decoded mesh/navigation inputs outlive this shared terrain context.
// Native node views are borrowed with the live-property contract above. Visitation
// words are copied from lane 7; neither asset bytes nor roadFlags are mutated.
// Queries are serial, as in the original shared terrain generation protocol.
// The caller must use exactly one shared terrain context per native world and
// keep its controllers in that world. Group indices are unique within this
// context only; separate contexts in the same world would duplicate groups.
class RetailHeightTerrain {
  public:
    RetailHeightTerrain(const CityMesh&, const CityNavigation&, RetailHeightPoint translation = {});
    // The global heightmap is the genuine first recognized mesh node selected
    // by original CreateNeighborTris. It may itself have ordered children.
    RetailHeightTerrain(const RetailHeightSceneNode& globalHeightmap, const CityNavigation&);
    RetailHeightTerrain(const RetailHeightTerrain&) = delete;
    RetailHeightTerrain& operator=(const RetailHeightTerrain&) = delete;
    std::uint16_t generation() const { return currentUsed; }
    const std::vector<std::vector<std::uint16_t>>& visitWords() const { return visits; }
    const RetailHeightSceneNode& heightmapNode() const { return *globalNode; }

  private:
    friend class RetailHeightController;
    const CityMesh& mesh;
    const CityNavigation& navigation;
    RetailHeightSceneNode defaultNode;
    const RetailHeightSceneNode* globalNode = nullptr;
    std::vector<std::vector<std::uint16_t>> visits;
    std::array<bool, 50> pool{};
    std::uint16_t currentUsed = 0;
    void validate(CityNavigation::TriangleRef) const;
    std::array<std::uint16_t, 3> indices(CityNavigation::TriangleRef) const;
    std::array<RetailHeightPoint, 3> vertices(CityNavigation::TriangleRef) const;
    std::array<RetailHeightPoint, 3> vertices(const CityMesh&, CityNavigation::TriangleRef,
                                           const RetailHeightPoint&) const;
    void loadGeometry(RetailHeightState&, bool normalAndMarker, const CityMesh&,
                      const RetailHeightPoint&) const;
    void validateScene(const RetailHeightSceneNode&) const;
    bool firstSeedNode(RetailHeightState&, const RetailHeightPoint&, const RetailHeightSceneNode&) const;
    bool firstSeed(RetailHeightState&, const RetailHeightPoint&) const;
};

// Original terrain query with optional typed jump composition. Steering, actor
// clock and VM binding remain outside this component. A null jump pointer is
// ordinary-only; a host that dispatches jump surfaces through its contact filter
// must pass that same actor's live jump controller here. Attachment does not
// construct a jump controller or reset its owner's current jump state.
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
                           b2Body* suppliedBody = nullptr, RetailJumpController* jump = nullptr);
    ~RetailHeightController();
    RetailHeightController(const RetailHeightController&) = delete;
    RetailHeightController& operator=(const RetailHeightController&) = delete;
    // Explicit selected-triangle initialization; this is not a full actor Init.
    void initializeSelected(std::size_t slot, CityNavigation::TriangleRef, const RetailHeightPoint&);
    // Exact node-taking helper: commits the last successful sibling and does not
    // refresh local cliff bodies. Explicit invocation is required; height() does
    // not lazily select a scene when the selected-node record is absent.
    void initializeFromScene(std::size_t slot, const RetailHeightPoint&);
    // An additional start node is exclusively borrowed with the same lifetime
    // and immutable topology contract. It need not be the global heightmap.
    void initializeFromNode(std::size_t slot, const RetailHeightSceneNode&, const RetailHeightPoint&);
    // Distinct original point overload. Reads the full overlap list, can accept
    // extrapolation, preserves cache and may leave partial identity on failure.
    // Repeated recursive states throw explicitly instead of an invented height.
    float setHeightAttributesFromPoint(std::size_t slot, const RetailHeightPoint&);
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
    // Optional original jump composition; it must outlive this controller.
    RetailJumpController* const jump;
    std::vector<RetailHeightState> states;
    std::vector<Owned> owned;
    int poolIndex = -1;
    float core(std::size_t, const RetailHeightPoint&, bool crossing, bool detailed);
    void destroy(std::size_t);
};
