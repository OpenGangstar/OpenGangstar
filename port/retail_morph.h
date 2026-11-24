#pragma once

#include "retail_skinning.h"

// Original CColladaMorphingMesh::morph CPU stream core. The host provides the
// genuine current source streams for controller.source, current target weights,
// decoded target geometry and its actual source-buffer [first,end) range.
// Reuses the existing caller-owned position/normal output representation.
// It does not choose damage/animation weights, own a driver process buffer, or
// replace source controller construction with a guessed base-mesh fallback.
// Outside-range output and absent/unused normal streams remain unchanged.
// Checked malformed-input errors are rejected before any output writes.
void retailMorphVertexRange(const CityMesh &currentSource, const CityMorphController &controller,
                            const std::vector<CityMesh> &targetGeometry, std::size_t firstVertex,
                            std::size_t endVertex, RetailSkinningOutput &output);
void retailMorphVertexRange(const RetailVertexStreams &currentSource, const CityMorphController &controller,
                            const std::vector<CityMesh> &targetGeometry, std::size_t firstVertex,
                            std::size_t endVertex, RetailSkinningOutput &output);
