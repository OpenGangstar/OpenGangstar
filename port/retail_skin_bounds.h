#pragma once

#include <array>
#include <optional>
#include <vector>

using RetailSkinBoundingBox = std::array<std::array<float, 3>, 2>;
using RetailSkinBoundsMatrix = std::array<float, 16>;
struct RetailSkinBoundsJoint {
    const RetailSkinBoundsMatrix *absolute;
    // Actual original CMatrix4 cached hint, not inferred matrix equality.
    bool identity;
};

// Original computeBoundingBox arithmetic after the owning mesh has performed
// its genuine skeleton-cache preparation. Uses LIVE JOINT ABSOLUTE matrices,
// not final skin matrices, inverse binds, bind shape or deformed vertex
// extrema. nullopt means missing authoritative metadata and is rejected. An
// explicitly empty authored table selects the original joint-translation
// branch. The original uint8 joint-loop truncation and unconditional first
// entry remain; truly empty joint input is rejected instead of reading out of
// bounds.
RetailSkinBoundingBox
computeRetailSkinBounds(const std::vector<RetailSkinBoundsJoint> &,
                        const std::optional<std::vector<RetailSkinBoundingBox>> &authoredJointBoxes);
