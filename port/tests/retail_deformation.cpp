#include "retail_deformation.h"
#include "filesystem.h"
#include "retail_actor_graph.h"

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
bool failNextAllocation = false;
}
void *operator new(std::size_t bytes) {
  if (failNextAllocation) {
    failNextAllocation = false;
    throw std::bad_alloc();
  }
  if (void *result = std::malloc(bytes ? bytes : 1))
    return result;
  throw std::bad_alloc();
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
  std::free(pointer);
}

namespace {
using Mesh = RetailDeformationMesh;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char *message) {
  bool rejected = false;
  try {
    action();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, message);
}
std::shared_ptr<CityMesh> geometry(std::string name = "body",
                                   std::size_t first = 2) {
  auto result = std::make_shared<CityMesh>();
  result->name = std::move(name);
  for (unsigned i = 0; i < 16; ++i)
    result->positions.push_back({float(i + 1), float(i + 2), float(i + 3)});
  result->normals.assign(16, {1, 0, 0});
  result->texCoords.assign(16, {.25f, .75f});
  result->colors.assign(16, {.2f, .3f, .4f, .5f});
  result->secondaryColors.assign(16, {.6f, .7f, .8f, .9f});
  result->auxiliaryTexCoords.assign(16, {.125f, .625f});
  result->buffers.push_back(
      {"material",
       {uint16_t(first), uint16_t(first + 1), uint16_t(first + 4)}});
  result->rawBoundsCm = RetailSkinBoundingBox{{{-1, -2, -3}, {4, 5, 6}}};
  return result;
}
std::shared_ptr<RetailMaterialState> material(bool lighting) {
  // Explicit synthetic original scalar state; these are existing supplied
  // handles for the process owner, not an invented material-preparation host.
  return std::make_shared<RetailMaterialState>(
      RetailMaterialState{1, lighting ? 0x387u : 0x385u, 0xff112233, 0x3f3f3f3f,
                          0xff445566, 0x87123456, 0xfd});
}
struct Base {
  std::shared_ptr<const CityMesh> metadata;
  std::shared_ptr<RetailSkinningOutput> storage;
  std::vector<std::shared_ptr<RetailMaterialState>> materials;
  Mesh::Ptr mesh;
};
Base base(std::shared_ptr<const CityMesh> metadata, bool lighting) {
  Base result{metadata,
              std::make_shared<RetailSkinningOutput>(
                  RetailSkinningOutput{metadata->positions, metadata->normals}),
              {},
              {}};
  for (std::size_t i = 0; i < metadata->buffers.size(); ++i)
    result.materials.push_back(material(lighting));
  result.mesh = Mesh::source(metadata, result.storage, result.materials);
  return result;
}
std::shared_ptr<CityScene> morphCatalogue(const CityMesh &source) {
  auto result = std::make_shared<CityScene>();
  result->path = "synthetic.bdae";
  result->meshes.push_back(source);
  result->meshes.push_back(source);
  result->meshes[1].name = "target";
  result->meshes[1].positions.assign(source.positions.size(), {40, 50, 60});
  result->meshes[1].normals.assign(source.positions.size(), {0, 1, 0});
  result->morphControllers.push_back({"morph", source.name, 0, {{1, .25f}}});
  return result;
}
void bindCatalogue(Base &source, const std::shared_ptr<CityScene> &catalogue) {
  source.metadata =
      std::shared_ptr<const CityMesh>(catalogue, &catalogue->meshes[0]);
  source.mesh = Mesh::source(source.metadata, source.storage, source.materials);
}
CityNode node(std::string name, std::string scope) {
  CityNode result{};
  result.name = std::move(name);
  result.jointName = std::move(scope);
  result.parent = -1;
  result.rotation = {0, 0, 0, 1};
  result.scale = {1, 1, 1};
  return result;
}
std::shared_ptr<RetailActorSceneSource>
actorResource(std::string sourceUid = "body", bool declareMorph = false) {
  auto source = std::make_shared<RetailActorSceneSource>();
  auto &scene = source->model;
  scene.path = "actor.bdae";
  scene.materialCatalog.push_back({"material", {}});
  scene.meshes.push_back(*geometry());
  if (declareMorph)
    scene.morphControllers.push_back({"morph", "body", 0, {}});
  scene.nodes = {node("body-node", {}), node("root", "Bone")};
  scene.nodes[0].geometry.push_back({{}, "body", {{{}, "material", 0}}});
  scene.nodes[0].instances.push_back({2, 0, {}, "skin", false});
  scene.visualScenes.push_back({"visual", "display", 0, 2});
  CitySkin skin{};
  skin.controllerUid = "skin";
  skin.sourceUid = std::move(sourceUid);
  skin.meshName = "body";
  skin.bindShape = retailSceneIdentity;
  skin.joints.push_back({1, retailSceneIdentity, "Bone"});
  skin.vertices.resize(16, {{0, 1}});
  skin.jointBoundsCm = std::vector<RetailSkinBoundingBox>{};
  source->skins.push_back(skin);
  return source;
}
std::shared_ptr<RetailActorScene> actor(std::string sourceUid = "body",
                                        bool declareMorph = false) {
  return std::make_shared<RetailActorScene>(
      actorResource(std::move(sourceUid), declareMorph));
}
RetailActorSceneMesh &palette(RetailActorScene &scene) {
  for (const auto &mesh : scene.meshes())
    if (mesh->skin())
      return *mesh;
  throw std::runtime_error("Missing synthetic real palette owner");
}
Base actorBase(const std::shared_ptr<RetailActorScene> &scene, bool lighting) {
  auto &mesh = palette(*scene);
  auto result =
      base(std::shared_ptr<const CityMesh>(scene, &mesh.geometry()), lighting);
  for (std::size_t slot = 0; slot < result.materials.size(); ++slot) {
    auto &state = mesh.baseMesh().sourceMaterial(slot);
    state = *result.materials[slot]; // Explicit synthetic live input mutation.
    result.materials[slot] =
        std::shared_ptr<RetailMaterialState>(scene, &state);
  }
  result.mesh = Mesh::source(scene, mesh.baseMesh(), result.storage);
  require(&result.mesh->view(0).boundingBox() ==
              &mesh.baseMesh().cachedBoundingBox(),
          "Actual ordinary source borrows its constructed box, not the "
          "immutable resource box");
  return result;
}

