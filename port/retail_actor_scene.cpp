#include "retail_actor_scene.h"
#include "retail_actor_graph.h"
#include "retail_actor_pose.h"
#include "retail_actor_resource.h"
#include "retail_morph.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
void addOriginalBox(RetailSkinBoundingBox &output, const RetailSkinBoundingBox &input) {
  // Original aabbox3d::addInternalBox0x20eaad adds MAX first, then reads MIN.
  // Keep strict comparisons, signed-zero/NaN behavior and possible alias timing.
  const auto maximum = input[1];
  for (std::size_t i = 0; i < 3; ++i)
    if (maximum[i] > output[1][i]) output[1][i] = maximum[i];
  for (std::size_t i = 0; i < 3; ++i)
    if (maximum[i] < output[0][i]) output[0][i] = maximum[i];
  const auto minimum = input[0];
  for (std::size_t i = 0; i < 3; ++i)
    if (minimum[i] > output[1][i]) output[1][i] = minimum[i];
  for (std::size_t i = 0; i < 3; ++i)
    if (minimum[i] < output[0][i]) output[0][i] = minimum[i];
}
bool originalIdentity(const RetailSceneMatrix &matrix) {
  constexpr float epsilon = 0.000001f;
  for (std::size_t i = 0; i < 4; ++i)
    if (!(1.f <= matrix[i * 5] + epsilon && 1.f >= matrix[i * 5] - epsilon))
      return false;
  for (std::size_t column = 0; column < 4; ++column)
    for (std::size_t row = 0; row < 4; ++row)
      if (row != column && !(std::abs(matrix[column * 4 + row]) <= epsilon))
        return false;
  return true;
}
} // namespace

std::shared_ptr<const RetailActorSceneSource>
loadRetailActorSceneSource(FileSystemBase &assets, const std::string &path) {
  auto source = std::make_shared<RetailActorSceneSource>();
  source->model = loadCityScene(assets, path);
  source->skins = loadCityActorSkins(source->model);
  return source;
}

