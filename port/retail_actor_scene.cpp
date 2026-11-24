#include "retail_actor_scene.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

namespace {
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
  if (dirty) {
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
  return matrices;
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
  for (const auto &animator : animators)
    animator->onUnbind(*this);
  animators.clear();
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
  if (found == source.end() || !found->rawBoundsCm)
    throw std::runtime_error("Missing original actor geometry/raw mesh box: " +
                             uid);
  auto mesh = std::unique_ptr<RetailActorSceneMesh>(new RetailActorSceneMesh);
  mesh->identity = uid;
  mesh->sourceMesh = &*found;
  mesh->bufferState.resize(found->buffers.size());
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
  auto &base = ordinaryMesh(found->meshName);
  auto mesh = std::unique_ptr<RetailActorSceneMesh>(new RetailActorSceneMesh);
  mesh->identity = uid;
  mesh->sourceMesh = base.sourceMesh;
  mesh->sourceSkin = &*found;
  mesh->base = &base;
  mesh->bufferState.resize(base.bufferState.size());
  mesh->bindIdentity = originalIdentity(found->bindShape);
  auto &result = *mesh;
  liveMeshes.push_back(std::move(mesh));
  return result;
}
RetailActorScene::RetailActorScene(
    std::shared_ptr<const RetailActorSceneSource> source)
    : decoded(std::move(source)) {
  if (!decoded || decoded->model.path.empty())
    throw std::invalid_argument(
        "Actor scene requires its shared original decoded resource.");
  const auto &model = decoded->model;
  if (model.hasSceneAnimations)
    throw std::runtime_error(
        "Embedded actor scene animators require original factory composition.");
  if (!model.morphControllers.empty())
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
  for (auto &node : liveNodes)
    node->detachAnimators();
  // All target invalidation is complete while every matrix/record is alive.
  liveMeshes.clear();
  liveNodes.clear();
}
