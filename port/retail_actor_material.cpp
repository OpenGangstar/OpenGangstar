#include "retail_actor_material.h"
#include "retail_actor_scene.h"

RetailActorMaterial::RetailActorMaterial(std::shared_ptr<const RetailActorSceneSource> decoded,
                                         uint32_t sourceIndex)
    : source(std::move(decoded)), index(sourceIndex),
      identity(source->model.materialCatalog.at(index).uid),
      prepared(source->model.materialCatalog.at(index).material) {
    if (const auto end = identity.find('\0'); end != std::string::npos)
        identity.resize(end);
}

namespace {
bool recognized(const RetailActorSceneNode &node) {
    return node.type() == retailSceneMeshType || node.type() == retailSceneSkinnedMeshType;
}
} // namespace
void setRetailActorColor(RetailActorSceneNode &node, uint32_t color, uint32_t selector) {
    if (recognized(node)) {
        node.materialOwner(0)->state().setColor(color, selector);
        return;
    }
    for (auto *child : node.children())
        setRetailActorColor(*child, color, selector);
}
void setRetailActorAlpha(RetailActorSceneNode &node, uint32_t alpha, uint32_t selector) {
    if (recognized(node)) {
        // The original selector test precedes the actual material0 access.
        if (selector == 0)
            node.materialOwner(0)->state().setAlpha(alpha, selector);
        return;
    }
    for (auto *child : node.children())
        setRetailActorAlpha(*child, alpha, selector);
}
void setRetailActorMaterialFlag(RetailActorSceneNode &node, uint32_t flag, bool value) {
    for (size_t i = 0; i < node.materialCount(); ++i)
        node.materialOwner(i)->state().setFlag(flag, value);
    for (auto *child : node.children())
        setRetailActorMaterialFlag(*child, flag, value);
}
