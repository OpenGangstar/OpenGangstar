#include "retail_actor_resource.h"

#include <stdexcept>
#include <utility>

struct RetailActorLoadedResource::Storage {
  explicit Storage(std::shared_ptr<const RetailActorSceneSource> decoded)
      : source(std::move(decoded)) {
    current.reserve(source->model.meshes.size());
    for (const auto &geometry : source->model.meshes)
      current.push_back({geometry.positions, geometry.normals});
  }
  std::shared_ptr<const RetailActorSceneSource> source;
  // Fully constructed before any alias is published; entries never move.
  std::vector<RetailSkinningOutput> current;
};

RetailActorLoadedResource::RetailActorLoadedResource(
    std::shared_ptr<const RetailActorSceneSource> source)
    : storage(std::make_shared<Storage>(std::move(source))) {}

std::shared_ptr<RetailActorLoadedResource> RetailActorLoadedResource::create(
    std::shared_ptr<const RetailActorSceneSource> source) {
  if (!source)
    throw std::invalid_argument(
        "Missing immutable actor loaded-resource source.");
  return std::shared_ptr<RetailActorLoadedResource>(
      new RetailActorLoadedResource(std::move(source)));
}

std::shared_ptr<RetailActorLoadedResource>
RetailActorLoadedResource::load(FileSystemBase &assets,
                                const std::string &path) {
  return create(loadRetailActorSceneSource(assets, path));
}

const std::shared_ptr<const RetailActorSceneSource> &
RetailActorLoadedResource::source() const {
  return storage->source;
}

std::shared_ptr<RetailSkinningOutput>
RetailActorLoadedResource::currentStreams(const CityMesh &geometry) const {
  const auto &meshes = storage->source->model.meshes;
  for (std::size_t index = 0; index < meshes.size(); ++index)
    if (&meshes[index] == &geometry)
      return std::shared_ptr<RetailSkinningOutput>(storage,
                                                   &storage->current[index]);
  throw std::invalid_argument(
      "Geometry does not belong to this actor loaded resource.");
}