const std::vector<RetailSceneMatrix> &RetailActorSceneMesh::skinMatrices() {
  if (!sourceSkin)
    throw std::logic_error("Ordinary actor mesh has no skin matrix cache.");
  if (dirty)
    prepareSkinMatrices();
  return matrices;
}
void RetailActorSceneMesh::setSkinningEnabled(bool value) {
  if (!sourceSkin)
    throw std::logic_error("Ordinary actor mesh has no skinning-enabled state.");
  dirty = !enabled && value;
  enabled = value;
}
void RetailActorSceneMesh::prepareSkinMatrices() {
  // The previous default-enabled original owner domain exposed no toggle.
  // Original disabled preparation returns without clearing shared dirty or
  // changing the existing palette; the live enabled flag now preserves that.
  if (!enabled)
    return;
  for (std::size_t i = 0; i < matrices.size(); ++i) {
    // Original collada CMatrix4*SMatrix uses rowMatrixProduct34. Live
    // cached node matrices have false identity hints, including numeric
    // identity. Preserve the literal affine arithmetic and source order.
    matrices[i] = retailSceneMultiply(*jointPointers[i],
                                      sourceSkin->joints[i].inverseBind);
    if (!bindIdentity)
      matrices[i] = retailSceneMultiply(matrices[i], sourceSkin->bindShape);
  }
  dirty = false;
}
const RetailSkinBoundingBox &RetailActorSceneMesh::boundingBox() {
  if (morphState) return base->boundingBox();
  // Unknown metadata must never expose the raw seed as a dynamic box,
  // including after palette preparation or a failed explicit compute.
  if (sourceSkin && !sourceSkin->jointBoundsCm)
    throw std::runtime_error("Missing authoritative original joint bounding boxes.");
  if (sourceSkin && dirty) {
    computeBoundingBox();
    dirty = false; // Getter clears after compute even when disabled preparation did not.
  }
  return cachedBox;
}
void RetailActorSceneMesh::computeBoundingBox() {
  if (!sourceSkin)
    throw std::logic_error(
        "Ordinary actor mesh has no dynamic skin bounds computation.");
  // Original explicit compute prepares even when clean.
  prepareSkinMatrices();
  std::vector<RetailSkinBoundsJoint> live;
  live.reserve(jointPointers.size());
  for (std::size_t i = 0; i < jointPointers.size(); ++i)
    live.push_back({jointPointers[i], jointNodes[i]->absoluteIdentityHint()});
  cachedBox = computeRetailSkinBounds(live, sourceSkin->jointBoundsCm);
}
const RetailSkinBoundingBox &RetailActorSceneNode::boundingBox() {
  if (runtimeMesh)
    return runtimeMesh->boundingBox();
  if (runtimeType == 0x72656164 || runtimeType == 0x6e656164)
    return emptyNodeBox; // Real CEmptySceneNode box; no child aggregation.
  else
    throw std::logic_error(
        "Unsupported original actor node local bounding-box type.");
}
const RetailSkinBoundingBox &RetailActorSceneNode::transformedBoundingBox() {
  if (runtimeType == retailSceneSkinnedMeshType)
    return boundingBox();
  if (flags & 0x200) {
    transformedBox = retailSceneTransformBoxEx(cachedAbsolute, boundingBox());
    flags &= ~0x200u;
  }
  return transformedBox;
}
bool RetailActorSceneNode::computeBoundingBox(RetailSkinBoundingBox &output) const {
  bool found = false;
  for (auto *child : childNodes) {
    if (child->type() == retailSceneMeshType) {
      const auto &box = child->transformedBoundingBox();
      // Literal source0x2aef41/0x2aefc1: this ordinary branch never sets found.
      if (found) output = box;
      else addOriginalBox(output, box);
    } else if (child->type() == 0x6e656164) {
      RetailSkinBoundingBox box{{{-1, -1, -1}, {1, 1, 1}}};
      if (child->computeBoundingBox(box)) {
        if (found) addOriginalBox(output, box);
        else { output = box; found = true; }
      }
    }
  }
  return found;
}
RetailActorGraph *RetailActorSceneNode::sceneManager() const {
  if (manager && managerState.expired())
    throw std::logic_error("Expired borrowed original actor scene manager.");
  return manager;
}

