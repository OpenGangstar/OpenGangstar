#include "IFileStream.h"
#include "texture.h"

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

int main()
try {
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
