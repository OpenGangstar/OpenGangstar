#include "menu_swf.h"
#include "IFileStream.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
uint32_t littleEndian(const uint8_t* bytes, unsigned count)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= uint32_t(bytes[i]) << (8 * i);
    return value;
}

std::vector<uint8_t> readSwfBody(IFileStream& file)
{
    constexpr size_t limit = 16 * 1024 * 1024;
    std::array<uint8_t, 8> header{};
    if (file.size() < header.size() || file.size() > limit ||
        file.read(header.data(), header.size()) != header.size())
        throw std::runtime_error("Could not read the SWF header.");
    const auto declared = littleEndian(header.data() + 4, 4);
    if (declared <= header.size() || declared > limit)
        throw std::runtime_error("Unsupported SWF length.");

    std::vector<uint8_t> body(declared - header.size());
    if (std::memcmp(header.data(), "FWS", 3) == 0) {
        if (file.size() != declared || file.read(body.data(), body.size()) != body.size())
            throw std::runtime_error("The SWF body does not match its declared length.");
        return body;
    }
    if (std::memcmp(header.data(), "CWS", 3) != 0)
        throw std::runtime_error("Expected an FWS or CWS menu file.");
    std::vector<uint8_t> compressed(static_cast<size_t>(file.size() - header.size()));
    if (file.read(compressed.data(), compressed.size()) != compressed.size())
        throw std::runtime_error("The compressed SWF body is truncated.");
    auto input = static_cast<uLong>(compressed.size());
    auto output = static_cast<uLongf>(body.size());
    if (uncompress2(body.data(), &output, compressed.data(), &input) != Z_OK ||
        output != body.size() || input != compressed.size())
        throw std::runtime_error("The compressed SWF body does not match its declared length.");
    return body;
}

void readExports(const uint8_t* tag, size_t length, std::map<uint16_t, std::string>& exports)
{
    if (length < 2) throw std::runtime_error("SWF ExportAssets tag has no asset count.");
    const unsigned count = littleEndian(tag, 2);
    const auto* end = tag + length;
    tag += 2;
    for (unsigned i = 0; i < count; ++i) {
        if (end - tag < 3) throw std::runtime_error("Truncated SWF asset export.");
        const auto id = static_cast<uint16_t>(littleEndian(tag, 2));
        tag += 2;
        const auto* zero = std::find(tag, end, uint8_t(0));
        if (zero == end || zero == tag)
            throw std::runtime_error("Invalid SWF asset export name.");
        if (!exports.emplace(id, std::string(tag, zero)).second)
            throw std::runtime_error("Duplicate SWF asset export ID.");
        tag = zero + 1;
    }
    if (tag != end) throw std::runtime_error("Unexpected data after SWF asset exports.");
}

struct Tag {
    uint32_t kind;
    const uint8_t* bytes;
    size_t size;
};

std::vector<Tag> readTags(const uint8_t* bytes, size_t size, size_t offset)
{
    std::vector<Tag> tags;
    while (offset + 2 <= size) {
        const auto header = littleEndian(bytes + offset, 2);
        const auto kind = header >> 6;
        uint32_t length = header & 63;
        offset += 2;
        if (length == 63) {
            if (size - offset < 4) throw std::runtime_error("Truncated SWF tag length.");
            length = littleEndian(bytes + offset, 4);
            offset += 4;
        }
        if (length > size - offset) throw std::runtime_error("Truncated SWF tag.");
        if (kind == 0 && !length) return tags;
        tags.push_back({kind, bytes + offset, length});
        offset += length;
    }
    throw std::runtime_error("SWF file has no End tag.");
}

class Bits {
public:
    Bits(const uint8_t* bytes, size_t size) : bytes(bytes), size(size) {}
    uint32_t take(unsigned count)
    {
        if (bit + count > size * 8) throw std::runtime_error("Truncated SWF bit field.");
        uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i, ++bit)
            value = (value << 1) | ((bytes[bit / 8] >> (7 - bit % 8)) & 1);
        return value;
    }
    int32_t signedValue(unsigned count)
    {
        const auto value = take(count);
        if (count && (value & (1u << (count - 1))))
            return static_cast<int32_t>(int64_t(value) - (int64_t(1) << count));
        return static_cast<int32_t>(value);
    }
    size_t used() const { return (bit + 7) / 8; }

private:
    const uint8_t* bytes;
    size_t size;
    size_t bit = 0;
};

struct Matrix {
    int32_t scaleX = 65536;
    int32_t scaleY = 65536;
    int32_t skewX = 0;
    int32_t skewY = 0;
    int32_t x = 0;
    int32_t y = 0;
};

