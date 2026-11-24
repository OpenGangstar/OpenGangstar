#include "IFileStream.h"
#include "texture.h"
#include "jpeg_decode.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
// Two RGBA pixels in separate rows, with different alpha values.
constexpr unsigned char sample[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x99, 0x81, 0xb6, 0x27, 0x00, 0x00, 0x00,
    0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x10, 0x54, 0x32, 0x76,
    0x61, 0x08, 0x4d, 0x2b, 0xef, 0x00, 0x00, 0x08, 0xac, 0x02, 0x65, 0x0b,
    0xd7, 0x78, 0x6d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82
};

class SampleStream final : public IFileStream {
public:
    explicit SampleStream(unsigned long long length, const unsigned char* bytes = sample) : length(length), bytes(bytes) {}
    unsigned long long size() const override { return length; }
    bool canRead() const override { return true; }
    bool canWrite() const override { return false; }
    unsigned long long tell() const override { return position; }
    void seek(unsigned long long offset) override { position = std::min(length, offset); }
    unsigned long long write(const unsigned char*, unsigned long long) override { return 0; }
    unsigned long long read(unsigned char* output, unsigned long long count) override
    {
        count = std::min(count, length - position);
        std::copy_n(bytes + position, count, output);
        position += count;
        return count;
    }

private:
    unsigned long long length;
    const unsigned char* bytes;
    unsigned long long position = 0;
};

void bitmapWord(std::vector<unsigned char>& bytes, size_t offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = static_cast<unsigned char>(value >> (i * 8));
}

std::vector<unsigned char> bitmap(unsigned width, int32_t height, unsigned bits,
                                  const std::vector<uint32_t>& palette,
                                  const std::vector<unsigned char>& pixels, unsigned compression = 0)
{
    std::vector<unsigned char> bytes(54);
    bytes[0] = 'B'; bytes[1] = 'M';
    bitmapWord(bytes, 2, static_cast<uint32_t>(54 + palette.size() * 4 + pixels.size()));
    bitmapWord(bytes, 10, static_cast<uint32_t>(54 + palette.size() * 4));
    bitmapWord(bytes, 14, 40);
    bitmapWord(bytes, 18, width);
    bitmapWord(bytes, 22, static_cast<uint32_t>(height));
    bytes[26] = 1;
    bytes[28] = static_cast<unsigned char>(bits);
    bitmapWord(bytes, 30, compression);
    bitmapWord(bytes, 34, static_cast<uint32_t>(pixels.size()));
    bitmapWord(bytes, 46, static_cast<uint32_t>(palette.size()));
    for (const auto color : palette)
        for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(color >> (8 * i)));
    bytes.insert(bytes.end(), pixels.begin(), pixels.end());
    return bytes;
}

Image decodeBitmap(const std::vector<unsigned char>& bytes, bool keepAlpha = false)
{
    SampleStream stream(bytes.size(), bytes.data());
    return readTexture(stream, keepAlpha);
}

