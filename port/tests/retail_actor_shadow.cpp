#include "retail_actor_shadow.h"
#include "filesystem.h"

#include <cstring>
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
std::shared_ptr<RetailActorSceneSource> fixture()
{
    auto source = std::make_shared<RetailActorSceneSource>();
    auto& model = source->model;
    model.path = "./projection-fixture.bdae";
    CityNode node{};
    node.name = "shadow-node";
    node.parent = -1;
    node.rotation = {0, 0, 0, 1};
    node.scale = {1, 1, 1};
    model.nodes.push_back(node);
    model.visualScenes.push_back({"visual", "visual", 0, 1});
    return source;
}
void cases(std::shared_ptr<const RetailActorSceneSource> source, const char* label, bool dump)
{
    RetailActorScene scene(std::move(source));
    auto* shadow = scene.root().findUid("shadow-node");
    require(shadow != nullptr, "Actual shadow container exists");
    const RetailSceneQuaternion previous{.1f, .2f, .3f, .4f};
    size_t count = 0;
    for (const auto normal : std::array<RetailHeightPoint, 12>{{
        {.6f, 0, .8f}, {0, 0, 1}, {0, .6f, .8f}, {0, 0, -1}, {.1f, .2f, -2},
        {0, 0, 0}, {-0.f, 0, -0.f}, {1, 2, 3}, {-1, -2, 3}, {-.5f, .25f, -.5f},
        {0, 0, -.5f}, {-0.f, -0.f, 1}}}) {
        for (bool changed : {false, true}) {
            shadow->setRotation(previous);
            shadow->setPosition({91, 92, 93});
            scene.root().updateAbsolutePosition(true);
            const auto cached = shadow->absolute();
            const auto flags = shadow->transformFlags();
            RetailHeightState live;
            live.normal = normal;
            live.changed = changed;
            live.hasHeightmap = !changed; // Deliberately independent original byte.
            updateRetailActorShadow(*shadow, {11, 22, 37}, 123.25f, live);
            require(shadow->position() == RetailHeightPoint{11, 22, 126.25f},
                    "Shadow uses actual worldXY and floor+3, not actorZ/rootHeight");
            require(std::memcmp(cached.data(), shadow->absolute().data(), sizeof cached) == 0,
                    "Projection preserves every cached absolute matrix bit until later scene update");
            require(shadow->transformFlags() == (flags | (changed ? 0x30u : 0x20u)),
                    "Only genuine position/conditional rotation setters mark original dirty flags");
            if (!changed)
                require(shadow->rotation() == previous, "Unchanged height retains previous quaternion");
            if (dump) {
                std::cout << "shadow " << label << ' ' << changed;
                for (auto value : normal) std::cout << ' ' << bits(value);
                for (auto value : shadow->position()) std::cout << ' ' << bits(value);
                for (auto value : shadow->rotation()) std::cout << ' ' << bits(value);
                std::cout << '\n';
            }
            ++count;
        }
    }
    std::cout << label << ": " << count << " actual-node shadow projection cases\n";
}
}
int main(int argc, char** argv)
{
    try {
        const bool dump = argc > 1 && std::string_view(argv[1]) == "--oracle-dump";
        cases(fixture(), "synthetic", dump);
        if (argc > 1 && !dump) {
            ResourceFileSystem files{std::filesystem::path(argv[1])};
            for (const char* name : {"char_pthug.bdae", "char_pthug_jail.bdae"})
                cases(loadRetailActorSceneSource(files, name), name,
                      argc > 2 && std::string_view(argv[2]) == "--oracle-dump");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
