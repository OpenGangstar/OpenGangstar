#include "retail_morph.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {
using Stream = std::vector<std::array<float, 3>>;
void validateStream(const Stream &stream, size_t first, size_t end) {
    if (stream.size() < end)
        throw std::runtime_error("Short used original morph stream.");
    for (size_t vertex = first; vertex < end; ++vertex)
        for (float component : stream[vertex])
            if (!std::isfinite(component))
                throw std::runtime_error("Nonfinite used original morph vector.");
}
void setWeighted(Stream &destination, const Stream &source, float weight, size_t first, size_t end) {
    if (weight == 0.f)
        return;
    for (size_t vertex = first; vertex < end; ++vertex) {
        if (weight == 1.f)
            destination[vertex] = source[vertex];
        else
            for (size_t axis = 0; axis < 3; ++axis)
                destination[vertex][axis] = weight * source[vertex][axis];
    }
}
void addWeighted(Stream &destination, const Stream &source, float weight, size_t first, size_t end) {
    if (weight == 0.f)
        return;
    for (size_t vertex = first; vertex < end; ++vertex)
        for (size_t axis = 0; axis < 3; ++axis) {
            if (weight == 1.f)
                destination[vertex][axis] += source[vertex][axis];
            else
                destination[vertex][axis] += weight * source[vertex][axis];
        }
}
} // namespace

void retailMorphVertexRange(const CityMesh &source, const CityMorphController &controller,
                            const std::vector<CityMesh> &targetGeometry, size_t first, size_t end,
                            RetailSkinningOutput &output) {
    if (source.name != controller.source)
        throw std::runtime_error(
            "Morph current source identity does not match the authored controller source.");
    if (first > end || end > source.positions.size() || output.positions.size() < end)
        throw std::runtime_error("Invalid original morph vertex range/output capacity.");
    if (controller.targets.size() >= std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Original morph target count exceeds the source-plus-target uint32 domain.");

    float baseWeight = 1.f;
    for (const auto &target : controller.targets) {
        if (!std::isfinite(target.weight))
            throw std::runtime_error("Nonfinite original morph target weight.");
        if (controller.method == 0)
            baseWeight -= target.weight;
    }
    float initialWeight = baseWeight;
    size_t firstAddedTarget = 0;
    if (baseWeight == 0.f) {
        while (firstAddedTarget < controller.targets.size() &&
               controller.targets[firstAddedTarget].weight == 0.f)
            ++firstAddedTarget;
        if (firstAddedTarget == controller.targets.size())
            throw std::runtime_error("Original morph has no nonzero initializer weight.");
        initialWeight = controller.targets[firstAddedTarget].weight;
        ++firstAddedTarget;
        // Original keeps the SOURCE buffer arrays for this initializer even
        // when its weight came from a later target. That target geometry is
        // skipped; only subsequent nonzero target geometries are accumulated.
    }

    validateStream(source.positions, first, end);
    const bool outputNormals = !output.normals.empty();
    const bool initializeNormals = outputNormals && !source.normals.empty();
    if (initializeNormals) {
        if (!source.floatNormals)
            throw std::runtime_error("Original morph requires used float normal streams.");
        validateStream(source.normals, first, end);
        if (output.normals.size() < end)
            throw std::runtime_error("Short original morph output normal stream.");
    }
    for (size_t i = firstAddedTarget; i < controller.targets.size(); ++i) {
        const auto &target = controller.targets[i];
        if (target.weight == 0.f)
            continue;
        if (target.mesh >= targetGeometry.size())
            throw std::runtime_error("Missing used original morph target.");
        const auto &mesh = targetGeometry[target.mesh];
        validateStream(mesh.positions, first, end);
        if (outputNormals && !mesh.normals.empty()) {
            // Original would pass a NULL adjusted output normal pointer here
            // if its initializer source had no normal array. Reject that
            // malformed combination instead of inventing a zero normal seed.
            if (!initializeNormals)
                throw std::runtime_error(
                    "Original morph normal accumulation has no initialized output pointer.");
            if (!mesh.floatNormals)
                throw std::runtime_error("Original morph requires used float target normals.");
            validateStream(mesh.normals, first, end);
        }
    }

    setWeighted(output.positions, source.positions, initialWeight, first, end);
    if (initializeNormals)
        setWeighted(output.normals, source.normals, initialWeight, first, end);
    for (size_t i = firstAddedTarget; i < controller.targets.size(); ++i) {
        const auto &target = controller.targets[i];
        if (target.weight == 0.f)
            continue;
        const auto &mesh = targetGeometry[target.mesh];
        addWeighted(output.positions, mesh.positions, target.weight, first, end);
        if (outputNormals && !mesh.normals.empty())
            addWeighted(output.normals, mesh.normals, target.weight, first, end);
    }
}