void processFlags() {
  auto source = base(geometry(), true);
  auto catalog = morphCatalogue(*source.metadata);
  bindCatalogue(source, catalog);
  auto morph = Mesh::morph(source.mesh, catalog, 0, true);
  auto initial = morph->view(0);
  require(!initial.positionsPresent() && !initial.normalsPresent(),
          "Deferred proxies clear only primary position/normal");
  require(&initial.topology() == &source.metadata->buffers[0] &&
              &initial.material() == source.materials[0].get() &&
              &initial.boundingBox() == &*source.metadata->rawBoundsCm &&
              initial.firstVertex() == 2 && initial.endVertex() == 7,
          "Proxy borrows actual topology/material/box and signed used range");
  require(morph->requestProcess(0, 3, RetailProcessBufferType::persistent,
                                false) == 16,
          "Missing persistent output returns16");
  require(morph->requestProcess(0, 3, RetailProcessBufferType::persistent,
                                true) == 4,
          "Real persistent allocation returns4");
  auto allocated = morph->view(0);
  const auto *positions = &allocated.streams().currentPositions;
  require(positions->size() == 16 && allocated.firstVertex() == 2 &&
              allocated.endVertex() == 7,
          "Checked native storage preserves full original indices with only "
          "the used range exposed");
  require(morph->requestProcess(0, 3, RetailProcessBufferType::persistent,
                                false) == 4 &&
              &morph->view(0).streams().currentPositions == positions,
          "Exact persistent reuse preserves storage");
  require(morph->requestProcess(0, 1, RetailProcessBufferType::persistent,
                                false) == 9 &&
              &morph->view(0).streams().currentPositions == positions,
          "Persistent mask mismatch returns9 without descriptor mutation");
  require(morph->requestProcess(0, 1, RetailProcessBufferType::transient,
                                false) == 16,
          "Retained persistent output suppresses transient allocation");
  rejects([&] { morph->release(0); },
          "Native rejects unsafe persistent-as-transient release");
  auto failing = Mesh::morph(source.mesh, catalog, 0, true);
  require(failing->requestProcess(0, 0, RetailProcessBufferType::persistent,
                                  true) == 8 &&
              !failing->view(0).positionsPresent(),
          "Zero stream mask produces8 without a fabricated output");
  failNextAllocation = true;
  require(failing->requestProcess(0, 3, RetailProcessBufferType::persistent,
                                  true) == 8 &&
              !failing->view(0).positionsPresent(),
          "Native allocation failure returns8 even for nonzero first; no "
          "legacy pointer-underflow success");
  rejects(
      [&] {
        failing->requestProcess(0, 2, RetailProcessBufferType::transient,
                                false);
      },
      "Unported stream mask is explicit");
  rejects(
      [&] {
        failing->requestProcess(0, 3, static_cast<RetailProcessBufferType>(2),
                                false);
      },
      "Unported process type is explicit");
}

