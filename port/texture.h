#pragma once

#include <cstdint>
#include <vector>

class IFileStream;

struct Image {
    uint32_t width;
    uint32_t height;
    // Top-down pixels in BGRA byte order.
    std::vector<uint32_t> bgra;
    std::vector<Image> mipLevels;
};

// Decode uncompressed BMP, PNG, TGA, or the mip chain of a retail ATC DDS.
Image readTexture(IFileStream& file, bool keepBitmapAlpha = false);
