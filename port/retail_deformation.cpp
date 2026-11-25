#include "retail_deformation.h"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace {
const RetailVertexStream absent;
}

RetailVertexStreams RetailDeformationView::streams() const {
  return {metadata->name, positionStorage ? positionStorage->positions : absent,
          normalStorage ? normalStorage->normals : absent,
          metadata->floatNormals};
}

RetailDeformationMesh::Ptr RetailDeformationMesh::source(
    std::shared_ptr<const CityMesh> metadata,
    std::shared_ptr<RetailSkinningOutput> current,
    std::vector<std::shared_ptr<RetailMaterialState>> materials) {
  if (!metadata || metadata->name.empty() || !current ||
      !metadata->rawBoundsCm || metadata->positions.empty() ||
      metadata->buffers.empty() ||
      materials.size() != metadata->buffers.size() ||
      current->positions.size() != metadata->positions.size() ||
      (!current->normals.empty() &&
       current->normals.size() != current->positions.size()))
    throw std::invalid_argument(
        "Incomplete original deformation source binding.");
  auto result = Ptr(new RetailDeformationMesh);
  result->identity = metadata->name;
  for (std::size_t slot = 0; slot < metadata->buffers.size(); ++slot) {
    const auto &indices = metadata->buffers[slot].indices;
    if (!materials[slot] || indices.empty() || indices.size() % 3)
      throw std::invalid_argument(
          "Missing original buffer material or triangle indices.");
    const auto range = std::minmax_element(indices.begin(), indices.end());
    // Original accessors sign-extend authored16 first/last. The authoritative
    // decoder already checks those extrema; no pointer-before-allocation is
    // used.
    if (*range.second > 32767 ||
        std::size_t(*range.second) >= current->positions.size())
      throw std::invalid_argument(
          "Unsupported signed original process-buffer range.");
    RetailDeformationView view;
    view.metadata = metadata;
    view.materialState = std::move(materials[slot]);
    view.positionStorage = current;
    if (!current->normals.empty())
      view.normalStorage = current;
    view.box = &*metadata->rawBoundsCm;
    view.slot = slot;
    view.first = *range.first;
    view.end = std::size_t(*range.second) + 1;
    result->buffers.push_back({std::move(view), {}, {}});
  }
  return result;
}

RetailDeformationMesh::Ptr
RetailDeformationMesh::source(std::shared_ptr<RetailActorScene> scene,
                              RetailActorSceneMesh &sourceOwner,
                              std::shared_ptr<RetailSkinningOutput> current) {
  if (!scene || !scene->isDeformationSource(sourceOwner) ||
      (scene->loadedResource() &&
       (!sourceOwner.loadedSourceStreams() || current != sourceOwner.loadedSourceStreams())))
    throw std::invalid_argument(
        "Missing actual retained deformation dependency/current loaded source owner.");
  std::vector<std::shared_ptr<RetailMaterialState>> materials;
  materials.reserve(sourceOwner.geometry().buffers.size());
  for (std::size_t slot = 0; slot < sourceOwner.geometry().buffers.size();
       ++slot)
    materials.emplace_back(scene, &sourceOwner.sourceMaterial(slot));
  auto result =
      source(std::shared_ptr<const CityMesh>(scene, &sourceOwner.geometry()),
             std::move(current), std::move(materials));
  for (auto &buffer : result->buffers) {
    buffer.current.sceneLifetime = scene;
    buffer.current.box = &sourceOwner.cachedBoundingBox();
  }
  result->scene = std::move(scene);
  result->runtimeSource = &sourceOwner;
  return result;
}

RetailDeformationMesh::Ptr
RetailDeformationMesh::morph(Ptr source,
                             std::shared_ptr<const CityScene> catalogue,
                             std::size_t index, bool defer) {
  auto result = bindMorph(std::move(source), std::move(catalogue), index, {});
  result->prepareProxies(defer);
  return result;
}

RetailDeformationMesh::Ptr RetailDeformationMesh::morph(
    Ptr source, std::shared_ptr<RetailActorScene> scene,
    RetailActorSceneMesh &owner, std::vector<Ptr> targets, bool defer) {
  if (!scene || !owner.morphRuntime() ||
      std::none_of(scene->meshes().begin(), scene->meshes().end(),
                   [&](const auto &candidate) { return candidate.get() == &owner; }) ||
      !source || source->kind() != Kind::source || source->scene != scene ||
      source->runtimeSource != &owner.baseMesh() || targets.size() != owner.morphTargets().size() ||
      owner.morphRuntime()->catalogue().get() != &scene->source().model)
    throw std::invalid_argument("Morph process requires its genuine constructed resource/base/state.");
  for (std::size_t i = 0; i < targets.size(); ++i)
    if (!targets[i] || targets[i]->kind() != Kind::source || targets[i]->scene != scene ||
        targets[i]->runtimeSource != owner.morphTargets()[i])
      throw std::invalid_argument("Morph process target resources differ from actual authored order/ownership.");
  const auto &runtime = owner.morphRuntime();
  auto result = morph(std::move(source), runtime->catalogue(), runtime->controllerIndex(),
                      std::move(targets), defer, runtime);
  result->scene = std::move(scene);
  result->runtimeSource = &owner;
  return result;
}

