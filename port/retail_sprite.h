#pragma once

#include "texture.h"
#include "retail_text.h"

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

struct RetailHudGauge {
    struct Layer {
        Image image;
        int32_t x, y;
    };
    std::array<Layer, 3> layers;
};

enum RetailStatusGauge { lifeGauge, armorGauge, statusGaugeCount };
inline constexpr std::string_view retailCashCharacters = "0123456789$,.";
struct RetailStatusHud {
    std::array<RetailHudGauge, statusGaugeCount> gauges;
    std::array<RetailHudGauge::Layer, 4> spyLayers;
    std::array<Image, retailCashCharacters.size()> cashGlyphs;
};
RetailStatusHud loadRetailStatusHud(FileSystemBase& assets);
std::string formatRetailCash(int32_t amount, RetailLanguage language);
Image rasterRetailMissionCash(FileSystemBase& assets, int32_t amount, RetailLanguage language);
uint32_t retailGaugeFillLeft(uint32_t width, int32_t value, int32_t maximum);

struct RetailHealthBlink {
    double elapsedMs = 0;
    bool hideFill = false;
    void update(int32_t health, double deltaMs);
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
    const std::string& spritePath, const std::string& animationName, uint32_t width, uint32_t height,
    bool keepBitmapAlpha = false);
void paintRetailSpriteDraws(const RetailSprite& sprite, const Image& atlas,
                           const std::vector<RetailSpriteDraw>& draws, Image& target);
std::vector<RetailSpriteDraw> composeRetailSpriteFrame(const RetailSprite& sprite, size_t frame,
                                                      int32_t x = 0, int32_t y = 0, uint8_t flags = 0);
std::vector<RetailSpriteDraw> composeRetailSpriteAnimationFrame(const RetailSprite& sprite, size_t animation,
                                                               size_t frame, int32_t x = 0, int32_t y = 0,
                                                               uint8_t flags = 0);