Matrix readMatrix(Bits& bits)
{
    Matrix matrix;
    if (bits.take(1)) {
        const auto count = bits.take(5);
        matrix.scaleX = bits.signedValue(count);
        matrix.scaleY = bits.signedValue(count);
    }
    if (bits.take(1)) {
        const auto count = bits.take(5);
        matrix.skewX = bits.signedValue(count);
        matrix.skewY = bits.signedValue(count);
    }
    const auto count = bits.take(5);
    matrix.x = bits.signedValue(count);
    matrix.y = bits.signedValue(count);
    return matrix;
}

std::array<int32_t, 4> readRect(Bits& bits)
{
    const auto count = bits.take(5);
    return {bits.signedValue(count), bits.signedValue(count), bits.signedValue(count), bits.signedValue(count)};
}

struct Placement {
    uint16_t id;
    Matrix transform;
    std::string_view name;
    uint16_t depth;
    bool moves;
    bool hasMatrix;
    std::optional<uint16_t> opacity;
};

Placement readPlacement(const Tag& tag)
{
    if (tag.size < 3) throw std::runtime_error("Truncated SWF placement.");
    const auto flags = tag.bytes[0];
    Placement placement{};
    placement.moves = (flags & 1) != 0;
    placement.hasMatrix = (flags & 4) != 0;
    placement.depth = static_cast<uint16_t>(littleEndian(tag.bytes + 1, 2));
    size_t offset = 3;
    if (flags & 2) {
        if (tag.size < 5) throw std::runtime_error("Truncated SWF character ID.");
        placement.id = static_cast<uint16_t>(littleEndian(tag.bytes + 3, 2));
        offset += 2;
    }
    Bits bits(tag.bytes + offset, tag.size - offset);
    if (placement.hasMatrix) placement.transform = readMatrix(bits);
    offset += bits.used();
    if (flags & 8) {
        Bits color(tag.bytes + offset, tag.size - offset);
        const bool additive = color.take(1) != 0;
        const bool multiply = color.take(1) != 0;
        const auto count = color.take(4);
        std::array<int32_t, 4> mul{256, 256, 256, 256};
        if (multiply)
            for (auto& channel : mul) channel = color.signedValue(count);
        std::array<int32_t, 4> add{};
        if (additive)
            for (auto& channel : add) channel = color.signedValue(count);
        if (mul[0] == 256 && mul[1] == 256 && mul[2] == 256 && add == std::array<int32_t, 4>{} &&
            mul[3] >= 0 && mul[3] <= 256)
            placement.opacity = static_cast<uint16_t>(mul[3]);
        offset += color.used();
    }
    if (flags & 16) offset += 2;
    if (!(flags & 32)) return placement;
    if (offset >= tag.size) throw std::runtime_error("Truncated SWF placement name.");
    const auto* begin = tag.bytes + offset;
    const auto* end = tag.bytes + tag.size;
    const auto* zero = std::find(begin, end, uint8_t(0));
    if (zero == end) throw std::runtime_error("Unterminated SWF placement name.");
    placement.name = {reinterpret_cast<const char*>(begin), static_cast<size_t>(zero - begin)};
    return placement;
}

bool isTranslation(const Matrix& matrix)
{
    return matrix.scaleX == 65536 && matrix.scaleY == 65536 && !matrix.skewX && !matrix.skewY;
}

constexpr int twips = 20;
int32_t pixel(int64_t value) { return static_cast<int32_t>(std::lround(double(value) / twips)); }

