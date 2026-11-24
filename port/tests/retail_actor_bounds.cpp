#include "retail_actor_scene.h"
#include "retail_actor_graph.h"
#include "filesystem.h"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Box = RetailSkinBoundingBox;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F fn) {
    try { fn(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected checked bounding-box rejection.");
}
template<class T> void words(const T& values) {
    std::cout << '[';
    bool first = true;
    for (float value : values) {
        uint32_t bits; std::memcpy(&bits, &value, 4);
        if (!first) std::cout << ',';
        first = false; std::cout << bits;
    }
    std::cout << ']';
}
void boxWords(const Box& box) {
    std::cout << '['; words(box[0]); std::cout << ','; words(box[1]); std::cout << ']';
}
void core(bool dump) {
    const Box box{{{-1, -2, -3}, {1, 2, 3}}};
    auto matrix = retailSceneIdentity;
    matrix[4] = -1;
    require(retailSceneTransformBoxEx(matrix, box) == Box{{{-3, -2, -3}, {3, 2, 3}}},
            "Ex sums independent min/max contributions; skin two-corner algorithm differs");
    std::vector<RetailSceneMatrix> matrices{retailSceneIdentity, matrix,
        {-2, 0, 0, 0, 0, 3, 0, 0, 0, 0, -4, 0, 10, 20, 30, 1},
        {.1f, .2f, -.3f, 0, -.4f, .5f, .6f, 0, .7f, -.8f, .9f, 0, 10000, -20000, 30000, 1},
        {1000, -1000, .000001f, 0, 1000, 1000, -.000002f, 0,
         -1000, 1000, .000003f, 0, -.125f, .375f, -.625f, 1}};
    matrix = retailSceneIdentity;
    matrix[0] = matrix[4] = matrix[8] = matrix[12] = -0.f;
    matrices.push_back(matrix);
    matrix = retailSceneIdentity;
    matrix[3] = matrix[7] = matrix[11] = matrix[15] = std::numeric_limits<float>::quiet_NaN();
    matrices.push_back(matrix);
    size_t index = 0;
    for (const auto& m : matrices)
        for (const auto& b : {box, Box{{{3, 2, 1}, {-3, -2, -1}}}, Box{{{-0.f, -0.f, -0.f}, {0.f, 0.f, 0.f}}}}) {
            const auto output = retailSceneTransformBoxEx(m, b);
            if (dump) {
                std::cout << "boxex {\"case\":" << index << ",\"matrix\":"; words(m);
                std::cout << ",\"input\":"; boxWords(b);
                std::cout << ",\"output\":"; boxWords(output); std::cout << "}\n";
            }
            ++index;
        }
    matrix[0] = std::numeric_limits<float>::infinity();
    rejects([&] { retailSceneTransformBoxEx(matrix, box); });
    auto bad = box; bad[0][2] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { retailSceneTransformBoxEx(retailSceneIdentity, bad); });
    RetailActorGraph global;
    rejects([&] { global.root().boundingBox(); });
}
void retail(const std::filesystem::path& path, bool dump) {
    ResourceFileSystem assets(path);
    for (const char* name : {"char_pthug.bdae", "char_pthug_jail.bdae"}) {
        auto source = loadRetailActorSceneSource(assets, name);
        RetailActorScene scene(source);
        auto* shadow = scene.root().findUid("shadow-node");
        require(shadow && !shadow->children().empty(), "Actual authored shadow graph");
        auto* mesh = shadow->children().front();
        require(mesh->mesh() && !mesh->mesh()->skin(), "Actual ordinary mesh node");
        require(scene.root().boundingBox() == Box{{{-1, -1, -1}, {1, 1, 1}}},
                "Default root cube is not child aggregation");
        const auto* localStorage = &mesh->boundingBox();
        const auto* transformedStorage = &mesh->transformedBoundingBox();
        require(localStorage != transformedStorage, "Ordinary transformed box owns distinct cache storage");
        const auto initial = *transformedStorage;
        for (unsigned phase = 0; phase < 9; ++phase) {
            if (phase == 1) scene.root().setPosition({100, 200, 300});
            if (phase == 2) scene.root().updateAbsolutePosition(true);
            if (phase == 3) {
                scene.root().setScale({-2, .75f, 1.5f});
                scene.root().setRotation({.2f, .3f, .4f, .5f});
                scene.root().updateAbsolutePosition(true);
            }
            if (phase == 4) scene.root().setPosition({-31, 11, 17});
            if (phase == 5) scene.root().onAnimate(5);
            if (phase == 6) scene.root().setVisible(false);
            if (phase == 7) {
                scene.root().setVisible(true);
                shadow->setPosition({100, 200, 300});
                shadow->updateAbsolutePosition(false);
            }
            if (phase == 8) shadow->updateAbsolutePosition(true);
            for (const auto& owned : scene.nodes()) {
                auto& node = *owned;
                if (node.type() == retailSceneSkinnedMeshType) continue;
                const auto before = node.transformFlags();
                const auto absolute = node.absolute();
                const auto local = node.boundingBox();
                const auto& transformed = node.transformedBoundingBox();
                require(node.absolute() == absolute && node.transformFlags() == (before & ~0x200u),
                        "Getter clears only dirty200, never refreshes absolute TRS");
                if (dump) {
                    std::cout << "actorbox {\"model\":" << std::quoted(name) << ",\"phase\":" << phase
                              << ",\"node\":" << node.index() << ",\"before\":" << before
                              << ",\"after\":" << node.transformFlags() << ",\"local\":";
                    boxWords(local); std::cout << ",\"transformed\":"; boxWords(transformed);
                    std::cout << ",\"absolute\":"; words(absolute); std::cout << "}\n";
                }
            }
            require(&mesh->boundingBox() == localStorage && &mesh->transformedBoundingBox() == transformedStorage,
                    "Original local and transformed reference identities remain stable");
            if (phase == 1) require(*transformedStorage == initial, "Setter alone retains clean stale world box");
        }
    }
}
}
int main(int argc, char** argv) {
    try {
        const bool dump = argc > 1 && std::string(argv[argc - 1]) == "--dump";
        core(dump);
        if (argc > 1 && std::string(argv[1]) != "--dump") retail(argv[1], dump);
        std::cout << "Original ordinary node bounding-box checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
