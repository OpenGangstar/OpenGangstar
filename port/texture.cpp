#include "texture.h"
#include "IFileStream.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace {
uint32_t little32(const uint8_t* bytes)
{
    return bytes[0] | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

unsigned little16(const uint8_t* bytes)
{
    return bytes[0] | (unsigned(bytes[1]) << 8);
}

int expand(int value, int bits)
{
    return (value << (8 - bits)) | (value >> (2 * bits - 8));
}

std::array<uint32_t, 4> colorPalette(const uint8_t* bytes)
{
    const uint32_t endpoints = little32(bytes);
    const uint32_t first = endpoints & 0x7fff;
    const uint32_t second = endpoints >> 16;
    const bool blackMode = (endpoints & 0x8000) != 0;
    const std::array<int, 3> a{expand(first & 31, 5), expand((first >> 5) & 31, 5), expand(first >> 10, 5)};
    const std::array<int, 3> b{expand(second & 31, 5), expand((second >> 5) & 63, 6), expand(second >> 11, 5)};

    // ATC uses 5:3 interpolation in the ordinary mode. The second mode
    // reserves one selector for black and darkens the first endpoint.
    std::array<uint32_t, 4> palette{};
    for (unsigned channel = 0; channel < a.size(); ++channel) {
        const int p = a[channel], q = b[channel];
        const unsigned shift = 8 * channel;
        palette[0] |= uint32_t(blackMode ? 0 : p) << shift;
        palette[1] |= uint32_t(blackMode ? std::max(0, p - q / 4) : (5 * p + 3 * q) / 8) << shift;
        palette[2] |= uint32_t(blackMode ? p : (3 * p + 5 * q) / 8) << shift;
        palette[3] |= uint32_t(q) << shift;
    }
    return palette;
}

enum class AtcAlpha { opaque, explicitAlpha, interpolated };

std::array<uint8_t, 16> decodeAtcAlpha(const uint8_t* block, AtcAlpha mode)
{
    std::array<uint8_t, 16> alpha{};
    if (mode == AtcAlpha::opaque) {
        alpha.fill(255);
    } else if (mode == AtcAlpha::explicitAlpha) {
        for (unsigned i = 0; i < alpha.size(); ++i)
            alpha[i] = ((block[i / 2] >> (4 * (i % 2))) & 15) * 17;
    } else {
        const unsigned first = block[0], second = block[1];
        std::array<unsigned, 8> palette{first, second};
        const unsigned steps = first > second ? 7 : 5;
        for (unsigned i = 1; i < steps; ++i)
            palette[i + 1] = ((steps - i) * first + i * second) / steps;
        if (steps == 5) palette[7] = 255;
        const auto selectors = uint64_t(little32(block + 2)) | (uint64_t(little16(block + 6)) << 32);
        for (unsigned i = 0; i < alpha.size(); ++i)
            alpha[i] = static_cast<uint8_t>(palette[(selectors >> (3 * i)) & 7]);
    }
    return alpha;
}

Image readAtcLevel(IFileStream& file, uint32_t width, uint32_t height, AtcAlpha mode)
{
    Image result{width, height, std::vector<uint32_t>(size_t(width) * height)};
    std::array<uint8_t, 16> block{};
    const size_t alphaBytes = mode == AtcAlpha::opaque ? 0 : 8;
    const auto blockBytes = alphaBytes + 8;
    for (uint32_t y = 0; y < height; y += 4) {
        for (uint32_t x = 0; x < width; x += 4) {
            if (file.read(block.data(), blockBytes) != blockBytes)
                throw std::runtime_error("ATC texture has a truncated level.");
            const auto alpha = decodeAtcAlpha(block.data(), mode);
            const auto* color = block.data() + alphaBytes;
            const auto palette = colorPalette(color);
            const auto selectors = little32(color + 4);
            for (uint32_t row = 0; row < 4 && y + row < height; ++row) {
                for (uint32_t col = 0; col < 4 && x + col < width; ++col) {
                    const uint32_t texel = 4 * row + col;
                    result.bgra[size_t(y + row) * width + x + col] =
                        palette[(selectors >> (2 * texel)) & 3] | (uint32_t(alpha[texel]) << 24);
                }
            }
        }
    }
    return result;
}

Image readAtcTexture(IFileStream& file)
{
    std::array<uint8_t, 128> header{};
    if (file.read(header.data(), header.size()) != header.size() ||
        std::memcmp(header.data(), "DDS ", 4) != 0 || little32(header.data() + 4) != 124 ||
        little32(header.data() + 76) != 32 || !(little32(header.data() + 80) & 4) ||
        little32(header.data() + 112) != 0)
        throw std::runtime_error("Expected a two-dimensional compressed DDS texture.");

    const auto* fourcc = header.data() + 84;
    AtcAlpha mode;
    if (std::memcmp(fourcc, "ATC ", 4) == 0) mode = AtcAlpha::opaque;
    else if (std::memcmp(fourcc, "ATCA", 4) == 0) mode = AtcAlpha::explicitAlpha;
    else if (std::memcmp(fourcc, "ATCI", 4) == 0) mode = AtcAlpha::interpolated;
    else throw std::runtime_error("Unsupported DDS compression. Expected ATC, ATCA, or ATCI.");

    auto height = little32(header.data() + 12);
    auto width = little32(header.data() + 16);
    if (!width || !height || width > 4096 || height > 4096)
        throw std::runtime_error("ATC texture dimensions are outside the supported range.");

    const auto flags = little32(header.data() + 8);
    const auto count = flags & 0x20000 ? little32(header.data() + 28) : 1u;
    uint32_t maximum = 1;
    for (auto extent = std::max(width, height); extent > 1; extent /= 2) ++maximum;
    if (!count || count > maximum) throw std::runtime_error("Invalid ATC mip count.");

    auto result = readAtcLevel(file, width, height, mode);
    for (uint32_t level = 1; level < count; ++level) {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
        result.mipLevels.push_back(readAtcLevel(file, width, height, mode));
    }
    return result;
}

uint32_t bitmap555(unsigned value)
{
    // The retail BMP loader stores indexed colors in A1R5G5B5, uploaded as
    // GL_UNSIGNED_SHORT_5_5_5_1. Choose the nearest BGRA8 UNORM representation.
    const auto channel = [](unsigned component) { return (component * 255 + 15) / 31; };
    return ((value & 0x8000) ? 0xff000000u : 0u) |
        (channel((value >> 10) & 31) << 16) | (channel((value >> 5) & 31) << 8) | channel(value & 31);
}

Image readBitmap(IFileStream& file, bool keepAlpha)
{
    std::array<uint8_t, 54> header{};
    if (file.read(header.data(), header.size()) != header.size() ||
        std::memcmp(header.data(), "BM", 2) != 0 || little32(header.data() + 14) < 40 ||
        little16(header.data() + 26) != 1)
        throw std::runtime_error("Expected a Windows bitmap with an information header.");

    const auto width = little32(header.data() + 18);
    const auto signedHeight = static_cast<int32_t>(little32(header.data() + 22));
    const auto bits = little16(header.data() + 28);
    const auto compression = little32(header.data() + 30);
    const auto pixelOffset = little32(header.data() + 10);
    const uint64_t paletteOffset = uint64_t(14) + little32(header.data() + 14);
    if (!width || width > 4096 || !signedHeight || signedHeight < -4096 || signedHeight > 4096 ||
        (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
        compression > 2 || (compression == 1 && bits != 8) || (compression == 2 && bits != 4) ||
        (compression && signedHeight < 0) || pixelOffset < paletteOffset)
        throw std::runtime_error("Unsupported bitmap dimensions, pixels, or header.");
    if (pixelOffset > file.size()) throw std::runtime_error("Bitmap pixel data is truncated.");

    const uint32_t height = signedHeight < 0 ? -signedHeight : signedHeight;
    std::vector<uint32_t> palette;
    if (bits == 4 || bits == 8) {
        const auto colorsUsed = little32(header.data() + 46);
        const auto count = colorsUsed ? colorsUsed : 1u << bits;
        if (count > (1u << bits) || uint64_t(count) * 4 > pixelOffset - paletteOffset)
            throw std::runtime_error("Invalid or truncated bitmap palette.");
        file.seek(paletteOffset);
        if (file.tell() != paletteOffset) throw std::runtime_error("Could not seek to the bitmap palette.");
        palette.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            std::array<uint8_t, 4> entry{};
            if (file.read(entry.data(), entry.size()) != entry.size())
                throw std::runtime_error("Bitmap palette is truncated.");
            // RGBQUAD's reserved byte is not alpha. Retail converts the upper
            // five bits of each channel to an opaque A1R5G5B5 palette entry.
            palette.push_back(bitmap555(0x8000 | (unsigned(entry[2] >> 3) << 10) |
                                        (unsigned(entry[1] >> 3) << 5) | (entry[0] >> 3)));
        }
    }
    const auto color = [&](unsigned index) {
        if (index >= palette.size()) throw std::runtime_error("Invalid bitmap palette index.");
        return palette[index];
    };
    const uint32_t stride = ((width * bits + 31) / 32) * 4;
    if (!compression && uint64_t(stride) * height > file.size() - pixelOffset)
        throw std::runtime_error("Bitmap pixel data is truncated.");
    file.seek(pixelOffset);
    if (file.tell() != pixelOffset)
        throw std::runtime_error("Could not seek to the bitmap pixels.");

    // Legacy RLE gaps exposed uninitialized memory. Use palette index zero for
    // skipped pixels and validate all packet boundaries instead of reproducing it.
    Image result{width, height, std::vector<uint32_t>(size_t(width) * height, compression ? palette.front() : 0u)};
    if (compression) {
        const auto declaredSize = little32(header.data() + 34);
        const uint64_t available = file.size() - pixelOffset;
        const uint64_t encodedSize = declaredSize ? declaredSize : available;
        if (!encodedSize || encodedSize > available || encodedSize > 64 * 1024 * 1024)
            throw std::runtime_error("Bitmap RLE data is truncated or too large.");
        std::vector<uint8_t> encoded(static_cast<size_t>(encodedSize));
        if (file.read(encoded.data(), encoded.size()) != encoded.size())
            throw std::runtime_error("Bitmap RLE data is truncated.");
        size_t cursor = 0;
        const auto byte = [&]() {
            if (cursor == encoded.size()) throw std::runtime_error("Bitmap RLE packet or end marker is truncated.");
            return encoded[cursor++];
        };
        uint32_t x = 0, y = 0;
        const auto checkRun = [&](unsigned count) {
            if (y >= height || count > width - x) throw std::runtime_error("Bitmap RLE run exceeds its row.");
        };
        const auto emit = [&](unsigned index) {
            result.bgra[size_t(height - 1 - y) * width + x++] = color(index);
        };
        for (;;) {
            const unsigned count = byte(), value = byte();
            if (count) {
                checkRun(count);
                for (unsigned i = 0; i < count; ++i)
                    emit(bits == 8 ? value : (value >> ((i & 1) ? 0 : 4)) & 15);
            } else if (value == 0) {
                if (y >= height) throw std::runtime_error("Bitmap RLE end-of-line exceeds the image.");
                x = 0;
                ++y;
            } else if (value == 1) {
                return result;
            } else if (value == 2) {
                const unsigned dx = byte(), dy = byte();
                if (y >= height || dx >= width - x || dy >= height - y)
                    throw std::runtime_error("Bitmap RLE delta exceeds the image.");
                x += dx;
                y += dy;
            } else {
                checkRun(value);
                unsigned packed = 0;
                for (unsigned i = 0; i < value; ++i) {
                    if (bits == 8 || !(i & 1)) packed = byte();
                    emit(bits == 8 ? packed : (packed >> ((i & 1) ? 0 : 4)) & 15);
                }
                // RLE4 absolute packets align their packed byte count, not the
                // pixel count used by the defective legacy decompressor.
                const unsigned bytes = bits == 8 ? value : (value + 1) / 2;
                if (bytes & 1) byte();
            }
        }
    }
    std::vector<uint8_t> row(stride);
    for (uint32_t y = 0; y < height; ++y) {
        if (file.read(row.data(), stride) != stride)
            throw std::runtime_error("Bitmap pixel data is truncated.");
        const uint32_t targetY = signedHeight < 0 ? y : height - 1 - y;
        auto* dest = result.bgra.data() + size_t(targetY) * width;
        for (uint32_t x = 0; x < width; ++x) {
            if (bits == 1) {
                // Retail convert1BitTo16Bit ignores the BMP palette.
                dest[x] = (row[x / 8] & (0x80 >> (x & 7))) ? 0xffffffff : 0xff000000;
            } else if (bits == 4 || bits == 8) {
                dest[x] = color(bits == 8 ? row[x] : (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15);
            } else if (bits == 16) {
                dest[x] = bitmap555(little16(row.data() + x * 2));
            } else {
                const auto* bgr = row.data() + x * (bits / 8);
                const uint32_t alpha = keepAlpha && bits == 32 ? bgr[3] : 255;
                dest[x] = (alpha << 24) | (uint32_t(bgr[2]) << 16) | (uint32_t(bgr[1]) << 8) | bgr[0];
            }
        }
    }
    return result;
}

bool tgaColorDepth(unsigned bits)
{
    return bits == 15 || bits == 16 || bits == 24 || bits == 32;
}

uint32_t tgaColor(const uint8_t* pixel, unsigned bits, bool hasAlpha)
{
    if (bits <= 16) {
        const auto word = little16(pixel);
        const uint32_t alpha = !hasAlpha || (word & 0x8000) ? 255 : 0;
        return (alpha << 24) | (uint32_t(expand((word >> 10) & 31, 5)) << 16) |
            (uint32_t(expand((word >> 5) & 31, 5)) << 8) | uint32_t(expand(word & 31, 5));
    }
    const uint32_t alpha = hasAlpha && bits == 32 ? pixel[3] : 255;
    return (alpha << 24) | (uint32_t(pixel[2]) << 16) | (uint32_t(pixel[1]) << 8) | pixel[0];
}

Image readTga(IFileStream& file)
{
    std::array<uint8_t, 18> header{};
    if (file.read(header.data(), header.size()) != header.size())
        throw std::runtime_error("TGA header is truncated.");
    const auto type = header[2];
    const bool rle = (type & 8) != 0;
    const auto baseType = type & ~8;
    const auto width = little16(header.data() + 12), height = little16(header.data() + 14);
    const auto bits = header[16], descriptor = header[17];
    const bool hasAlpha = (descriptor & 15) != 0;
    if (header[1] > 1 || baseType < 1 || baseType > 3 || (descriptor & 0xc0) ||
        !width || width > 4096 || !height || height > 4096 ||
        (baseType == 1 && (header[1] != 1 || (bits != 8 && bits != 16))) ||
        (baseType == 2 && !tgaColorDepth(bits)) ||
        (baseType == 3 && bits != 8 && bits != 16))
        throw std::runtime_error("Unsupported TGA image header.");

    const auto read = [&](uint8_t* bytes, size_t count) {
        if (file.read(bytes, count) != count) throw std::runtime_error("TGA image data is truncated.");
    };
    std::array<uint8_t, 255> comment{};
    if (header[0]) read(comment.data(), header[0]);
    const auto firstIndex = little16(header.data() + 3);
    std::vector<uint32_t> palette;
    if (header[1]) {
        const auto count = little16(header.data() + 5);
        const auto depth = header[7];
        if (!count || firstIndex + count > 65536 || !tgaColorDepth(depth))
            throw std::runtime_error("Invalid TGA color map.");
        palette.resize(count);
        for (auto& color : palette) {
            std::array<uint8_t, 4> entry{};
            read(entry.data(), (depth + 7) / 8);
            color = tgaColor(entry.data(), depth, depth == 16 || depth == 32);
        }
    }

    const auto decode = [&](const uint8_t* pixel) -> uint32_t {
        if (baseType == 2) return tgaColor(pixel, bits, hasAlpha);
        if (baseType == 3) {
            const uint32_t alpha = bits == 16 && hasAlpha ? pixel[1] : 255;
            return (alpha << 24) | (uint32_t(pixel[0]) * 0x010101);
        }
        const auto index = bits == 16 ? little16(pixel) : pixel[0];
        if (index < firstIndex || index - firstIndex >= palette.size())
            throw std::runtime_error("TGA pixel references an invalid palette index.");
        return palette[index - firstIndex];
    };
    Image result{width, height, std::vector<uint32_t>(size_t(width) * height)};
    const size_t pixelBytes = (bits + 7) / 8;
    std::array<uint8_t, 128 * 4> packet{};
    for (size_t position = 0; position < result.bgra.size();) {
        uint8_t packetHeader = 0;
        if (rle) read(&packetHeader, 1);
        const size_t remaining = result.bgra.size() - position;
        const size_t count = rle ? (packetHeader & 127) + 1 : std::min(remaining, size_t(128));
        const bool repeat = (packetHeader & 128) != 0;
        if (count > remaining) throw std::runtime_error("TGA packet overruns the image.");
        read(packet.data(), (repeat ? 1 : count) * pixelBytes);
        for (size_t i = 0; i < count; ++i, ++position) {
            const size_t row = position / width, column = position % width;
            const size_t y = (descriptor & 32) ? row : height - 1 - row;
            const size_t x = (descriptor & 16) ? width - 1 - column : column;
            result.bgra[y * width + x] = decode(packet.data() + (repeat ? 0 : i * pixelBytes));
        }
    }
    return result;
}

Image readPsd(IFileStream& file)
{
    const auto read = [&](uint8_t* bytes, size_t count) {
        if (file.read(bytes, count) != count) throw std::runtime_error("PSD data is truncated.");
    };
    const auto big16 = [](const uint8_t* bytes) { return unsigned(bytes[0]) << 8 | bytes[1]; };
    const auto big32 = [](const uint8_t* bytes) {
        return uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
    };
    std::array<uint8_t, 26> header{};
    read(header.data(), header.size());
    const auto channels = big16(header.data() + 12);
    const auto height = big32(header.data() + 14), width = big32(header.data() + 18);
    const auto depth = big16(header.data() + 22), mode = big16(header.data() + 24);
    if (big16(header.data() + 4) != 1 || channels < 3 || channels > 4 || depth != 8 || mode != 3 ||
        !width || width > 4096 || !height || height > 4096)
        throw std::runtime_error("Expected a version 1 RGB PSD with 8-bit channels.");
    // The composite follows the color mode data, image resources and layer data.
    for (int section = 0; section < 3; ++section) {
        std::array<uint8_t, 4> length{};
        read(length.data(), length.size());
        const auto bytes = big32(length.data());
        if (bytes > file.size() - file.tell()) throw std::runtime_error("PSD section is truncated.");
        file.seek(file.tell() + bytes);
    }
    std::array<uint8_t, 2> compressionBytes{};
    read(compressionBytes.data(), compressionBytes.size());
    const auto compression = big16(compressionBytes.data());
    if (compression > 1) throw std::runtime_error("Unsupported PSD compression.");
    std::vector<unsigned> rowLengths;
    if (compression == 1) {
        rowLengths.resize(channels * height);
        for (auto& rowLength : rowLengths) {
            std::array<uint8_t, 2> bytes{};
            read(bytes.data(), bytes.size());
            rowLength = big16(bytes.data());
        }
    }
    Image result{width, height, std::vector<uint32_t>(size_t(width) * height, channels == 3 ? 0xff000000u : 0u)};
    std::vector<uint8_t> row(width), encoded;
    constexpr std::array shifts{16u, 8u, 0u, 24u};
    for (unsigned channel = 0; channel < channels; ++channel) {
        for (uint32_t y = 0; y < height; ++y) {
            if (compression == 0) {
                read(row.data(), row.size());
            } else {
                encoded.resize(rowLengths[channel * height + y]);
                read(encoded.data(), encoded.size());
                size_t out = 0;
                for (size_t in = 0; in < encoded.size();) {
                    const unsigned control = encoded[in++];
                    if (control == 128) continue;
                    const bool repeated = control > 128;
                    const size_t count = repeated ? 257 - control : control + 1;
                    const size_t length = repeated ? 1 : count;
                    if (count > row.size() - out || length > encoded.size() - in)
                        throw std::runtime_error("Invalid PSD RLE packet.");
                    if (repeated) std::fill_n(row.begin() + out, count, encoded[in]);
                    else std::copy_n(encoded.begin() + in, count, row.begin() + out);
                    out += count;
                    in += length;
                }
                if (out != row.size()) throw std::runtime_error("Incomplete PSD RLE row.");
            }
            for (uint32_t x = 0; x < width; ++x)
                result.bgra[size_t(y) * width + x] |= uint32_t(row[x]) << shifts[channel];
        }
    }
    return result;
}

Image readPng(IFileStream& file)
{
    const auto length = file.size();
    if (!length || length > 64 * 1024 * 1024)
        throw std::runtime_error("PNG file size is outside the supported range.");
    std::vector<BYTE> bytes(static_cast<size_t>(length));
    if (file.read(bytes.data(), length) != length)
        throw std::runtime_error("Could not read the PNG file.");

    const auto check = [](HRESULT result, const char* message) {
        if (FAILED(result)) throw std::runtime_error(message);
    };
    struct Apartment {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment;
    if (apartment.result != RPC_E_CHANGED_MODE)
        check(apartment.result, "Could not initialize COM for PNG decoding.");

    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(factory.GetAddressOf())), "Could not open Windows Imaging Component.");
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(stream.GetAddressOf()), "Could not create a PNG stream.");
    check(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size())),
          "Could not initialize the PNG stream.");
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoder(GUID_ContainerFormatPng, nullptr, decoder.GetAddressOf()),
          "Could not create the PNG decoder.");
    check(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand), "Invalid PNG container.");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.GetAddressOf()), "Could not open the PNG frame.");
    UINT width = 0, height = 0;
    check(frame->GetSize(&width, &height), "Could not read PNG dimensions.");
    if (!width || !height || width > 4096 || height > 4096)
        throw std::runtime_error("PNG dimensions are outside the supported range.");

    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(converter.GetAddressOf()), "Could not create a PNG pixel converter.");
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom), "Could not convert PNG pixels to BGRA.");
    Image result{width, height, std::vector<uint32_t>(size_t(width) * height)};
    check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(result.bgra.size() * 4),
                                reinterpret_cast<BYTE*>(result.bgra.data())), "Could not decode PNG pixels.");
    return result;
}
}

Image readTexture(IFileStream& file, bool keepBitmapAlpha)
{
    std::array<uint8_t, 4> signature{};
    if (file.read(signature.data(), signature.size()) != signature.size())
        throw std::runtime_error("Could not read the texture signature.");
    file.seek(0);
    if (file.tell() != 0)
        throw std::runtime_error("Could not rewind the texture.");
    if (std::memcmp(signature.data(), "BM", 2) == 0)
        return readBitmap(file, keepBitmapAlpha);
    if (std::memcmp(signature.data(), "\x89PNG", 4) == 0)
        return readPng(file);
    if (std::memcmp(signature.data(), "DDS ", 4) == 0)
        return readAtcTexture(file);
    if (std::memcmp(signature.data(), "8BPS", 4) == 0)
        return readPsd(file);
    return readTga(file);
}
