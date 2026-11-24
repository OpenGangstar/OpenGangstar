#include "retail_skinning.h"
#include "filesystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Vec = std::array<float, 3>;
using Matrix = RetailSkinningMatrix;
const Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
uint32_t bits(float value) {
    uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}
void near(float a, float b, const char *message) {
    require(std::isfinite(a) && std::abs(a - b) < .00001f, message);
}
template <class F> void rejects(F f, const char *message) {
    bool rejected = false;
    try {
        f();
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, message);
}
struct Fixture {
    CityMesh mesh;
    CitySkin skin;
    std::vector<Matrix> matrices;
    RetailSkinningOutput out;
    size_t first = 0, end = 1;
    Fixture(size_t vertices = 1, size_t joints = 1) {
        mesh.name = skin.meshName = "fixture-mesh";
        mesh.positions.resize(vertices, {1, 2, 3});
        mesh.normals.resize(vertices, {1, 2, 3});
        skin.vertices.resize(vertices, {{0, 1}});
        skin.joints.resize(joints);
        matrices.resize(joints, identity);
        out.positions.resize(vertices, {91, 92, 93});
        out.normals.resize(vertices, {81, 82, 83});
        end = vertices;
    }
    void run() {
        retailSkinVertexRange(mesh, skin, matrices, first, end, out);
    }
};
void words(Vec value) {
    std::cout << '[' << bits(value[0]) << ',' << bits(value[1]) << ',' << bits(value[2]) << ']';
}
void dump(const Fixture &f, const std::string &name) {
    std::cout << "skin {\"name\":" << std::quoted(name) << ",\"first\":" << f.first << ",\"end\":" << f.end
              << ",\"inputNormals\":" << (!f.mesh.normals.empty() ? "true" : "false")
              << ",\"outputNormals\":" << (!f.out.normals.empty() ? "true" : "false") << ",\"matrices\":[";
    for (size_t j = 0; j < f.matrices.size(); ++j) {
        if (j)
            std::cout << ',';
        std::cout << '[';
        for (size_t c = 0; c < 16; ++c) {
            if (c)
                std::cout << ',';
            std::cout << bits(f.matrices[j][c]);
        }
        std::cout << ']';
    }
    std::cout << "],\"vertices\":[";
    for (size_t i = 0; i < f.mesh.positions.size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << "{\"position\":";
        words(f.mesh.positions[i]);
        std::cout << ",\"normal\":";
        words(f.mesh.normals.empty() ? Vec{} : f.mesh.normals[i]);
        std::cout << ",\"weights\":[";
        for (size_t j = 0; j < f.skin.vertices[i].size(); ++j) {
            if (j)
                std::cout << ',';
            const auto &w = f.skin.vertices[i][j];
            std::cout << '[' << w.joint << ',' << bits(w.weight) << ']';
        }
        std::cout << "],\"outPosition\":";
        words(f.out.positions[i]);
        std::cout << ",\"outNormal\":";
        words(f.out.normals.empty() ? Vec{} : f.out.normals[i]);
        std::cout << '}';
    }
    std::cout << "]}\n";
}
void unit(bool emit) {
    Fixture scale;
    scale.matrices[0] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 10, 20, 30, 1};
    scale.run();
    require(scale.out.positions[0] == Vec{12, 26, 42}, "Original point matrix orientation and translation");
    require(scale.out.normals[0] == Vec{2, 6, 12},
            "Original normal raw linear transform, no inverse transpose/unit normalization");
    if (emit)
        dump(scale, "scale-and-translation");
    Fixture weighted(1, 2);
    weighted.mesh.positions[0] = {0, 0, 0};
    weighted.mesh.normals[0] = {0, 0, 2};
    weighted.matrices[1][12] = 100;
    weighted.skin.vertices[0] = {{0, .25f}, {1, .75f}};
    weighted.run();
    require(weighted.out.positions[0] == Vec{75, 0, 0} && weighted.out.normals[0] == Vec{0, 0, 2},
            "Original influence weighting preserves normal magnitude");
    if (emit)
        dump(weighted, "unit-total");
    weighted.skin.vertices[0] = {{0, 1}, {1, 1}};
    weighted.run();
    require(weighted.out.positions[0][0] == 50, "Nonunit total divides weighted position");
    if (emit)
        dump(weighted, "nonunit-total");
    weighted.skin.vertices[0] = {{65535, -0.f}, {0, .5f}, {1, std::nextafter(std::nextafter(.5f, 1.f), 1.f)}};
    weighted.run();
    near(weighted.out.positions[0][0], 50.00000762939453125f,
         "Near-one total uses exact comparison and reciprocal");
    if (emit)
        dump(weighted, "near-one-total-and-zero-invalid-joint");
    Fixture empty;
    empty.skin.vertices[0].clear();
    empty.run();
    for (size_t c = 0; c < 3; ++c)
        require(std::isnan(empty.out.positions[0][c]) && std::isnan(empty.out.normals[0][c]),
                "Zero influences retain original nonfinite arithmetic");
    if (emit)
        dump(empty, "no-influences");
    Fixture zeros;
    zeros.skin.vertices[0] = {{65535, 0}, {65534, -0.f}};
    zeros.run();
    require(std::isnan(zeros.out.positions[0][0]),
            "Zero weights skip invalid joint reads then retain zero-total arithmetic");
    if (emit)
        dump(zeros, "zero-weights");
    Fixture cancel(1, 2);
    cancel.skin.vertices[0] = {{0, 1}, {1, -1}};
    cancel.matrices[1][12] = 10;
    cancel.run();
    require(std::isinf(cancel.out.positions[0][0]) && std::signbit(cancel.out.positions[0][0]),
            "Cancelled total is not silently changed to bind pose");
    if (emit)
        dump(cancel, "cancelled-total");
    Fixture order(1, 3);
    order.skin.vertices[0] = {{0, 16777216.f}, {1, 1}, {2, -16777216.f}};
    order.run();
    require(std::isnan(order.out.positions[0][0]), "Original float accumulation order preserved");
    if (emit)
        dump(order, "source-order-cancellation");
    order.skin.vertices[0] = {{0, 16777216.f}, {2, -16777216.f}, {1, 1}};
    order.run();
    require(order.out.positions[0] == Vec{1, 2, 3}, "Influences must not be reordered by joint or weight");
    if (emit)
        dump(order, "source-order-rearranged");
    Fixture partial(3, 2);
    partial.first = 1;
    partial.end = 2;
    partial.skin.vertices[0] = {{65535, 1}};
    partial.skin.vertices[1] = {{0, .25f}, {1, .75f}};
    partial.matrices[1][13] = 20;
    partial.run();
    require(partial.out.positions[0] == Vec{91, 92, 93} && partial.out.positions[2] == Vec{91, 92, 93},
            "Outside-range output remains untouched");
    require(partial.out.positions[1] == Vec{1, 17, 3},
            "Global vertex/influence identity survives nonzero range start");
    if (emit)
        dump(partial, "partial-range");
    Fixture noSourceNormals;
    noSourceNormals.mesh.normals.clear();
    noSourceNormals.run();
    require(noSourceNormals.out.normals[0] == Vec{81, 82, 83},
            "Missing input normal selects original position-only branch");
    if (emit)
        dump(noSourceNormals, "no-source-normals");
    Fixture noOutputNormals;
    noOutputNormals.out.normals.clear();
    noOutputNormals.run();
    require(noOutputNormals.out.positions[0] == Vec{1, 2, 3},
            "Missing output normal selects position-only branch");
    if (emit)
        dump(noOutputNormals, "no-output-normals");
    Fixture signedZero;
    signedZero.mesh.positions[0] = {-0.f, -0.f, -0.f};
    signedZero.mesh.normals[0] = {-0.f, -0.f, -0.f};
    signedZero.matrices[0][12] = signedZero.matrices[0][13] = signedZero.matrices[0][14] = -0.f;
    signedZero.run();
    for (float component : signedZero.out.positions[0])
        require(bits(component) == 0,
                "Original initial+zero accumulation is not optimized to first influence copy");
    if (emit)
        dump(signedZero, "signed-zero");
    Fixture unused;
    for (size_t c : {3u, 7u, 11u, 15u})
        unused.matrices[0][c] = std::numeric_limits<float>::quiet_NaN();
    unused.run();
    require(unused.out.positions[0] == Vec{1, 2, 3},
            "Unused homogeneous matrix components are not substituted into original point projection");
    if (emit)
        dump(unused, "unused-matrix-row");
    Fixture bad(2, 1);
    bad.skin.vertices[1] = {{1, 1}};
    rejects([&] { bad.run(); }, "Reject missing used cache before writing any prior vertex");
    require(bad.out.positions[0] == Vec{91, 92, 93}, "Malformed input preflight is atomic for output");
    bad = Fixture();
    bad.matrices.clear();
    rejects([&] { bad.run(); }, "Reject missing cache size");
    bad = Fixture();
    bad.skin.vertices.clear();
    rejects([&] { bad.run(); }, "Reject missing per-vertex influence records");
    bad = Fixture();
    bad.skin.meshName = "another-mesh";
    rejects([&] { bad.run(); }, "Reject a skin belonging to another source mesh");
    bad = Fixture(2);
    bad.mesh.normals.resize(1);
    rejects([&] { bad.run(); }, "Reject short input float normal stream");
    bad = Fixture(2);
    bad.out.normals.resize(1);
    rejects([&] { bad.run(); }, "Reject short output float normal stream");
    require(bad.out.positions[0] == Vec{91, 92, 93},
            "Normal capacity error does not partially write positions");
    bad = Fixture();
    bad.end = 2;
    rejects([&] { bad.run(); }, "Reject out-of-range vertex span");
    bad = Fixture();
    bad.out.positions.clear();
    rejects([&] { bad.run(); }, "Reject short output capacity");
    bad = Fixture();
    bad.mesh.floatNormals = false;
    rejects([&] { bad.run(); }, "Packed normal conversion remains explicit unsupported input");
    bad.out.normals.clear();
    bad.run();
    require(bad.out.positions[0] == Vec{1, 2, 3},
            "Position-only path does not pretend to decode packed normals");
    bad = Fixture();
    bad.skin.vertices[0].resize(256, {0, 1});
    rejects([&] { bad.run(); }, "Reject influence count outside original byte range");
    bad = Fixture();
    bad.skin.vertices[0][0].weight = std::numeric_limits<float>::infinity();
    rejects([&] { bad.run(); }, "Reject nonfinite source weight");
    bad = Fixture();
    bad.matrices[0][12] = std::numeric_limits<float>::infinity();
    rejects([&] { bad.run(); }, "Reject nonfinite used cache component");
    bad = Fixture();
    bad.first = bad.end = 1;
    bad.run();
    require(bad.out.positions[0] == Vec{91, 92, 93}, "Empty range writes nothing");
    if (emit)
        dump(bad, "empty-range");
    for (size_t iteration = 0; iteration < 40; ++iteration) {
        Fixture f(3, 3);
        f.first = iteration % 2;
        f.end = 3;
        for (size_t j = 0; j < 3; ++j)
            for (size_t c = 0; c < 16; ++c)
                f.matrices[j][c] = float(int((iteration * 19 + j * 7 + c * 3) % 29) - 14) * .125f;
        for (size_t v = 0; v < 3; ++v) {
            f.mesh.positions[v] = {float(iteration) * .03125f, float(int(v) - 1) * 13.125f, -.0625f};
            f.mesh.normals[v] = {-.75f, .125f, float(v) * .375f};
            f.skin.vertices[v] = {{uint16_t((iteration + v) % 3), .125f},
                                  {uint16_t((iteration + v + 1) % 3), .625f},
                                  {uint16_t((iteration + v + 2) % 3), iteration % 2 ? .125f : .25f}};
        }
        if (iteration % 3 == 0)
            f.out.normals.clear();
        f.run();
        if (emit)
            dump(f, "finite-fixture-" + std::to_string(iteration));
    }
}
void retail(const std::filesystem::path &root, bool emit) {
    ResourceFileSystem files(root);
    CityAssets requests;
    for (uint8_t i = 0; i < 2; ++i) {
        CitySpawnMetatype entry{};
        entry.name = "reference-resource-load";
        entry.objectType = 5;
        entry.model = i ? "./char_pthug_jail.bdae" : "./char_pthug.bdae";
        requests.spawnMetatypes.emplace(i, std::move(entry));
    }
    // Existing public loader invokes the same authoritative skin decoder. The
    // synthetic loader request is not a claim about the Player's runtime category.
    const auto actors = loadCitySpawnActors(files, requests);
    size_t checked = 0;
    for (const auto &[id, actor] : actors)
        for (const auto &skin : actor.skins) {
            const auto mesh = std::find_if(actor.model.meshes.begin(), actor.model.meshes.end(),
                                           [&](const auto &m) { return m.name == skin.meshName; });
            require(mesh != actor.model.meshes.end(), "Actual decoded skin source mesh");
            for (size_t phase = 0; phase < 3; ++phase) {
                Fixture f;
                f.mesh = *mesh;
                f.skin = skin;
                f.first = phase == 2 ? 13 : 0;
                f.end = mesh->positions.size() - (phase == 2 ? 7 : 0);
                f.matrices.assign(skin.joints.size(), identity);
                f.out.positions.assign(mesh->positions.size(), {91, 92, 93});
                f.out.normals.assign(mesh->positions.size(), {81, 82, 83});
                // Explicit caller-supplied FINAL-cache fixtures, not guessed live
                // animation or a duplicate skeleton cache implementation.
                if (phase)
                    for (size_t j = 0; j < f.matrices.size(); ++j) {
                        auto &m = f.matrices[j];
                        m[0] = 1 + float(j) * .03125f;
                        m[5] = .75f;
                        m[10] = 1.25f;
                        m[4] = float(j % 3) * .0625f;
                        m[12] = float(j) * 3;
                        m[13] = -float(j);
                        m[14] = 7;
                    }
                f.run();
                for (size_t v = f.first; v < f.end; ++v)
                    for (size_t c = 0; c < 3; ++c)
                        require(std::isfinite(f.out.positions[v][c]) && std::isfinite(f.out.normals[v][c]),
                                "Actual supplied skin data processes finitely");
                checked += f.end - f.first;
                if (emit)
                    dump(f, actor.model.path + "/final-cache-fixture-" + std::to_string(phase));
            }
        }
    require(checked == 2363,
            "All405/396 supplied vertices and partial ranges checked with3 final-cache fixtures");
    std::cout << checked
              << " supplied-resource vertex evaluations; final matrices are explicit test fixtures.\n";
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        const bool synthetic = argc == 2 && std::wstring(argv[1]) == L"--oracle-dump";
        unit(synthetic);
        if (argc > 1 && !synthetic)
            retail(argv[1], argc > 2 && std::wstring(argv[2]) == L"--oracle-dump");
        std::cout << "Original CPU skin position/normal/weight/range fixtures passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
