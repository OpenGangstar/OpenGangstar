#pragma once

#include "retail_scene.h"
#include "retail_skin_bounds.h"

#include <list>
#include <memory>
#include <optional>
#include <string_view>

class RetailActorScene;
class RetailActorSceneNode;
class RetailActorGraph;
class RetailActorSubtree;
struct RetailActorGraphState;

// Ordinary frame dispatch and target-retention are separate original lists.
// onUnbind must invalidate this node's target references before they can die.
// Callbacks may write TRS/visibility/options. Erasing animator lists during
// their dispatch/unbind is outside the original safe iterator domain and is
// rejected.
class RetailActorSceneAnimator {
public:
  virtual ~RetailActorSceneAnimator() = default;
  virtual void onBind(RetailActorSceneNode &) = 0;
  virtual void onUnbind(RetailActorSceneNode &) noexcept = 0;
  virtual void animateNode(RetailActorSceneNode &, std::uint32_t timeMs) = 0;
};

// Share these decoded immutable resources across live instances. Source node
// indices and controller indices are catalogue metadata, never runtime handles.
struct RetailActorSceneSource {
  CityScene model;
  std::vector<CitySkin> skins;
};
std::shared_ptr<const RetailActorSceneSource>
loadRetailActorSceneSource(FileSystemBase &, const std::string &path);

// Every original factory construction owns a separate mutable resource, even
// for the same source UID. Only geometry()/skin() refer to shared decoded data.
class RetailActorSceneMesh {
public:
  RetailActorSceneMesh(const RetailActorSceneMesh &) = delete;
  RetailActorSceneMesh &operator=(const RetailActorSceneMesh &) = delete;
  const std::string &uid() const { return identity; }
  const CityMesh &geometry() const { return *sourceMesh; }
  const CitySkin *skin() const { return sourceSkin; }
  RetailActorSceneMesh &baseMesh() { return base ? *base : *this; }
  const RetailActorSceneMesh &baseMesh() const { return base ? *base : *this; }
  const std::vector<RetailActorSceneNode *> &joints() const {
    return jointNodes;
  }
  const std::vector<const RetailSceneMatrix *> &jointMatrices() const {
    return jointPointers;
  }
  const std::vector<RetailSceneMatrix> &skinMatrices();
  // Shares the original skin dirty flag with skinMatrices(). Explicit compute
  // forces genuine cache preparation even clean; palette-first can leave an
  // older cached box visible. Ordinary meshes retain their actual raw box.
  const RetailSkinBoundingBox &boundingBox();
  void computeBoundingBox();
  bool skinCacheDirty() const { return dirty; }
  // Original buffer byte9 is not initialized by the mesh-buffer constructors.
  // A renderer may establish its actual byte. Hiding a mesh writes zero even
  // on repeated hide. Unknown is explicit; do not invent an initial value.
  const std::vector<std::optional<std::uint8_t>> &bufferByte9() const {
    return bufferState;
  }
  void setBufferByte9(std::size_t index, std::uint8_t value) {
    bufferState.at(index) = value;
  }

private:
  friend class RetailActorScene;
  friend class RetailActorSceneNode;
  friend class RetailActorSubtree;
  RetailActorSceneMesh() = default;
  void prepareSkinMatrices();
  std::string identity;
  const CityMesh *sourceMesh = nullptr;
  const CitySkin *sourceSkin = nullptr;
  RetailActorSceneMesh *base = nullptr;
  std::vector<RetailActorSceneNode *> jointNodes;
  std::vector<const RetailSceneMatrix *> jointPointers;
  std::vector<RetailSceneMatrix> matrices;
  std::vector<std::optional<std::uint8_t>> bufferState;
  // Required raw source geometry seeds this once during construction.
  RetailSkinBoundingBox cachedBox{};
  bool dirty = true, bindIdentity = false;
};