void morphOwnership() {
  auto source = base(geometry(), true);
  auto catalog = morphCatalogue(*source.metadata);
  bindCatalogue(source, catalog);
  auto first = Mesh::morph(source.mesh, catalog, 0, true),
       second = Mesh::morph(source.mesh, catalog, 0, true);
  require(first->prepare(0, 0) == 16 && !first->processBuffersPresent(),
          "Registration16 does not compute a fake initial pose");
  require(first->prepare(1, 0) == 4 && first->processBuffersPresent(),
          "Transient preparation computes the actual morph");
  auto retained = first->view(0);
  const auto value = retained.streams().currentPositions[2];
  require(value == std::array<float, 3>{12.25f, 15.5f, 18.75f},
          "Literal original weighted source then target");
  require(source.storage->positions == source.metadata->positions &&
              source.storage->normals == source.metadata->normals,
          "Mask3 output is distinct from genuine current source streams");
  require(
      second->prepare(1, 0) == 4 &&
          &retained.streams().currentPositions !=
              &second->view(0).streams().currentPositions,
      "Independent mutable process owners do not share output by source UID");
  first->release(0);
  require(!first->view(0).positionsPresent() &&
              !first->view(0).normalsPresent() &&
              !first->processBuffersPresent(),
          "Transient release clears descriptors and byte36");
  require(retained.streams().currentPositions[2] == value,
          "A retained view safely owns released backing storage");
  first->setMorphWeight(0, .5f);
  require(
      first->prepare(1, 0) == 4 &&
          first->view(0).streams().currentPositions[2] != value &&
          retained.streams().currentPositions[2] == value,
      "New transient frame does not overwrite a released retained allocation");
  std::weak_ptr<const CityMesh> metadata = source.metadata;
  source.metadata.reset();
  source.mesh.reset();
  first.reset();
  second.reset();
  catalog.reset();
  require(!metadata.expired() && retained.geometry().name == "body" &&
              retained.topology().indices[0] == 2,
          "Retained metadata survives every process/source owner");
  retained = RetailDeformationView{};
  require(metadata.expired(),
          "Metadata releases after the final retained view");
}