void RetailActorSceneNode::setPosition(const std::array<float, 3> &value) {
  localPosition = value;
  flags |= 0x20;
}
void RetailActorSceneNode::setRotation(const RetailSceneQuaternion &value) {
  localRotation = value;
  flags |= 0x10;
}
void RetailActorSceneNode::setScale(const std::array<float, 3> &value) {
  localScale = value;
  flags |= 0x08;
}
const RetailSceneMatrix &RetailActorSceneNode::relative() {
  if (flags & 0x3f) {
    if (flags & 0x1b) {
      CityNode values{};
      values.translation = localPosition;
      values.rotation = localRotation;
      values.scale = localScale;
      cachedRelative = retailSceneRelative(values);
    } else {
      // Exact original branch includes animation mask4: do not silently
      // turn a raw animated scale write into an ordinary setScale call.
      std::copy(localPosition.begin(), localPosition.end(),
                cachedRelative.begin() + 12);
    }
    flags &= ~0x38u;
  }
  return cachedRelative;
}
void RetailActorSceneNode::updateAbsolutePosition(bool recursive) {
  if ((parentNode && (parentNode->flags & 0x40)) || (flags & 0xbf)) {
    const auto &local = relative();
    cachedAbsolute =
        parentNode ? retailSceneMultiply(parentNode->cachedAbsolute, local)
                   : local;
    flags = (flags | 0x240u) & ~0x80u;
  }
  if (recursive)
    for (auto *child : childNodes)
      child->updateAbsolutePosition(true);
}
void RetailActorSceneNode::setOption(std::uint32_t mask, bool enabled) {
  if (enabled)
    options |= mask;
  else
    options &= ~mask;
}
void RetailActorSceneNode::notifyVisibilityChanged(bool visible) {
  inheritedVisible = visible;
  const bool before = effectiveVisible;
  effectiveVisible = localVisible && inheritedVisible;
  if (before != effectiveVisible)
    for (auto *child : childNodes)
      child->notifyVisibilityChanged(effectiveVisible);
  if (runtimeMesh && !visible)
    for (auto &byte : runtimeMesh->bufferState)
      byte = 0;
}
void RetailActorSceneNode::setVisible(bool visible) {
  if (localVisible != visible) {
    localVisible = visible;
    const bool before = effectiveVisible;
    effectiveVisible = localVisible && inheritedVisible;
    if (before != effectiveVisible)
      for (auto *child : childNodes)
        child->notifyVisibilityChanged(effectiveVisible);
  }
  if (runtimeMesh && !visible)
    for (auto &byte : runtimeMesh->bufferState)
      byte = 0;
}
void RetailActorSceneNode::onAnimate(std::uint32_t time) {
  if (!effectiveVisible)
    return;
  if (!option(1)) {
    if (dispatching)
      throw std::logic_error(
          "Recursive dispatch of the same actor node animator list.");
    dispatching = true;
    try {
      // Increment after callback, as the original live linked list does;
      // appending an animator is observable within this frame.
      for (auto it = animators.begin(); it != animators.end(); ++it) {
        (*it)->animateNode(*this, time);
      }
    } catch (...) {
      dispatching = false;
      throw;
    }
    dispatching = false;
    updateAbsolutePosition(false);
    for (auto *child : childNodes)
      child->onAnimate(time);
    flags &= ~0x40u;
  }
  // Ordinary CColladaMesh uses literal empty IColladaMesh::onAnimate0x2b2e0d.
  // Skin onAnimate calls that base then marks cache dirty. Both mesh node
  // overrides call mesh onAnimate after base returns, even with option1 set.
  if (runtimeMesh && runtimeMesh->sourceSkin)
    runtimeMesh->dirty = true;
}
RetailActorSceneNode *RetailActorSceneNode::findUid(std::string_view wanted) {
  wanted = wanted.substr(0, wanted.find('\0'));
  if (identity == wanted)
    return this;
  for (auto *child : childNodes)
    if (auto *result = child->findUid(wanted))
      return result;
  return nullptr;
}
RetailActorSceneNode *RetailActorSceneNode::findScope(std::string_view wanted) {
  wanted = wanted.substr(0, wanted.find('\0'));
  if (scopeIdentity == wanted)
    return this;
  for (auto *child : childNodes)
    if (auto *result = child->findScope(wanted))
      return result;
  return nullptr;
}
RetailActorSceneNode *RetailActorSceneNode::findMesh() {
  updateAbsolutePosition(false);
  if ((runtimeType == retailSceneMeshType ||
       runtimeType == retailSceneSkinnedMeshType) &&
      identity.find("Plane") == std::string::npos &&
      identity.find("Jump") == std::string::npos)
    return this;
  for (auto *child : childNodes)
    if (auto *result = child->findMesh())
      return result;
  return nullptr;
}
void RetailActorSceneNode::addAnimator(
    std::shared_ptr<RetailActorSceneAnimator> animator) {
  if (!animator)
    return;
  if (detaching)
    throw std::logic_error(
        "Cannot add an animator during actor target invalidation.");
  auto *retained = animator.get();
  animators.push_back(
      std::move(animator)); // Ordinary list intentionally allows duplicates.
  retained->onBind(*this);
  if (auto *current = sceneManager())
    current->notifyHierarchyChanged();
}
void RetailActorSceneNode::addBoundAnimator(
    std::shared_ptr<RetailActorSceneAnimator> animator) {
  if (!animator)
    return;
  if (detaching)
    throw std::logic_error(
        "Cannot retain an animator during actor target invalidation.");
  if (std::none_of(
          boundAnimators.begin(), boundAnimators.end(),
          [&](const auto &current) { return current.get() == animator.get(); }))
    boundAnimators.push_back(std::move(animator));
}
void RetailActorSceneNode::removeAnimator(RetailActorSceneAnimator &animator) {
  if (dispatching || detaching)
    throw std::logic_error(
        "Erasing an actor animator during dispatch/unbind is unsupported.");
  const auto found = std::find_if(
      animators.begin(), animators.end(),
      [&](const auto &current) { return current.get() == &animator; });
  if (found != animators.end()) {
    detaching = true;
    (*found)->onUnbind(*this);
    detaching = false;
    animators.erase(found);
    if (auto *current = sceneManager())
      current->notifyHierarchyChanged();
  }
}
void RetailActorSceneNode::removeBoundAnimator(
    RetailActorSceneAnimator &animator) {
  if (detaching)
    throw std::logic_error("Erasing an actor bound animator during "
                           "unbind is unsupported.");
  const auto found = std::find_if(
      boundAnimators.begin(), boundAnimators.end(),
      [&](const auto &current) { return current.get() == &animator; });
  if (found != boundAnimators.end()) {
    detaching = true;
    (*found)->onUnbind(*this);
    detaching = false;
    boundAnimators.erase(found);
  }
}
void RetailActorSceneNode::clearOrdinaryAnimators(bool destroying) {
  for (auto &entry : animators) {
    auto retained = std::move(entry);
    retained->onUnbind(*this);
    retained.reset(); // Original drops each animator before clearing list records.
  }
  animators.clear();
  if (destroying && manager && managerState.expired())
    return;
  if (auto *current = sceneManager())
    current->notifyHierarchyChanged();
}
void RetailActorSceneNode::removeAnimators() {
  if (dispatching || detaching)
    throw std::logic_error(
        "Clearing actor animators during dispatch/unbind is unsupported.");
  detaching = true;
  try {
    clearOrdinaryAnimators(false);
  } catch (...) {
    detaching = false;
    throw;
  }
  detaching = false;
}
std::array<float, 3> &RetailActorSceneNode::animationPosition(
    std::shared_ptr<RetailActorSceneAnimator> a) {
  if (!a)
    throw std::invalid_argument(
        "Animated position requires a retained animator.");
  addBoundAnimator(std::move(a));
  flags |= 4;
  return localPosition;
}
RetailSceneQuaternion &RetailActorSceneNode::animationRotation(
    std::shared_ptr<RetailActorSceneAnimator> a) {
  if (!a)
    throw std::invalid_argument(
        "Animated rotation requires a retained animator.");
  addBoundAnimator(std::move(a));
  flags |= 2;
  return localRotation;
}
std::array<float, 3> &RetailActorSceneNode::animationScale(
    std::shared_ptr<RetailActorSceneAnimator> a) {
  if (!a)
    throw std::invalid_argument("Animated scale requires a retained animator.");
  addBoundAnimator(std::move(a));
  flags |= 1;
  return localScale;
}
void RetailActorSceneNode::detachAnimators() noexcept {
  detaching = true;
  clearOrdinaryAnimators(true);
  for (const auto &animator : boundAnimators)
    animator->onUnbind(*this);
  boundAnimators.clear();
}

