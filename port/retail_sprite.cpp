#include "retail_sprite.h"
#include "FileSystemBase.h"
#include "IFileStream.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
class SpriteReader {
public:
    explicit SpriteReader(IFileStream& source)
    {
        if (source.size() < 56 || source.size() > 1024 * 1024) fail();
        bytes.resize(static_cast<size_t>(source.size()));
        if (source.read(bytes.data(), bytes.size()) != bytes.size()) fail();
    }

    uint32_t word(size_t offset, unsigned count = 4) const
    {
        checkSpan(offset, count);
        uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i) value |= uint32_t(bytes[offset + i]) << (8 * i);
        return value;
    }

    uint16_t half(size_t offset) const { return static_cast<uint16_t>(word(offset, 2)); }
    uint8_t byte(size_t offset) const { return static_cast<uint8_t>(word(offset, 1)); }
    int32_t signedWord(size_t offset) const
    {
        const auto value = word(offset);
        return static_cast<int32_t>(value < 0x80000000u ? value : int64_t(value) - 0x100000000LL);
    }
    int16_t coordinate(size_t offset) const
    {
        const auto value = half(offset);
        return static_cast<int16_t>(value < 0x8000 ? value : int(value) - 0x10000);
    }

    void checkSpan(size_t offset, uint64_t count) const
    {
        if (offset > bytes.size() || count > bytes.size() - offset) fail();
    }

    std::string name(size_t offset, size_t lowerBound) const
    {
        checkSpan(offset, 1);
        if (offset < lowerBound) fail();
        const auto begin = bytes.begin() + offset;
        const auto end = std::find(begin, bytes.end(), 0);
        if (end == bytes.end()) fail();
        return {begin, end};
    }

    [[noreturn]] static void fail() { throw std::runtime_error("Invalid or unsupported retail sprite table."); }

private:
    std::vector<uint8_t> bytes;
};
}

RetailSprite readRetailSprite(IFileStream& source)
{
    const SpriteReader reader(source);
    constexpr std::array<uint32_t, 7> stride{1052, 10, 16, 28, 8, 12, 9};
    std::array<uint32_t, 7> counts{}, offsets{};
    uint64_t end = 56;
    for (size_t i = 0; i < stride.size(); ++i) {
        counts[i] = reader.word(i * 4);
        offsets[i] = reader.word(28 + i * 4);
        if (offsets[i] != end) SpriteReader::fail();
        const auto size = uint64_t(counts[i]) * stride[i];
        reader.checkSpan(offsets[i], size);
        end += size;
    }
    RetailSprite sprite;
    for (uint32_t i = 0; i < counts[0]; ++i) {
        const auto row = offsets[0] + stride[0] * i;
        sprite.images.push_back({reader.word(row), reader.word(row + 4)});
    }
    for (uint32_t i = 0; i < counts[1]; ++i) {
        const auto row = offsets[1] + stride[1] * i;
        const auto image = reader.byte(row + 1);
        if (reader.byte(row) != 0 || image >= sprite.images.size()) SpriteReader::fail();
        const RetailSpriteModule module{image, reader.half(row + 2), reader.half(row + 4),
                                        reader.half(row + 6), reader.half(row + 8)};
        if (uint32_t(module.x) + module.width > sprite.images[image].width ||
            uint32_t(module.y) + module.height > sprite.images[image].height) SpriteReader::fail();
        sprite.modules.push_back(module);
    }
    for (uint32_t i = 0; i < counts[4]; ++i) {
        const auto row = offsets[4] + stride[4] * i;
        const auto flags = reader.byte(row + 7);
        const auto index = reader.half(row);
        if (index >= ((flags & 8) ? counts[3] : counts[1])) SpriteReader::fail();
        sprite.parts.push_back({index, reader.coordinate(row + 2), reader.coordinate(row + 4),
                                reader.byte(row + 6), flags});
    }
    for (uint32_t i = 0; i < counts[3]; ++i) {
        const auto row = offsets[3] + stride[3] * i;
        const auto count = reader.half(row);
        const auto first = reader.half(row + 20);
        if (uint32_t(first) + count > counts[4]) SpriteReader::fail();
        sprite.frames.push_back({first, count, reader.name(reader.word(row + 24), static_cast<size_t>(end)),
                                 {reader.signedWord(row + 4), reader.signedWord(row + 8),
                                  reader.signedWord(row + 12), reader.signedWord(row + 16)}});
    }
    for (uint32_t i = 0; i < counts[5]; ++i) {
        const auto row = offsets[5] + stride[5] * i;
        const auto count = reader.half(row);
        const auto first = reader.half(row + 2);
        if (uint32_t(first) + count > counts[6]) SpriteReader::fail();
        sprite.animations.push_back({first, count, reader.name(reader.word(row + 8), static_cast<size_t>(end))});
    }
    for (uint32_t i = 0; i < counts[6]; ++i) {
        const auto row = offsets[6] + stride[6] * i;
        RetailSpriteAnimationFrame entry{reader.half(row), {}};
        if (entry.frame >= counts[3]) SpriteReader::fail();
        for (size_t j = 0; j < entry.parameters.size(); ++j) entry.parameters[j] = reader.byte(row + 2 + j);
        sprite.animationFrames.push_back(entry);
    }
    return sprite;
}