void sourceSharingAndValidation() {
  auto catalogue = morphCatalogue(*geometry());
  auto source = base(
      std::shared_ptr<const CityMesh>(catalogue, &catalogue->meshes[0]), false);
  auto second = Mesh::source(source.metadata, source.storage, source.materials);
  auto firstMorph = Mesh::morph(source.mesh, catalogue, 0, false);
  auto secondMorph = Mesh::morph(second, catalogue, 0, false);
  require(firstMorph->prepare(0, 0) == 4 &&
              source.storage->normals[2] == std::array<float, 3>{.75f, .25f, 0},
          "Eager mask1 writes genuine shared current normals");
  require(
      secondMorph->prepare(0, 0) == 4 &&
          source.storage->normals[2] ==
              std::array<float, 3>{.5625f, .4375f, 0} &&
          &second->view(0).streams().currentNormals ==
              &source.mesh->view(0).streams().currentNormals &&
          source.metadata->normals[2] == std::array<float, 3>{1, 0, 0},
      "Two constructed sources share only explicitly aliased mutable streams");
  auto count = source.storage->positions.size();
  source.storage->positions.pop_back();
  rejects([&] { source.mesh->view(0); },
          "Resized borrowed current storage is rejected before consumption");
  source.storage->positions.resize(count);
  rejects([&] { Mesh::source(source.metadata, source.storage, {}); },
          "Missing source-buffer materials are rejected");
  auto invalid = geometry();
  invalid->buffers[0].indices[2] = 32768;
  rejects([&] { base(invalid, true); },
          "Original signed16 used-range boundary is checked");
}

void multipleBuffers() {
  auto resource = actorResource();
  resource->model.meshes[0].buffers.push_back({"second", {8, 9, 12}});
  resource->model.nodes[0].geometry[0].materials.push_back({{}, "material", 0});
  auto scene = std::make_shared<RetailActorScene>(resource);
  auto source = actorBase(scene, true);
  auto process = Mesh::skin(source.mesh, scene, palette(*scene), true);
  require(
      process->bufferCount() == 2 && process->view(0).firstVertex() == 2 &&
          process->view(0).endVertex() == 7 &&
          process->view(1).firstVertex() == 8 &&
          process->view(1).endVertex() == 13 &&
          &process->view(1).topology() == &source.metadata->buffers[1] &&
          &process->view(1).material() == source.materials[1].get(),
      "Each proxy retains its own actual topology, material and signed range");
  require(process->prepare(1, 0) == 4 && process->prepare(1, 1) == 4,
          "Both source buffers produce distinct real output allocations");
  auto first = process->view(0), second = process->view(1);
  require(&first.streams().currentPositions !=
                  &second.streams().currentPositions &&
              second.streams().currentPositions[8] ==
                  std::array<float, 3>{9, 10, 11},
          "Nonzero second range keeps full original source indices");
  process->release(1);
  require(!process->processBuffersPresent() &&
              process->view(0).positionsPresent() &&
              !process->view(1).positionsPresent() &&
              second.streams().currentPositions[12] ==
                  std::array<float, 3>{13, 14, 15},
          "Original byte36 is shared while each retained allocation owns its "
          "lifetime");
  process->release(0);
}

void liveSourceMaterial() {
  auto scene = actor();
  auto source = actorBase(scene, true);
  auto &state = palette(*scene).baseMesh().sourceMaterial(0);
  auto process = Mesh::skin(source.mesh, scene, palette(*scene), true);
  require(&process->view(0).material() == &state,
          "Proxy retains actual embedded source material identity");
  state.flags &= ~2u;
  require(process->prepare(1, 0) == 4 && !process->view(0).normalsPresent(),
          "Current source flags select mask1 after binding");
  process->release(0);
  state.flags |= 2u;
  require(process->prepare(1, 0) == 4 && process->view(0).normalsPresent() &&
              process->view(0).streams().currentNormals[2] ==
                  std::array<float, 3>{1, 0, 0},
          "Next preparation observes actual source mutation and selects mask3");
  process->release(0);
}

