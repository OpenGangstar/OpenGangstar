#include "retail_material.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void write(const RetailMaterialState& state)
{
    std::cout << state.vertexColorControl << ' ' << state.flags << ' ' << state.ambient << ' '
              << state.emissive << ' ' << state.specular << ' ' << state.diffuse << ' ' << state.dirtyFlags;
}

void fixtures()
{
    RetailMaterialState state;
    require(state.vertexColorControl == 1 && state.flags == 0x387 && state.dirtyFlags == 0xfc &&
            state.ambient == 0xffffffff && state.diffuse == 0xffffffff &&
            state.specular == 0xffffffff && state.emissive == 0, "Original material constructor scalars");
    state.dirtyFlags = 0;
    state.setColor(0x87654321, 0);
    require(state.diffuse == 0x87654321 && state.dirtyFlags == 8, "Current color has its own dirty bit");
    state.setFlag(4, false);
    require(state.vertexColorControl == 0 && !(state.flags & 4) && state.dirtyFlags == 9,
            "Public flag4 changes word0 and derived tracking; it is not stored flag4");
    require(!state.usesVertexColors(true), "Requested lighting with public vertex colors disabled selects constant");
    state.setFlag(2, false);
    require(state.usesVertexColors(true) && !state.usesVertexColors(false),
            "Lighting-disabled selector still requires an actual stream");
    const auto rgba = retailMaterialRgba(0x87654321);
    require(rgba == std::array<float, 4>{33.f / 255, 67.f / 255, 101.f / 255, 135.f / 255},
            "Literal GL RGBA bytes, without a red/blue packed-color swap");
    state.setAlpha(0x123, 1);
    require(state.diffuse == 0x87654321, "SetAlpha selectors1/2/3 have no mutation");
    state.setAlpha(0x123, 0);
    require(state.diffuse == 0x23654321, "Original alpha uses its low byte and preserves RGB");
}

void corpus(bool dump)
{
    const std::uint32_t flags[]{0, 1, 2, 4, 6, 16, 32, 512, 1024, 0x80000000u, 0xffffffffu};
    const std::uint32_t dirty[]{0, 0x81, 0xffffffff};
    const std::uint32_t selectors[]{0, 1, 2, 3, 4, 0xffffffff};
    size_t count = 0;
    const auto run = [&](RetailMaterialState state, unsigned operation, std::uint32_t argument,
                         std::uint32_t value) {
        const auto before = state;
        if (operation == 0) state.setFlag(argument, value != 0);
        else if (operation == 1) state.setColor(value, argument);
        else state.setAlpha(value, argument);
        if (dump) {
            std::cout << "material "; write(before);
            std::cout << ' ' << operation << ' ' << argument << ' ' << value << ' '; write(state);
            std::cout << ' ' << state.usesVertexColors(false) << ' ' << state.usesVertexColors(true) << '\n';
        }
        ++count;
    };
    for (unsigned profile = 0; profile < 32; ++profile) {
        RetailMaterialState state;
        state.vertexColorControl = 0xa0 | (profile & 1);
        state.flags = 0x380 | ((profile & 2) ? 2 : 0) | ((profile & 4) ? 4 : 0);
        state.ambient = 0x87654321;
        state.diffuse = (profile & 8) ? state.ambient : 0x12345678;
        state.emissive = (profile & 16) ? 0xffffffff : 0;
        for (const auto bits : dirty) {
            state.dirtyFlags = bits;
            for (const auto flag : flags)
                for (unsigned value = 0; value < 2; ++value) run(state, 0, flag, value);
            for (const auto selector : selectors) {
                for (const auto color : {0u, 0xffffffffu, 0x87654321u, 0x12345678u})
                    run(state, 1, selector, color);
                for (const auto alpha : {0u, 0x87u, 0x123u, 0xffffffffu})
                    run(state, 2, selector, alpha);
            }
        }
    }
    require(count == 6720, "Every original branch corpus entry emitted");
    std::cout << count << " material flag/color/alpha mutation cases\n";
}
}

int main(int argc, char** argv)
{
    try {
        fixtures();
        corpus(argc > 1 && std::string_view(argv[1]) == "--oracle-dump");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