SwfBitmap resolveBitmap(Placement placed, const Matrix& widget, const std::map<uint16_t, Tag>& definitions,
                        const std::map<uint16_t, std::string>& exports)
{
    int64_t x = widget.x, y = widget.y;
    for (unsigned nesting = 0; nesting < 8; ++nesting) {
        const auto definition = definitions.find(placed.id);
        if (!placed.id || !isTranslation(placed.transform) || definition == definitions.end()) break;
        x += placed.transform.x;
        y += placed.transform.y;
        const auto& tag = definition->second;
        if (tag.kind == 39) {
            Placement child{};
            for (const auto& nested : readTags(tag.bytes, tag.size, 4)) {
                if (nested.kind == 26) child = readPlacement(nested);
                if (nested.kind == 1) break;
            }
            placed = child;
            continue;
        }

        Bits boundsBits(tag.bytes + 2, tag.size - 2);
        const auto bounds = readRect(boundsBits);
        const auto offset = 2 + boundsBits.used();
        if (tag.size - offset < 4 || tag.bytes[offset] != 1 ||
            (tag.bytes[offset + 1] != 0x42 && tag.bytes[offset + 1] != 0x43))
            throw std::runtime_error("The SWF splash shape needs a single bitmap fill.");
        const auto textureId = static_cast<uint16_t>(littleEndian(tag.bytes + offset + 2, 2));
        const auto texture = exports.find(textureId);
        Bits fillBits(tag.bytes + offset + 4, tag.size - offset - 4);
        const auto fill = readMatrix(fillBits);
        if (texture == exports.end() || fill.scaleX != twips * 65536 || fill.scaleY != twips * 65536 ||
            fill.skewX || fill.skewY || bounds[0] >= bounds[1] || bounds[2] >= bounds[3])
            throw std::runtime_error("The SWF splash uses an unsupported texture transform.");
        return {texture->second, pixel(x + bounds[0]), pixel(y + bounds[2]),
                pixel(x + bounds[1]), pixel(y + bounds[3]), pixel(x + fill.x), pixel(y + fill.y)};
    }
    throw std::runtime_error("The SWF splash has no rectangular bitmap shape.");
}
}

SwfSplash readSwfSplash(IFileStream& file)
{
    const auto body = readSwfBody(file);
    Bits stageBits(body.data(), body.size());
    const auto stage = readRect(stageBits);
    const auto tags = readTags(body.data(), body.size(), stageBits.used() + 4);
    Placement widget{};
    std::map<uint16_t, std::string> exports;
    std::map<uint16_t, Tag> definitions;
    for (const auto& tag : tags) {
        if (tag.kind == 56) readExports(tag.bytes, tag.size, exports);
        if ((tag.kind == 2 || tag.kind == 39) && tag.size >= 2)
            definitions.emplace(static_cast<uint16_t>(littleEndian(tag.bytes, 2)), tag);
        if (tag.kind == 26) {
            const auto placed = readPlacement(tag);
            if (placed.name == "widgetSplash") widget = placed;
        }
    }
    const auto movie = definitions.find(widget.id);
    if (!widget.id || !isTranslation(widget.transform) || movie == definitions.end() || movie->second.kind != 39)
        throw std::runtime_error("The SWF has no supported widgetSplash placement.");

    const size_t frameInfo = stageBits.used();
    if (body.size() - frameInfo < 4) throw std::runtime_error("Missing SWF frame metadata.");
    const auto rate = littleEndian(body.data() + frameInfo, 2);
    if (!rate || stage[0] || stage[2] || stage[1] <= 0 || stage[3] <= 0 ||
        stage[1] > 4096 * twips || stage[3] > 4096 * twips || stage[1] % twips || stage[3] % twips)
        throw std::runtime_error("Unsupported SWF splash stage or frame rate.");
    SwfSplash result{};
    result.width = static_cast<uint32_t>(stage[1] / twips);
    result.height = static_cast<uint32_t>(stage[3] / twips);
    result.frameMs = 256000.0 / rate;
    std::string_view label;
    std::map<uint16_t, Placement> visible;
    for (const auto& tag : readTags(movie->second.bytes, movie->second.size, 4)) {
        if (tag.kind == 43 && tag.size >= 5 && tag.bytes[4] == 0)
            label = {reinterpret_cast<const char*>(tag.bytes), 4};
        if (tag.kind == 28 && tag.size == 2)
            visible.erase(static_cast<uint16_t>(littleEndian(tag.bytes, 2)));
        if (tag.kind == 26) {
            const auto placed = readPlacement(tag);
            if (!placed.moves) visible[placed.depth] = placed;
            else {
                auto& prior = visible.at(placed.depth);
                if (placed.id) prior.id = placed.id;
                if (placed.hasMatrix) prior.transform = placed.transform;
                if (placed.opacity) prior.opacity = placed.opacity;
            }
        }
        if (tag.kind == 1 && label == "show" && result.show.texture.empty())
            result.show = resolveBitmap(visible.at(1), widget.transform, definitions, exports);
        if (tag.kind == 1 && label == "hide") {
            if (result.opacities.empty()) {
                result.background = resolveBitmap(visible.at(1), widget.transform, definitions, exports);
                result.outgoing = resolveBitmap(visible.at(3), widget.transform, definitions, exports);
            }
            result.opacities.push_back(visible.at(3).opacity.value_or(256));
        }
    }
    if (result.show.texture.empty() || result.opacities.empty() || result.opacities.back())
        throw std::runtime_error("The SWF splash is missing its show or hide timeline.");
    result.exports = std::move(exports);
    return result;
}