void skinOwnership() {
  for (bool defer : {false, true})
    for (bool lighting : {false, true}) {
      auto scene = actor();
      auto &live = palette(*scene);
      auto source = actorBase(scene, lighting);
      scene->root().findUid("root")->setScale({2, 1, 1});
      scene->root().onAnimate(1);
      const auto *box = &live.cachedBoundingBox();
      const bool dirty = live.skinCacheDirty();
      auto skin = Mesh::skin(source.mesh, scene, live, defer);
      require(&skin->view(0).boundingBox() == box &&
                  live.skinCacheDirty() == dirty,
              "Proxy bbox storage borrow never performs lazy preparation");
      const auto flags = skin->prepare(0, 0);
      require(flags == (defer ? 16u : 4u),
              "Original registration process result");
      if (defer)
        require(skin->prepare(1, 0) == 4, "Original deferred draw preparation");
      auto result = skin->view(0);
      require(result.streams().currentPositions[2] ==
                  std::array<float, 3>{6, 4, 5},
              "Actual shared actor palette drives native CPU skin output");
      require(!live.skinCacheDirty(),
              "Real palette preparation clears the single shared dirty state");
      if (lighting)
        require(result.streams().currentNormals[2] ==
                        std::array<float, 3>{2, 0, 0} &&
                    source.storage->normals[2][0] == 1,
                "Mask3 owns output normals without source mutation");
      else if (defer)
        require(!result.normalsPresent() && source.storage->normals[2][0] == 1,
                "Deferred mask1 keeps output normals absent");
      else
        require(&result.streams().currentNormals == &source.storage->normals &&
                    source.storage->normals[2][0] == 2,
                "Eager mask1 mutates the actual borrowed current source normal "
                "storage");
      require(source.metadata->normals[2][0] == 1,
              "Immutable decoded normals are not the mutable original source "
              "storage");
      if (defer) {
        skin->release(0);
        require(!skin->view(0).positionsPresent(),
                "Direct actual Player-shaped release");
        skin->setSkinningEnabled(false);
        require(!skin->skinningEnabled() && !live.skinCacheDirty() &&
                    &skin->view(0).streams().currentPositions ==
                        &source.storage->positions,
                "Disabled absent-output proxy restores actual source");
        skin->setSkinningEnabled(true);
        require(live.skinCacheDirty() && !skin->view(0).positionsPresent(),
                "Disabled-to-enabled sets shared dirty and clears "
                "absent-output descriptors");
      } else {
        const auto *output = &result.streams().currentPositions;
        skin->setSkinningEnabled(false);
        require(
            !live.skinCacheDirty() &&
                &skin->view(0).streams().currentPositions == output &&
                skin->processBuffersPresent(),
            "Disabling present output retains its original process storage");
      }
    }
  auto first = actor(), second = actor();
  auto source = actorBase(first, true);
  rejects([&] { Mesh::skin(source.mesh, first, palette(*second), true); },
          "Foreign actual scene palette is rejected");
  const auto sharedResource = actorResource();
  auto sharedFirst = std::make_shared<RetailActorScene>(sharedResource);
  auto sharedSecond = std::make_shared<RetailActorScene>(sharedResource);
  auto sharedSource = actorBase(sharedFirst, true);
  require(&palette(*sharedFirst).geometry() ==
              &palette(*sharedSecond).geometry(),
          "The resource-alias regression really shares decoded geometry");
  rejects(
      [&] {
        Mesh::source(sharedFirst, palette(*sharedSecond).baseMesh(),
                     sharedSource.storage);
      },
      "Source storage binding rejects an equal-resource base from another "
      "actor");
  rejects(
      [&] {
        Mesh::skin(sharedSource.mesh, sharedSecond, palette(*sharedSecond),
                   true);
      },
      "Equal decoded pointers do not identify the same constructed source "
      "resource");
  auto catalog = morphCatalogue(*source.metadata);
  rejects([&] { Mesh::morph(source.mesh, catalog, 0, true); },
          "Foreign morph catalogue with equal source UID is not actual "
          "geometry identity");
  auto foreign =
      base(std::shared_ptr<const CityMesh>(catalog, &catalog->meshes[0]), true);
  auto morph = Mesh::morph(foreign.mesh, catalog, 0, true);
  rejects([&] { Mesh::skin(morph, first, palette(*first), true); },
          "Geometry-name-only pretend morph-to-skin binding is rejected");
  rejects([&] { actor("morph", true); },
          "Current actor scene has no genuine morph-factory domain; matching "
          "sourceUid alone is insufficient");
  auto skin = Mesh::skin(source.mesh, first, palette(*first), true);
  skin->prepare(1, 0);
  auto retained = skin->view(0);
  std::weak_ptr<RetailActorScene> lifetime = first;
  morph.reset();
  catalog.reset();
  first.reset();
  source.mesh.reset();
  source.metadata.reset();
  source.materials.clear();
  skin.reset();
  require(!lifetime.expired() && retained.positionsPresent(),
          "Retained proxy keeps actual scene/bbox/palette lifetimes safe");
  retained = RetailDeformationView{};
  require(lifetime.expired(), "Final scene borrower releases genuine owner");
}

