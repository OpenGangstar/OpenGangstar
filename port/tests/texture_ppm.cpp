#include "IFileStream.h"
#include "texture.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
class MemoryStream final : public IFileStream {
public:
    explicit MemoryStream(const std::vector<unsigned char>& data) : bytes(data) {}
    unsigned long long size() const override { return bytes.size(); }
    bool canRead() const override { return true; }
    bool canWrite() const override { return false; }
    unsigned long long tell() const override { return position; }
    void seek(unsigned long long offset) override { position = std::min<size_t>(bytes.size(), size_t(offset)); }
    unsigned long long write(const unsigned char*, unsigned long long) override { return 0; }
    unsigned long long read(unsigned char* out, unsigned long long count) override {
        const auto n = static_cast<size_t>(std::min<unsigned long long>(count, bytes.size() - position));
        std::copy_n(bytes.data() + position, n, out); position += n; return n;
    }
private:
    const std::vector<unsigned char>& bytes;
    size_t position = 0;
};
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::vector<unsigned char> data(const std::string& text) { return {text.begin(), text.end()}; }
Image decode(const std::vector<unsigned char>& bytes) { MemoryStream file(bytes); return readTexture(file); }
void rejected(const std::vector<unsigned char>& bytes) {
    try { (void)decode(bytes); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Expected checked PPM rejection");
}
void expected(const std::vector<unsigned char>& bytes, unsigned width, unsigned height,
              const std::vector<uint32_t>& pixels) {
    const auto image = decode(bytes);
    require(image.width == width && image.height == height && image.bgra == pixels &&
                image.baseFormat == TextureBaseFormat::rgba, "Original PPM dimensions/upload channels/pixels");
}
void controls() {
    constexpr uint32_t black = 0xff000000, white = 0xffffffff;
    expected(data("P1\n8 1\n1 0 1 0 1 0 1 0\n"), 8, 1,
             {black, white, black, white, black, white, black, white});
    expected(data("P1\n3 1\n1 0 1\n       "), 3, 1, {black, black, black});
    expected(data("P1\n4 1\n1 1 1 1\n"), 4, 1, {black, black, black, black}); // Allocation repair.
    expected(data("P2\n3 1\n15\n0 7 255\n "), 3, 1, {black, 0xff070707, white});
    expected(data("P3\n3 1\n255\n255 0 0 0 255 0 0 0 255\n"), 3, 1,
             {0xffff0000, 0xff00ff00, 0xff0000ff});
    auto p4 = data("P4\n3 2\n"); p4.insert(p4.end(), {0xa0, 0x40, 0, 0, 0});
    expected(p4, 3, 2, {white, black, white, black, white, black});
    auto p5 = data("P5\n2 2\n255\n"); p5.insert(p5.end(), {0, 127, 255, 1});
    expected(p5, 2, 2, {black, 0xff7f7f7f, white, 0xff010101});
    auto p6 = data("P6\n1 2\n1\n"); p6.insert(p6.end(), {17, 53, 121, 233, 19, 79});
    expected(p6, 1, 2, {0xff113579, 0xffe9134f});
    expected(data("P2\n1#inline\n 1\nno-max\n257suffix\n"), 1, 1, {0xff010101});
    expected(data("P3\t\f\v\r\n#header\r\n1 1\n0\n17#splice\n3 53 121\n       "), 1, 1, {0xffad3579});
    // Binary skipToNextToken treats sample whitespace and # as header syntax.
    auto skipped = data("P5\n1 1\n255\n\t\n\r \f\v#not-pixels\n"); skipped.push_back(65);
    expected(skipped, 1, 1, {0xff414141});
    auto nul = data("P5\n1 1\n255\n"); nul.push_back(0); expected(nul, 1, 1, {black});
    auto odd = data("P4\n9 2\n"); odd.insert(odd.end(), {0x80, 0x80, 0x01, 0x80, 0});
    std::vector<uint32_t> pixels(18, black); pixels[0] = pixels[8] = pixels[16] = pixels[17] = white;
    expected(odd, 9, 2, pixels);
    // Flat LSB ASCII packing is retained even when row conversion has padding.
    expected(data("P1\n3 2\n1 1 1 1 1 1\n"), 3, 2, {black, black, white, black, black, black});
    expected(data("P2\n2 1\n255\nno -3\n"), 2, 1, {black, black});
    rejected(data("P5\n1 1\n255\n")); // Original <=11-byte bound.
    rejected(data("P2\n1 1\n256\n0       "));
    rejected(data("P2\n0 1\n255\n0       "));
    rejected(data("P2\n4097 1\n255\n0    "));
    rejected(data("P2\n4294967296 1\n255\n0   "));
    rejected(data("P2\n1 1\n255\n4294967296\n"));
    rejected(data("P2\n2 1\n255\n0 1")); // Original ASCII minimum span.
    rejected(data("P2\n2 1\n255\n0       ")); // No invented missing sample tail.
    rejected(data("P6\n2 1\n255\nabcde"));
    rejected(data("P4\n#row\n9 2\n\x80\x80\x01"));
    rejected(data("P5\n1 1\n255\n#unterminated comment"));
    rejected(data("P7\n1 1\n255\n123456"));
    rejected(data("p6\n1 1\n255\n123456"));
    std::vector<unsigned char> huge(64u * 1024 * 1024 + 1); huge[0] = 'P'; huge[1] = '5'; rejected(huge);
}
}
int main(int argc, char** argv) {
    try {
        controls();
        if (argc == 2) {
            for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
                if (!entry.is_regular_file()) continue;
                std::ifstream file(entry.path(), std::ios::binary);
                const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
                std::cout << entry.path().stem().string();
                try {
                    const auto image = decode(bytes);
                    std::cout << " decoded " << image.width << ' ' << image.height;
                    for (auto pixel : image.bgra) std::cout << ' ' << pixel;
                } catch (const std::runtime_error&) { std::cout << " rejected"; }
                std::cout << '\n';
            }
        }
        std::cout << "Original P1-P6 token/packing/upload and checked-boundary tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
