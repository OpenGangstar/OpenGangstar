#pragma once

#include <cstdint>
#include <vector>

class IFileStream;

enum class TextureBaseFormat { rgb, rgba };

struct Image {
    uint32_t width;
    uint32_t height;
    // Top-down pixels in BGRA byte order.
    std::vector<uint32_t> bgra;
    std::vector<Image> mipLevels;
    // Original upload base format under default driver options 0x12. BGRA storage is separate.
    TextureBaseFormat baseFormat = TextureBaseFormat::rgba;
};

// Decode BMP (BI_RGB 1/4/8/16/24/32-bit or RLE4/RLE8), PNG, IJG 6b JPEG (RGB/grayscale),
// original PCX (8-bit indexed or literal 24-bit), TGA, PSD, or retail ATC DDS mip levels.
// keepBitmapAlpha preserves the existing 32-bit BMP alpha-byte option. New
// indexed BMP variants use retail opaque palettes; 16-bit pixels retain A1R5G5B5 alpha.
Image readTexture(IFileStream& file, bool keepBitmapAlpha = false);
