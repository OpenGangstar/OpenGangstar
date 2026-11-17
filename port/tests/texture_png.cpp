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
    if (readTexture(rgb).bgra != std::vector<uint32_t>{
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
    if (readTexture(rgba).bgra != std::vector<uint32_t>{
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
