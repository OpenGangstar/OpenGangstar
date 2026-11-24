#pragma once

#include "city_assets.h"

#include <memory>

class RetailActorScene;
class RetailActorSceneNode;
struct RetailActorSceneSource;

// One original root-library CMaterial identity. Nodes retain this owner rather
// than a scalar snapshot; it retains immutable source, never the live actor.
// Static prepared image descriptors only: dynamic CImage replacement is separate.
class RetailActorMaterial {
  public:
    RetailActorMaterial(const RetailActorMaterial &) = delete;
    RetailActorMaterial &operator=(const RetailActorMaterial &) = delete;
    const std::string &uid() const { return identity; }
    uint32_t sourceIndex() const { return index; }
    const CityMaterial &current() const { return prepared; }
    RetailMaterialState &state() { return prepared.state; }
    const RetailMaterialState &state() const { return prepared.state; }

  private:
    friend class RetailActorScene;
    RetailActorMaterial(std::shared_ptr<const RetailActorSceneSource>, uint32_t);
    std::shared_ptr<const RetailActorSceneSource> source;
    uint32_t index;
    std::string identity;
    CityMaterial prepared;
};

// Original traversal contracts differ: recognized color/alpha nodes stop at
// material0; flags visit every own material and then every actual child.
void setRetailActorColor(RetailActorSceneNode &, uint32_t color, uint32_t selector);
void setRetailActorAlpha(RetailActorSceneNode &, uint32_t alpha, uint32_t selector);
void setRetailActorMaterialFlag(RetailActorSceneNode &, uint32_t flag, bool value);