void extractedShadowLifetime() {
  auto resource = actorResource();
  resource->model.meshes.push_back(*geometry("shadow"));
  auto shadowNode = node("shadow-node", {});
  shadowNode.geometry.push_back({{}, "shadow", {{{}, "material", 0}}});
  shadowNode.instances.push_back({3, 0, {}, "shadow", false});
  resource->model.nodes.push_back(std::move(shadowNode));
  resource->model.visualScenes[0].nodeCount = 3;
  auto scene = std::make_shared<RetailActorScene>(resource);
  auto *shadow = scene->root().findUid("shadow-node");
  require(shadow && shadow->findMesh(), "Real ordinary shadow fixture");
  auto &shadowMesh = *shadow->findMesh()->mesh();
  auto storage = std::make_shared<RetailSkinningOutput>(RetailSkinningOutput{
      shadowMesh.geometry().positions, shadowMesh.geometry().normals});
  rejects(
      [&] { Mesh::source(scene, shadowMesh, storage); },
      "Transferable ordinary resource is rejected before borrowing its box");
  auto source = actorBase(scene, true);
  auto process = Mesh::skin(source.mesh, scene, palette(*scene), true);
  process->prepare(1, 0);
  auto retained = process->view(0);
  const auto *box = &retained.boundingBox();
  std::weak_ptr<RetailActorScene> lifetime = scene;
  {
    RetailActorGraph graph;
    graph.attachActor(scene);
    auto &subtree = graph.registerShadow(*scene, *shadow);
    require(subtree.meshes().size() == 1 &&
                scene->root().findUid("shadow-node") == nullptr,
            "Real extraction moves ordinary mesh ownership out of actor");
    require(graph.removeChild(&subtree.root()), "Drop extracted shadow owner");
  }
  scene.reset();
  source.mesh.reset();
  source.metadata.reset();
  source.materials.clear();
  process.reset();
  require(!lifetime.expired() && &retained.boundingBox() == box &&
              retained.streams().currentPositions[2] ==
                  std::array<float, 3>{3, 4, 5},
          "Extracted shadow destruction preserves retained skin-base storage "
          "and box");
  retained = RetailDeformationView{};
  require(lifetime.expired(),
          "Final skin borrower releases actor after shadow drop");
}