RetailDeformationMesh::Ptr
RetailDeformationMesh::bindMorph(Ptr source,
                                 std::shared_ptr<const CityScene> catalogue,
                                 std::size_t index, std::shared_ptr<RetailMorphState> runtime) {
  if (!source || !catalogue || index >= catalogue->morphControllers.size())
    throw std::invalid_argument(
        "Missing original morph source/controller binding.");
  if (runtime && (runtime->catalogue().get() != catalogue.get() || runtime->controllerIndex() != index))
    throw std::invalid_argument("Morph runtime requires its exact catalogue/controller identity.");
  const auto &controller = catalogue->morphControllers[index];
  if (controller.name.empty() || controller.source != source->uid() ||
      controller.source != source->view(0).geometry().name ||
      std::none_of(catalogue->meshes.begin(), catalogue->meshes.end(),
                   [&](const CityMesh &mesh) {
                     return &mesh == &source->view(0).geometry();
                   }))
    throw std::invalid_argument(
        "Unsupported original morph source identity/chain.");
  auto result = Ptr(new RetailDeformationMesh);
  result->meshKind = Kind::morph;
  result->identity = controller.name;
  result->underlying = std::move(source);
  result->morphState = runtime ? std::move(runtime) : std::make_shared<RetailMorphState>(std::move(catalogue), index);
  return result;
}

RetailDeformationMesh::Ptr
RetailDeformationMesh::morph(Ptr source,
                             std::shared_ptr<const CityScene> catalogue,
                             std::size_t index, std::vector<Ptr> targets,
                             bool defer, std::shared_ptr<RetailMorphState> runtime) {
  // Bind and validate real target owners before process allocation/publication.
  auto result = bindMorph(std::move(source), std::move(catalogue), index, std::move(runtime));
  result->morphTargets = std::move(targets);
  result->validateMorphTargets();
  result->prepareProxies(defer);
  return result;
}

void RetailDeformationMesh::validateMorphTargets() const {
  if (!morphTargets) return;
  const auto &targets = morphState->current.targets;
  if (morphTargets->size() != targets.size())
    throw std::invalid_argument("Missing authored current morph target bindings.");
  for (std::size_t i = 0; i < targets.size(); ++i) {
    const auto &target = morphTargets->at(i);
    if (!target || target->kind() != Kind::source || target->buffers.empty() ||
        targets[i].mesh >= morphState->source->meshes.size() ||
        &target->buffers[0].current.geometry() !=
            &morphState->source->meshes[targets[i].mesh])
      throw std::invalid_argument("Current morph target requires its exact catalogue source identity.");
    const auto streams = target->buffers[0].current.streams();
    for (std::size_t previous = 0; previous < i; ++previous) {
      if (targets[previous].mesh != targets[i].mesh) continue;
      const auto other = morphTargets->at(previous)->buffers[0].current.streams();
      if (&streams.currentPositions != &other.currentPositions ||
          &streams.currentNormals != &other.currentNormals ||
          streams.floatNormals != other.floatNormals)
        throw std::invalid_argument("Repeated morph target index has conflicting current stream bindings.");
    }
  }
}

RetailDeformationMesh::Ptr
RetailDeformationMesh::skin(Ptr source, std::shared_ptr<RetailActorScene> scene,
                            RetailActorSceneMesh &paletteOwner, bool defer) {
  if (!source || !scene || !paletteOwner.skin() ||
      std::none_of(scene->meshes().begin(), scene->meshes().end(),
                   [&](const auto &candidate) {
                     return candidate.get() == &paletteOwner;
                   }) ||
      &source->view(0).geometry() != &paletteOwner.geometry() ||
      source->runtimeSource != &paletteOwner.baseMesh() || source->scene != scene ||
      paletteOwner.skin()->sourceUid != source->uid() ||
      source->kind() == Kind::skin ||
      (source->kind() == Kind::morph &&
       std::none_of(scene->source().model.morphControllers.begin(),
                    scene->source().model.morphControllers.end(),
                    [&](const auto &controller) {
                      return controller.name == source->uid();
                    })))
    throw std::invalid_argument("Skin process binding requires its actual "
                                "source and retained palette owner.");
  auto result = Ptr(new RetailDeformationMesh);
  result->meshKind = Kind::skin;
  result->identity = paletteOwner.uid();
  result->underlying = std::move(source);
  result->scene = std::move(scene);
  result->palette = &paletteOwner;
  result->prepareProxies(defer);
  return result;
}

