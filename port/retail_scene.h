#pragma once

#include "city_assets.h"
#include "retail_height.h"

#include <array>
#include <memory>
#include <vector>

using RetailSceneMatrix = std::array<float, 16>;
using RetailSceneQuaternion = std::array<float, 4>;
inline constexpr RetailSceneMatrix retailSceneIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
inline constexpr uint32_t retailSceneMeshType = 0x6d656164;
inline constexpr uint32_t retailSceneSkinnedMeshType = 0x73656164;
inline constexpr uint32_t retailSceneEmptyType = 0x79746d65;

// Original raw quaternion returning-matrix path, scale comparison and literal
// affine arithmetic. Identity child multiplication must not be algebraically removed.
RetailSceneMatrix retailSceneRelative(const CityNode&, bool omitScale = false);
RetailSceneMatrix retailSceneMultiply(const RetailSceneMatrix&, const RetailSceneMatrix&);
// Original quaternion::normalize0x1c8d11 and Euler set(0,0,z)0x2185c9.
// Literal exact-one shortcut/float sum/sqrt/reciprocal; no zero-quaternion fallback.
// These are explicit actor/jump operations, never implicit raw SNode normalization.
RetailSceneQuaternion retailSceneNormalizeQuaternion(RetailSceneQuaternion);
RetailSceneQuaternion retailSceneYawQuaternion(float angleRadians);
// CColladaMeshSceneNode::getType uses the actual database path, not mesh/SNode UID
// or game-object category tables. Applies only to this ordinary mesh factory.
uint32_t retailSceneMeshRuntimeType(const std::string& databasePath);

// Owns decoded storage and stable native records for the evidenced default static
// Collada factory path. Its view/mesh addresses outlive terrain/controllers that
// borrow them. Construct once for a fresh world's first-recognized selection;
// destruction requires those borrowers already destroyed. No global ARM pointers.
class RetailHeightSceneBinding {
  public:
    explicit RetailHeightSceneBinding(CityScene);
    ~RetailHeightSceneBinding();
    RetailHeightSceneBinding(const RetailHeightSceneBinding&) = delete;
    RetailHeightSceneBinding& operator=(const RetailHeightSceneBinding&) = delete;
    const RetailHeightSceneNode& root() const;
    const RetailHeightSceneNode& globalHeightmap() const;
    const RetailSceneMatrix& absoluteTransform(const RetailHeightSceneNode&) const;
    const CityScene& source() const { return scene; }
    size_t nodeCount() const { return nodes.size(); }

  private:
    struct Node;
    CityScene scene;
    std::vector<std::unique_ptr<Node>> nodes;
    Node* rootNode = nullptr;
    const RetailHeightSceneNode* selected = nullptr;
    Node& add(uint32_t type, const RetailSceneMatrix&, Node* parent, const CityMesh* mesh = nullptr);
};

// Original GS3DStuff::load selects this exact resource before CreateNeighborTris.
// Navigation data and the physics world remain separately owned host prerequisites.
std::unique_ptr<RetailHeightSceneBinding> loadOriginalHeightScene(FileSystemBase&);
