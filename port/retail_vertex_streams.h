#pragma once

#include <array>
#include <string_view>
#include <vector>

using RetailVertexStream = std::vector<std::array<float, 3>>;

// A const borrowed view of the CURRENT full-index streams, not base geometry.
// Name/storage must outlive the call; no data, topology or owner is copied or
// retained. Empty currentNormals means absent, even if immutable base normals
// exist. floatNormals is the current stream format, not an inferred default.
// The caller supplies the same validation identity as the existing CityMesh
// entry point. This view does not resolve or rename runtime controller chains.
struct RetailVertexStreams {
    std::string_view sourceName;
    const RetailVertexStream& currentPositions;
    const RetailVertexStream& currentNormals;
    bool floatNormals;
};
