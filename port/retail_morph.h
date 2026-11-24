#pragma once

#include "retail_skinning.h"

#include <memory>

// Sole runtime morph weight storage, independently retained from process
// outputs. The catalogue owner must be immutable decoded storage, never an
// alias retaining a live ActorScene. A scene may retain this state while a
// process retains that same scene/state; state owns neither live object.
class RetailMorphState {
public:
    RetailMorphState(std::shared_ptr<const CityScene> catalogue, std::size_t controllerIndex);
    RetailMorphState(const RetailMorphState&) = delete;
    RetailMorphState& operator=(const RetailMorphState&) = delete;
    const std::shared_ptr<const CityScene>& catalogue() const { return source; }
    std::size_t controllerIndex() const { return index; }
    const CityMorphController& controller() const { return current; }
    void setWeight(std::size_t authoredTarget, float weight);
    float baseWeight() const { return base; }
    // Actual entry0 storage write; not an invented animation/damage producer.
    void setBaseWeight(float value) { base = value; }

private:
    friend class RetailDeformationMesh;
    std::shared_ptr<const CityScene> source;
    std::size_t index;
    CityMorphController current;
    float base = 1.f;
};

// Original CColladaMorphingMesh::morph CPU stream core. The host provides the
// genuine current source streams for controller.source, current target weights,
// genuine current target streams and its actual source-buffer [first,end) range.
// Reuses the existing caller-owned position/normal output representation.
// It does not choose damage/animation weights, own a driver process buffer, or
// replace source controller construction with a guessed base-mesh fallback.
// Outside-range output and absent/unused normal streams remain unchanged.
// Checked malformed-input errors are rejected before any output writes. The
// explicit runtime base entry is reset/subtracted before used stream validation;
// that observable state is not rolled back when a later checked error occurs.
// Current targets are indexed by controller.targets[i].mesh, like the actual
// catalogue. Views borrow actual target buffer0 streams, not source buffer slot.
void retailMorphVertexRange(const RetailVertexStreams &currentSource, const CityMorphController &controller,
                            const std::vector<RetailVertexStreams> &currentTargets, std::size_t firstVertex,
                            std::size_t endVertex, RetailSkinningOutput &output, float &runtimeBaseWeight);

// Bounded immutable-target compatibility/control entry points. They create
// borrowed views and delegate the same kernel, discarding its runtime base
// entry; a real morph owner uses explicit current targets and its owned entry0.
void retailMorphVertexRange(const CityMesh &currentSource, const CityMorphController &controller,
                            const std::vector<CityMesh> &targetGeometry, std::size_t firstVertex,
                            std::size_t endVertex, RetailSkinningOutput &output);
void retailMorphVertexRange(const RetailVertexStreams &currentSource, const CityMorphController &controller,
                            const std::vector<CityMesh> &targetGeometry, std::size_t firstVertex,
                            std::size_t endVertex, RetailSkinningOutput &output);
