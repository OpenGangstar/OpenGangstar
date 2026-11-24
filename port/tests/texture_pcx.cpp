#include "IFileStream.h"
#include "texture.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
class MemoryStream final : public IFileStream {
  public:
    explicit MemoryStream(const std::vector<unsigned char>& bytes) : bytes(bytes) {}
    unsigned long long size() const override { return bytes.size(); }
    bool canRead() const override { return true; }
    bool canWrite() const override { return false; }
    unsigned long long tell() const override { return position; }
    void seek(unsigned long long offset) override { position = std::min<size_t>(bytes.size(), size_t(offset)); }
    unsigned long long write(const unsigned char*, unsigned long long) override { return 0; }
    unsigned long long read(unsigned char* output, unsigned long long count) override {
        const size_t n = static_cast<size_t>(std::min<unsigned long long>(count, bytes.size() - position));
        std::copy_n(bytes.data() + position, n, output);
        position += n;
        return n;
    }
  private:
    const std::vector<unsigned char>& bytes;
    size_t position = 0;
};
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void half(std::vector<unsigned char>& bytes, size_t at, unsigned value) {
    bytes.at(at) = static_cast<unsigned char>(value);
    bytes.at(at + 1) = static_cast<unsigned char>(value >> 8);
}
std::vector<unsigned char> fixture(unsigned bits, unsigned width, unsigned height, unsigned line,
                                   unsigned planes, const std::vector<unsigned char>& decoded) {
    std::vector<unsigned char> bytes(128);
    bytes[0] = 10; bytes[1] = 5; bytes[2] = 1; bytes[3] = static_cast<unsigned char>(bits);
    half(bytes, 4, 7); half(bytes, 6, 11);
    half(bytes, 8, 7 + width - 1); half(bytes, 10, 11 + height - 1);
    bytes[65] = static_cast<unsigned char>(planes); half(bytes, 66, line);
    for (const auto value : decoded) {
        if (value >= 192) bytes.push_back(193);
        bytes.push_back(value);
    }
    if (bits == 8) {
        bytes.push_back(12);
        for (unsigned i = 0; i < 256; ++i) {
            bytes.push_back(static_cast<unsigned char>(i * 37 + 1));
            bytes.push_back(static_cast<unsigned char>(i * 73 + 7));
            bytes.push_back(static_cast<unsigned char>(i * 101 + 5));
        }
    }
    return bytes;
}
Image decode(const std::vector<unsigned char>& bytes) {
    MemoryStream stream(bytes);
    return readTexture(stream);
}
void rejected(const std::vector<unsigned char>& bytes) {
    try { decode(bytes); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Unsafe or unsupported PCX input was accepted.");
}
// Captured from actual original PCX loadImage + convert8BitTo16Bit for all
// palette entries; original default A1R5G5B5 upload expansion is bit replication.
constexpr std::array<uint32_t, 256> paletteGolden{
    0xff000000u, 0xff21526bu, 0xff4a9cceu, 0xff73e731u, 0xff94299cu, 0xffbd73ffu, 0xffdebd63u, 0xff0000ceu,
    0xff294a29u, 0xff4a9c94u, 0xff73e7f7u, 0xff9c295au, 0xffbd73c6u, 0xffe7bd21u, 0xff00008cu, 0xff294af7u,
    0xff529452u, 0xff73e7bdu, 0xff9c2918u, 0xffc67384u, 0xffe7bdefu, 0xff08004au, 0xff294ab5u, 0xff529418u,
    0xff7bde7bu, 0xff9c29e7u, 0xffc67342u, 0xffefbdadu, 0xff080010u, 0xff314a73u, 0xff5294deu, 0xff7bde42u,
    0xffa521a5u, 0xffc67308u, 0xffefbd6bu, 0xff1000d6u, 0xff314a39u, 0xff5a949cu, 0xff7bde00u, 0xffa5216bu,
    0xffce6bceu, 0xffefbd31u, 0xff100094u, 0xff394affu, 0xff5a9463u, 0xff84dec6u, 0xffa52129u, 0xffce6b94u,
    0xfff7b5f7u, 0xff10005au, 0xff394abdu, 0xff639421u, 0xff84de8cu, 0xffad21efu, 0xffce6b52u, 0xfff7b5bdu,
    0xff18ff18u, 0xff394a84u, 0xff6394e7u, 0xff8cde4au, 0xffad21b5u, 0xffd66b10u, 0xfff7b57bu, 0xff18ffe7u,
    0xff424242u, 0xff6394adu, 0xff8cde08u, 0xffb52173u, 0xffd66bdeu, 0xffffb539u, 0xff18ffa5u, 0xff424208u,
    0xff6b8c6bu, 0xff8cded6u, 0xffb52131u, 0xffde6b9cu, 0xffffb500u, 0xff21ff63u, 0xff4242ceu, 0xff6b8c31u,
    0xff94d694u, 0xffb521ffu, 0xffde6b5au, 0xff00b5c6u, 0xff21ff29u, 0xff4a428cu, 0xff6b8cf7u, 0xff94d65au,
    0xffbd18bdu, 0xffde6b21u, 0xff00b584u, 0xff29ffefu, 0xff4a4252u, 0xff738cb5u, 0xff94d618u, 0xffbd1884u,
    0xffe763e7u, 0xff00b54au, 0xff29ffadu, 0xff524210u, 0xff738c7bu, 0xff9cd6deu, 0xffbd1842u, 0xffe763adu,
    0xff08ad08u, 0xff29ff73u, 0xff5242d6u, 0xff7b8c39u, 0xff9cd6a5u, 0xffc61800u, 0xffe7636bu, 0xff08add6u,
    0xff31f731u, 0xff52429cu, 0xff7b8cffu, 0xffa5d663u, 0xffc618ceu, 0xffef6329u, 0xff08ad94u, 0xff31f7ffu,
    0xff5a395au, 0xff7b8cc6u, 0xffa5d621u, 0xffce188cu, 0xffef63f7u, 0xff10ad52u, 0xff31f7bdu, 0xff5a3921u,
    0xff848484u, 0xffa5d6efu, 0xffce184au, 0xfff763b5u, 0xff10ad18u, 0xff39f77bu, 0xff5a39e7u, 0xff84844au,
    0xffadceadu, 0xffce1810u, 0xfff76373u, 0xff18addeu, 0xff39f742u, 0xff6339a5u, 0xff848408u, 0xffadce73u,
    0xffd610d6u, 0xfff76339u, 0xff18ad9cu, 0xff42f700u, 0xff63396bu, 0xff8c84ceu, 0xffadce31u, 0xffd6109cu,
    0xffff5affu, 0xff18ad63u, 0xff42f7c6u, 0xff6b3929u, 0xff8c8494u, 0xffb5cef7u, 0xffd6105au, 0xffff5ac6u,
    0xff21a521u, 0xff42f78cu, 0xff6b39efu, 0xff948452u, 0xffb5cebdu, 0xffde1018u, 0xffff5a84u, 0xff21a5efu,
    0xff4aef4au, 0xff6b39b5u, 0xff948410u, 0xffbdce7bu, 0xffde10e7u, 0xff005a42u, 0xff21a5adu, 0xff4aef10u,
    0xff733173u, 0xff9484deu, 0xffbdce39u, 0xffe710a5u, 0xff005a08u, 0xff29a56bu, 0xff4aefd6u, 0xff733139u,
    0xff9c7b9cu, 0xffbdce00u, 0xffe71063u, 0xff085aceu, 0xff29a531u, 0xff52ef94u, 0xff7331ffu, 0xff9c7b63u,
    0xffc6c6c6u, 0xffe71029u, 0xff085a8cu, 0xff31a5f7u, 0xff52ef5au, 0xff7b31bdu, 0xff9c7b21u, 0xffc6c68cu,
    0xffef08efu, 0xff085a52u, 0xff31a5b5u, 0xff5aef18u, 0xff7b3184u, 0xffa57be7u, 0xffc6c64au, 0xffef08b5u,
    0xff105210u, 0xff31a57bu, 0xff5aefdeu, 0xff843142u, 0xffa57badu, 0xffcec608u, 0xffef0873u, 0xff1052deu,
    0xff399c39u, 0xff5aefa5u, 0xff843100u, 0xffad7b6bu, 0xffcec6d6u, 0xfff70831u, 0xff10529cu, 0xff399c00u,
    0xff63e763u, 0xff8431ceu, 0xffad7b29u, 0xffd6c694u, 0xfff708ffu, 0xff18525au, 0xff399cc6u, 0xff63e729u,
    0xff8c298cu, 0xffad7bf7u, 0xffd6c652u, 0xffff08bdu, 0xff185221u, 0xff429c84u, 0xff63e7efu, 0xff8c2952u,
    0xffb573b5u, 0xffd6c618u, 0xffff087bu, 0xff2152e7u, 0xff429c4au, 0xff6be7adu, 0xff8c2910u, 0xffb5737bu,
    0xffdebddeu, 0xffff0842u, 0xff2152a5u, 0xff4a9c08u, 0xff6be773u, 0xff9429d6u, 0xffb57339u, 0xffdebda5u
};
void tests() {
    std::vector<unsigned char> indices;
    for (unsigned i = 0; i < 256; ++i) indices.push_back(static_cast<unsigned char>(i));
    const auto palette = decode(fixture(8, 256, 1, 256, 1, indices));
    require(palette.width == 256 && palette.height == 1 && palette.baseFormat == TextureBaseFormat::rgba &&
            palette.mipLevels.empty() && std::equal(palette.bgra.begin(), palette.bgra.end(), paletteGolden.begin()),
            "Original PCX palette quantization or upload expansion changed.");
    const auto padded = decode(fixture(8, 2, 2, 4, 1, {0, 1, 200, 201, 2, 3, 202, 203}));
    require(padded.bgra == std::vector<uint32_t>{paletteGolden[0], paletteGolden[1], paletteGolden[2], paletteGolden[3]},
            "PCX origin, row padding or orientation changed.");
    const auto planes = decode(fixture(8, 2, 2, 2, 3, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));
    require(planes.bgra == std::vector<uint32_t>{paletteGolden[0], paletteGolden[1], paletteGolden[6], paletteGolden[7]},
            "Original multi-plane indexed PCX behavior changed.");
    auto run = fixture(8, 2, 2, 2, 1, {});
    run.insert(run.begin() + 128, {196, 193});
    require(decode(run).bgra == std::vector<uint32_t>(4, paletteGolden[193]), "PCX RLE across rows changed.");
    auto zeroRun = fixture(8, 2, 1, 2, 1, {});
    zeroRun.insert(zeroRun.begin() + 128, {192, 99, 0, 1});
    require(decode(zeroRun).bgra == std::vector<uint32_t>{paletteGolden[0], paletteGolden[1]},
            "Original zero-length PCX run must consume its value and continue.");
    auto ignored = fixture(8, 2, 1, 2, 1, {6, 7});
    ignored[1] = 0; ignored[2] = 0; ignored[ignored.size() - 769] = 99;
    require(decode(ignored).bgra == std::vector<uint32_t>{paletteGolden[6], paletteGolden[7]},
            "Original PCX ignored version/encoding/palette marker behavior changed.");
    std::vector<unsigned char> rgb{17,34,51,68,85,102,119,136,153,170,187,204};
    rgb.resize(36, 221);
    require(decode(fixture(24, 2, 2, 6, 1, rgb)).bgra ==
            std::vector<uint32_t>{0xff112233u, 0xff445566u, 0xff778899u, 0xffaabbccu},
            "Original literal 24-bit PCX channel order or layout changed.");
    std::vector<unsigned char> rgbPadded{11,22,33,77,44,55,66}; rgbPadded.resize(24, 99);
    require(decode(fixture(24, 1, 2, 4, 1, rgbPadded)).bgra == std::vector<uint32_t>{0xff0b1621u,0xff2c3742u},
            "Original absolute PCX row padding changed.");
    std::vector<unsigned char> tga(18 + 10);
    tga[0] = 10; tga[2] = 2; tga[12] = 1; tga[14] = 1; tga[16] = 24; tga[17] = 0x20;
    tga.insert(tga.end(), {51, 34, 17});
    require(decode(tga).bgra == std::vector<uint32_t>{0xff112233u},
            "PCX signature routing regressed TGA with a ten-byte comment.");
    for (unsigned bits : {1, 4, 16, 32}) rejected(fixture(bits, 1, 1, 1, 1, {0}));
    auto bad = fixture(24, 1, 1, 1, 1, {});
    bad.insert(bad.end(), {196, 12}); rejected(bad); // Run exceeds original decoded allocation.
    bad.resize(128); bad.insert(bad.end(), {192, 12}); rejected(bad); // No data after a zero-length run.
    bad.resize(128); bad.push_back(193); rejected(bad); // Missing run value.
    rejected(std::vector<unsigned char>{10, 5, 1, 8});
    bad = fixture(8, 1, 1, 1, 1, {0}); bad.resize(128 + 767); rejected(bad);
    bad = fixture(24, 1, 1, 1, 0, {0, 0, 0}); rejected(bad);
    bad = fixture(8, 2, 2, 1, 3, {0, 1, 2, 3, 4, 5}); rejected(bad); // Unsafe original row access.
    bad = fixture(24, 1, 1, 1, 1, {0, 0, 0}); half(bad, 8, 7 + 4096); rejected(bad);
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        tests();
        if (argc == 2) {
            for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
                if (entry.path().extension() != L".pcx") continue;
                std::ifstream file(entry.path(), std::ios::binary);
                const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), {}};
                try {
                    const auto image = decode(bytes);
                    std::cout << entry.path().stem().string() << ' ' << image.width << ' ' << image.height;
                    for (auto pixel : image.bgra) std::cout << ' ' << pixel;
                    std::cout << '\n';
                } catch (const std::runtime_error&) { std::cout << entry.path().stem().string() << " rejected\n"; }
            }
        }
        std::cout << "Original PCX and native bounded-input fixtures passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