RetailActorSceneNode &RetailActorScene::add(std::uint32_t type,
                                            RetailActorSceneNode *parent,
                                            std::string uid, std::string scope,
                                            std::string name) {
  auto node = std::unique_ptr<RetailActorSceneNode>(new RetailActorSceneNode);
  node->liveIndex = liveNodes.size();
  node->runtimeType = type;
  node->identity = std::move(uid);
  node->scopeIdentity = std::move(scope);
  node->displayName = std::move(name);
  node->parentNode = parent;
  if (parent) {
    node->flags |= 0x80;
    node->notifyVisibilityChanged(parent->effectiveVisible);
    parent->childNodes.push_back(node.get());
  }
  auto &result = *node;
  liveNodes.push_back(std::move(node));
  return result;
}
RetailActorSceneMesh &RetailActorScene::ordinaryMesh(const std::string &uid) {
  const auto &source = decoded->model.meshes;
  const auto found =
      std::find_if(source.begin(), source.end(),
                   [&](const auto &mesh) { return mesh.name == uid; });
  if (found == source.end())
    throw std::runtime_error("Missing original actor geometry/raw mesh box: " +
                             uid);
  return ordinaryMesh(static_cast<std::size_t>(found - source.begin()));
}
RetailActorSceneMesh &RetailActorScene::ordinaryMesh(std::size_t index) {
  const auto &geometry = decoded->model.meshes.at(index);
  if (!geometry.rawBoundsCm)
    throw std::runtime_error("Missing original ordinary resource box.");
  auto mesh = std::unique_ptr<RetailActorSceneMesh>(new RetailActorSceneMesh);
  mesh->identity = geometry.name;
  mesh->sourceMesh = &geometry;
  mesh->cachedBox = *geometry.rawBoundsCm;
  mesh->bufferState.resize(geometry.buffers.size());
  mesh->sourceMaterials.resize(geometry.buffers.size()); // Actual embedded SMaterial ctor defaults.
  if (loaded) mesh->loadedStreams = loaded->currentStreams(geometry);
  auto &result = *mesh;
  liveMeshes.push_back(std::move(mesh));
  return result;
}
RetailActorSceneMesh &RetailActorScene::morphMesh(std::size_t index) {
  if (!loaded) throw std::logic_error("Morph resources require an explicit physical loaded resource.");
  const auto &controller = decoded->model.morphControllers.at(index);
  // The proved DefaultFactory path has an ordinary immediate morph source.
  // Never flatten another controller or silently use the skin's terminal UID.
  auto &ordinary = ordinaryMesh(controller.source);
  auto mesh = std::unique_ptr<RetailActorSceneMesh>(new RetailActorSceneMesh);
  mesh->identity = controller.name;
  mesh->sourceMesh = &ordinary.geometry();
  mesh->base = &ordinary;
  auto catalogue = std::shared_ptr<const CityScene>(decoded, &decoded->model);
  mesh->morphState = std::make_shared<RetailMorphState>(std::move(catalogue), index);
  for (const auto &target : controller.targets)
    mesh->targetMeshes.push_back(&ordinaryMesh(target.mesh));
  mesh->bufferState.resize(ordinary.bufferState.size());
  auto &result = *mesh;
  liveMeshes.push_back(std::move(mesh));
  return result;
}
RetailActorSceneMesh &RetailActorScene::skinMesh(const std::string &uid) {
  const auto &source = decoded->skins;
  const auto found =
      std::find_if(source.begin(), source.end(),
                   [&](const auto &skin) { return skin.controllerUid == uid; });
  if (found == source.end())
    throw std::runtime_error(
        "Unsupported/missing original actor skin controller: " + uid);
  RetailActorSceneMesh *sourceResource = nullptr;
  if (loaded) {
    if (found->sourceUid.empty()) throw std::runtime_error("Loaded skin lacks its original immediate source UID.");
    const auto &controllers = decoded->model.morphControllers;
    const auto morph = std::find_if(controllers.begin(), controllers.end(),
        [&](const auto &controller) { return controller.name == found->sourceUid; });
    sourceResource = morph == controllers.end() ? &ordinaryMesh(found->sourceUid) :
        &morphMesh(static_cast<std::size_t>(morph - controllers.begin()));
  } else sourceResource = &ordinaryMesh(found->meshName);
  auto &base = *sourceResource;
  if (base.geometry().name != found->meshName)
    throw std::runtime_error("Original skin immediate source/terminal geometry disagree.");
  auto mesh = std::unique_ptr<RetailActorSceneMesh>(new RetailActorSceneMesh);
  mesh->identity = uid;
  mesh->sourceMesh = base.sourceMesh;
  mesh->sourceSkin = &*found;
  mesh->base = &base;
  mesh->cachedBox = base.cachedBoundingBox();
  mesh->bufferState.resize(base.bufferState.size());
  mesh->bindIdentity = originalIdentity(found->bindShape);
  auto &result = *mesh;
  liveMeshes.push_back(std::move(mesh));
  return result;
}
void RetailActorScene::bindMaterials(RetailActorSceneNode &node,
                                    const CityGeometryRef &geometry,
                                    uint32_t instanceKind) {
  const auto count = node.runtimeMesh->geometry().buffers.size();
  // Original reserves exactly this many raw binding slots: missing entries
  // remain uninitialized, while extra authored entries can overwrite capacity.
  // Check that unsafe domain rather than fabricate a default material.
  if (geometry.materials.size() != count)
    throw std::runtime_error("Unsupported actor material/buffer binding count mismatch.");
  node.materials.reserve(count);
  for (const auto &binding : geometry.materials) {
    // Controller binding0x2964c9 selects the local index even if the unused
    // path/name fields disagree. Ordinary geometry0x2966f9 has an external arm.
    if (instanceKind == 3 && !binding.sourcePath.empty())
      throw std::runtime_error("External actor material database binding is unsupported.");
    if (!binding.sourceIndex || *binding.sourceIndex >= decoded->model.materialCatalog.size())
      throw std::runtime_error("Missing original actor material source index.");
    const auto index = *binding.sourceIndex;
    std::string_view uid = decoded->model.materialCatalog[index].uid;
    uid = uid.substr(0, uid.find('\0'));
    const auto found = std::find_if(materials.begin(), materials.end(),
                                   [&](const auto &owner) { return owner->uid() == uid; });
    if (found != materials.end())
      node.materials.push_back(*found);
    else {
      auto owner = std::shared_ptr<RetailActorMaterial>(new RetailActorMaterial(decoded, index));
      materials.push_back(owner);
      node.materials.push_back(std::move(owner));
    }
  }
}

