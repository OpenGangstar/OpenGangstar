#pragma once

#include "texture.h"

class FileSystemBase;

struct SpriteRegion {
    uint32_t sourceX;
    uint32_t sourceY;
    uint32_t targetX;
    uint32_t targetY;
    uint32_t width;
    uint32_t height;
    bool flipX = false;
    bool flipY = false;
};

struct StartupLogo {
    Image atlas;
    std::vector<SpriteRegion> regions;
};

StartupLogo readStartupLogo(FileSystemBase& assets, uint32_t width, uint32_t height);
