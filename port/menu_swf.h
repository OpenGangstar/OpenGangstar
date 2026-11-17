#pragma once

#include "texture.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class IFileStream;

struct SwfBitmap {
    std::string texture;
    int32_t x;
    int32_t y;
    int32_t right;
    int32_t bottom;
    int32_t textureX;
    int32_t textureY;
};

struct SwfSplash {
    uint32_t width;
    uint32_t height;
    SwfBitmap show;
    SwfBitmap background;
    SwfBitmap outgoing;
    double frameMs;
    std::vector<uint16_t> opacities;
    std::map<uint16_t, std::string> exports;
};

SwfSplash readSwfSplash(IFileStream& file);

struct SplashFrames {
    Image show;
    Image background;
    Image outgoing;
    double frameMs;
    std::vector<uint16_t> opacities;
    std::map<uint16_t, std::string> exports;
};
