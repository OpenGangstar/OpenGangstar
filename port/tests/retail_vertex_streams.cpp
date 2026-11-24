#include "retail_morph.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using Vec = std::array<float, 3>;
const RetailSkinningMatrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
uint32_t bits(float value) { uint32_t result; std::memcpy(&result, &value, 4); return result; }
void vector(const Vec& value) {
    std::cout << '[' << bits(value[0]) << ',' << bits(value[1]) << ',' << bits(value[2]) << ']';
}
void stream(const RetailVertexStream& values) {
    std::cout << '[';
    for (size_t i = 0; i < values.size(); ++i) { if (i) std::cout << ','; vector(values[i]); }
    std::cout << ']';
}
void mesh(const RetailVertexStream& positions, const RetailVertexStream& normals) {
    std::cout << "{\"positions\":"; stream(positions); std::cout << ",\"normals\":"; stream(normals); std::cout << '}';
}
void dumpMorph(const std::string& name, const RetailVertexStreams& source, const CityMorphController& controller,
               const std::vector<CityMesh>& targets, const RetailSkinningOutput& initial, const RetailSkinningOutput& result) {
    std::cout << "morph {\"name\":\"" << name << "\",\"first\":1,\"end\":3,\"method\":" << controller.method << ",\"weights\":[";
    for (size_t i = 0; i < controller.targets.size(); ++i) { if (i) std::cout << ','; std::cout << bits(controller.targets[i].weight); }
    std::cout << "],\"source\":"; mesh(source.currentPositions, source.currentNormals); std::cout << ",\"targets\":[";
    for (size_t i = 0; i < controller.targets.size(); ++i) {
        if (i) std::cout << ',';
        const auto index = controller.targets[i].mesh;
        if (index >= targets.size()) std::cout << "null";
        else mesh(targets[index].positions, targets[index].normals);
    }
    std::cout << "],\"initial\":"; mesh(initial.positions, initial.normals);
    std::cout << ",\"result\":"; mesh(result.positions, result.normals); std::cout << "}\n";
}
void dumpSkin(const std::string& name, const RetailVertexStreams& source, const CitySkin& skin,
              const std::vector<RetailSkinningMatrix>& matrices, const RetailSkinningOutput& result) {
    std::cout << "skin {\"name\":\"" << name << "\",\"first\":1,\"end\":3,\"inputNormals\":"
              << (!source.currentNormals.empty() ? "true" : "false") << ",\"outputNormals\":true,\"matrices\":[";
    for (size_t i = 0; i < matrices.size(); ++i) {
        if (i) std::cout << ','; std::cout << '[';
        for (size_t c = 0; c < 16; ++c) { if (c) std::cout << ','; std::cout << bits(matrices[i][c]); }
        std::cout << ']';
    }
    std::cout << "],\"vertices\":[";
    for (size_t i = 0; i < source.currentPositions.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << "{\"position\":"; vector(source.currentPositions[i]); std::cout << ",\"normal\":";
        vector(source.currentNormals.empty() ? Vec{} : source.currentNormals[i]); std::cout << ",\"weights\":[";
        for (size_t j = 0; j < skin.vertices[i].size(); ++j) {
            if (j) std::cout << ',';
            std::cout << '[' << skin.vertices[i][j].joint << ',' << bits(skin.vertices[i][j].weight) << ']';
        }
        std::cout << "],\"outPosition\":"; vector(result.positions[i]); std::cout << ",\"outNormal\":"; vector(result.normals[i]); std::cout << '}';
    }
    std::cout << "]}\n";
}
template<class F> void rejectsUnchanged(RetailSkinningOutput& output, F action) {
    const auto before = output;
    bool rejected = false;
    try { action(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected && before.positions == output.positions && before.normals == output.normals,
            "Borrowed stream preflight rejects before writing output");
}
void composition(bool dump) {
    for (unsigned profile = 0; profile < 7; ++profile) {
        std::string name = "source";
        RetailVertexStream positions{{-0.f,1,2},{3,4,5},{6,7,8},{9,10,11}};
        RetailVertexStream normals(4, {1,2,3});
        if (profile == 5) normals.clear();
        CityMorphController controller{}; controller.source = name; controller.method = profile == 3 ? 1 : 0;
        controller.targets = {{0,.25f},{1,.5f}};
        if (profile == 1) controller.targets = {{9999,1.f},{1,0.f}};
        if (profile == 2) controller.targets = {{0,-.5f},{1,1.5f}};
        if (profile == 4) controller.targets = {{9999,-0.f},{9999,0.f}};
        std::vector<CityMesh> targets(2);
        for (size_t i = 0; i < 2; ++i) {
            targets[i].positions.assign(4, {float(20 + i),float(30 + i),float(40 + i)});
            targets[i].normals.assign(4, {float(2 + i),float(3 + i),float(4 + i)});
        }
        CitySkin skin{}; skin.meshName = name; skin.joints.resize(2); skin.vertices.resize(4, {{0,1}});
        skin.vertices[0] = skin.vertices[3] = {{65535,std::numeric_limits<float>::quiet_NaN()}};
        skin.vertices[2] = {{0,.25f},{1,.75f},{65535,-0.f}};
        if (profile == 6) skin.vertices[2] = {{0,1},{1,-1},{65535,-0.f}};
        std::vector<RetailSkinningMatrix> matrices(2, identity);
        matrices[0][12] = 10; matrices[0][13] = 20; matrices[0][14] = 30;
        matrices[1][0] = 2; matrices[1][5] = 3; matrices[1][10] = 4;
        RetailSkinningOutput morphed{RetailVertexStream(4, {41,42,43}), RetailVertexStream(4, {51,52,53})};
        if (profile == 5) morphed.normals.clear();
        // Construct ONCE, before any morph writes. The same view sees every new
        // current result directly; it owns no CityMesh/topology or vector copy.
        const RetailVertexStreams current{name, positions, normals, profile != 5};
        const RetailVertexStreams skinInput{name, morphed.positions, morphed.normals, profile != 5};
        const auto* sourceAddress = positions.data();
        const auto* intermediateAddress = morphed.positions.data();
        require(&current.currentPositions == &positions && current.sourceName.data() == name.data() &&
                &skinInput.currentPositions == &morphed.positions && &skinInput.currentNormals == &morphed.normals,
                "View preserves borrowed name/vector identities without copying storage");
        float previous = 0;
        for (unsigned frame = 0; frame < 3; ++frame) {
            positions[1][0] += 16;
            const auto initial = morphed;
            RetailSkinningOutput output{RetailVertexStream(4, {91,92,93}), RetailVertexStream(4, {81,82,83})};
            const auto* outputAddress = output.positions.data();
            retailMorphVertexRange(current, controller, targets, 1, 3, morphed);
            retailSkinVertexRange(skinInput, skin, matrices, 1, 3, output);
            require(sourceAddress == positions.data() && intermediateAddress == morphed.positions.data() &&
                    skinInput.currentPositions.data() == intermediateAddress && outputAddress == output.positions.data(),
                    "Current morph output feeds skin at the same address; cores never resize streams");
            require(output.positions[0] == Vec{91,92,93} && output.positions[3] == Vec{91,92,93} &&
                    morphed.positions[0] == Vec{41,42,43} && morphed.positions[3] == Vec{41,42,43},
                    "Full-index storage preserves outside-range values and ignores unread invalid influences");
            require(frame == 0 || output.positions[1][0] != previous, "Reused borrowed view observes later source updates");
            previous = output.positions[1][0];
            if (profile == 1) require(morphed.positions[1] == positions[1], "Literal base-zero initializer reads source, not skipped target");
            if (profile == 5) require(output.normals == RetailVertexStream(4, {81,82,83}), "Absent current normals never fall back to base/target normals");
            if (profile == 6) require(!std::isfinite(output.positions[2][0]), "Zero-total skin arithmetic remains nonfinite without a fallback");
            if (dump) {
                const auto id = "current-flow-" + std::to_string(profile) + "-" + std::to_string(frame);
                dumpMorph(id, current, controller, targets, initial, morphed);
                dumpSkin(id, skinInput, skin, matrices, output);
            }
        }
    }
}
void checkedViews() {
    std::string name = "source";
    RetailVertexStream positions(4, {1,2,3}), normals(4, {4,5,6}), absent;
    CitySkin skin{}; skin.meshName = name; skin.vertices.resize(4, {{0,1}}); skin.joints.resize(1);
    std::vector<RetailSkinningMatrix> matrices{identity};
    CityMorphController morph{}; morph.source = name;
    RetailSkinningOutput output{RetailVertexStream(4, {91,92,93}), RetailVertexStream(4, {81,82,83})};
    const auto view = RetailVertexStreams{name, positions, normals, true};
    const auto copiedView = view;
    require(&copiedView.currentPositions == &positions && copiedView.currentNormals.data() == normals.data(), "Copying a view only copies borrowed identities");
    name[0] = 'S';
    rejectsUnchanged(output, [&] { retailSkinVertexRange(view, skin, matrices, 1, 3, output); });
    rejectsUnchanged(output, [&] { retailMorphVertexRange(view, morph, {}, 1, 3, output); });
    name[0] = 's';
    skin.vertices.resize(3);
    rejectsUnchanged(output, [&] { retailSkinVertexRange(view, skin, matrices, 1, 3, output); });
    skin.vertices.resize(4, {{0,1}});
    const auto packed = RetailVertexStreams{name, positions, normals, false};
    rejectsUnchanged(output, [&] { retailSkinVertexRange(packed, skin, matrices, 1, 3, output); });
    rejectsUnchanged(output, [&] { retailMorphVertexRange(packed, morph, {}, 1, 3, output); });
    positions[2][0] = std::numeric_limits<float>::infinity();
    rejectsUnchanged(output, [&] { retailSkinVertexRange(view, skin, matrices, 1, 3, output); });
    rejectsUnchanged(output, [&] { retailMorphVertexRange(view, morph, {}, 1, 3, output); });
    positions[2][0] = 1;
    const auto missing = RetailVertexStreams{name, positions, absent, false};
    retailSkinVertexRange(missing, skin, matrices, 1, 3, output);
    require(output.normals == RetailVertexStream(4, {81,82,83}), "Missing current stream stays absent even with output capacity and false format");
    CityMesh base{}; base.name = name; base.positions = positions; base.normals = normals;
    auto legacy = output, borrowed = output;
    retailSkinVertexRange(base, skin, matrices, 1, 3, legacy);
    retailSkinVertexRange(view, skin, matrices, 1, 3, borrowed);
    require(legacy.positions == borrowed.positions && legacy.normals == borrowed.normals, "Existing mesh entry point and borrowed entry point share one core");
}
}
int main(int argc, char** argv) {
    static_assert(std::is_trivially_copyable_v<RetailVertexStreams>);
    try {
        composition(argc > 1 && std::string(argv[1]) == "--oracle-dump");
        checkedViews();
        std::cout << "Borrowed current streams:21 morph-to-skin cases, stable addresses, absent normals and unchanged preflight passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