Image loadRetailSpriteAtlas(FileSystemBase& assets, const std::string& spritePath, const RetailSprite& sprite,
                           bool keepBitmapAlpha)
{
    if (sprite.images.size() != 1) throw std::runtime_error("Expected a single retail sprite atlas.");
    const auto suffix = spritePath.rfind('.');
    const auto stem = spritePath.substr(0, suffix);
    // ASprite tries the same resource name with these texture extensions in order.
    for (const auto* extension : {".bmp", ".png", ".tga", ".psd"}) {
        const auto path = stem + extension;
        const std::unique_ptr<IFileStream> file(assets.openResource(path.c_str()));
        if (!file) continue;
        auto atlas = readTexture(*file, keepBitmapAlpha);
        for (const auto& module : sprite.modules) {
            if (uint32_t(module.x) + module.width > atlas.width ||
                uint32_t(module.y) + module.height > atlas.height)
                throw std::runtime_error("Sprite module lies outside " + path);
        }
        return atlas;
    }
    throw std::runtime_error("Missing retail sprite atlas for " + spritePath);
}

RetailStatusHud loadRetailStatusHud(FileSystemBase& assets)
{
    constexpr char path[] = "huds.bsprite";
    const std::unique_ptr<IFileStream> file(assets.openResource(path));
    if (!file) throw std::runtime_error("Missing retail HUD sprite.");
    const auto sprite = readRetailSprite(*file);
    const auto atlas = loadRetailSpriteAtlas(assets, path, sprite, true);
    RetailStatusHud hud;
    constexpr std::array<const char*, 6> names{
        "BAR_LIFE_BG", "BAR_LIFE", "BAR_LIFE_FT", "BAR_ARMOR_BG", "BAR_ARMOR", "BAR_ARMOR_FT"};
    const auto loadGaugeLayer = [&](std::string_view name, RetailHudGauge::Layer& layer) {
        const auto frame = std::find_if(sprite.frames.begin(), sprite.frames.end(),
            [&](const RetailSpriteFrame& entry) { return entry.name == name; });
        if (frame == sprite.frames.end()) throw std::runtime_error("Missing retail HUD gauge frame.");
        auto draws = composeRetailSpriteFrame(sprite, frame - sprite.frames.begin());
        if (draws.size() != 1) throw std::runtime_error("Unexpected retail HUD gauge frame layout.");
        auto& draw = draws.front();
        const auto& module = sprite.modules.at(draw.module);
        layer.x = draw.x;
        layer.y = draw.y;
        layer.image = {module.width, module.height, std::vector<uint32_t>(size_t(module.width) * module.height)};
        draw.x = draw.y = 0;
        paintRetailSpriteDraws(sprite, atlas, draws, layer.image);
    };
    for (size_t i = 0; i < names.size(); ++i)
        loadGaugeLayer(names[i], hud.gauges[i / 3].layers[i % 3]);
    constexpr std::array spyNames{"SPY_GAUGE_BG", "SPY_GAUGE", "SPY_GAUGE_ICON", "SPY_GAUGE_INDICATOR"};
    for (size_t i = 0; i < spyNames.size(); ++i) loadGaugeLayer(spyNames[i], hud.spyLayers[i]);
    constexpr std::array starNames{"STAR_01", "STAR_02", "STAR_03", "STAR_04", "STAR_05"};
    for (size_t i = 0; i < starNames.size(); ++i) loadGaugeLayer(starNames[i], hud.wantedStars[i]);
    // The default CHudManager cash widget uses LITTLEY digits and separators.
    constexpr std::array<size_t, retailCashCharacters.size()> cashFrames{
        96, 87, 88, 89, 90, 91, 92, 93, 94, 95, 86, 97, 172};
    for (size_t i = 0; i < cashFrames.size(); ++i) {
        const auto frameIndex = cashFrames[i];
        const auto& frame = sprite.frames.at(frameIndex);
        const auto& bounds = frame.bounds;
        const auto width = int64_t(bounds.right) - bounds.left;
        const auto height = int64_t(bounds.bottom) - bounds.top;
        if (bounds.left != 0 || bounds.top != 0 || width <= 0 || width > 64 || height <= 0 || height > 64)
            throw std::runtime_error("Invalid retail cash glyph bounds.");
        auto& glyph = hud.cashGlyphs[i];
        glyph = {static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                 std::vector<uint32_t>(static_cast<size_t>(width * height))};
        paintRetailSpriteDraws(sprite, atlas, composeRetailSpriteFrame(sprite, frameIndex), glyph);
    }
    return hud;
}

