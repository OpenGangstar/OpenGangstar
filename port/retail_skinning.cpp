#include "retail_skinning.h"

#include <cmath>
#include <stdexcept>

namespace {
void finiteVector(const std::array<float, 3>& value)
{
    for (float component : value)
        if (!std::isfinite(component)) throw std::runtime_error("Nonfinite source skin vector.");
}

float linearComponent(const RetailSkinningMatrix& matrix, const std::array<float, 3>& value, size_t axis)
{
    return (value[0] * matrix[axis] + value[1] * matrix[axis + 4]) + value[2] * matrix[axis + 8];
}
}

void retailSkinVertexRange(const CityMesh& mesh, const CitySkin& skin,
    const std::vector<RetailSkinningMatrix>& skeletonCache,
    size_t firstVertex, size_t endVertex, RetailSkinningOutput& output)
{
    if (skin.meshName != mesh.name || skin.vertices.size() != mesh.positions.size() ||
        skeletonCache.size() != skin.joints.size())
        throw std::runtime_error("Skin source geometry or final joint-cache identity/count mismatch.");
    if (firstVertex > endVertex || endVertex > mesh.positions.size() || output.positions.size() < endVertex)
        throw std::runtime_error("Invalid original skin vertex range/output capacity.");
    const bool normals = !mesh.normals.empty() && !output.normals.empty();
    if (normals && (!mesh.floatNormals || mesh.normals.size() < endVertex || output.normals.size() < endVertex))
        throw std::runtime_error("Original CPU skinning requires complete float normal streams.");

    // Checked native input adaptation: reject malformed used references before
    // partial output writes. Zero weights skip their joint read as in the game.
    for (size_t vertex = firstVertex; vertex < endVertex; ++vertex) {
        finiteVector(mesh.positions[vertex]);
        if (normals) finiteVector(mesh.normals[vertex]);
        if (skin.vertices[vertex].size() > 255)
            throw std::runtime_error("Skin influence count exceeds the original byte-sized domain.");
        for (const auto& influence : skin.vertices[vertex]) {
            if (!std::isfinite(influence.weight)) throw std::runtime_error("Nonfinite skin weight.");
            if (influence.weight == 0.f) continue;
            if (influence.joint >= skeletonCache.size()) throw std::runtime_error("Missing used skin joint cache.");
            const auto& matrix = skeletonCache[influence.joint];
            for (size_t column = 0; column < 4; ++column)
                for (size_t row = 0; row < 3; ++row)
                    if (!std::isfinite(matrix[column * 4 + row]))
                        throw std::runtime_error("Nonfinite used skin matrix component.");
        }
    }

    for (size_t vertex = firstVertex; vertex < endVertex; ++vertex) {
        std::array<float, 3> position{}, normal{};
        float total = 0.f;
        for (const auto& influence : skin.vertices[vertex]) {
            const float weight = influence.weight;
            if (weight == 0.f) continue;
            const auto& matrix = skeletonCache[influence.joint];
            for (size_t axis = 0; axis < 3; ++axis) {
                const float transformed = linearComponent(matrix, mesh.positions[vertex], axis) + matrix[12 + axis];
                position[axis] += weight * transformed;
                if (normals) normal[axis] += weight * linearComponent(matrix, mesh.normals[vertex], axis);
            }
            total += weight;
        }
        if (total != 1.f) {
            const float inverse = 1.f / total;
            for (size_t axis = 0; axis < 3; ++axis) {
                position[axis] *= inverse;
                if (normals) normal[axis] *= inverse;
            }
        }
        output.positions[vertex] = position;
        if (normals) output.normals[vertex] = normal;
    }
}
