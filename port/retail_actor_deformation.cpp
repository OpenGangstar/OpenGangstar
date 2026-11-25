#include "retail_actor_deformation.h"

#include <stdexcept>
#include <utility>

std::shared_ptr<RetailActorDeformationPipeline>
RetailActorDeformationPipeline::create(std::shared_ptr<RetailActorScene> scene) {
  if (!scene || !scene->loadedResource())
    throw std::invalid_argument("Actual deformation composition requires its explicit loaded Scene.");
  if (auto current = scene->activeDeformation.lock()) return current;
  auto result = std::shared_ptr<RetailActorDeformationPipeline>(
      new RetailActorDeformationPipeline(scene));
  scene->activeDeformation = result;
  return result;
}
RetailActorDeformationPipeline::RetailActorDeformationPipeline(std::shared_ptr<RetailActorScene> scene)
    : owner(std::move(scene)) {
  for (const auto &mesh : owner->meshes())
    if (mesh->skin() || mesh->morphRuntime()) build(*mesh);
}
RetailDeformationMesh::Ptr RetailActorDeformationPipeline::build(RetailActorSceneMesh &resource) {
  if (const auto found = processes.find(&resource); found != processes.end()) return found->second;
  RetailDeformationMesh::Ptr current;
  if (resource.skin()) {
    current = RetailDeformationMesh::skin(build(resource.baseMesh()),owner,resource,true);
  } else if (resource.morphRuntime()) {
    auto source = build(resource.baseMesh());
    std::vector<RetailDeformationMesh::Ptr> targets;
    for (auto *target : resource.morphTargets()) targets.push_back(build(*target));
    current = RetailDeformationMesh::morph(std::move(source),owner,resource,std::move(targets),true);
  } else {
    current = RetailDeformationMesh::source(owner,resource,resource.loadedSourceStreams());
  }
  processes.emplace(&resource,current);
  return current;
}
RetailDeformationMesh& RetailActorDeformationPipeline::process(const RetailActorSceneMesh &resource) const {
  const auto found = processes.find(&resource);
  if (found == processes.end())
    throw std::invalid_argument("Resource is not an actual immovable dependency in this deformation composition.");
  return *found->second;
}