std::string formatRetailCash(int32_t amount, RetailLanguage language)
{
    auto digits = std::to_string(std::max(0, amount));
    char separator = '.';
    if (language == RetailLanguage::english) separator = ',';
    if (language == RetailLanguage::french || language == RetailLanguage::spanish ||
        language == RetailLanguage::italian)
        separator = ' ';
    if (separator != ' ' || language == RetailLanguage::italian || amount > 9999) {
        for (int offset = static_cast<int>(digits.size()) - 3; offset > 0; offset -= 3)
            digits.insert(static_cast<size_t>(offset), 1, separator);
    }
    if (language == RetailLanguage::english || language == RetailLanguage::spanish) return '$' + digits;
    if (language == RetailLanguage::italian) return digits + '$';
    return digits + " $";
}

Image rasterRetailMissionCash(FileSystemBase& assets, int32_t amount, RetailLanguage language)
{
    constexpr char path[] = "huds.bsprite";
    const std::unique_ptr<IFileStream> file(assets.openResource(path));
    if (!file) throw std::runtime_error("Missing retail HUD sprite.");
    const auto sprite = readRetailSprite(*file);
    const auto atlas = loadRetailSpriteAtlas(assets, path, sprite, true);
    // The retail Reward widget uses BIGB cash frames and a fixed left anchor.
    constexpr std::array<size_t, retailCashCharacters.size()> frames{
        84, 75, 76, 77, 78, 79, 80, 81, 82, 83, 74, 85, 85};
    Image result{480, 320, std::vector<uint32_t>(480 * 320)};
    int32_t x = 8;
    for (const char character : formatRetailCash(amount, language)) {
        if (character == ' ') {
            x += 8;
            continue;
        }
        const auto frame = frames.at(retailCashCharacters.find(character));
        paintRetailSpriteDraws(sprite, atlas, composeRetailSpriteFrame(sprite, frame, x, 152), result);
        const auto& bounds = sprite.frames.at(frame).bounds;
        x += bounds.right - bounds.left;
    }
    return result;
}

uint32_t retailGaugeFillLeft(uint32_t width, int32_t value, int32_t maximum)
{
    return width - static_cast<uint32_t>(uint64_t(width) * std::clamp(value, 0, maximum) / maximum);
}

void RetailHealthBlink::update(int32_t health, double deltaMs)
{
    if (health >= 150) {
        hideFill = false;
        return;
    }
    elapsedMs += deltaMs;
    if (elapsedMs >= 1000.) {
        elapsedMs = 0;
        hideFill = false;
    } else if (elapsedMs >= 500.) {
        hideFill = true;
    }
}

