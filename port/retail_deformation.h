#pragma once

#include "retail_actor_scene.h"
#include "retail_material.h"
#include "retail_morph.h"

#include <memory>
#include <optional>

enum class RetailProcessBufferType : std::uint32_t {
  transient = 0,
  persistent = 1
};

// A retained current-buffer view. Metadata and backing storage outlive the
// view, including mesh destruction/release. Persistent reuse still changes its
// values; an asynchronous renderer must copy or synchronize before another
// preparation.
class RetailDeformationView {
public:
  RetailVertexStreams streams() const;
  bool positionsPresent() const { return bool(positionStorage); }
  bool normalsPresent() const { return bool(normalStorage); }
  const CityMesh &geometry() const { return *metadata; }
  const CityMeshBuffer &topology() const { return metadata->buffers.at(slot); }
  RetailMaterialState &material() const { return *materialState; }
  const RetailSkinBoundingBox &boundingBox() const { return *box; }
  std::size_t firstVertex() const { return first; }
  std::size_t endVertex() const { return end; }

private:
  friend class RetailDeformationMesh;
  std::shared_ptr<const CityMesh> metadata;
  std::shared_ptr<RetailMaterialState> materialState;
  std::shared_ptr<RetailSkinningOutput> positionStorage, normalStorage;
  std::shared_ptr<RetailActorScene> sceneLifetime;
  const RetailSkinBoundingBox *box = nullptr;
  std::size_t slot = 0, first = 0, end = 0;
};

// Owns actual software process outputs, borrowing the explicitly bound source.
// It is not a GL driver, resource-name resolver, actor factory or weight
// animator.
class RetailDeformationMesh {
public:
  using Ptr = std::shared_ptr<RetailDeformationMesh>;
  enum class Kind { source, morph, skin };

  // The caller supplies genuine current mutable storage and material handles.
  // Sharing either handle explicitly shares subsequent source mutations. No
  // immutable CityMesh streams are written or used as a silent current
  // fallback.
  static Ptr
  source(std::shared_ptr<const CityMesh> metadata,
         std::shared_ptr<RetailSkinningOutput> current,
         std::vector<std::shared_ptr<RetailMaterialState>> materials);
  // Actual skin-base owner: borrow its own cached box, not an equal immutable
  // box. Transferable standalone ordinary resources need a separate lease
  // contract and are rejected before borrowing anything. Material handles alias
  // the actual embedded sourceMaterial states with the scene's lifetime.
  static Ptr source(std::shared_ptr<RetailActorScene> scene,
                    RetailActorSceneMesh &sourceOwner,
                    std::shared_ptr<RetailSkinningOutput> current);
  // Immutable-target compatibility/control domain; production current targets
  // must use the explicit retained source overload below.
  static Ptr morph(Ptr source, std::shared_ptr<const CityScene> catalogue,
                   std::size_t controllerIndex, bool defer);
  // Actual current target sources, one per authored target in exact order.
  // Each must be Kind::source and use the exact catalogue geometry object at
  // that target's mesh index. Repeated indices require identical current
  // position/normal vector and format identities; conflicting bindings reject.
  // Target buffer0 is borrowed anew on every evaluation, without preparing it.
  static Ptr morph(Ptr source, std::shared_ptr<const CityScene> catalogue,
                   std::size_t controllerIndex, std::vector<Ptr> currentTargets,
                   bool defer, std::shared_ptr<RetailMorphState> runtime = {});
  // Genuine constructed morph, with its exact immediate base/ordered targets
  // and sole intrinsic runtime state. Same UIDs do not establish ownership.
  static Ptr morph(Ptr source, std::shared_ptr<RetailActorScene> scene,
                   RetailActorSceneMesh &morphOwner, std::vector<Ptr> targets,
                   bool defer);
  static Ptr skin(Ptr source, std::shared_ptr<RetailActorScene> scene,
                  RetailActorSceneMesh &paletteOwner, bool defer);

  ~RetailDeformationMesh() = default;
  RetailDeformationMesh(const RetailDeformationMesh &) = delete;
  RetailDeformationMesh &operator=(const RetailDeformationMesh &) = delete;

  Kind kind() const { return meshKind; }
  const std::string &uid() const { return identity; }
  std::size_t bufferCount() const { return buffers.size(); }
  RetailDeformationView view(std::size_t slot) const;
  bool processBuffersPresent() const {
    return present;
  } // Original mesh byte36.
  bool sourceReleasePending() const { return releaseSource; } // Skin byte42.

  // Literal software allocator leaf: stream mask1/3 is separate from type0/1.
  // Flags4/8/9/16 retain their original meanings. It does not run a CPU kernel.
  std::uint32_t requestProcess(std::size_t slot, std::uint32_t streamMask,
                               RetailProcessBufferType, bool allowAllocation);
  // Original step0 uses persistent type1; nonzero steps use transient type0.
  // Only step1 arms source-release tracking. Underlying source prepares first.
  std::uint32_t prepare(std::uint32_t step, std::size_t slot);
  void release(std::size_t slot);
  void setSkinningEnabled(bool);
  bool skinningEnabled() const;
  // Runtime target weights only; the real animation/damage producer is
  // external.
  void setMorphWeight(std::size_t target, float weight);
  // Actual runtime entry0 written by the one CPU kernel, not recomputed from
  // target weights by this getter. Remains unchanged when no kernel runs.
  float morphBaseWeight() const;
  // Retain the canonical state independently of process outputs. A supplied
  // state must have the exact same catalogue object and controller index.
  const std::shared_ptr<RetailMorphState>& morphRuntime() const;

private:
  struct Binding {
    std::shared_ptr<RetailSkinningOutput> storage;
    std::uint32_t mask = 0;
  };
  struct Buffer {
    RetailDeformationView current;
    std::optional<Binding> persistent;
    std::shared_ptr<RetailSkinningOutput> transient;
  };
  RetailDeformationMesh() = default;
  static Ptr bindMorph(Ptr source, std::shared_ptr<const CityScene> catalogue,
                       std::size_t controllerIndex, std::shared_ptr<RetailMorphState> runtime);
  void prepareProxies(bool defer);
  void evaluate(std::size_t slot);
  void validateMorphTargets() const;
  std::uint32_t requestedStreams(std::size_t slot) const;

  Kind meshKind = Kind::source;
  std::string identity;
  Ptr underlying;
  std::shared_ptr<RetailMorphState> morphState;
  std::optional<std::vector<Ptr>> morphTargets;
  std::shared_ptr<RetailActorScene> scene;
  RetailActorSceneMesh *runtimeSource =
      nullptr; // Actual constructed ordinary/morph resource, never a UID cache.
  RetailActorSceneMesh *palette =
      nullptr; // Retained by scene; sole dirty/cache/bbox/enabled owner.
  std::vector<Buffer> buffers;
  bool present = false, releaseSource = false;
};
