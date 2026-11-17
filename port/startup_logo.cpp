#include "startup_logo.h"
#include "retail_sprite.h"

#include "FileSystemBase.h"
#include "IFileStream.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

StartupLogo readStartupLogo(FileSystemBase& assets, uint32_t width, uint32_t height)
{
    const char path[] = "gameloft_logo_01.bsprite";
    const std::unique_ptr<IFileStream> spriteFile(assets.openResource(path));
    if (!spriteFile) throw std::runtime_error("Could not open the Gameloft startup sprite.");

    const auto sprite = readRetailSprite(*spriteFile);
    StartupLogo logo{loadRetailSpriteAtlas(assets, path, sprite), {}};
    // ASprite uses absolute table offsets and paints frame zero for the logo.
    if (sprite.frames.empty() || !sprite.frames.front().count)
        throw std::runtime_error("The startup sprite frame has invalid parts.");
    for (const auto& draw : composeRetailSpriteFrame(sprite, 0, int(width / 3), 165)) {
        const auto [module, x, y, flipX, flipY] = draw;
        const auto [image, sourceX, sourceY, partWidth, partHeight] = sprite.modules[module];
        (void)image;
        const int left = std::max(0, x), top = std::max(0, y);
        const int right = std::min(int(width), x + int(partWidth));
        const int bottom = std::min(int(height), y + int(partHeight));
        if (left < right && top < bottom)
            logo.regions.push_back({sourceX + uint32_t(flipX ? x + partWidth - right : left - x),
                                    sourceY + uint32_t(flipY ? y + partHeight - bottom : top - y),
                                    uint32_t(left), uint32_t(top), uint32_t(right - left), uint32_t(bottom - top),
                                    flipX, flipY});
    }
    return logo;
}
