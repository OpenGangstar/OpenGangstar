#include "retail_material.h"

void RetailMaterialState::refreshColorMaterial()
{
    const bool enabled = !(flags & 2) || (vertexColorControl & 1) || diffuse == ambient;
    if (enabled != bool(flags & 4)) dirtyFlags |= 1;
    if (enabled) flags |= 4;
    else flags &= ~std::uint32_t(4);
}

void RetailMaterialState::setFlag(std::uint32_t flag, bool value)
{
    // The original tests bit2 of the supplied flag, not equality with4. A
    // combined mask containing it takes this same special branch exclusively.
    if (flag & 4) {
        if (bool(vertexColorControl & 1) == value) return;
        if (value) vertexColorControl |= 1;
        else vertexColorControl &= ~std::uint32_t(1);
        refreshColorMaterial();
        return;
    }
    if (bool(flags & flag) == value) return;
    dirtyFlags |= (flag & 0x1f) ? 1 : 4;
    if (value) flags |= flag;
    else flags &= ~flag;
    if (flag & 2) refreshColorMaterial();
}

void RetailMaterialState::setColor(std::uint32_t color, std::uint32_t selector)
{
    std::uint32_t* target = nullptr;
    switch (selector) {
    case 0: target = &diffuse; break;
    case 1: target = &ambient; break;
    case 2: target = &specular; break;
    case 3: target = &emissive; break;
    default: return; // Original recognized mesh returns without a mutation.
    }
    if (*target == color) return;
    *target = color;
    dirtyFlags |= selector == 0 ? 8 : 4;
    if (selector < 2) refreshColorMaterial();
}

void RetailMaterialState::setAlpha(std::uint32_t alpha, std::uint32_t selector)
{
    if (selector == 0) setColor((diffuse & 0x00ffffff) | ((alpha & 0xff) << 24), 0);
}

bool RetailMaterialState::usesVertexColors(bool primaryStreamPresent) const
{
    return primaryStreamPresent && (!(flags & 2) || (vertexColorControl & 1));
}

std::array<float, 4> retailMaterialRgba(std::uint32_t color)
{
    return {float(color & 255) / 255, float((color >> 8) & 255) / 255,
            float((color >> 16) & 255) / 255, float(color >> 24) / 255};
}