RetailActorScene::RetailActorScene(
    std::shared_ptr<const RetailActorSceneSource> source)
    : decoded(std::move(source)) {
  construct();
}
RetailActorScene::RetailActorScene(std::shared_ptr<RetailActorLoadedResource> resource)
    : decoded(resource ? resource->source() : nullptr), loaded(std::move(resource)) {
  construct();
}
void RetailActorScene::construct() {
  if (!decoded || decoded->model.path.empty())
    throw std::invalid_argument(
        "Actor scene requires its shared original decoded resource.");
  const auto &model = decoded->model;
  // SCollada+16 describes a global library, not an attached animator. Actual
  // DefaultFactory scenes leave it unattached; the legacy metadata-only path
  // keeps its conservative domain. Loaded node attachments are checked below.
  if (model.hasSceneAnimations && !loaded)
    throw std::runtime_error(
        "Embedded actor scene animators require original factory composition.");
  if (!loaded && !model.morphControllers.empty())
    throw std::runtime_error("Actor morph resources require the original morph "
                             "runtime implementation.");
  rootNode = &add(0x72656164, nullptr, model.path);
  std::size_t covered = 0;
  for (const auto &visual : model.visualScenes) {
    if (visual.firstNode != covered ||
        visual.nodeCount > model.nodes.size() - covered)
      throw std::runtime_error("Invalid actor active visual source ranges.");
    const auto end = covered + visual.nodeCount;
    auto &visualNode = add(0x6e656164, rootNode, {}, {}, visual.displayName);
    std::vector<std::vector<std::size_t>> children(visual.nodeCount);
    std::vector<unsigned> depths(visual.nodeCount);
    std::vector<std::size_t> roots;
    for (std::size_t i = covered; i < end; ++i) {
      const auto parent = model.nodes[i].parent;
      if (parent == -1)
        roots.push_back(i);
      else {
        if (parent < 0 || std::size_t(parent) < covered ||
            std::size_t(parent) >= i)
          throw std::runtime_error("Invalid actor source hierarchy.");
        depths[i - covered] = depths[std::size_t(parent) - covered] + 1;
        if (depths[i - covered] > 64)
          throw std::runtime_error("Invalid actor source hierarchy depth.");
        children[std::size_t(parent) - covered].push_back(i);
      }
    }
    std::function<void(std::size_t, RetailActorSceneNode &)> build =
        [&](std::size_t i, auto &parent) {
          const auto &authored = model.nodes[i];
          if (authored.factorySelector)
            throw std::runtime_error(
                "Unsupported alternate actor node factory selector.");
          auto &container = add(0x6e656164, &parent, authored.name,
                                authored.jointName, authored.displayName);
          container.authoredIndex = i;
          container.setPosition(authored.translation);
          container.setRotation(authored.rotation);
          container.setScale(authored.scale);
          std::vector<bool> bound(authored.geometry.size());
          for (const auto &instance : authored.instances) {
            if (loaded && (instance.kind < 2 || instance.kind > 4))
              throw std::runtime_error("Unsupported actual loaded actor instance/animator attachment.");
            if (loaded && !instance.sourcePath.empty())
              throw std::runtime_error("External loaded actor instance requires its genuine resource factory.");
            if ((instance.kind == 2 || instance.kind == 3) &&
                instance.geometry) {
              const auto &ref = authored.geometry.at(*instance.geometry);
              if (bound.at(*instance.geometry) || !ref.sourcePath.empty() ||
                  !instance.sourcePath.empty())
                throw std::runtime_error(
                    "Unsupported duplicate/external actor mesh factory.");
              bound[*instance.geometry] = true;
              auto &mesh = instance.kind == 2 ? skinMesh(instance.target)
                                              : ordinaryMesh(ref.meshName);
              if (mesh.sourceMesh->name != ref.meshName)
                throw std::runtime_error(
                    "Actor skin controller/geometry identity mismatch.");
              const auto type = instance.kind == 2
                                    ? retailSceneSkinnedMeshType
                                    : retailSceneMeshRuntimeType(model.path);
              auto &child = add(type, &container, mesh.uid());
              child.runtimeMesh = &mesh;
              bindMaterials(child, ref, instance.kind);
            } else if (instance.kind == 3) {
              if (!instance.localSpline || !instance.sourcePath.empty())
                throw std::runtime_error(
                    "Missing actor ordinary geometry instance.");
            } else if (instance.kind == 4) {
              if (std::find(model.lightUids.begin(), model.lightUids.end(),
                            instance.target) != model.lightUids.end())
                throw std::runtime_error("Resolved actor light requires its "
                                         "original light runtime owner.");
            } else if (instance.kind == 1 || instance.kind == 2 ||
                       (instance.kind >= 9 && instance.kind <= 11))
              throw std::runtime_error(
                  "Unsupported actor instance factory kind.");
          }
          if (std::find(bound.begin(), bound.end(), false) != bound.end())
            throw std::runtime_error(
                "Missing actor authored geometry metadata.");
          for (auto child : children[i - covered])
            build(child, container);
        };
    for (auto i : roots)
      build(i, visualNode);
    covered = end;
  }
  if (covered != model.nodes.size())
    throw std::runtime_error("Missing actor active visual-scene grouping.");
  rootNode->updateAbsolutePosition(
      true); // Default root postLoad precedes attachSkin.
  // Actual CRootSceneNode::onPostLoad0x293da9 computes this separate private
  // cache before attaching skins. Getter side effects remain observable even
  // when the helper's literal false return prevents descendant aggregation.
  rootNode->computeBoundingBox(postLoadBox);
  for (auto &mesh : liveMeshes) {
    if (!mesh->sourceSkin)
      continue;
    for (const auto &joint : mesh->sourceSkin->joints) {
      auto *node = rootNode->findScope(joint.scope);
      if (!node)
        throw std::runtime_error("Missing live actor joint scope: " +
                                 joint.scope);
      mesh->jointNodes.push_back(node);
      mesh->jointPointers.push_back(&node->cachedAbsolute);
    }
    mesh->matrices.resize(mesh->jointPointers.size(), retailSceneIdentity);
  }
}
RetailActorScene::~RetailActorScene() {
  RetailActorGraph::detachOwnedRoot(*rootNode);
  for (auto &node : liveNodes)
    node->detachAnimators();
  // All target invalidation is complete while every matrix/record is alive.
  liveMeshes.clear();
  liveNodes.clear();
}
bool RetailActorScene::isDeformationSource(const RetailActorSceneMesh &source) const {
  if (source.skin() || source.morphRuntime() ||
      std::none_of(liveMeshes.begin(), liveMeshes.end(), [&](const auto &mesh) { return mesh.get() == &source; }))
    return false;
  std::function<bool(const RetailActorSceneMesh&)> contains = [&](const auto &mesh) {
    if (&mesh == &source) return true;
    if (mesh.skin() || mesh.morphRuntime()) {
      if (contains(mesh.baseMesh())) return true;
      for (auto *target : mesh.morphTargets()) if (contains(*target)) return true;
    }
    return false;
  };
  for (const auto &mesh : liveMeshes)
    if ((mesh->skin() || mesh->morphRuntime()) && contains(*mesh)) return true;
  return false;
}

