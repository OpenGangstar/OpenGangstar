#include "retail_minimap.h"
#include "FileSystemBase.h"
#include "IFileStream.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace {
std::array<float, 2> normalizedMapRight(std::array<float, 2> right)
{
    const auto length = std::hypot(right[0], right[1]);
    if (length <= 0) throw std::runtime_error("Invalid retail minimap camera direction.");
    return {right[0] / length, right[1] / length};
}
}

RetailMinimapAssets loadRetailMinimap(FileSystemBase& assets, RetailMap area)
{
    const char spritePath[] = "huds.bsprite";
    const std::unique_ptr<IFileStream> sheets(assets.openResource(spritePath));
    const auto mapPath = area == RetailMap::city ? "minimap_map.bmp" : "prison_map.bmp";
    const std::unique_ptr<IFileStream> map(assets.openResource(mapPath));
    if (!sheets || !map) throw std::runtime_error("Missing retail minimap assets.");
    const auto sprite = readRetailSprite(*sheets);
    const auto atlas = loadRetailSpriteAtlas(assets, spritePath, sprite, true);
    RetailMinimapAssets result{readTexture(*map), {}};
    for (size_t i = 0; i < sprite.frames.size(); ++i) {
        const auto& frame = sprite.frames[i];
        if (frame.name.compare(0, 8, "MINIMAP_") != 0) continue;
        const auto [left, right, top, bottom] = frame.bounds;
        const auto width = int64_t(right) - left, height = int64_t(bottom) - top;
        if (width <= 0 || height <= 0 || width > 512 || height > 512 ||
            left < -16384 || left > 16384 || top < -16384 || top > 16384)
            throw std::runtime_error("Invalid minimap frame bounds for " + frame.name);
        Image image{static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                    std::vector<uint32_t>(static_cast<size_t>(width * height))};
        paintRetailSpriteDraws(sprite, atlas, composeRetailSpriteFrame(sprite, i, -left, -top), image);
        if (!result.frames.emplace(frame.name, RetailMinimapFrame{frame.bounds, std::move(image)}).second)
            throw std::runtime_error("Duplicate retail minimap frame " + frame.name);
    }
    for (const auto* name : {"MINIMAP_BG", "MINIMAP_PLAYER", "MINIMAP_NORTH", "MINIMAP_CHECKPOINT"})
        if (!result.frames.count(name)) throw std::runtime_error(std::string("Missing retail minimap frame ") + name);
    const auto sequence = std::find_if(sprite.animations.begin(), sprite.animations.end(),
        [](const RetailSpriteFrame& animation) { return animation.name == "ICON_CHECKPOINT"; });
    if (sequence == sprite.animations.end() || !sequence->count)
        throw std::runtime_error("Missing retail minimap checkpoint animation.");
    result.checkpointAnimationId = static_cast<int32_t>(sequence - sprite.animations.begin());
    for (size_t i = sequence->first; i < size_t(sequence->first) + sequence->count; ++i) {
        const auto& entry = sprite.animationFrames[i];
        const auto& frame = sprite.frames[entry.frame].name;
        if (!result.frames.count(frame) || !entry.parameters[0] || (entry.parameters.back() & 15))
            throw std::runtime_error("Unsupported retail minimap checkpoint animation frame.");
        result.checkpointAnimation.push_back({frame, entry.parameters[0]});
    }
    return result;
}

size_t retailMinimapAnimationStep(const std::vector<RetailMinimapStep>& animation, double time)
{
    size_t length = 0;
    for (const auto& step : animation) length += step.duration;
    if (!length || !std::isfinite(time)) throw std::runtime_error("Invalid retail minimap animation time.");
    auto phase = std::fmod(std::max(0., time), length);
    for (size_t i = 0; i < animation.size(); ++i) {
        if (phase < animation[i].duration) return i;
        phase -= animation[i].duration;
    }
    return 0;
}

float retailMinimapScale(float speedKmh)
{
    // MiniMap's constructor sets the thresholds and scale range used by update().
    constexpr float lowSpeed = 30, highSpeed = 100, lowScale = 1.5f, highScale = 2.5f;
    const auto fraction = (std::clamp(speedKmh, lowSpeed, highSpeed) - lowSpeed) / (highSpeed - lowSpeed);
    return lowScale + (highScale - lowScale) * fraction;
}

std::array<float, 2> retailMinimapUv(RetailMap area, float worldX, float worldY)
{
    if (area == RetailMap::city) {
        // Map::setMap uses these bounds for minimap_map.bmp. The origin on Y is inverted.
        return {(worldX + 80000.f) / 160000.f, (80000.f - worldY) / 160000.f};
    }
    // The prison map uses an eastward origin and the same inverted Y convention.
    return {(worldX - 184700.f) / 6000.f, (-worldY - 6000.f) / 6000.f};
}

std::array<float, 2> retailMinimapSample(std::array<float, 2> center, std::array<float, 2> right,
                                       std::array<float, 2> pixels, float zoom, uint32_t width, uint32_t height)
{
    if (!width || !height) throw std::runtime_error("Invalid retail minimap viewport.");
    const auto [x, y] = normalizedMapRight(right);
    return {center[0] + (x * pixels[0] + y * pixels[1]) * zoom / width,
            center[1] + (-y * pixels[0] + x * pixels[1]) * zoom / height};
}

std::array<float, 2> retailMinimapMarkerOffset(std::array<float, 2> center, std::array<float, 2> target,
                                               std::array<float, 2> right, std::array<float, 2> limits,
                                               float zoom, uint32_t width, uint32_t height)
{
    if (zoom <= 0 || limits[0] <= 0 || limits[1] <= 0 || !width || !height)
        throw std::runtime_error("Invalid retail minimap marker viewport.");
    const auto [x, y] = normalizedMapRight(right);
    const auto dx = (target[0] - center[0]) * width / zoom;
    const auto dy = (target[1] - center[1]) * height / zoom;
    const auto horizontal = x * dx - y * dy;
    const auto vertical = y * dx + x * dy;
    const auto shrink = std::max({1.f, std::abs(horizontal) / limits[0], std::abs(vertical) / limits[1]});
    return {horizontal / shrink, vertical / shrink};
}