void RetailDeformationMesh::prepareProxies(bool defer) {
  buffers.reserve(underlying->bufferCount());
  for (std::size_t slot = 0; slot < underlying->bufferCount(); ++slot) {
    auto current = underlying->view(slot);
    if (palette) {
      current.sceneLifetime = scene;
      current.box = &palette->cachedBoundingBox(); // Borrow storage without a
                                                   // lazy palette/bbox query.
    }
    buffers.push_back({std::move(current), {}, {}});
    if (defer) {
      buffers.back().current.positionStorage.reset();
      buffers.back().current.normalStorage.reset();
    } else {
      requestProcess(slot, requestedStreams(slot),
                     RetailProcessBufferType::persistent, true);
      present = true; // Original constructors set byte36 regardless of the
                      // allocator return flags.
    }
  }
}

RetailDeformationView RetailDeformationMesh::view(std::size_t slot) const {
  const auto &view = buffers.at(slot).current;
  const auto count = view.geometry().positions.size();
  if ((view.positionStorage &&
       view.positionStorage->positions.size() != count) ||
      (view.normalStorage && view.normalStorage->normals.size() != count))
    throw std::runtime_error(
        "Current original buffer storage changed its vertex count.");
  return view;
}

std::uint32_t RetailDeformationMesh::requestedStreams(std::size_t slot) const {
  return (buffers.at(slot).current.material().flags & 2) ? 3u : 1u;
}

std::uint32_t
RetailDeformationMesh::requestProcess(std::size_t slot, std::uint32_t mask,
                                      RetailProcessBufferType type,
                                      bool allow) {
  auto &buffer = buffers.at(slot);
  if ((mask != 0 && mask != 1 && mask != 3) ||
      (type != RetailProcessBufferType::transient &&
       type != RetailProcessBufferType::persistent))
    throw std::invalid_argument(
        "Unsupported software process type or stream mask.");
  const auto assign = [&](const std::shared_ptr<RetailSkinningOutput> &data) {
    buffer.current.positionStorage = data;
    if (mask & 2)
      buffer.current.normalStorage = data;
  };
  if (type == RetailProcessBufferType::persistent) {
    if (!buffer.persistent)
      buffer.persistent.emplace();
    auto &binding = *buffer.persistent;
    if (binding.storage && binding.mask == mask) {
      assign(binding.storage);
      return 4;
    }
    if (!allow)
      return binding.storage ? 9u : 16u;
  } else if (buffer.persistent && buffer.persistent->storage) {
    return 16;
  }

  std::shared_ptr<RetailSkinningOutput> allocation;
  try {
    if (mask && buffer.current.first < buffer.current.end) {
      allocation = std::make_shared<RetailSkinningOutput>();
      // Checked native adaptation: kernels use full original indices. Only
      // [first,end) is original output; unused vector entries are not drawn.
      allocation->positions.resize(buffer.current.geometry().positions.size());
      if (mask & 2)
        allocation->normals.resize(allocation->positions.size());
    }
  } catch (const std::bad_alloc &) {
    allocation.reset();
  }
  if (type == RetailProcessBufferType::persistent) {
    buffer.persistent->storage = allocation;
    buffer.persistent->mask = mask;
  } else if (allocation) {
    buffer.transient = allocation;
  }
  if (!allocation)
    return 8;
  assign(allocation);
  return 4;
}

std::uint32_t RetailDeformationMesh::prepare(std::uint32_t step,
                                             std::size_t slot) {
  if (slot >= buffers.size())
    throw std::out_of_range("Missing original deformation buffer.");
  if (meshKind == Kind::source)
    return 16; // Literal original IColladaMesh::onPrepare,0x2b2e11.
  if (meshKind == Kind::skin) {
    const auto sourceFlags = underlying->prepare(step, slot);
    releaseSource = step == 1 && (sourceFlags & 4);
    if (!palette->skinningEnabled())
      return sourceFlags;
  }
  const auto flags =
      requestProcess(slot, requestedStreams(slot),
                     step == 0 ? RetailProcessBufferType::persistent
                               : RetailProcessBufferType::transient,
                     false);
  if (flags & 4) {
    present = true;
    evaluate(slot);
  }
  return flags;
}