Image rasterRetailSpriteFrame(FileSystemBase& assets, const std::string& spritePath, const std::string& frameName,
                              uint32_t width, uint32_t height, int32_t x, int32_t y)
{
    const std::unique_ptr<IFileStream> file(assets.openResource(spritePath.c_str()));
    if (!file) throw std::runtime_error("Missing retail sprite " + spritePath);
    const auto sprite = readRetailSprite(*file);
    const auto frame = std::find_if(sprite.frames.begin(), sprite.frames.end(),
        [&](const RetailSpriteFrame& entry) { return entry.name == frameName; });
    if (frame == sprite.frames.end()) throw std::runtime_error("Missing retail sprite frame " + frameName);
    if (!width || !height || uint64_t(width) * height > 4096ULL * 4096)
        throw std::runtime_error("Unsupported sprite render size.");
    Image pixels{width, height, std::vector<uint32_t>(size_t(width) * height)};
    const auto atlas = loadRetailSpriteAtlas(assets, spritePath, sprite);
    paintRetailSpriteDraws(sprite, atlas, composeRetailSpriteFrame(sprite, frame - sprite.frames.begin(), x, y), pixels);
    return pixels;
}

Image rasterRetailDialoguePortrait(FileSystemBase& assets, uint32_t animation)
{
    const std::string path = "cinematics_char.bsprite";
    const std::unique_ptr<IFileStream> file(assets.openResource(path.c_str()));
    if (!file) throw std::runtime_error("Missing retail dialogue portrait sprite.");
    const auto sprite = readRetailSprite(*file);
    if (animation >= sprite.animations.size() || sprite.animations[animation].count != 1)
        throw std::runtime_error("Unsupported retail dialogue portrait animation.");
    const auto atlas = loadRetailSpriteAtlas(assets, path, sprite);
    Image image{480, 320, std::vector<uint32_t>(480 * 320)};
    paintRetailSpriteDraws(sprite, atlas, composeRetailSpriteAnimationFrame(sprite, animation, 0), image);
    return image;
}

std::vector<RetailSpriteRasterStep> rasterRetailSpriteAnimation(FileSystemBase& assets,
    const std::string& spritePath, const std::string& animationName, uint32_t width, uint32_t height,
    bool keepBitmapAlpha)
{
    const std::unique_ptr<IFileStream> file(assets.openResource(spritePath.c_str()));
    if (!file) throw std::runtime_error("Missing retail sprite " + spritePath);
    const auto sprite = readRetailSprite(*file);
    const auto animation = std::find_if(sprite.animations.begin(), sprite.animations.end(),
        [&](const RetailSpriteFrame& sequence) { return sequence.name == animationName; });
    if (animation == sprite.animations.end() || !animation->count)
        throw std::runtime_error("Missing retail sprite animation " + animationName);
    if (!width || !height || uint64_t(width) * height > 4096ULL * 4096 / animation->count)
        throw std::runtime_error("Unsupported sprite animation render size.");
    const auto atlas = loadRetailSpriteAtlas(assets, spritePath, sprite, keepBitmapAlpha);
    std::vector<RetailSpriteRasterStep> steps;
    for (size_t i = 0; i < animation->count; ++i) {
        const auto& parameters = sprite.animationFrames[animation->first + i].parameters;
        const auto duration = uint32_t(parameters[0]) | (uint32_t(parameters[1]) << 8);
        Image frame{width, height, std::vector<uint32_t>(size_t(width) * height)};
        auto draws = composeRetailSpriteAnimationFrame(sprite, animation - sprite.animations.begin(), i);
        paintRetailSpriteDraws(sprite, atlas, draws, frame);
        steps.push_back({std::move(frame), duration});
    }
    return steps;
}

