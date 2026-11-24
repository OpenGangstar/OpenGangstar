#pragma once

#include "retail_actor_scene.h"
#include "retail_skinning.h"

// One explicit physical loaded-resource identity. Every fresh instance using
// this load must reuse the same shared handle. Creating another resource is a
// separate load even when its parsed immutable metadata is shared.
class RetailActorLoadedResource {
public:
  static std::shared_ptr<RetailActorLoadedResource>
      create(std::shared_ptr<const RetailActorSceneSource>);
  static std::shared_ptr<RetailActorLoadedResource>
  load(FileSystemBase &, const std::string &path);

  RetailActorLoadedResource(const RetailActorLoadedResource &) = delete;
  RetailActorLoadedResource &
  operator=(const RetailActorLoadedResource &) = delete;

  const std::shared_ptr<const RetailActorSceneSource> &source() const;
  // Requires the exact geometry object in source()->model.meshes. The returned
  // mutable arrays retain their fixed bank and immutable source, without
  // retaining an actor, material, palette or the resource wrapper itself.
  std::shared_ptr<RetailSkinningOutput> currentStreams(const CityMesh &) const;

private:
  explicit RetailActorLoadedResource(
      std::shared_ptr<const RetailActorSceneSource>);
  struct Storage;
  std::shared_ptr<Storage> storage;
};