class RetailActorSceneNode {
public:
  RetailActorSceneNode(const RetailActorSceneNode &) = delete;
  RetailActorSceneNode &operator=(const RetailActorSceneNode &) = delete;
  const std::string &uid() const { return identity; }
  const std::string &scope() const { return scopeIdentity; }
  const std::string &name() const { return displayName; }
  std::uint32_t type() const { return runtimeType; }
  std::size_t index() const { return liveIndex; }
  std::optional<std::size_t> sourceIndex() const { return authoredIndex; }
  RetailActorSceneNode *parent() const { return parentNode; }
  const std::vector<RetailActorSceneNode *> &children() const {
    return childNodes;
  }
  RetailActorSceneMesh *mesh() const { return runtimeMesh; }
  const std::array<float, 3> &position() const { return localPosition; }
  const RetailSceneQuaternion &rotation() const { return localRotation; }
  const std::array<float, 3> &scale() const { return localScale; }
  const RetailSceneMatrix &absolute() const { return cachedAbsolute; }
  // Original constructor runs update with dirty0x38, clearing this hint.
  // All supported writes preserve false; no raw matrix/hint setter exists.
  // This is a constructor/update invariant, never numeric matrix equality.
  bool absoluteIdentityHint() const { return false; }
  const RetailSkinBoundingBox &boundingBox();
  // Original skin-node getter aliases its mesh box; ordinary/container
  // transformed-box implementations are outside this bounded API.
  const RetailSkinBoundingBox &transformedBoundingBox();
  const RetailSceneMatrix &relative();
  std::uint32_t transformFlags() const { return flags; }
  void setPosition(const std::array<float, 3> &);
  void setRotation(const RetailSceneQuaternion &);
  void setScale(const std::array<float, 3> &);
  void updateAbsolutePosition(bool recursive);
  void setOption(std::uint32_t mask, bool);
  bool option(std::uint32_t mask) const { return (options & mask) != 0; }
  bool visible() const { return effectiveVisible; }
  bool locallyVisible() const { return localVisible; }
  bool parentVisible() const { return inheritedVisible; }
  void setVisible(bool);
  RetailActorGraph *sceneManager() const;
  std::int32_t renderingLayer() const { return layer; }
  void setRenderingLayer(std::int32_t value) { layer = value; }
  void onAnimate(std::uint32_t timeMs);
  RetailActorSceneNode *findUid(std::string_view);
  RetailActorSceneNode *findScope(std::string_view);
  RetailActorSceneNode *findMesh(); // Original FindAMesh: no visibility filter.
  void addAnimator(std::shared_ptr<RetailActorSceneAnimator>);
  void addBoundAnimator(std::shared_ptr<RetailActorSceneAnimator>);
  void removeAnimator(RetailActorSceneAnimator &);
  void removeAnimators(); // Ordinary list only; original always notifies a live manager.
  void removeBoundAnimator(RetailActorSceneAnimator &);
  std::size_t animatorCount() const { return animators.size(); }
  std::size_t boundAnimatorCount() const { return boundAnimators.size(); }
  // Original forceBind retains once in the bound list and sets persistent
  // animation mask1/2/4. Returned references are live local TRS, never source.
  std::array<float, 3> &
      animationPosition(std::shared_ptr<RetailActorSceneAnimator>);
  RetailSceneQuaternion &
      animationRotation(std::shared_ptr<RetailActorSceneAnimator>);
  std::array<float, 3> &
      animationScale(std::shared_ptr<RetailActorSceneAnimator>);

private:
  friend class RetailActorScene;
  friend class RetailActorGraph;
  friend class RetailActorSubtree;
  RetailActorSceneNode() = default;
  void notifyVisibilityChanged(bool);
  void detachAnimators() noexcept;
  void clearOrdinaryAnimators(bool destroying);
  std::string identity, scopeIdentity, displayName;
  std::uint32_t runtimeType = 0, flags = 0x240, options = 0;
  std::size_t liveIndex = 0;
  std::optional<std::size_t> authoredIndex;
  RetailActorSceneNode *parentNode = nullptr;
  std::vector<RetailActorSceneNode *> childNodes;
  RetailActorSceneMesh *runtimeMesh = nullptr;
  RetailActorGraph *manager = nullptr;
  std::weak_ptr<RetailActorGraphState> managerState;
  std::int32_t layer = 0;
  std::array<float, 3> localPosition{}, localScale{1, 1, 1};
  RetailSceneQuaternion localRotation{0, 0, 0, 1};
  RetailSceneMatrix cachedRelative = retailSceneIdentity,
                    cachedAbsolute = retailSceneIdentity;
  bool effectiveVisible = true, localVisible = true, inheritedVisible = true;
  bool dispatching = false, detaching = false;
  std::list<std::shared_ptr<RetailActorSceneAnimator>> animators,
      boundAnimators;
};

// Owns stable mutable records/resources. Source stays shared and immutable.
// Every borrower (body/contact metadata, pose binder, renderer) must release
// its pointers before scene destruction. Destroying the owner invalidates
// retained animator targets while every node/matrix still exists, before
// freeing records.
class RetailActorScene {
public:
  explicit RetailActorScene(std::shared_ptr<const RetailActorSceneSource>);
  ~RetailActorScene();
  RetailActorScene(const RetailActorScene &) = delete;
  RetailActorScene &operator=(const RetailActorScene &) = delete;
  RetailActorSceneNode &root() const { return *rootNode; }
  const RetailActorSceneSource &source() const { return *decoded; }
  const std::vector<std::unique_ptr<RetailActorSceneNode>> &nodes() const {
    return liveNodes;
  }
  const std::vector<std::unique_ptr<RetailActorSceneMesh>> &meshes() const {
    return liveMeshes;
  }

private:
  friend class RetailActorGraph;
  std::unique_ptr<RetailActorSubtree> extractOrdinarySubtree(RetailActorSceneNode &);
  RetailActorSceneNode &add(std::uint32_t, RetailActorSceneNode *,
                            std::string uid = {}, std::string scope = {},
                            std::string name = {});
  RetailActorSceneMesh &ordinaryMesh(const std::string &);
  RetailActorSceneMesh &skinMesh(const std::string &);
  std::shared_ptr<const RetailActorSceneSource> decoded;
  std::vector<std::unique_ptr<RetailActorSceneNode>> liveNodes;
  std::vector<std::unique_ptr<RetailActorSceneMesh>> liveMeshes;
  RetailActorSceneNode *rootNode = nullptr;
};
