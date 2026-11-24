#include "retail_morph.h"
#include "filesystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Vec = std::array<float, 3>;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
uint32_t bits(float x) {
    uint32_t result;
    std::memcpy(&result, &x, 4);
    return result;
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Expected checked original morph rejection");
}
void stream(const std::vector<Vec> &data) {
    std::cout << '[';
    for (size_t i = 0; i < data.size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << '[' << bits(data[i][0]) << ',' << bits(data[i][1]) << ',' << bits(data[i][2]) << ']';
    }
    std::cout << ']';
}
void mesh(const std::vector<Vec> &positions, const std::vector<Vec> &normals) {
    std::cout << "{\"positions\":";
    stream(positions);
    std::cout << ",\"normals\":";
    stream(normals);
    std::cout << '}';
}
struct Fixture {
    CityMesh source;
    CityMorphController controller;
    std::vector<CityMesh> geometry;
    RetailSkinningOutput output, initial;
    size_t first = 0, end = 4;
    Fixture() {
        source.name = "source";
        source.positions.assign(4, {1, 2, 3});
        source.normals.assign(4, {0, 0, 1});
        controller = {"morph", "source", 0, {}};
        for (size_t i = 0; i < 3; ++i) {
            CityMesh target;
            target.name = "target" + std::to_string(i);
            target.positions.assign(4, {float(10 * (i + 1)), float(20 * (i + 1)), float(30 * (i + 1))});
            target.normals.assign(4, {1, 0, 0});
            geometry.push_back(target);
            controller.targets.push_back({i, 0.f});
        }
        output.positions.assign(4, {91, 92, 93});
        output.normals.assign(4, {81, 82, 83});
    }
    void run() {
        initial = output;
        retailMorphVertexRange(source, controller, geometry, first, end, output);
    }
    void dump(const std::string &name, bool emit) const {
        if (!emit)
            return;
        std::cout << "morph {\"name\":\"" << name << "\",\"method\":" << controller.method
                  << ",\"first\":" << first << ",\"end\":" << end << ",\"weights\":[";
        for (size_t i = 0; i < controller.targets.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << bits(controller.targets[i].weight);
        }
        std::cout << "],\"source\":";
        mesh(source.positions, source.normals);
        std::cout << ",\"targets\":[";
        for (size_t i = 0; i < controller.targets.size(); ++i) {
            if (i)
                std::cout << ',';
            const auto index = controller.targets[i].mesh;
            if (index >= geometry.size())
                std::cout << "null";
            else
                mesh(geometry[index].positions, geometry[index].normals);
        }
        std::cout << "],\"initial\":";
        mesh(initial.positions, initial.normals);
        std::cout << ",\"result\":";
        mesh(output.positions, output.normals);
        std::cout << "}\n";
    }
};
void unit(bool emit) {
    Fixture f;
    f.controller.targets.clear();
    f.run();
    require(f.output.positions[0] == Vec{1, 2, 3}, "No targets copies source");
    f.dump("no-targets", emit);
    f = Fixture{};
    f.controller.targets[0].mesh = 999;
    f.run();
    require(f.output.positions[0] == Vec{1, 2, 3}, "Zero-weight target reference unread");
    f.dump("zero-unused-reference", emit);
    f = Fixture{};
    f.controller.targets[0].weight = 1;
    f.controller.targets[0].mesh = 999;
    f.run();
    require(f.output.positions[0] == Vec{1, 2, 3}, "Original base-zero initializer keeps source arrays");
    f.dump("base-zero-first-target-is-source", emit);
    f = Fixture{};
    f.controller.targets[0].mesh = 999;
    f.controller.targets[1] = {999, .25f};
    f.controller.targets[2].weight = .75f;
    f.run();
    require(f.output.positions[0] == Vec{22.75f, 45.5f, 68.25f},
            "Zero skipped then first nonzero weight initializes source");
    f.dump("base-zero-skipped-first-nonzero", emit);
    f = Fixture{};
    f.controller.targets[0].weight = .25f;
    f.controller.targets[1].weight = .5f;
    f.run();
    require(f.output.positions[0] == Vec{12.75f, 25.5f, 38.25f},
            "Normalized authored weighted source and targets");
    require(f.output.normals[0] == Vec{.75f, 0, .25f}, "Original does not normalize normals");
    f.dump("normalized", emit);
    f = Fixture{};
    f.controller.method = 1;
    f.controller.targets[0].weight = .25f;
    f.run();
    require(f.output.positions[0] == Vec{3.5f, 7, 10.5f}, "Relative method retains source weight one");
    f.dump("relative", emit);
    f.controller.method = 123;
    f.run();
    require(f.output.positions[0] == Vec{3.5f, 7, 10.5f},
            "All original nonzero methods take relative branch");
    f.dump("nonzero-method", emit);
    f = Fixture{};
    f.controller.targets[0].weight = 2;
    f.controller.targets[1].weight = -3;
    f.run();
    require(f.output.positions[0] == Vec{-38, -76, -114},
            "Do not clamp or normalize authored target weights");
    f.dump("negative-large-weights", emit);
    f = Fixture{};
    for (auto &t : f.controller.targets)
        t.weight = .1f;
    for (auto &m : f.geometry)
        m.positions.assign(4, {0, 0, 0});
    f.source.positions.assign(4, {1, 1, 1});
    f.run();
    require(bits(f.output.positions[0][0]) == 0x3f333332u, "Base weight subtracts each target sequentially");
    f.dump("sequential-base-subtractions", emit);
    f = Fixture{};
    f.source.positions.assign(4, {1, 1, 1});
    f.geometry[0].positions = f.geometry[1].positions = std::vector<Vec>(4, {16777216, 16777216, 16777216});
    f.controller.targets[0].weight = 1;
    f.controller.targets[1].weight = -1;
    f.run();
    require(f.output.positions[0] == Vec{0, 0, 0},
            "Weighted contributions retain literal float cancellation order");
    f.dump("float-cancellation", emit);
    f = Fixture{};
    f.source.positions[0] = {-0.f, 0, -0.f};
    f.source.normals[0] = {-0.f, -0.f, 0};
    f.run();
    require(bits(f.output.positions[0][0]) == 0x80000000u && bits(f.output.normals[0][1]) == 0x80000000u,
            "Weight one raw copy preserves signed zero");
    f.dump("raw-copy-signed-zero", emit);
    f = Fixture{};
    f.first = 1;
    f.end = 3;
    f.controller.targets[0].weight = .5f;
    f.run();
    require(f.output.positions[0] == Vec{91, 92, 93} && f.output.positions[3] == Vec{91, 92, 93},
            "Outside source-buffer range untouched");
    f.dump("nonzero-partial-range", emit);
    f = Fixture{};
    f.first = f.end = 1;
    f.run();
    require(f.output.positions == f.initial.positions, "Empty range untouched");
    f.dump("empty-range", emit);
    f = Fixture{};
    f.output.normals.clear();
    f.source.floatNormals = false;
    f.controller.targets[0].weight = .5f;
    f.run();
    require(f.output.normals.empty(), "Absent output normal stream ignored");
    f.dump("no-output-normals", emit);
    f = Fixture{};
    f.source.normals.clear();
    f.run();
    require(f.output.normals[0] == Vec{81, 82, 83}, "Missing source/unused target normals unchanged");
    f.dump("no-source-normals-no-used-target", emit);
    f = Fixture{};
    f.geometry[0].normals.clear();
    f.controller.targets[0].weight = .5f;
    f.run();
    require(f.output.normals[0] == Vec{0, 0, .5f},
            "Missing target normals skip contribution, no normalization");
    f.dump("missing-target-normals", emit);
    Fixture bad;
    bad.source.normals.clear();
    bad.controller.targets[0].weight = .5f;
    rejects([&] { bad.run(); });
    require(bad.output.positions[0] == Vec{91, 92, 93},
            "Malformed normal combination preflight before position writes");
    bad = Fixture{};
    bad.controller.targets[0].weight = .5f;
    bad.controller.targets[0].mesh = 999;
    rejects([&] { bad.run(); });
    bad = Fixture{};
    bad.geometry[0].positions.resize(1);
    bad.controller.targets[0].weight = .5f;
    rejects([&] { bad.run(); });
    bad = Fixture{};
    bad.output.positions.resize(1);
    rejects([&] { bad.run(); });
    bad = Fixture{};
    bad.source.floatNormals = false;
    rejects([&] { bad.run(); });
    bad = Fixture{};
    bad.geometry[0].floatNormals = false;
    bad.controller.targets[0].weight = .5f;
    rejects([&] { bad.run(); });
    bad = Fixture{};
    bad.controller.targets[0].weight = std::numeric_limits<float>::infinity();
    rejects([&] { bad.run(); });
    bad = Fixture{};
    bad.source.name = "unrelated";
    rejects([&] { bad.run(); });
    bad = Fixture{};
    bad.first = 3;
    bad.end = 2;
    rejects([&] { bad.run(); });
    for (size_t iteration = 0; iteration < 32; ++iteration) {
        f = Fixture{};
        f.first = iteration % 2;
        f.end = 3;
        f.controller.method = static_cast<uint32_t>(iteration % 3);
        for (size_t j = 0; j < 3; ++j)
            f.controller.targets[j].weight = float(int((iteration * 7 + j * 11) % 19) - 9) * .125f;
        for (size_t v = 0; v < 4; ++v) {
            f.source.positions[v] = {float(iteration) * .03125f, float(v) * -13.125f, -.0625f};
            for (size_t j = 0; j < 3; ++j) {
                f.geometry[j].positions[v] = {float(j + v) * .33333334f, -123.125f, float(iteration) * 7.75f};
                f.geometry[j].normals[v] = {-.125f, float(j) * .25f, float(v) * .375f};
            }
        }
        if (iteration % 4 == 0)
            f.output.normals.clear();
        f.run();
        f.dump("finite-fixture-" + std::to_string(iteration), emit);
    }
}
void retail(const std::filesystem::path &root, bool emit) {
    ResourceFileSystem files(root);
    // Exhaustive supplied local morph-controller model set, independently
    // enumerated over all359BRES resources in the reference inventory.
    const char *paths[] = {"car_ambulance",     "car_bmw",           "car_cadillac", "car_cooper",
                           "car_cooper_green",  "car_cop",           "car_delivery", "car_ford150",
                           "car_ford150_green", "car_fordvan",       "car_hummer",   "car_hummer_red",
                           "car_limo",          "car_lincoln",       "car_plymouth", "car_plymouth_flames",
                           "car_porsche",       "car_porsche_black", "car_taxi",     "car_viper",
                           "car_viper_pink"};
    size_t controllers = 0, evaluations = 0;
    for (const char *path : paths) {
        const std::string resource = std::string(path) + ".bdae";
        const auto model = loadCityScene(files, resource);
        for (const auto &controller : model.morphControllers) {
            ++controllers;
            const auto source = std::find_if(model.meshes.begin(), model.meshes.end(),
                                             [&](const auto &m) { return m.name == controller.source; });
            require(source != model.meshes.end(), "Actual decoded local morph source geometry");
            require(controller.method == 0 && controller.targets.size() == 2,
                    "Supplied method/target metadata");
            require(source->buffers.size() == 1, "Supplied original source buffer count");
            const auto range = std::minmax_element(source->buffers.front().indices.begin(),
                                                   source->buffers.front().indices.end());
            require(*range.first == 0 && size_t(*range.second) + 1 == source->positions.size(),
                    "Supplied actual source-buffer range covers its current stream");
            for (size_t phase = 0; phase < 6; ++phase) {
                Fixture f;
                f.source = *source;
                f.controller = controller;
                f.geometry = model.meshes;
                f.end = source->positions.size();
                f.output.positions.assign(f.end, {91, 92, 93});
                f.output.normals.assign(f.end, {81, 82, 83});
                if (phase == 1) {
                    f.controller.targets[0].weight = .25f;
                    f.controller.targets[1].weight = .5f;
                }
                if (phase == 2) {
                    f.controller.targets[0].weight = 1;
                    f.controller.targets[1].weight = 0;
                }
                if (phase == 3) {
                    f.controller.targets[0].weight = .5f;
                    f.controller.targets[1].weight = .5f;
                }
                if (phase == 4) {
                    f.controller.targets[0].weight = -.25f;
                    f.controller.targets[1].weight = 1.5f;
                }
                if (phase == 5) {
                    f.first = 13;
                    f.end -= 7;
                    f.controller.targets[0].weight = .125f;
                    f.controller.targets[1].weight = .25f;
                }
                f.run();
                for (size_t v = f.first; v < f.end; ++v)
                    for (size_t c = 0; c < 3; ++c) {
                        require(std::isfinite(f.output.positions[v][c]) &&
                                    std::isfinite(f.output.normals[v][c]),
                                "All supplied morph output finite");
                        if (phase == 0 || phase == 2)
                            require(bits(f.output.positions[v][c]) == bits(source->positions[v][c]) &&
                                        bits(f.output.normals[v][c]) == bits(source->normals[v][c]),
                                    "Original authored-zero/base-zero copy behavior on actual resource");
                    }
                evaluations += f.end - f.first;
                f.dump(resource + "/" + controller.name + "/phase" + std::to_string(phase), emit);
            }
        }
    }
    require(controllers == 21, "Every supplied local morph controller enumerated");
    require(evaluations == 61464, "All supplied morph source vertices and selected subranges validated");
    std::cout << controllers << " supplied morph controllers; " << evaluations
              << " vertex evaluations using authored and explicit test weights.\n";
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        const bool dump = argc > 1 && std::wstring(argv[1]) == L"--oracle-dump";
        unit(dump);
        if (argc > 1 && !dump)
            retail(argv[1], argc > 2 && std::wstring(argv[2]) == L"--oracle-dump");
        std::cout << "Original morph stream/weight/range checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