void RetailDeformationMesh::evaluate(std::size_t slot) {
  auto &current = buffers.at(slot).current;
  if (!current.positionStorage)
    throw std::logic_error(
        "Original process allocation has no position output.");
  auto source = underlying->view(slot);
  const auto streams = source.streams();
  // Prepare the real shared cache before borrowing a mutable normal vector.
  // This shares its existing dirty/bbox behavior; no second palette is cached.
  const auto *matrices = palette ? &palette->skinMatrices() : nullptr;
  auto &output = *current.positionStorage;
  const bool borrowedNormals =
      current.normalStorage && current.normalStorage != current.positionStorage;
  if (borrowedNormals)
    output.normals.swap(current.normalStorage->normals);
  try {
    const auto &normals =
        borrowedNormals && source.normalStorage == current.normalStorage
            ? output.normals
            : streams.currentNormals;
    const RetailVertexStreams effective{streams.sourceName,
                                        streams.currentPositions, normals,
                                        streams.floatNormals};
    if (meshKind == Kind::morph) {
      validateMorphTargets();
      std::vector<RetailVertexStreams> targets;
      targets.reserve(morphState->source->meshes.size());
      for (std::size_t index = 0; index < morphState->source->meshes.size(); ++index) {
        const RetailDeformationView *live = nullptr;
        if (morphTargets) {
          const auto &entries = morphState->current.targets;
          for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].mesh == index) {
              live = &morphTargets->at(i)->buffers[0].current;
              break;
            }
        }
        if (live) {
          const auto target = live->streams();
          const auto &targetNormals = borrowedNormals && live->normalStorage == current.normalStorage
                                        ? output.normals : target.currentNormals;
          targets.push_back({target.sourceName, target.currentPositions, targetNormals, target.floatNormals});
        } else if (!morphTargets) {
          const auto &mesh = morphState->source->meshes[index];
          targets.push_back({mesh.name, mesh.positions, mesh.normals, mesh.floatNormals});
        } else {
          targets.push_back({{}, absent, absent, true});
        }
      }
      retailMorphVertexRange(effective, morphState->current, targets,
                             current.first, current.end, output, morphState->base);
    } else
      retailSkinVertexRange(effective, *palette->skin(), *matrices,
                            current.first, current.end, output);
  } catch (...) {
    if (borrowedNormals)
      output.normals.swap(current.normalStorage->normals);
    throw;
  }
  if (borrowedNormals)
    output.normals.swap(current.normalStorage->normals);
}

void RetailDeformationMesh::release(std::size_t slot) {
  const auto &before = buffers.at(slot);
  if (meshKind == Kind::source)
    return; // Literal original IColladaMesh::release,0x2b2e15.
  const bool disabled = palette && !palette->skinningEnabled();
  // Reject unsafe legacy ownership before mutating the real shared toggle or
  // releasing a nested source. A disabled absent-output skin clears its
  // borrowed descriptors on the temporary enable, as in the original.
  if (before.current.positionStorage &&
      before.current.positionStorage != before.transient &&
      !(disabled && !present))
    throw std::logic_error("Original transient release does not own this "
                           "persistent/borrowed position span.");
  if (meshKind == Kind::skin && releaseSource) {
    underlying->release(slot);
    releaseSource = false;
  }
  if (disabled)
    setSkinningEnabled(true);
  auto &buffer = buffers.at(slot);
  buffer.current.positionStorage.reset();
  buffer.current.normalStorage.reset();
  buffer.transient.reset();
  present = false;
  if (disabled)
    setSkinningEnabled(false);
}

void RetailDeformationMesh::setSkinningEnabled(bool enabled) {
  if (!palette)
    throw std::logic_error("Only an actual skin owner has a skinning toggle.");
  palette->setSkinningEnabled(enabled);
  if (!present) {
    for (std::size_t slot = 0; slot < buffers.size(); ++slot) {
      auto &current = buffers[slot].current;
      if (enabled) {
        current.positionStorage.reset();
        current.normalStorage.reset();
      } else {
        const auto source = underlying->view(slot);
        current.positionStorage = source.positionStorage;
        current.normalStorage = source.normalStorage;
      }
    }
  }
}

bool RetailDeformationMesh::skinningEnabled() const {
  if (!palette)
    throw std::logic_error("Only an actual skin owner has a skinning state.");
  return palette->skinningEnabled();
}

void RetailDeformationMesh::setMorphWeight(std::size_t target, float weight) {
  if (!morphState)
    throw std::logic_error("Only an actual morph owner has target weights.");
  morphState->setWeight(target, weight);
}

float RetailDeformationMesh::morphBaseWeight() const {
  if (!morphState)
    throw std::logic_error("Only an actual morph owner has a runtime base weight.");
  return morphState->baseWeight();
}

const std::shared_ptr<RetailMorphState>& RetailDeformationMesh::morphRuntime() const {
  if (!morphState)
    throw std::logic_error("Only an actual morph owner has runtime weight storage.");
  return morphState;
}
