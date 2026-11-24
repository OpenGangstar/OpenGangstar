#include "retail_random48.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Vector {
    std::uint32_t seed;
    std::array<std::int32_t, 8> values;
};
// Fixed independent vectors from the unmodified historical Bionic C files,
// compiled with their original 32-bit long/16-bit seed-word widths.
const std::array<Vector, 10> vectors{
    {{0, {366850414, 1610402240, 206956554, 1869309841, 1239749840, 1687491058, 1486475625, 791919534}},
     {1, {89400484, 976015093, 1792756325, 721524505, 1214379247, 3794415, 402845420, 2126940991}},
     {0xffffffffu,
      {644300343, 97305740, 768640432, 869611528, 1265120434, 1223704054, 422622183, 1604381726}},
     {0x80000000u,
      {1440592238, 536660416, 1280698378, 795568017, 166008016, 613749234, 412733801, 1865661358}},
     {0x7fffffffu,
      {1718042167, 1171047564, 1842382256, 1943353352, 191378610, 149962230, 1496364007, 530639902}},
     {0x1234abcdu,
      {851401618, 1804928587, 758783491, 959030623, 684387517, 1903590565, 33463914, 1254324197}},
     {0x0000ffffu, {429768247, 184632460, 271254960, 1667086344, 730248370, 485277174, 917713895, 791768094}},
     {0xffff0000u,
      {581382510, 1523075520, 704342026, 1071835025, 1774621904, 278434290, 991383913, 1604533166}},
     {0x80000001u,
      {1163142308, 2049756917, 719014501, 1795266329, 140637423, 1077536239, 1476587244, 1053199167}},
     {0xdeadbeefu,
      {768226383, 1109990596, 1006921096, 103257856, 1986884682, 1097785774, 735295807, 49758870}}}};
void scalar() {
    static_assert(!std::is_copy_constructible_v<RetailRandom48> &&
                  !std::is_copy_assignable_v<RetailRandom48>);
    RetailRandom48 stream;
    require(stream.state() == 0x1234abcd330eULL, "Bionic process default is distinct from srand48(0)");
    for (auto value : vectors[5].values)
        require(stream.lrand48() == value, "Default Bionic lrand48 vector");
    for (const auto &row : vectors) {
        // Reseed the same already-consumed instance, retaining no stale low word.
        stream.srand48(row.seed);
        require(stream.state() == (std::uint64_t(row.seed) << 16 | 0x330eULL),
                "Exact seed bits replace upper32 and restore low16");
        for (auto expected : row.values) {
            const auto value = stream.lrand48();
            require(value == expected && value >= 0 && stream.state() <= 0xffffffffffffULL,
                    "Historical C boundary seed vector and full-width state");
        }
        stream.srand48(row.seed);
        require(stream.lrand48() == row.values[0],
                "Reseeding after consumption resets the entire original stream");
    }
}
struct Navigation {
    CityMesh mesh;
    CityNavigation nav;
    Navigation() {
        mesh.positions = {{0, 0, 0}, {100, 0, 10}, {0, 100, 20}, {100, 100, 30}};
        mesh.buffers.resize(8);
        nav.buffers.resize(8);
        nav.materialBuffers = {1, 0, 5, 2, 4, 3, 6, 7};
        nav.cliffVertexKeys.resize(8);
        for (std::size_t b = 0; b < 8; ++b) {
            mesh.buffers[b].indices = {0, 1, 2, 1, 3, 2};
            auto &data = nav.buffers[b];
            data.roadFlags.resize(2);
            data.overlaps.resize(2);
            for (std::size_t e = 0; e < 8; ++e)
                data.neighbors[e].assign(2, e < 6 ? UINT16_MAX : 0);
        }
        nav.buffers[0].neighbors[0][0] = 3;
        nav.buffers[0].neighbors[3][0] = 0;
        nav.buffers[0].neighbors[2][1] = 0;
        nav.buffers[0].neighbors[5][1] = 0;
    }
};
void sharedNavigation() {
    Navigation fixture;
    RetailHeightTerrain terrain(fixture.mesh, fixture.nav);
    b2AABB bounds;
    bounds.lowerBound.Set(-2000, -2000);
    bounds.upperBound.Set(2000, 2000);
    b2World world(bounds, b2Vec2(0, 0), true);
    RetailHeightController firstActor(terrain, world), secondActor(terrain, world);
    firstActor.initializeSelected(0, {0, 0}, {10, 10, 0});
    secondActor.initializeSelected(0, {0, 0}, {10, 10, 0});
    RetailRandom48 stream;
    stream.srand48(0);
    RetailRandomPosition first(terrain, stream), second(terrain, stream);
    require(first.next(firstActor, 0, 0, {10, 10, 0}) == RetailHeightPoint{50, 0, 5} &&
                stream.state() == 0x2bbb62dc5101ULL,
            "First actual native navigation consumer advances the shared source once");
    require(stream.lrand48() == 1610402240 && stream.state() == 0xbff993816378ULL,
            "An interleaved application consumer advances that same state");
    require(second.next(secondActor, 0, 0, {10, 10, 0}) == RetailHeightPoint{50, 0, 5} &&
                stream.state() == 0x18abd0152a23ULL,
            "Second actual actor attributes borrow the same concrete stream without reseeding");
    RetailHeightState fallback;
    fallback.triangle = {2, 0};
    const auto point = first.next(fallback, 0, {1000, 1000, 0});
    require(point == RetailHeightPoint{200.f * (1.f / 3.f), 200.f * (1.f / 3.f), 20} &&
                fallback.randomEdge.triangle.indexOffset == 3 && stream.state() == 0x93ca29a18d15ULL,
            "Actual destination fallback consumes the next two shared Bionic values in order");
}
void dump() {
    RetailRandom48 stream;
    std::cout << "initial default " << stream.state() << '\n';
    for (std::size_t n = 0; n < 1024; ++n)
        std::cout << "rng default " << n << ' ' << stream.lrand48() << ' ' << stream.state() << '\n';
    std::array<std::uint32_t, 74> seeds{};
    for (std::size_t n = 0; n < vectors.size(); ++n)
        seeds[n] = vectors[n].seed;
    std::uint32_t value = 0x4ecb29d1u;
    for (std::size_t n = vectors.size(); n < seeds.size(); ++n) {
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        seeds[n] = value;
    }
    for (std::size_t index = 0; index < seeds.size(); ++index) {
        stream.srand48(seeds[index]);
        std::cout << "initial " << seeds[index] << ' ' << stream.state() << '\n';
        for (std::size_t n = 0; n < 1024; ++n)
            std::cout << "rng " << seeds[index] << ' ' << n << ' ' << stream.lrand48() << ' '
                      << stream.state() << '\n';
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        scalar();
        sharedNavigation();
        if (argc == 2 && std::string(argv[1]) == "--oracle-dump")
            dump();
        std::cout << "Original shared Bionic random stream and native navigation composition passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
