#include "retail_actor_floor.h"
#include "filesystem.h"

#include <cmath>
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
b2AABB bounds()
{
    b2AABB result;
    result.lowerBound.Set(-2000, -2000); result.upperBound.Set(2000, 2000);
    return result;
}
std::shared_ptr<RetailActorSceneSource> source(bool parent = false)
{
    auto result = std::make_shared<RetailActorSceneSource>();
    result->model.path = "./floor-fixture.bdae";
    if (parent) {
        CityNode node{};
        node.name = "parent"; node.parent = -1; node.translation = {10, 20, 30};
        node.rotation = {0, 0, 0, 1}; node.scale = {1, 1, 1};
        result->model.nodes.push_back(node);
        node.name = "actor"; node.parent = 0; node.translation = {1, 2, 3};
        result->model.nodes.push_back(node);
        result->model.visualScenes.push_back({"visual", "visual", 0, 2});
    }
    return result;
}
struct Geometry {
    CityMesh mesh;
    CityNavigation navigation;
    Geometry()
    {
        mesh.positions = {{0, 0, 31}, {1, 0, 31}, {0, 1, 31}};
        mesh.buffers.push_back({"", {0, 1, 2}});
        navigation.buffers.resize(1);
        navigation.buffers[0].roadFlags.resize(1);
        for (size_t i = 0; i < 8; ++i)
            navigation.buffers[0].neighbors[i].assign(1, i < 6 ? UINT16_MAX : 0);
        navigation.buffers[0].overlaps.resize(1);
        navigation.materialBuffers.fill(0);
        navigation.cliffVertexKeys.resize(6);
    }
};
void row(unsigned variant, const RetailActorFloorState& floor, const RetailActorSceneNode& actor,
         const RetailHeightController& height, bool dump)
{
    if (!dump) return;
    std::cout << "floor " << variant << ' ' << bits(floor.heightCm);
    for (float value : actor.position()) std::cout << ' ' << bits(value);
    std::cout << ' ' << height.state(0).changed;
    for (float value : height.state(0).normal) std::cout << ' ' << bits(value);
    for (size_t axis = 12; axis < 15; ++axis) std::cout << ' ' << bits(actor.absolute()[axis]);
    std::cout << ' ' << actor.transformFlags() << '\n';
}
void synthetic(bool dump)
{
    Geometry geometry;
    RetailHeightTerrain terrain(geometry.mesh, geometry.navigation);
    b2World world(bounds(), b2Vec2(0, 0), true);
    for (unsigned variant = 0; variant < 3; ++variant) {
        RetailHeightController height(terrain, world);
        if (variant == 1) (void)height.height({0, 0, 37});
        if (variant == 2) height.initializeSelected(0, {0, 0}, {.1f, .1f, 0});
        RetailActorScene scene(source());
        scene.root().setPosition({.25f, .25f, 200});
        scene.root().updateAbsolutePosition(false);
        RetailAnimationClock animation;
        animation.rootHeight = 9.5f;
        RetailActorFloorState floor;
        require(bits(floor.heightCm) == 0, "Original retained floor initializes positive zero");
        updateRetailActorFloor(floor, scene.root(), height, animation);
        const float expected = variant == 0 ? 200.f : variant == 1 ? 37.f : 31.f;
        const RetailHeightPoint position{.25f, .25f, expected + 9.5f};
        require(floor.heightCm == expected && scene.root().position() == position &&
                scene.root().absolute()[14] == position[2] && !height.state(0).changed,
                "Actual wrapper/cache floor then independent live root-height commit");
        row(variant, floor, scene.root(), height, dump);
        updateRetailActorFloor(floor, scene.root(), height, animation);
        require(scene.root().position() == position, "Repeated floor update does not accumulate root height");
    }
    {
        RetailHeightController height(terrain, world);
        RetailActorScene scene(source());
        scene.root().setPosition({.25f, .25f, 200}); scene.root().updateAbsolutePosition(false);
        scene.root().setPosition({99, 88, 777}); // Deliberately stale absolute cache.
        RetailAnimationClock animation; animation.rootHeight = 9.5f;
        RetailActorFloorState floor;
        updateRetailActorFloor(floor, scene.root(), height, animation);
        require(floor.heightCm == 200 && scene.root().position() == RetailHeightPoint{.25f, .25f, 209.5f},
                "Original GetPosition reads cached absolute values, without an early refresh");
        row(3, floor, scene.root(), height, dump);
    }
    {
        RetailHeightController height(terrain, world);
        RetailActorScene scene(source(true));
        auto& actor = *scene.root().findUid("actor");
        RetailAnimationClock animation; animation.rootHeight = 9.5f;
        RetailActorFloorState floor;
        updateRetailActorFloor(floor, actor, height, animation);
        require(floor.heightCm == 33 && actor.position() == RetailHeightPoint{11, 22, 42.5f} &&
                actor.absolute()[12] == 21 && actor.absolute()[13] == 42 && actor.absolute()[14] == 72.5f,
                "Original world snapshots become local position without an invented inverse-parent transform");
        row(4, floor, actor, height, dump);
    }
    require(world.GetBodyCount() == 1, "Floor commit creates no actor body or world steps");
}
void retail(const std::filesystem::path& directory)
{
    ResourceFileSystem files(directory);
    const auto city = loadCityAssets(files);
    auto opening = loadCityOpening(files, city);
    RetailHeightSceneBinding binding(std::move(opening.heightMap));
    const auto& global = binding.globalHeightmap();
    RetailHeightTerrain terrain(global, opening.navigation);
    b2World world(bounds(), b2Vec2(0, 0), true);
    RetailActorScene actor(loadRetailActorSceneSource(files, "char_pthug.bdae"));
    size_t checked = 0;
    for (size_t buffer = 0; buffer < global.mesh->buffers.size() && checked < 4; ++buffer) {
        const auto& indices = global.mesh->buffers[buffer].indices;
        for (size_t offset = 0; offset < indices.size() && checked < 4; offset += 3) {
            const auto& a = global.mesh->positions[indices[offset]];
            const auto& b = global.mesh->positions[indices[offset + 1]];
            const auto& c = global.mesh->positions[indices[offset + 2]];
            if ((b[0] - a[0]) * (c[1] - a[1]) == (b[1] - a[1]) * (c[0] - a[0])) continue;
            RetailHeightPoint point;
            for (size_t axis = 0; axis < 3; ++axis)
                point[axis] = ((a[axis] + b[axis]) + c[axis]) / 3.f + global.absoluteTranslationCm[axis];
            RetailHeightController height(terrain, world), reference(terrain, world);
            height.initializeFromScene(0, point); reference.initializeFromScene(0, point);
            const auto expected = reference.height(point, 0, false);
            actor.root().setPosition(point); actor.root().updateAbsolutePosition(false);
            const auto child = actor.root().children().front()->absolute();
            RetailAnimationClock animation; animation.rootHeight = 9.5f;
            RetailActorFloorState floor;
            updateRetailActorFloor(floor, actor.root(), height, animation);
            require(bits(floor.heightCm) == bits(expected) &&
                    bits(actor.root().position()[2]) == bits(expected + 9.5f) &&
                    std::memcmp(child.data(), actor.root().children().front()->absolute().data(), sizeof child) == 0,
                    "Actual decoded height and Player integrate without early descendant refresh");
            ++checked;
        }
    }
    require(checked == 4, "Actual decoded terrain floor integration cases");
    std::cout << checked << " decoded Player/terrain floor commits passed\n";
}
}
int main(int argc, char** argv)
{
    try {
        const bool dump = argc > 1 && std::string_view(argv[1]) == "--dump";
        synthetic(dump);
        if (argc > 1 && !dump) retail(argv[1]);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
