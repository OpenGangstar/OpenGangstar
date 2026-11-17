#pragma once

#include "retail_sprite.h"

#include <map>

struct RetailMinimapFrame {
    RetailSpriteBounds bounds;
    Image image;
};

struct RetailMinimapStep {
    std::string frame;
    uint8_t duration;
};

struct RetailMinimapAssets {
    Image map;
    std::map<std::string, RetailMinimapFrame> frames;
    std::vector<RetailMinimapStep> checkpointAnimation;
    int32_t checkpointAnimationId = -1;
};

enum class RetailMap { city, prison };

RetailMinimapAssets loadRetailMinimap(FileSystemBase& assets, RetailMap area = RetailMap::city);
float retailMinimapScale(float speedKmh);
size_t retailMinimapAnimationStep(const std::vector<RetailMinimapStep>& animation, double time);
std::array<float, 2> retailMinimapUv(RetailMap area, float worldX, float worldY);
std::array<float, 2> retailMinimapSample(std::array<float, 2> center, std::array<float, 2> right,
                                       std::array<float, 2> pixels, float zoom, uint32_t width, uint32_t height);
std::array<float, 2> retailMinimapMarkerOffset(std::array<float, 2> center, std::array<float, 2> target,
                                               std::array<float, 2> right, std::array<float, 2> limits,
                                               float zoom, uint32_t width, uint32_t height);
