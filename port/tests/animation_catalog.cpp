#include "city_assets.h"
#include "filesystem.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
struct Fixture {
    std::vector<unsigned char> data = std::vector<unsigned char>(156);
    uint32_t allocate(size_t size) {
        const auto p = static_cast<uint32_t>(data.size());
        data.resize(p + size);
        return p;
    }
    void word(uint32_t p, uint32_t v) {
        for (unsigned i = 0; i < 4; ++i)
            data.at(p + i) = static_cast<unsigned char>(v >> (8 * i));
    }
    void scalar(uint32_t p, float v) {
        uint32_t raw;
        std::memcpy(&raw, &v, 4);
        word(p, raw);
    }
    uint32_t text(const std::string& value) {
        const auto p = allocate((value.size() + 4) & ~size_t(3));
        std::copy(value.begin(), value.end(), data.begin() + p);
        return p;
    }
    void write(const std::filesystem::path& path) {
        std::copy_n("BRES", 4, data.begin());
        word(4, 0xfffe);
        word(8, 32);
        word(12, static_cast<uint32_t>(data.size()));
        word(20, 32);
        word(24, 32);
        word(28, 32);
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        require(bool(file), "Write original-layout animation fixture");
    }
};
void checks() {
    struct Temporary {
        std::filesystem::path path;
        bool owned = false;
        ~Temporary() {
            if (!owned)
                return;
            std::error_code ignored;
            std::filesystem::remove(path / "clips.bdae", ignored);
            std::filesystem::remove(path, ignored);
        }
    } temp{std::filesystem::temp_directory_path() /
           ("OpenGangstar-animation-catalog-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    temp.owned = std::filesystem::create_directory(temp.path);
    require(temp.owned, "Create animation fixture directory");
    Fixture f;
    const auto clips = f.allocate(60);
    f.word(32 + 28, 5);
    f.word(32 + 32, clips);
    const std::array<std::string, 5> names{"move", "MOVE", "move", "auto_sceneanims_range",
                                           "AUTO_SceneAnims_Range"};
    const std::array<int32_t, 5> begins{-100, -80, -60, -50, 0}, ends{100, 120, 140, 150, 200};
    for (uint32_t i = 0; i < 5; ++i) {
        f.word(clips + i * 12, f.text(names[i]));
        f.word(clips + i * 12 + 4, uint32_t(begins[i]));
        f.word(clips + i * 12 + 8, uint32_t(ends[i]));
    }
    const auto animation = f.allocate(36), sources = f.allocate(24), sampler = f.allocate(20),
               channel = f.allocate(16);
    const auto times = f.allocate(16), values = f.allocate(48);
    f.word(32 + 16, 1);
    f.word(32 + 20, animation);
    f.word(animation + 4, 2);
    f.word(animation + 8, sources);
    f.word(animation + 12, 1);
    f.word(animation + 16, sampler);
    f.word(animation + 20, 1);
    f.word(animation + 24, channel);
    f.word(channel + 4, f.text("Bip01-node"));
    f.word(channel + 8, 1);
    f.word(sampler + 4, 0);
    f.word(sampler + 8, 1);
    f.word(sources, 4);
    f.word(sources + 4, 4);
    f.word(sources + 8, times);
    f.word(sources + 12, 6);
    f.word(sources + 16, 12);
    f.word(sources + 20, values);
    for (uint32_t i = 0; i < 4; ++i) {
        f.word(times + i * 4, uint32_t(-100 + int32_t(i) * 100));
        for (uint32_t axis = 0; axis < 3; ++axis)
            f.scalar(values + i * 12 + axis * 4, float(i * 10 + axis));
    }
    f.write(temp.path / "clips.bdae");
    ResourceFileSystem files(temp.path);
    const auto catalog = loadCityAnimationCatalog(files, "clips.bdae");
    require(catalog.size() == 5, "Source catalogue entries were collapsed");
    for (uint32_t i = 0; i < 5; ++i) {
        require(catalog[i].name == names[i] && catalog[i].source && catalog[i].source->index == i &&
                    catalog[i].source->startMs == begins[i] && catalog[i].source->endMs == ends[i] &&
                    catalog[i].durationMs == uint32_t(ends[i] - begins[i]),
                "Source-order identity, exact name or signed source range lost");
        require(catalog[i].channels.size() == 1 && catalog[i].channels[0].target == "Bip01-node" &&
                    !catalog[i].channels[0].node,
                "Indexed catalogue did not reuse the original channel decoder");
    }
    const auto& a = catalog[0].channels[0].keys;
    require(a.size() == 3 && a[0].timeMs == 0 && a[1].timeMs == 100 && a[2].timeMs == 200 &&
                a[0].value[0] == 0 && a[1].value[0] == 10 && a[2].value[0] == 20,
            "Existing exact-boundary key filtering changed");
    const auto& b = catalog[1].channels[0].keys;
    require(b.size() == 2 && b[0].timeMs == 80 && b[1].timeMs == 180 && b[0].value[0] == 10 &&
                b[1].value[0] == 20,
            "Existing in-range-only sparse filtering changed; no new bracketing claim is intended");
    const auto decoded = loadCityScene(files, "clips.bdae");
    const auto legacy = readCityParticleClip(decoded);
    require(legacy.name == "AUTO_SceneAnims_Range" && legacy.source && legacy.source->index == 3 &&
                legacy.source->startMs == -50 && legacy.source->endMs == 150,
            "Legacy script lookup must retain first case-insensitive match and requested result name");
    f.word(clips + 4, 100);
    f.word(clips + 8, 100);
    f.write(temp.path / "clips.bdae");
    bool rejected = false;
    try {
        loadCityAnimationCatalog(files, "clips.bdae");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Existing unsupported nonpositive clip range silently accepted");
}
} // namespace
int main() {
    try {
        checks();
        std::cout << "Indexed source animation catalogue and legacy lookup fixtures passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
