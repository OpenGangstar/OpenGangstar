#pragma once

#include "texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class IFileStream;
class FileSystemBase;

struct RetailSpriteImage { uint32_t width, height; };
struct RetailSpriteModule { uint8_t image; uint16_t x, y, width, height; };
struct RetailSpritePart { uint16_t index; int16_t x, y; uint8_t palette, flags; };
struct RetailSpriteBounds { int32_t left, right, top, bottom; };
struct RetailSpriteFrame {
    uint16_t first, count;
    std::string name;
    RetailSpriteBounds bounds{};
};
struct RetailSpriteAnimationFrame { uint16_t frame; std::array<uint8_t, 7> parameters; };
struct RetailSprite {
    std::vector<RetailSpriteImage> images;
    std::vector<RetailSpriteModule> modules;
    std::vector<RetailSpritePart> parts;
    std::vector<RetailSpriteFrame> frames, animations;
    std::vector<RetailSpriteAnimationFrame> animationFrames;
};

struct RetailSpriteDraw {
    uint16_t module;
    int32_t x, y;
    bool flipX, flipY;
};

RetailSprite readRetailSprite(IFileStream& source);
Image loadRetailSpriteAtlas(FileSystemBase& assets, const std::string& spritePath, const RetailSprite& sprite,
                           bool keepBitmapAlpha = false);
Image rasterRetailSpriteFrame(FileSystemBase& assets, const std::string& spritePath, const std::string& frameName,
                              uint32_t width, uint32_t height, int32_t x = 0, int32_t y = 0);
Image rasterRetailDialoguePortrait(FileSystemBase& assets, uint32_t animation);
struct RetailSpriteRasterStep {
    Image image;
    uint32_t duration;
};
std::vector<RetailSpriteRasterStep> rasterRetailSpriteAnimation(FileSystemBase& assets,
    const std::string& spritePath, const std::string& animationName, uint32_t width, uint32_t height);
void paintRetailSpriteDraws(const RetailSprite& sprite, const Image& atlas,
                           const std::vector<RetailSpriteDraw>& draws, Image& target);
std::vector<RetailSpriteDraw> composeRetailSpriteFrame(const RetailSprite& sprite, size_t frame,
                                                      int32_t x = 0, int32_t y = 0, uint8_t flags = 0);
std::vector<RetailSpriteDraw> composeRetailSpriteAnimationFrame(const RetailSprite& sprite, size_t animation,
                                                               size_t frame, int32_t x = 0, int32_t y = 0,
                                                               uint8_t flags = 0);
