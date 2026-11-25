#pragma once

#include "retail_actor_resource.h"
#include "retail_deformation.h"

#include <map>

// One active DefaultFactory software composition per actual loaded Scene.
// Scene holds only a weak association; this owner retains Scene and all process
// dependencies. Returned process refs are borrowed and must not outlive it.
// Retained output views use the existing separate allocation/scene lifetime.
class RetailActorDeformationPipeline {
public:
  static std::shared_ptr<RetailActorDeformationPipeline>
      create(std::shared_ptr<RetailActorScene>);
  RetailActorDeformationPipeline(const RetailActorDeformationPipeline&) = delete;
  RetailActorDeformationPipeline& operator=(const RetailActorDeformationPipeline&) = delete;
  RetailDeformationMesh& process(const RetailActorSceneMesh&) const;
  RetailActorScene& scene() const { return *owner; }

private:
  explicit RetailActorDeformationPipeline(std::shared_ptr<RetailActorScene>);
  RetailDeformationMesh::Ptr build(RetailActorSceneMesh&);
  std::shared_ptr<RetailActorScene> owner;
  std::map<const RetailActorSceneMesh*,RetailDeformationMesh::Ptr> processes;
};