void paintRetailSpriteDraws(const RetailSprite& sprite, const Image& atlas,
                           const std::vector<RetailSpriteDraw>& draws, Image& target)
{
    if (uint64_t(atlas.width) * atlas.height != atlas.bgra.size() ||
        uint64_t(target.width) * target.height != target.bgra.size())
        throw std::runtime_error("Invalid sprite image buffer.");
    for (const auto& draw : draws) {
        const auto& m = sprite.modules.at(draw.module);
        if (m.image || uint32_t(m.x) + m.width > atlas.width || uint32_t(m.y) + m.height > atlas.height)
            throw std::runtime_error("Invalid sprite atlas module.");
        const auto x0 = std::max<int64_t>(0, draw.x);
        const auto y0 = std::max<int64_t>(0, draw.y);
        const auto x1 = std::min<int64_t>(target.width, int64_t(draw.x) + m.width);
        const auto y1 = std::min<int64_t>(target.height, int64_t(draw.y) + m.height);
        for (auto y = y0; y < y1; ++y) {
            const auto row = y - draw.y;
            const auto sy = m.y + (draw.flipY ? m.height - 1 - row : row);
            for (auto x = x0; x < x1; ++x) {
                const auto column = x - draw.x;
                const auto sx = m.x + (draw.flipX ? m.width - 1 - column : column);
                const auto fore = atlas.bgra[static_cast<size_t>(sy) * atlas.width + static_cast<size_t>(sx)];
                const auto alpha = fore >> 24;
                if (!alpha) continue;
                auto& back = target.bgra[static_cast<size_t>(y) * target.width + static_cast<size_t>(x)];
                if (alpha == 255) {
                    back = fore;
                    continue;
                }
                const auto backgroundWeight = (back >> 24) * (255 - alpha);
                const auto foregroundWeight = alpha * 255;
                const auto totalWeight = foregroundWeight + backgroundWeight;
                uint32_t blend = ((totalWeight + 127) / 255) << 24;
                for (unsigned shift = 0; shift < 24; shift += 8) {
                    const auto channels = ((fore >> shift) & 255) * foregroundWeight +
                                          ((back >> shift) & 255) * backgroundWeight;
                    blend |= ((channels + totalWeight / 2) / totalWeight) << shift;
                }
                back = blend;
            }
        }
    }
}

std::vector<RetailSpriteDraw> composeRetailSpriteFrame(const RetailSprite& sprite, size_t frame,
                                                      int32_t x, int32_t y, uint8_t flags)
{
    constexpr uint8_t flipX = 1, flipY = 2, nestedFrame = 8;
    std::vector<RetailSpriteDraw> draws;
    std::vector<bool> active(sprite.frames.size());
    size_t expandedParts = 0;
    const auto visit = [&](const auto& self, size_t index, int64_t originX, int64_t originY,
                           uint8_t transform, unsigned depth) -> void {
        if (depth >= 64 || active.at(index)) throw std::runtime_error("Cyclic or too deeply nested sprite frame.");
        active[index] = true;
        const auto& entry = sprite.frames[index];
        for (size_t i = entry.first; i < size_t(entry.first) + entry.count; ++i) {
            if (++expandedParts > 65536) throw std::runtime_error("Retail sprite frame exceeds composition limits.");
            const auto& part = sprite.parts.at(i);
            const auto combined = uint8_t(transform ^ (part.flags & (flipX | flipY)));
            if (part.flags & ~(flipX | flipY | nestedFrame))
                throw std::runtime_error("Unsupported retail sprite part transform.");
            if (part.flags & nestedFrame) {
                // PaintFModule adds nested-frame offsets before applying that frame's flags.
                self(self, part.index, originX + part.x, originY + part.y, combined, depth + 1);
            } else {
                const auto& module = sprite.modules.at(part.index);
                const auto left = originX + ((transform & flipX) ? -int(part.x) - module.width : part.x);
                const auto top = originY + ((transform & flipY) ? -int(part.y) - module.height : part.y);
                if (left < std::numeric_limits<int32_t>::min() || left > std::numeric_limits<int32_t>::max() ||
                    top < std::numeric_limits<int32_t>::min() || top > std::numeric_limits<int32_t>::max())
                    throw std::runtime_error("Retail sprite frame exceeds composition limits.");
                draws.push_back({part.index, static_cast<int32_t>(left), static_cast<int32_t>(top),
                                 (combined & flipX) != 0, (combined & flipY) != 0});
            }
        }
        active[index] = false;
    };
    if (flags & ~(flipX | flipY)) throw std::runtime_error("Unsupported retail sprite frame transform.");
    visit(visit, frame, x, y, flags, 0);
    return draws;
}

std::vector<RetailSpriteDraw> composeRetailSpriteAnimationFrame(const RetailSprite& sprite, size_t animation,
                                                               size_t frame, int32_t x, int32_t y, uint8_t flags)
{
    const auto& sequence = sprite.animations.at(animation);
    if (frame >= sequence.count) throw std::out_of_range("Retail sprite animation frame is out of range.");
    const auto& entry = sprite.animationFrames.at(sequence.first + frame);
    // PaintAFrame uses the low four bits of the last byte and leaves the anchor unchanged.
    return composeRetailSpriteFrame(sprite, entry.frame, x, y, uint8_t(flags ^ (entry.parameters.back() & 15)));
}