std::uint32_t bits(float value) {
  std::uint32_t result;
  std::memcpy(&result, &value, sizeof result);
  return result;
}
void dumpRange(const RetailVertexStream &stream) {
  std::cout << '[';
  for (std::size_t vertex = 2; vertex < 7; ++vertex) {
    if (vertex != 2)
      std::cout << ',';
    std::cout << '[' << bits(stream.at(vertex)[0]) << ','
              << bits(stream.at(vertex)[1]) << ',' << bits(stream.at(vertex)[2])
              << ']';
  }
  std::cout << ']';
}
void comparisonDump() {
  for (bool skinned : {false, true})
    for (bool defer : {false, true})
      for (bool lighting : {false, true}) {
        std::shared_ptr<RetailActorScene> scene;
        Base source;
        Mesh::Ptr process;
        if (skinned) {
          scene = actor();
          scene->root().findUid("root")->setScale({2, 1, 1});
          scene->root().onAnimate(1);
          palette(*scene).skinMatrices(); // Genuine cache is prepared before
                                          // the scoped lifecycle probe.
          source = actorBase(scene, lighting);
          process = Mesh::skin(source.mesh, scene, palette(*scene), defer);
        } else {
          source = base(geometry(), lighting);
          auto catalogue = morphCatalogue(*source.metadata);
          bindCatalogue(source, catalogue);
          process = Mesh::morph(source.mesh, catalogue, 0, defer);
        }
        const auto snapshot = [&](unsigned phase, int flags) {
          const auto view = process->view(0);
          std::cout << "owner {\"skin\":" << skinned << ",\"defer\":" << defer
                    << ",\"lighting\":" << lighting << ",\"phase\":" << phase
                    << ",\"flags\":" << flags
                    << ",\"present\":" << process->processBuffersPresent()
                    << ",\"sourceRelease\":" << process->sourceReleasePending()
                    << ",\"positionPresent\":" << view.positionsPresent()
                    << ",\"normalPresent\":" << view.normalsPresent()
                    << ",\"dirty\":"
                    << (skinned && palette(*scene).skinCacheDirty())
                    << ",\"sourceNormals\":";
          dumpRange(source.storage->normals);
          if (phase != 0 && view.positionsPresent()) {
            std::cout << ",\"positions\":";
            dumpRange(view.streams().currentPositions);
          }
          if (phase != 0 && view.normalsPresent()) {
            std::cout << ",\"normals\":";
            dumpRange(view.streams().currentNormals);
          }
          std::cout << "}\n";
        };
        snapshot(0, -1);
        snapshot(1, int(process->prepare(0, 0)));
        snapshot(2, int(process->prepare(1, 0)));
        snapshot(3, int(process->prepare(0, 0)));
        if (defer)
          process->release(0);
        snapshot(4, -1);
        if (skinned) {
          process->setSkinningEnabled(false);
          snapshot(5, -1);
          process->setSkinningEnabled(true);
          snapshot(6, -1);
          snapshot(7, int(process->prepare(1, 0)));
          if (defer)
            process->release(0);
          snapshot(8, -1);
        }
      }
}

void retail(const char *root) {
  ResourceFileSystem assets(root);
  for (const auto *path : {"char_pthug.bdae", "char_pthug_jail.bdae"}) {
    auto decoded = loadRetailActorSceneSource(assets, path);
    auto scene = std::make_shared<RetailActorScene>(decoded);
    for (const auto &owned : scene->meshes()) {
      if (!owned->skin())
        continue;
      auto &live = *owned;
      auto metadata = std::shared_ptr<const CityMesh>(scene, &live.geometry());
      auto storage = std::make_shared<RetailSkinningOutput>(
          RetailSkinningOutput{metadata->positions, metadata->normals});
      for (std::size_t slot = 0; slot < metadata->buffers.size(); ++slot) {
        const auto &state = live.baseMesh().sourceMaterial(slot);
        require(
            state.vertexColorControl == 1 && state.flags == 0x387 &&
                state.ambient == 0xffffffff && state.emissive == 0 &&
                state.specular == 0xffffffff && state.diffuse == 0xffffffff &&
                state.dirtyFlags == 0xfc,
            "Actual embedded source material matches original factory capture");
      }
      auto source = Mesh::source(scene, live.baseMesh(), storage);
      auto skin = Mesh::skin(source, scene, live, true);
      require(skin->prepare(0, 0) == 16, "Actual Player deferred registration");
      require(skin->prepare(1, 0) == 4 &&
                  skin->view(0).streams().currentPositions.size() ==
                      metadata->positions.size(),
              "Actual Player native output/current stream binding");
      require(
          &skin->view(0).topology().indices == &metadata->buffers[0].indices &&
              &skin->view(0).material() == &live.baseMesh().sourceMaterial(0) &&
              &skin->view(0).material() == &live.sourceMaterial(0),
          "Actual Player topology/material ownership preserved");
      auto retained = skin->view(0);
      skin->release(0);
      require(!skin->view(0).positionsPresent() && retained.positionsPresent(),
              "Actual Player draw/release/retention");
      std::cout << path << " owned skin vertices " << metadata->positions.size()
                << '\n';
    }
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    processFlags();
    morphOwnership();
    skinOwnership();
    sourceSharingAndValidation();
    multipleBuffers();
    liveSourceMaterial();
    extractedShadowLifetime();
    if (argc == 2) {
      if (std::string(argv[1]) == "--dump")
        comparisonDump();
      else
        retail(argv[1]);
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Original software deformation ownership checks passed\n";
}
