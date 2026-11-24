#pragma once

#include <array>
#include <cstdint>

// Native mutable scalar state, not the original pointer-containing SMaterial
// layout. Texture ownership/matrices and prepared source decoding are separate.
// Packed colors retain the original literal byte0/1/2/3 RGBA interpretation.
struct RetailMaterialState {
    std::uint32_t vertexColorControl = 1; // Original word0; public flag4 changes bit0.
    std::uint32_t flags = 0x387;          // Original flags40, a different namespace.
    std::uint32_t ambient = 0xffffffff, emissive = 0, specular = 0xffffffff;
    std::uint32_t diffuse = 0xffffffff;   // Original current color at material+84.
    std::uint32_t dirtyFlags = 0xfc;

    void setFlag(std::uint32_t flag, bool value);
    // Per-material leaves only. The owning scene performs the original
    // recognized-mesh material0 selection or child traversal, not this state.
    void setColor(std::uint32_t color, std::uint32_t selector);
    void setAlpha(std::uint32_t alpha, std::uint32_t selector);
    // Original setupArrays branch when the renderer requests color attribute4.
    // A caller that does not request it must preserve its actual current color.
    bool usesVertexColors(bool primaryStreamPresent) const;

private:
    void refreshColorMaterial();
};

std::array<float, 4> retailMaterialRgba(std::uint32_t originalColor);
