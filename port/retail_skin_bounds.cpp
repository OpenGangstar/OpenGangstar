#include "retail_skin_bounds.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
void extendPoint(RetailSkinBoundingBox &box, const std::array<float, 3> &point) {
    for (size_t axis = 0; axis < 3; ++axis)
        if (point[axis] > box[1][axis])
            box[1][axis] = point[axis];
    for (size_t axis = 0; axis < 3; ++axis)
        if (point[axis] < box[0][axis])
            box[0][axis] = point[axis];
}

RetailSkinBoundingBox transformBox(const RetailSkinBoundsJoint &joint, RetailSkinBoundingBox box) {
    if (joint.identity)
        return box;
    const auto &m = *joint.absolute;
    for (auto &point : box) {
        const auto source = point;
        for (size_t axis = 0; axis < 3; ++axis)
            point[axis] =
                ((source[0] * m[axis] + source[1] * m[axis + 4]) + source[2] * m[axis + 8]) + m[axis + 12];
    }
    // Original transformBox transforms ONLY these two opposite corners, then
    // repairs their ordering. An eight-corner AABB would change its behavior.
    for (size_t axis = 0; axis < 3; ++axis)
        if (box[0][axis] > box[1][axis])
            std::swap(box[0][axis], box[1][axis]);
    return box;
}
} // namespace

RetailSkinBoundingBox
computeRetailSkinBounds(const std::vector<RetailSkinBoundsJoint> &joints,
                        const std::optional<std::vector<RetailSkinBoundingBox>> &authoredJointBoxes) {
    if (!authoredJointBoxes)
        throw std::runtime_error("Missing authoritative original joint bounding boxes.");
    if (joints.empty() || joints.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Original skin bounds require a readable first joint.");
    const auto count = static_cast<uint8_t>(joints.size());
    const size_t used = count ? count : 1;
    const bool boxes = !authoredJointBoxes->empty();
    if (boxes && authoredJointBoxes->size() < used)
        throw std::runtime_error("Short original authored joint bounding-box table.");
    for (size_t joint = 0; joint < used; ++joint) {
        const auto &view = joints[joint];
        if (!view.absolute)
            throw std::runtime_error("Missing live joint absolute matrix for skin bounds.");
        if (boxes) {
            for (const auto &point : authoredJointBoxes->at(joint))
                for (float value : point)
                    if (!std::isfinite(value))
                        throw std::runtime_error("Nonfinite authored joint bounding box.");
            if (view.identity)
                continue;
        }
        for (size_t column = boxes ? 0 : 3; column < 4; ++column)
            for (size_t row = 0; row < 3; ++row)
                if (!std::isfinite(view.absolute->at(column * 4 + row)))
                    throw std::runtime_error("Nonfinite addressed live joint bounding matrix.");
    }
    if (!boxes) {
        const auto &first = *joints.front().absolute;
        RetailSkinBoundingBox result{{{first[12], first[13], first[14]}, {first[12], first[13], first[14]}}};
        for (size_t i = 1; i < count; ++i) {
            const auto &m = *joints[i].absolute;
            extendPoint(result, {m[12], m[13], m[14]});
        }
        return result;
    }
    auto result = transformBox(joints.front(), authoredJointBoxes->front());
    for (size_t i = 1; i < count; ++i) {
        const auto candidate = transformBox(joints[i], authoredJointBoxes->at(i));
        // addInternalBox tests the candidate MAX before its MIN, including ties.
        extendPoint(result, candidate[1]);
        extendPoint(result, candidate[0]);
    }
    return result;
}
