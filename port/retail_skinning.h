#pragma once

#include "city_assets.h"

#include <array>
#include <cstddef>
#include <vector>

using RetailSkinningMatrix = std::array<float, 16>;
struct RetailSkinningOutput {
    std::vector<std::array<float, 3>> positions;
    // Empty means no output normal stream. With either normal stream absent,
    // the original position-only branch leaves output normals unchanged.
    std::vector<std::array<float, 3>> normals;
};

// Original CColladaSkinnedMesh::skin core over [firstVertex,endVertex). The host
// supplies the CURRENT source positions/normals (including an earlier original
// morph operation when applicable), not an assumed undeformed base geometry.
// It also supplies its current FINAL skeleton cache (joint absolute/inverse-bind/bind
// shape already applied), in CitySkin joint order. Nothing is retained here.
// Output streams are caller-owned and already sized; outside-range values stay
// unchanged. Normals use the original raw linear transform, not inverse transpose
// or unit-vector normalization. Only original total-weight division is applied.
// Zero influences/zero total retain original nonfinite arithmetic, never a bind
// pose fallback. Checked structural errors fail before any output writes.
void retailSkinVertexRange(const CityMesh&, const CitySkin&,
    const std::vector<RetailSkinningMatrix>& skeletonCache,
    std::size_t firstVertex, std::size_t endVertex, RetailSkinningOutput&);