std::unique_ptr<RetailActorSubtree>
RetailActorScene::extractOrdinarySubtree(RetailActorSceneNode &root, RetailActorPose *allowedPose) {
  if (&root == rootNode)
    throw std::logic_error("Actor root is not an ordinary detachable subtree.");
  if (allowedPose && !allowedPose->boundTo(*this))
    throw std::logic_error("Bound shadow transfer requires this actor's genuine live plain pose.");
  std::set<RetailActorSceneNode *> nodes;
  std::set<RetailActorSceneMesh *> meshes;
  std::function<void(RetailActorSceneNode &)> visit = [&](auto &node) {
    if (!nodes.insert(&node).second)
      throw std::logic_error("Cyclic or repeated borrowed actor subtree.");
    if (std::none_of(liveNodes.begin(), liveNodes.end(),
                     [&](const auto &owned) { return owned.get() == &node; }))
      throw std::logic_error("External borrowed subtree ownership is unsupported.");
    if (!node.animators.empty())
      throw std::logic_error(
          "Ordinary frame-animator subtree transfer is unsupported.");
    if (!node.boundAnimators.empty()) {
      if (!allowedPose || std::any_of(node.boundAnimators.begin(), node.boundAnimators.end(),
                                     [&](const auto &entry) { return entry.get() != allowedPose; }))
        throw std::logic_error("Only the genuine explicitly supplied plain pose may move with its shadow.");
      bool actualTarget = false;
      for (std::size_t i = 0; i < allowedPose->bindingCount(); ++i)
        actualTarget = actualTarget || allowedPose->target(i) == &node;
      if (!actualTarget)
        throw std::logic_error("Moved node retains a plain pose without its actual live target binding.");
    }
    if (node.runtimeMesh) {
      if (node.runtimeMesh->sourceSkin || node.runtimeMesh->base)
        throw std::logic_error(
            "Skinned subtree ownership requires its external joint dependencies.");
      meshes.insert(node.runtimeMesh);
    }
    for (auto *child : node.childNodes)
      visit(*child);
  };
  visit(root);
  for (auto *mesh : meshes)
    if (isDeformationSource(*mesh))
      throw std::logic_error("Detached ordinary resource is still a live skin/morph dependency.");
  for (const auto &mesh : liveMeshes)
    for (auto *joint : mesh->jointNodes)
      if (nodes.count(joint))
        throw std::logic_error(
            "Detached ordinary subtree is still borrowed by a live skin cache.");
  for (const auto &node : liveNodes)
    if (!nodes.count(node.get()) && meshes.count(node->runtimeMesh))
      throw std::logic_error(
          "Mutable ordinary mesh is shared outside the detached subtree.");
  auto result = std::unique_ptr<RetailActorSubtree>(new RetailActorSubtree);
  result->decoded = decoded;
  result->liveNodes.reserve(nodes.size());
  result->liveMeshes.reserve(meshes.size());
  result->rootNode = &root;
  for (auto it = liveNodes.begin(); it != liveNodes.end();)
    if (nodes.count(it->get())) {
      result->liveNodes.push_back(std::move(*it));
      it = liveNodes.erase(it);
    } else
      ++it;
  for (auto it = liveMeshes.begin(); it != liveMeshes.end();)
    if (meshes.count(it->get())) {
      result->liveMeshes.push_back(std::move(*it));
      it = liveMeshes.erase(it);
    } else
      ++it;
  // Allocation IDs are never renumbered; owner vectors remain dense iteration
  // views. Moved node/mesh/matrix addresses and source indices are unchanged.
  return result;
}
