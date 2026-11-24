#include "city_assets.h"
#include "filesystem.h"
#include "retail_skinning.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
uint32_t bits(float value)
{
    uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
const RetailSkinningMatrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
void synthetic()
{
    CityScene model;
    model.path = "zero-row-fixture.bdae";
    model.bytes.resize(320);
    const auto word = [&](size_t offset, uint32_t value) {
        for (size_t i = 0; i < 4; ++i) model.bytes.at(offset + i) = uint8_t(value >> (i * 8));
    };
    const auto allocate = [&](size_t size) {
        model.bytes.resize((model.bytes.size() + 3) & ~size_t(3));
        const auto offset = uint32_t(model.bytes.size());
        model.bytes.resize(model.bytes.size() + size);
        return offset;
    };
    const auto string = [&](const char* text) {
        const auto offset = uint32_t(model.bytes.size());
        while (*text) model.bytes.push_back(uint8_t(*text++));
        model.bytes.push_back(0);
        return offset;
    };
    CityMesh mesh;
    mesh.name = "body";
    mesh.positions = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    mesh.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    model.meshes.push_back(mesh);
    CityNode bone{};
    bone.name = "bone-node";
    bone.jointName = "Bone";
    model.nodes.push_back(bone);
    word(84, 1); word(88, 128);
    word(128, 0); word(132, string("body-skin")); word(136, 160);
    word(160, string("#body"));
    for (size_t i = 0; i < 16; ++i) word(164 + i * 4, bits(identity[i]));
    word(228, 1);
    const auto scopes = allocate(4); word(232, scopes); word(scopes, string("Bone"));
    word(236, 16);
    const auto inverse = allocate(64); word(240, inverse);
    for (size_t i = 0; i < 16; ++i) word(inverse + i * 4, bits(identity[i]));
    word(244, 1);
    const auto weights = allocate(4); word(248, weights); word(weights, bits(1));
    word(252, 3);
    const auto counts = allocate(3); word(256, counts); model.bytes[counts + 1] = 1;
    word(260, 2); word(264, allocate(4)); // One (joint0,weight0) pair, only for middle row.
    const auto skins = loadCityActorSkins(model);
    require(skins.size() == 1 && skins[0].vertices.size() == 3 && skins[0].vertices[0].empty() &&
            skins[0].vertices[1].size() == 1 && skins[0].vertices[2].empty(),
            "Serialized empty influence rows survive around a real nonempty row");
    RetailSkinningOutput output;
    output.positions.resize(3); output.normals.resize(3);
    retailSkinVertexRange(mesh, skins[0], {identity}, 0, 3, output);
    require(output.positions[1] == mesh.positions[1] && output.normals[1] == mesh.normals[1],
            "Empty rows consume no influence pair or fabricate a bind-pose weight");
    for (size_t vertex : {0u, 2u})
        for (size_t axis = 0; axis < 3; ++axis)
            require(std::isnan(output.positions[vertex][axis]) && std::isnan(output.normals[vertex][axis]),
                    "Original zero-total arithmetic reaches the existing CPU core");
    const auto rejects = [&] {
        try { (void)loadCityActorSkins(model); } catch (const std::runtime_error&) { return; }
        throw std::runtime_error("Malformed nonempty/trailing influence data must still reject");
    };
    model.bytes[counts + 1] = 2; rejects(); // Too many pairs for the nonempty row.
    model.bytes[counts + 1] = 0; rejects(); // Unconsumed serialized pair.
    model.bytes[counts + 1] = 1; word(weights, bits(.5f)); rejects(); // Existing nonempty total check.
}

void catalog(const std::filesystem::path& directory, bool dump)
{
    ResourceFileSystem files(directory);
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
        if (entry.is_regular_file() && entry.path().extension() == ".bdae")
            paths.push_back(std::filesystem::relative(entry.path(), directory));
    std::sort(paths.begin(), paths.end());
    size_t controllers = 0, vertices = 0, empty = 0, nan = 0;
    for (const auto& path : paths) {
        const auto model = loadCityScene(files, path.generic_string());
        for (const auto& skin : loadCityActorSkins(model)) {
            ++controllers; vertices += skin.vertices.size();
            if (dump) std::cout << "catalog {\"path\":" << std::quoted(path.generic_string())
                << ",\"controller\":" << std::quoted(skin.controllerUid) << ",\"index\":"
                << skin.controllerIndex << ",\"vertices\":[";
            for (size_t i = 0; i < skin.vertices.size(); ++i) {
                const auto& row = skin.vertices[i];
                if (row.empty()) {
                    ++empty;
                    require(path.filename() == "car_limo.bdae" && skin.controllerUid == "car_body-mesh-morpher-skin" &&
                            ((i >= 408 && i < 456) || (i >= 495 && i < 543)),
                            "Actual original empty-row identities and exact ranges");
                }
                if (dump) {
                    if (i) std::cout << ',';
                    std::cout << '[';
                    for (size_t j = 0; j < row.size(); ++j) {
                        if (j) std::cout << ',';
                        std::cout << '[' << row[j].joint << ',' << bits(row[j].weight) << ']';
                    }
                    std::cout << ']';
                }
            }
            if (dump) std::cout << "]}\n";
            if (path.filename() == "car_limo.bdae" && skin.controllerUid == "car_body-mesh-morpher-skin") {
                const auto mesh = std::find_if(model.meshes.begin(), model.meshes.end(),
                    [&](const auto& candidate) { return candidate.name == skin.meshName; });
                require(mesh != model.meshes.end(), "Actual limo source geometry");
                RetailSkinningOutput output;
                output.positions.assign(mesh->positions.size(), {91, 92, 93});
                output.normals.assign(mesh->positions.size(), {81, 82, 83});
                for (const auto range : {std::pair<size_t, size_t>{408, 456}, {495, 543}})
                    retailSkinVertexRange(*mesh, skin, std::vector<RetailSkinningMatrix>(skin.joints.size(), identity),
                                          range.first, range.second, output);
                for (size_t i = 0; i < skin.vertices.size(); ++i)
                    for (size_t axis = 0; axis < 3; ++axis)
                        if (skin.vertices[i].empty()) {
                            require(std::isnan(output.positions[i][axis]) && std::isnan(output.normals[i][axis]),
                                    "Actual decoded96 empty rows preserve original576 NaN classifications");
                            nan += 2;
                        } else require(output.positions[i] == std::array<float, 3>{91, 92, 93} &&
                                       output.normals[i] == std::array<float, 3>{81, 82, 83},
                                       "Outside selected original zero ranges remains untouched");
            }
        }
    }
    require(paths.size() == 359 && controllers == 83 && empty == 96 && nan == 576,
            "Every supplied skin and every original zero-influence row is enumerated");
    std::cout << paths.size() << " BRES, " << controllers << " skins, " << vertices
              << " vertices, " << empty << " empty rows, " << nan << " original NaN classifications\n";
}
}
int main(int argc, char** argv)
{
    try {
        synthetic();
        if (argc > 1) catalog(argv[1], argc > 2 && std::string_view(argv[2]) == "--dump");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