void rejectBitmap(const std::vector<unsigned char>& bytes, const char* message)
{
    try {
        decodeBitmap(bytes);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

void checkBitmapRgb()
{
    const auto rgb = bitmap(1, 2, 24, {}, {3, 2, 1, 0xcd, 6, 5, 4, 0xef});
    const auto rgbImage = decodeBitmap(rgb);
    if (rgbImage.baseFormat != TextureBaseFormat::rgba ||
        rgbImage.bgra != std::vector<uint32_t>{0xff040506, 0xff010203})
        throw std::runtime_error("BMP 24-bit channels, bottom-up order, or DWORD row padding changed.");
    auto rgba = bitmap(1, -2, 32, {}, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88});
    if (decodeBitmap(rgba).bgra != std::vector<uint32_t>{0xff332211, 0xff776655} ||
        decodeBitmap(rgba, true).bgra != std::vector<uint32_t>{0x44332211, 0x88776655})
        throw std::runtime_error("BMP 32-bit channels, top-down order, or keepBitmapAlpha changed.");
    // BI_RGB permits biSizeImage zero. Extended information headers retain the same pixels.
    bitmapWord(rgba, 34, 0);
    rgba.insert(rgba.begin() + 54, 16, 0);
    bitmapWord(rgba, 14, 56);
    bitmapWord(rgba, 10, 70);
    if (decodeBitmap(rgba, true).bgra != std::vector<uint32_t>{0x44332211, 0x88776655})
        throw std::runtime_error("Extended BI_RGB header or zero biSizeImage changed existing BMP decoding.");

    const auto rgb555 = bitmap(3, 2, 16, {}, {
        0x00, 0xfc, 0xe0, 0x03, 0x1f, 0x80, 0xde, 0xad,
        0x00, 0x80, 0xff, 0xff, 0x07, 0x8f, 0xbe, 0xef});
    if (decodeBitmap(rgb555).bgra != std::vector<uint32_t>{
            0xff000000, 0xffffffff, 0xff18c639, 0xffff0000, 0x0000ff00, 0xff0000ff})
        throw std::runtime_error("Retail BMP A1R5G5B5 channels, alpha, original RGBA32 expansion, or padding changed.");
    std::vector<unsigned char> ramp;
    for (unsigned level = 0; level < 32; ++level) {
        const unsigned pixel = 0x8000 | (level << 10) | (level << 5) | level;
        ramp.push_back(static_cast<unsigned char>(pixel));
        ramp.push_back(static_cast<unsigned char>(pixel >> 8));
    }
    constexpr std::array<unsigned, 32> originalRamp{
        0, 8, 16, 24, 33, 41, 49, 57, 66, 74, 82, 90, 99, 107, 115, 123,
        132, 140, 148, 156, 165, 173, 181, 189, 198, 206, 214, 222, 231, 239, 247, 255};
    const auto levels = decodeBitmap(bitmap(32, -1, 16, {}, ramp));
    if (levels.baseFormat != TextureBaseFormat::rgba)
        throw std::runtime_error("Default driver options did not preserve the original BMP RGBA32 upload format.");
    for (size_t i = 0; i < originalRamp.size(); ++i)
        if (levels.bgra[i] != (0xff000000 | originalRamp[i] * 0x010101))
            throw std::runtime_error("BMP expansion differs from the original 65536-word RGBA32 conversion oracle.");
    // Actual original converter and same-size Blit both exhaustively matched this 32-entry ramp.
    std::vector<unsigned char> packed;
    packed.reserve(65536 * 2);
    for (unsigned value = 0; value < 65536; ++value) {
        packed.push_back(static_cast<unsigned char>(value));
        packed.push_back(static_cast<unsigned char>(value >> 8));
    }
    const auto allWords = decodeBitmap(bitmap(256, -256, 16, {}, packed));
    for (unsigned value = 0; value < 65536; ++value) {
        const auto expected = ((value & 0x8000) ? 0xff000000u : 0u) |
            (originalRamp[(value >> 10) & 31] << 16) | (originalRamp[(value >> 5) & 31] << 8) | originalRamp[value & 31];
        if (allWords.bgra[value] != expected)
            throw std::runtime_error("BMP packed word differs from the exhaustive original conversion oracle.");
    }

}

void checkBitmapIndexed()
{
    // Retail's 1-bit converter deliberately ignores even a colored BMP palette.
    const auto mono = decodeBitmap(bitmap(9, 2, 1, {0xffff0000, 0xff0000ff},
        {0x80, 0x80, 0xaa, 0xbb, 0x7f, 0, 0xcc, 0xdd}));
    if (mono.width != 9 || mono.height != 2 || mono.bgra != std::vector<uint32_t>{
            0xff000000, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xff000000,
            0xffffffff, 0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xffffffff})
        throw std::runtime_error("Retail monochrome BMP bit order, row orientation, or palette bypass changed.");
    if (decodeBitmap(bitmap(1, 1, 1, {}, {0x80, 0, 0, 0})).bgra != std::vector<uint32_t>{0xffffffff})
        throw std::runtime_error("Retail monochrome BMP without an unused palette was rejected.");
    const std::vector<uint32_t> palette{0, 0x12ff0000, 0x3400ff00, 0x560000ff};
    if (decodeBitmap(bitmap(3, -2, 4, palette, {0x12, 0x3f, 0xde, 0xad, 0x31, 0x2e, 0xbe, 0xef})).bgra !=
            std::vector<uint32_t>{0xffff0000, 0xff00ff00, 0xff0000ff, 0xff0000ff, 0xffff0000, 0xff00ff00})
        throw std::runtime_error("Indexed BMP high-first nibbles, palette alpha, or odd-width row padding changed.");
    auto fullPalette = palette;
    fullPalette.resize(256);
    fullPalette[200] = 0x001fc73f;
    auto indexed = bitmap(3, 2, 8, fullPalette, {2, 1, 0, 0xee, 200, 2, 1, 0xdd});
    bitmapWord(indexed, 46, 0); // Default full palette size when biClrUsed is zero.
    if (decodeBitmap(indexed).bgra != std::vector<uint32_t>{
            0xff18c639, 0xff00ff00, 0xffff0000, 0xff00ff00, 0xffff0000, 0xff000000})
        throw std::runtime_error("Indexed BMP default palette, unsigned index, or retail 555 quantization changed.");
}

void checkBitmapRle()
{
    const std::vector<uint32_t> palette{
        0xff000000, 0xffff0000, 0xff00ff00, 0xff0000ff,
        0xffffff00, 0xff00ffff, 0xffff00ff, 0xffffffff};
    const auto rle8 = bitmap(8, 4, 8, palette, {
        3, 1, 0, 5, 2, 3, 4, 5, 6, 0xee, 0, 0,
        0, 2, 2, 0, 2, 7, 0, 2, 1, 1, 3, 3, 0, 0,
        0, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 1}, 1);
    if (decodeBitmap(rle8).bgra != std::vector<uint32_t>{
            0xffffffff, 0xffff00ff, 0xff00ffff, 0xffffff00, 0xff0000ff, 0xff00ff00, 0xffff0000, 0xff000000,
            0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xff0000ff, 0xff0000ff, 0xff0000ff,
            0xff000000, 0xff000000, 0xffffffff, 0xffffffff, 0xff000000, 0xff000000, 0xff000000, 0xff000000,
            0xffff0000, 0xffff0000, 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffff00, 0xff00ffff, 0xffff00ff})
        throw std::runtime_error("BMP RLE8 encoded, absolute, padded, delta, or end-of-line packet changed.");
    const auto rle4 = bitmap(7, 3, 4, palette, {
        5, 0x12, 2, 0x34, 0, 0,
        0, 7, 0x76, 0x54, 0x32, 0x1e, 0, 0,
        0, 5, 0x17, 0x26, 0x3f, 0xab, 0, 2, 1, 0, 1, 0x4f, 0, 1}, 2);
    if (decodeBitmap(rle4).bgra != std::vector<uint32_t>{
            0xffff0000, 0xffffffff, 0xff00ff00, 0xffff00ff, 0xff0000ff, 0xff000000, 0xffffff00,
            0xffffffff, 0xffff00ff, 0xff00ffff, 0xffffff00, 0xff0000ff, 0xff00ff00, 0xffff0000,
            0xffff0000, 0xff00ff00, 0xffff0000, 0xff00ff00, 0xffff0000, 0xff0000ff, 0xffffff00})
        throw std::runtime_error("BMP RLE4 nibble alternation, absolute word padding, or delta packet changed.");
    auto early = bitmap(3, 2, 8, palette, {1, 1, 0, 1}, 1);
    bitmapWord(early, 34, 0); // Bound the stream by the file when the header omits its size.
    if (decodeBitmap(early).bgra != std::vector<uint32_t>{
            0xff000000, 0xff000000, 0xff000000, 0xffff0000, 0xff000000, 0xff000000})
        throw std::runtime_error("Early BMP RLE end marker did not leave deterministic palette-zero gaps.");
}

void checkBitmapInvalid()
{
    const std::vector<uint32_t> palette{0, 0x00ff0000};
    auto invalid = bitmap(1, 1, 24, {}, {1, 2, 3, 0});
    invalid.pop_back();
    rejectBitmap(invalid, "Truncated uncompressed BMP row was accepted.");
    invalid.resize(53);
    rejectBitmap(invalid, "Truncated BMP information header was accepted.");
    for (const auto dimensions : std::array<std::array<uint32_t, 2>, 4>{{{0, 1}, {4097, 1}, {1, 0}, {1, 0x80000000}}}) {
        invalid = bitmap(dimensions[0], static_cast<int32_t>(dimensions[1]), 24, {}, {1, 2, 3, 0});
        rejectBitmap(invalid, "Unsupported BMP dimensions were accepted.");
    }
    invalid = bitmap(1, 1, 24, {}, {1, 2, 3, 0});
    bitmapWord(invalid, 10, 53);
    rejectBitmap(invalid, "BMP pixels overlapping its header were accepted.");
    bitmapWord(invalid, 10, 1000);
    rejectBitmap(invalid, "BMP pixel offset beyond the file was accepted.");
    invalid = bitmap(1, 1, 8, palette, {2, 0, 0, 0});
    rejectBitmap(invalid, "BMP index outside a shortened palette was accepted.");
    bitmapWord(invalid, 46, 257);
    rejectBitmap(invalid, "Oversized BMP palette was accepted.");
    bitmapWord(invalid, 46, 3);
    rejectBitmap(invalid, "Truncated BMP palette was accepted.");
    invalid = bitmap(1, 1, 16, {}, {0, 0, 0, 0}, 3);
    rejectBitmap(invalid, "Unevidenced BI_BITFIELDS BMP was accepted.");
    rejectBitmap(bitmap(1, -1, 8, palette, {1, 1, 0, 1}, 1), "Top-down RLE BMP was accepted.");
    rejectBitmap(bitmap(1, 1, 4, palette, {1, 1, 0, 1}, 1), "RLE8 BMP with a four-bit depth was accepted.");
    rejectBitmap(bitmap(1, 1, 8, palette, {1, 1, 0, 1}, 2), "RLE4 BMP with an eight-bit depth was accepted.");
    for (const auto& packets : std::vector<std::vector<unsigned char>>{
            {9, 1, 0, 1}, {0, 9, 0, 1}, {0, 2, 9, 0, 0, 1}, {0, 2, 0, 4, 0, 1},
            {3}, {0, 2, 1}, {0, 5, 1, 0}, {0, 3, 1, 0, 1},
            {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1}, {1, 2, 0, 1}})
        rejectBitmap(bitmap(8, 4, 8, palette, packets, 1), "Malformed BMP RLE8 packet was accepted.");
    for (const auto& packets : std::vector<std::vector<unsigned char>>{
            {9, 0x11, 0, 1}, {0, 5, 0x11, 0x11, 0x10}, {0, 3, 0x12, 0x10, 0, 1},
            {0, 2, 0, 4, 0, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}})
        rejectBitmap(bitmap(8, 4, 4, palette, packets, 2), "Malformed BMP RLE4 packet was accepted.");
    invalid = bitmap(2, 1, 8, palette, {2, 1, 0, 1}, 1);
    bitmapWord(invalid, 34, 3);
    rejectBitmap(invalid, "BMP RLE read beyond its declared stream size.");
    bitmapWord(invalid, 34, 5);
    rejectBitmap(invalid, "Truncated BMP RLE stream size was accepted.");
}

void checkMipChain()
{
    std::array<unsigned char, 128 + 4 * 16> bytes{};
    const auto word = [&](size_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<unsigned char>(value >> (8 * i));
    };
    std::memcpy(bytes.data(), "DDS ", 4);
    word(4, 124);
    word(8, 0xa1007);
    word(12, 3);
    word(16, 5);
    word(28, 3);
    word(76, 32);
    word(80, 4);
    std::memcpy(bytes.data() + 84, "ATCA", 4);
    for (size_t i = 0; i < 4; ++i) {
        const auto offset = 128 + i * 16;
        std::fill_n(bytes.begin() + offset, 8, static_cast<unsigned char>(0x11 * (1 + i)));
        bytes[offset + 9] = 0x7c;
    }
    SampleStream valid(bytes.size(), bytes.data());
    const auto image = readTexture(valid);
    if (image.width != 5 || image.height != 3 || image.bgra.at(4) != 0x22ff0000 ||
        image.mipLevels.size() != 2 || image.mipLevels[0].width != 2 || image.mipLevels[0].height != 1 ||
        image.mipLevels[0].bgra != std::vector<uint32_t>{0x33ff0000, 0x33ff0000} ||
        image.mipLevels[1].width != 1 || image.mipLevels[1].height != 1 ||
        image.mipLevels[1].bgra != std::vector<uint32_t>{0x44ff0000})
        throw std::runtime_error("ATCA mip dimensions, block boundaries, or alpha changed.");

    SampleStream truncated(bytes.size() - 1, bytes.data());
    try {
        readTexture(truncated);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Truncated ATCA mip level was accepted.");
}

void checkAtcVariants()
{
    std::array<unsigned char, 128 + 32> bytes{};
    std::memcpy(bytes.data(), "DDS ", 4);
    bytes[4] = 124;
    bytes[12] = 1;
    bytes[16] = 8;
    bytes[76] = 32;
    bytes[80] = 4;
    std::memcpy(bytes.data() + 84, "ATC ", 4);
    bytes[129] = 0x7c;
    bytes[136] = 0x1f;
    SampleStream rgb(128 + 16, bytes.data());
    const auto rgbImage = readTexture(rgb);
    if (rgbImage.baseFormat != TextureBaseFormat::rgb || rgbImage.bgra != std::vector<uint32_t>{
            0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000,
            0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff})
        throw std::runtime_error("ATC RGB block stride or opacity changed.");

    std::memcpy(bytes.data() + 84, "ATCI", 4);
    std::fill(bytes.begin() + 128, bytes.end(), 0);
    bytes[128] = 20;
    bytes[129] = 120;
    bytes[130] = 0xc8; // Selectors 0, 1, 3, and 7.
    bytes[131] = 0x0e;
    bytes[137] = 0x7c;
    bytes[144] = 200;
    bytes[145] = 60;
    bytes[146] = 0xca; // Selectors 2, 1, 7, and 0.
    bytes[147] = 0x01;
    bytes[152] = 0x1f;
    SampleStream rgba(bytes.size(), bytes.data());
    const auto rgbaImage = readTexture(rgba);
    if (rgbaImage.baseFormat != TextureBaseFormat::rgba || rgbaImage.bgra != std::vector<uint32_t>{
            0x14ff0000, 0x78ff0000, 0x3cff0000, 0xffff0000,
            0xb40000ff, 0x3c0000ff, 0x500000ff, 0xc80000ff})
        throw std::runtime_error("ATCI alpha endpoints, interpolation, or selectors changed.");
}

void checkPsd()
{
    const std::vector<unsigned char> header{
        '8', 'B', 'P', 'S', 0, 1, 0, 0, 0, 0, 0, 0, 0, 4,
        0, 0, 0, 1, 0, 0, 0, 2, 0, 8, 0, 3,
        0, 0, 0, 1, 0x7f, 0, 0, 0, 0, 0, 0, 0, 0
    };
    const std::vector<uint32_t> expected{0x80113344, 0xff223355};
    auto raw = header;
    raw.insert(raw.end(), {0, 0, 0x11, 0x22, 0x33, 0x33, 0x44, 0x55, 0x80, 0xff});
    SampleStream rawStream(raw.size(), raw.data());
    const auto image = readTexture(rawStream);
    if (image.width != 2 || image.height != 1 || image.bgra != expected)
        throw std::runtime_error("Raw PSD channels or alpha changed.");

    auto rle = header;
    rle.insert(rle.end(), {0, 1, 0, 4, 0, 2, 0, 3, 0, 3, 128, 1, 0x11, 0x22,
                           255, 0x33, 1, 0x44, 0x55, 1, 0x80, 0xff});
    SampleStream rleStream(rle.size(), rle.data());
    if (readTexture(rleStream).bgra != expected)
        throw std::runtime_error("PSD PackBits channels or alpha changed.");
    rle[rle.size() - 3] = 254; // Repeat three pixels into a two-pixel row.
    SampleStream invalid(rle.size(), rle.data());
    try {
        readTexture(invalid);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("PSD row overrun was accepted.");
}

void checkTga()
{
    // A bottom-right origin, one run packet, and one raw packet of BGRA pixels.
    unsigned char rgba[]{
        0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 0x18,
        0x81, 1, 2, 3, 4, 1, 5, 6, 7, 8, 9, 10, 11, 12
    };
    SampleStream pixels(sizeof rgba, rgba);
    if (readTexture(pixels).bgra != std::vector<uint32_t>{
            0x0c0b0a09, 0x08070605, 0x04030201, 0x04030201})
        throw std::runtime_error("TGA origin, packet decoding, or alpha changed.");

    const unsigned char indexed[]{
        1, 1, 1, 5, 0, 2, 0, 24, 0, 0, 0, 0, 2, 0, 1, 0, 8, 0x20,
        'a', 0, 0, 255, 255, 0, 0, 6, 5
    };
    SampleStream palette(sizeof indexed, indexed);
    if (readTexture(palette).bgra != std::vector<uint32_t>{0xff0000ff, 0xffff0000})
        throw std::runtime_error("TGA palette base index or pixel order changed.");

    const unsigned char grayscale[]{
        0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 16, 0x28, 0x67, 0x89
    };
    SampleStream luminance(sizeof grayscale, grayscale);
    if (readTexture(luminance).bgra != std::vector<uint32_t>{0x89676767})
        throw std::runtime_error("TGA grayscale or grayscale alpha changed.");

    rgba[18] = 0x84;
    SampleStream overrun(sizeof rgba, rgba);
    try {
        readTexture(overrun);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("TGA packet overrun was accepted.");
}
}

namespace {
// Hand-authored JPEG coefficient fixtures, independent of the Windows decoder.
// One block per component: zero DC gives 128; DC -512/+512 gives 64/192
// with quantizer 1. No encoder, external image or game data is required.
std::vector<unsigned char> jpeg(unsigned components, bool progressive = false, bool rgb = false)
{
    std::vector<unsigned char> bytes{0xff, 0xd8};
    const auto segment = [&](unsigned marker, const std::vector<unsigned char>& data) {
        const auto length = data.size() + 2;
        bytes.insert(bytes.end(), {0xff, static_cast<unsigned char>(marker),
            static_cast<unsigned char>(length >> 8), static_cast<unsigned char>(length)});
        bytes.insert(bytes.end(), data.begin(), data.end());
    };
    if (rgb || components == 4)
        segment(0xee, {'A','d','o','b','e',0,100,0,0,0,0,0});
    else
        segment(0xe0, {'J','F','I','F',0,1,1,0,0,1,0,1,0,0});
    std::vector<unsigned char> quantizer(65, 1); quantizer[0] = 0;
    segment(0xdb, quantizer);
    std::vector<unsigned char> frame{8, 0, 1, 0, 2, static_cast<unsigned char>(components)};
    for (unsigned c = 0; c < components; ++c) {
        const auto id = rgb ? static_cast<unsigned char>("RGB"[c]) : static_cast<unsigned char>(c + 1);
        frame.insert(frame.end(), {id, 0x11, 0});
    }
    segment(progressive ? 0xc2 : 0xc0, frame);
    std::vector<unsigned char> tables(17, 0); tables[1] = 1;
    if (rgb) tables[2] = 1;
    tables.push_back(0);
    if (rgb) tables.push_back(10);
    tables.push_back(0x10); tables.push_back(1);
    tables.insert(tables.end(), 15, 0); tables.push_back(0);
    segment(0xc4, tables);
    const auto entropy = [&](std::string bits) {
        while (bits.size() % 8) bits += '1';
        for (size_t i = 0; i < bits.size(); i += 8) {
            unsigned value = 0;
            for (size_t j = 0; j < 8; ++j) value = value * 2 + unsigned(bits[i+j] == '1');
            bytes.push_back(static_cast<unsigned char>(value));
            if (value == 255) bytes.push_back(0);
        }
    };
    std::vector<unsigned char> scan{static_cast<unsigned char>(components)};
    for (unsigned c = 0; c < components; ++c) scan.insert(scan.end(), {frame[6+c*3], 0});
    scan.insert(scan.end(), {0, static_cast<unsigned char>(progressive ? 0 : 63), 0});
    segment(0xda, scan);
    if (progressive) {
        entropy(std::string(components, '0'));
        for (unsigned c = 0; c < components; ++c) {
            segment(0xda, {1, frame[6+c*3], 0, 1, 63, 0});
            entropy("0");
        }
    } else {
        entropy(rgb ? "0010011111111101010000000000" : std::string(components * 2, '0'));
    }
    bytes.insert(bytes.end(), {0xff, 0xd9});
    return bytes;
}

void checkJpeg()
{
    for (const auto& bytes : {jpeg(1), jpeg(3), jpeg(3, true), jpeg(3, false, true)}) {
        SampleStream stream(bytes.size(), bytes.data());
        const auto image = readTexture(stream);
        const bool rgb = bytes[3] == 0xee;
        const uint32_t expected = rgb ? 0xff8040c0u : 0xff808080u;
        if (image.width != 2 || image.height != 1 || image.bgra != std::vector<uint32_t>(2, expected) ||
            image.baseFormat != TextureBaseFormat::rgba)
            throw std::runtime_error("JPEG baseline/progressive/grayscale/RGB output or default upload format changed.");
    }
    const auto cmyk = jpeg(4);
    SampleStream unsupported(cmyk.size(), cmyk.data());
    try { readTexture(unsupported); }
    catch (const std::runtime_error&) {
        const auto valid = jpeg(3);
        SampleStream truncated(24, valid.data());
        try { readTexture(truncated); }
        catch (const std::runtime_error&) { return; }
        throw std::runtime_error("Truncated JPEG was accepted.");
    }
    throw std::runtime_error("Original unsupported CMYK-to-RGB JPEG conversion was accepted.");
}
}

namespace {
void checkOriginalJpegPixels()
{
    // Generated17x11 JPEG with nonconstant AC/chroma, not encoded by this decoder.
    // Encoded SHA2569f2bb5902ebe9eaa58616944416fbebb90af3c8f1dc81c84356ba877d365a094.
    // Expected pixels captured from actual original CImageLoaderJPG/libjpeg,
    // do_fancy_upsampling=FALSE. WIC differed186/187pixels by up to76/channel.
    static const std::vector<unsigned char> bytes{
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01,
        0x01, 0x01, 0x00, 0x60, 0x00, 0x60, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43,
        0x00, 0x03, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04,
        0x03, 0x03, 0x04, 0x05, 0x08, 0x05, 0x05, 0x04, 0x04, 0x05, 0x0a, 0x07,
        0x07, 0x06, 0x08, 0x0c, 0x0a, 0x0c, 0x0c, 0x0b, 0x0a, 0x0b, 0x0b, 0x0d,
        0x0e, 0x12, 0x10, 0x0d, 0x0e, 0x11, 0x0e, 0x0b, 0x0b, 0x10, 0x16, 0x10,
        0x11, 0x13, 0x14, 0x15, 0x15, 0x15, 0x0c, 0x0f, 0x17, 0x18, 0x16, 0x14,
        0x18, 0x12, 0x14, 0x15, 0x14, 0xff, 0xdb, 0x00, 0x43, 0x01, 0x03, 0x04,
        0x04, 0x05, 0x04, 0x05, 0x09, 0x05, 0x05, 0x09, 0x14, 0x0d, 0x0b, 0x0d,
        0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
        0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
        0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
        0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
        0x14, 0x14, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x0b, 0x00, 0x11, 0x03,
        0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xff, 0xc4, 0x00,
        0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00,
        0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00,
        0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21,
        0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81,
        0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24,
        0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25,
        0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
        0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
        0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
        0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86,
        0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
        0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3,
        0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6,
        0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
        0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1,
        0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc4, 0x00,
        0x1f, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x11, 0x00,
        0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00,
        0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31,
        0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08,
        0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15,
        0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18,
        0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55,
        0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84,
        0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
        0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4,
        0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
        0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
        0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xda, 0x00,
        0x0c, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0xe7,
        0xbe, 0x10, 0x78, 0x63, 0xe1, 0x8f, 0x84, 0xad, 0xac, 0xac, 0xad, 0xa4,
        0xb8, 0xd7, 0xee, 0xbe, 0xd1, 0xe4, 0xcd, 0x06, 0x91, 0x66, 0x5c, 0xa8,
        0x5c, 0xfc, 0xe1, 0xe4, 0xd9, 0x1b, 0x26, 0xe1, 0xc3, 0x23, 0x9c, 0xf1,
        0x8c, 0x8c, 0x91, 0xd2, 0xfc, 0x3d, 0xf1, 0x5d, 0xf6, 0xb9, 0xe2, 0x5d,
        0x43, 0x51, 0xf0, 0xcf, 0xc3, 0xdb, 0x4b, 0x14, 0xfb, 0x64, 0x92, 0x45,
        0x2e, 0xaf, 0x3b, 0x4d, 0xbe, 0x22, 0xcc, 0x42, 0xb4, 0x68, 0x13, 0x6b,
        0x96, 0x61, 0xc8, 0x76, 0x03, 0x04, 0x61, 0xb2, 0x0d, 0x7a, 0xa7, 0xc2,
        0x2f, 0x06, 0x68, 0x92, 0x68, 0xda, 0xba, 0xb6, 0x9b, 0x01, 0x09, 0xa7,
        0x48, 0xab, 0xf2, 0xf4, 0x1e, 0x51, 0x3f, 0xcc, 0x9a, 0xf1, 0x0f, 0x11,
        0xf8, 0xf7, 0xc4, 0x1a, 0x1f, 0xc4, 0x7d, 0x47, 0xc2, 0xda, 0x5e, 0xab,
        0x3e, 0x95, 0xa2, 0x5a, 0xdc, 0x59, 0xbc, 0x50, 0x69, 0xe4, 0x5b, 0xc8,
        0x37, 0x5a, 0xa3, 0x90, 0x65, 0x40, 0x24, 0x60, 0x59, 0xd8, 0xe0, 0xb1,
        0x1d, 0x38, 0xe0, 0x63, 0x99, 0x62, 0x96, 0x2e, 0xb6, 0x26, 0x14, 0x53,
        0x93, 0xa5, 0xcf, 0x67, 0x52, 0x4f, 0x4f, 0xab, 0xf2, 0xc1, 0x25, 0x18,
        0xd9, 0x35, 0x2a, 0x95, 0x39, 0xda, 0x7b, 0x5b, 0xe4, 0x19, 0x0d, 0x18,
        0x67, 0x99, 0x8e, 0x5d, 0x93, 0xd3, 0x4e, 0x4e, 0x30, 0x95, 0x59, 0x4a,
        0xa4, 0xdd, 0x9d, 0xe5, 0x08, 0xe8, 0xa0, 0x96, 0xab, 0xa2, 0x7a, 0x34,
        0xf5, 0xd5, 0x23, 0xd0, 0x7e, 0xd5, 0xf1, 0x23, 0xfe, 0x8a, 0x25, 0x9f,
        0xfd, 0xfa, 0xb1, 0xff, 0x00, 0xe3, 0x74, 0x57, 0xc9, 0x7f, 0xf0, 0x98,
        0x78, 0x93, 0xfe, 0x86, 0x8d, 0x7f, 0xff, 0x00, 0x06, 0xf7, 0x3f, 0xfc,
        0x5d, 0x15, 0xd9, 0xff, 0x00, 0x10, 0xcb, 0x13, 0xff, 0x00, 0x41, 0x51,
        0xff, 0x00, 0xc0, 0x47, 0xff, 0x00, 0x11, 0x3f, 0x35, 0xff, 0x00, 0xa1,
        0x6e, 0x13, 0xff, 0x00, 0x05, 0xff, 0x00, 0xc0, 0x3f, 0xff, 0xd9,
    };
    static const std::vector<uint32_t> original{
        0xff00000b, 0xff121420, 0xff331970, 0xff432980, 0xff6838b0, 0xff7949c1, 0xffa63c2e, 0xffc45a4c,
        0xff798da5, 0xff3b4f67, 0xff3b7775, 0xff306c6a, 0xff528935, 0xff659c48, 0xff9895aa, 0xffaaa7bc,
        0xffd2c778, 0xff1d1f2b, 0xff292b37, 0xff50368d, 0xff5c4299, 0xff8555cd, 0xff9969e1, 0xffcb6153,
        0xffd56b5d, 0xff91a5bd, 0xff5a6e86, 0xff569290, 0xff4c8886, 0xff6da450, 0xff84bb67, 0xffb2afc4,
        0xffcbc8dd, 0xffd2c778, 0xff0e5130, 0xff1f6241, 0xff407180, 0xff568796, 0xff878f6a, 0xff7c845f,
        0xffb79a4a, 0xffc6a959, 0xff98d0d1, 0xff649c9d, 0xff43d254, 0xff42d153, 0xff75c06e, 0xff8dd886,
        0xffe8c9ff, 0xffefd0ff, 0xffd6070d, 0xff246746, 0xff458867, 0xff548594, 0xff70a1b0, 0xffa5ad88,
        0xff99a17c, 0xffd6b969, 0xffdcbf6f, 0xffb1e9ea, 0xff7fb7b8, 0xff3fce50, 0xff57e668, 0xff9fea98,
        0xff186311, 0xff62437f, 0xff7d5e9a, 0xfff3242a, 0xff12ac3a, 0xff28c250, 0xff50b1ba, 0xff67c8d1,
        0xffb2b035, 0xffccca4f, 0xffffacb3, 0xffffc9d0, 0xff6bb6ff, 0xff0033b3, 0xff411026, 0xff55243a,
        0xff702081, 0xff8e3e9f, 0xffc05687, 0xffa73d6e, 0xffe5572f, 0xff2cc654, 0xff3ed866, 0xff78d9e2,
        0xff87e8f1, 0xffcfcd52, 0xff4e4c00, 0xff9c343b, 0xffac444b, 0xff0037b7, 0xff0047c7, 0xff59283e,
        0xff7d4c62, 0xff883899, 0xffa858b9, 0xffcc6293, 0xffd06697, 0xffff754d, 0xff90afde, 0xff11305f,
        0xff610b8a, 0xff832dac, 0xff9e2133, 0xffb13446, 0xffd3398f, 0xffeb51a7, 0xff1f5b73, 0xff17536b,
        0xff416a4c, 0xff4f785a, 0xff87839c, 0xff9894ad, 0xffd7a448, 0xffd19e42, 0xff92d574, 0xff143362,
        0xff274675, 0xff8731b0, 0xff7923a2, 0xffbb3e50, 0xffcd5062, 0xffef55ab, 0xffff68be, 0xff3b778f,
        0xff336f87, 0xff5d8668, 0xff6f987a, 0xff9b97b0, 0xffb4b0c9, 0xffd3a044, 0xffeebb5f, 0xff63a645,
        0xff244e9a, 0xff3963af, 0xff896f72, 0xff7a6063, 0xff9e794d, 0xffb28d61, 0xff88abc9, 0xff547795,
        0xff1fa800, 0xff32bb0d, 0xff51b464, 0xff68cb7b, 0xffb0bee5, 0xffbdcbf2, 0xffffb577, 0xffffc385,
        0xff4bcbbc, 0xff436db9, 0xff5781cd, 0xff9d8386, 0xff997f82, 0xffbd986c, 0xffd3ae82, 0xffa9ccea,
        0xff7295b3, 0xff36bf11, 0xff47d022, 0xff78db8b, 0xff84e797, 0xffc8d6fd, 0xff47557c, 0xff953b00,
        0xffa64c0e, 0xff004f40, 0xff558bb7, 0xff689eca, 0xff7bb916, 0xff85c320, 0xffbdbf6c, 0xffd4d683,
        0xff8efff6, 0xff55c7bd, 0xff31f332, 0xff40ff41, 0xff7c0295, 0xff890fa2, 0xffb31f81, 0xffb21e80,
        0xffe42841, 0xfffa3e57, 0xff15499c,
    };
    SampleStream stream(bytes.size(), bytes.data());
    const auto image = readTexture(stream, true);
    if (image.width != 17 || image.height != 11 || image.bgra != original ||
        image.baseFormat != TextureBaseFormat::rgba)
        throw std::runtime_error("JPEG IDCT/chroma upsampling differs from the original187pixel reference.");
    OpenGangstarJpeg raw{};
    if (!opengangstar_decode_jpeg(bytes.data(), bytes.size(), &raw))
        throw std::runtime_error(raw.error);
    bool matches = raw.width == 17 && raw.height == 11 && raw.warnings == 0;
    for (size_t i = 0; i < original.size(); ++i)
        matches = matches && raw.rgb[3*i] == ((original[i] >> 16) & 255) &&
            raw.rgb[3*i+1] == ((original[i] >> 8) & 255) && raw.rgb[3*i+2] == (original[i] & 255);
    opengangstar_free_jpeg(&raw);
    if (!matches || raw.rgb || raw.width || raw.height)
        throw std::runtime_error("Native JPEG RGB bytes, warnings or release ownership differ from original reference.");
    auto color = jpeg(3, false, true);
    color[17] = 1; // Adobe transform1: coefficients Y128,Cb64,Cr192.
    for (size_t i = 0; i + 18 < color.size(); ++i)
        if (color[i] == 255 && color[i+1] == 0xc0)
            for (unsigned c = 0; c < 3; ++c) color[i+10+3*c] = static_cast<unsigned char>(c+1);
    for (size_t i = 0; i + 10 < color.size(); ++i)
        if (color[i] == 255 && color[i+1] == 0xda)
            for (unsigned c = 0; c < 3; ++c) color[i+5+2*c] = static_cast<unsigned char>(c+1);
    SampleStream colored(color.size(), color.data());
    if (readTexture(colored).bgra != std::vector<uint32_t>(2, 0xffda680f))
        throw std::runtime_error("JPEG YCbCr conversion differs from original218/104/15 reference.");
}

void rejectJpegCode(const std::vector<unsigned char>& bytes, int expected)
{
    OpenGangstarJpeg result{};
    const bool decoded = opengangstar_decode_jpeg(bytes.data(), bytes.size(), &result) != 0;
    const bool correct = !decoded && !result.rgb && !result.width && !result.height &&
        result.error[0] && result.error_code == expected;
    opengangstar_free_jpeg(&result);
    if (!correct)
        throw std::runtime_error("JPEG error code/cleanup differs from the bounded original decoder contract.");
    SampleStream stream(bytes.size(), bytes.data());
    try { readTexture(stream); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid JPEG unexpectedly produced a native image.");
}

void checkJpegErrors()
{
    auto cmyk = jpeg(4);
    rejectJpegCode(cmyk, 27); // Original JERR_CONVERSION_NOTIMPL.
    cmyk[17] = 2;
    rejectJpegCode(cmyk, 27); // AdobeYCCK has the same original unsupported conversion.
    for (auto bytes : {jpeg(1), jpeg(3), jpeg(3, true), jpeg(3, false, true)}) {
        bytes.resize(bytes.size()-2);
        rejectJpegCode(bytes, 43); // Native bounded EOF; original refill could read beyond its buffer.
        bytes.insert(bytes.begin()+2, {0xff,0xe1,0,6,0xff,0xd9,0xaa,0xbb});
        rejectJpegCode(bytes, 43); // FakeEOI in APP payload is not an image terminator.
    }
    auto truncated = jpeg(3); truncated.resize(24);
    rejectJpegCode(truncated, 43);
    for (const auto width : {0u,4097u,65535u}) {
        auto bytes = jpeg(3);
        for (size_t i=0; i+18<bytes.size(); ++i)
            if (bytes[i]==255 && bytes[i+1]==0xc0) {
                bytes[i+7]=static_cast<unsigned char>(width>>8);
                bytes[i+8]=static_cast<unsigned char>(width); break;
            }
        rejectJpegCode(bytes, width==0 ? 32 : width==4097 ? -1 : 41);
    }
    auto precision = jpeg(3);
    for (size_t i=0; i+18<precision.size(); ++i)
        if (precision[i]==255 && precision[i+1]==0xc0) { precision[i+4]=12; break; }
    rejectJpegCode(precision, 15);
    auto trailing = jpeg(3);
    trailing.insert(trailing.end(), {0,1,2,0xff,0xd8,0xff,0xd9});
    SampleStream stream(trailing.size(), trailing.data());
    if (readTexture(stream).bgra != std::vector<uint32_t>(2,0xff808080))
        throw std::runtime_error("Original JPEG trailing-data behavior changed.");
    auto badSkip = jpeg(3); badSkip[4]=0x7f; badSkip[5]=0xff;
    rejectJpegCode(badSkip, 43);
    auto earlyEnd = jpeg(3); earlyEnd.erase(earlyEnd.end()-3); // Keep actual EOI, remove entropy byte.
    OpenGangstarJpeg warned{};
    if (!opengangstar_decode_jpeg(earlyEnd.data(), earlyEnd.size(), &warned) ||
        warned.width!=2 || warned.height!=1 || warned.warnings==0 ||
        !std::all_of(warned.rgb,warned.rgb+6,[](unsigned char value){return value==128;})) {
        opengangstar_free_jpeg(&warned);
        throw std::runtime_error("JPEG original nonfatal premature-marker recovery/logging changed.");
    }
    opengangstar_free_jpeg(&warned);
    // Repeated recovery then success proves no stale error/source state crosses calls.
    for (unsigned i=0; i<64; ++i) {
        rejectJpegCode(cmyk,27);
        const auto valid=jpeg(3); SampleStream next(valid.size(),valid.data());
        if (readTexture(next).bgra!=std::vector<uint32_t>(2,0xff808080))
            throw std::runtime_error("JPEG recovery leaked state into a subsequent decode.");
    }
    OpenGangstarJpeg result{}; const unsigned char dummy=0;
    if (opengangstar_decode_jpeg(&dummy,64u*1024u*1024u+1u,&result) ||
        result.error_code!=-1 || result.rgb || !result.error[0])
        throw std::runtime_error("JPEG encoded-size bound did not precede input access.");
}
}

int main()
try {
    checkJpeg();
    checkOriginalJpegPixels();
    checkJpegErrors();
    checkBitmapRgb();
    checkBitmapIndexed();
    checkBitmapRle();
    checkBitmapInvalid();
    checkMipChain();
    checkAtcVariants();
    checkTga();
    checkPsd();
    SampleStream valid(sizeof sample);
    const auto image = readTexture(valid);
    if (image.width != 1 || image.height != 2 ||
        image.bgra != std::vector<uint32_t>{0x44112233, 0x88556677})
        throw std::runtime_error("PNG dimensions, row order, BGRA channels, or alpha changed.");

    SampleStream truncated(28);
    try {
        readTexture(truncated);
    } catch (const std::runtime_error&) {
        return 0;
    }
    throw std::runtime_error("Truncated PNG was accepted.");
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
