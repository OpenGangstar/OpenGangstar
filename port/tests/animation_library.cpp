#include "city_assets.h"
#include "filesystem.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
uint32_t bits(float value) { uint32_t word; std::memcpy(&word, &value, 4); return word; }
struct Fixture {
    CityScene scene;
    std::vector<uint32_t> animations, samplers, channels, sources, times, values;
    Fixture() {
        scene.path = "synthetic animation library";
        scene.rootOffset = allocate(36);
        word(scene.rootOffset + 12, 5);
    }
    uint32_t allocate(size_t size) {
        while (scene.bytes.size() % 4) scene.bytes.push_back(0);
        const auto offset = uint32_t(scene.bytes.size());
        scene.bytes.resize(offset + size);
        return offset;
    }
    void word(uint32_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) scene.bytes.at(at + i) = uint8_t(value >> (i * 8));
    }
    uint32_t string(const std::string& value) {
        const auto p = allocate(value.size() + 1);
        std::memcpy(scene.bytes.data() + p, value.c_str(), value.size() + 1);
        return p;
    }
    void tracks(const std::vector<uint32_t>& transforms, const std::vector<int32_t>& domain) {
        const auto table = allocate(transforms.size() * 36);
        word(scene.rootOffset + 16, uint32_t(transforms.size()));
        word(scene.rootOffset + 20, table);
        for (size_t i = 0; i < transforms.size(); ++i) {
            const auto a = table + uint32_t(i * 36), src = allocate(36), sampler = allocate(20), channel = allocate(16);
            const auto time = allocate(domain.size() * 4);
            const uint32_t width = transforms[i] == 4 ? 1 : transforms[i] == 5 ? 4 : 3;
            const auto value = allocate(domain.size() * width * 4);
            animations.push_back(a); samplers.push_back(sampler); channels.push_back(channel);
            sources.push_back(src); times.push_back(time); values.push_back(value);
            word(a, string(i == 2 ? "source0" : "source" + std::to_string(i)));
            word(a + 4, 3); word(a + 8, src); word(a + 12, 1); word(a + 16, sampler);
            word(a + 20, 1); word(a + 24, channel);
            // Deliberately use source indices2/0 instead of assuming0/1.
            word(src, 6); word(src + 4, uint32_t(domain.size()) * width); word(src + 8, value);
            word(src + 24, 4); word(src + 28, uint32_t(domain.size())); word(src + 32, time);
            word(sampler, i == 0 ? 0 : i == 1 ? 1 : i == 2 ? 7 : 0xffffffffu);
            word(sampler + 4, 2); word(sampler + 8, 0);
            word(channel, 0xffffffffu); word(channel + 4, string("target"));
            word(channel + 8, transforms[i]); word(channel + 12, 0xffffffffu);
            for (size_t k = 0; k < domain.size(); ++k) {
                word(time + uint32_t(k * 4), uint32_t(domain[k]));
                for (uint32_t c = 0; c < width; ++c)
                    word(value + uint32_t((k * width + c) * 4), bits(k == 0 && c == 0 ? -0.f : float(k * 10 + c + 1)));
            }
        }
    }
};
template<class F> void rejects(F change, const char* message)
{
    Fixture f; f.tracks({1}, {-1, 0, 100}); change(f);
    bool rejected = false;
    try { (void)readCityAnimationLibrary(f.scene); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, message);
}
void synthetic()
{
    Fixture f;
    const std::vector<int32_t> domain{std::numeric_limits<int32_t>::min(), -1, 0, 0, std::numeric_limits<int32_t>::max()};
    f.tracks({1, 4, 5, 10}, domain);
    const auto library = readCityAnimationLibrary(f.scene);
    require(library.path == f.scene.path && library.commonCacheMetadata == 5 && library.tracks.size() == 4,
            "Full library metadata/source order");
    for (size_t i = 0; i < 4; ++i) {
        const auto& t = library.tracks[i];
        require(t.sourceIndex == i && t.target == "target" && t.keys.size() == domain.size() &&
                t.inputSourceIndex == 2 && t.outputSourceIndex == 0,
                "Source and target identities/indexed sampler sources survive");
        for (size_t k = 0; k < domain.size(); ++k)
            require(t.keys[k].timeMs == domain[k], "Signed full time domain and duplicate-key order survive");
        require(bits(t.keys.front().value[0]) == bits(-0.f), "Source scalar signed-zero bits survive");
    }
    require(library.tracks[0].sourceUid == library.tracks[2].sourceUid &&
            library.tracks[0].transform == CityAnimationTransform::translation &&
            library.tracks[1].transform == CityAnimationTransform::translationZ &&
            library.tracks[2].transform == CityAnimationTransform::rotation &&
            library.tracks[3].transform == CityAnimationTransform::scale &&
            library.tracks[0].interpolationSelector == 0 && library.tracks[1].interpolationSelector == 1 &&
            library.tracks[2].interpolationSelector == 7 && library.tracks[3].interpolationSelector == 0xffffffffu,
            "No UID deduplication, transform reinterpretation or selector normalization");
    require(library.tracks[1].keys[0].value[1] == 0 && library.tracks[0].keys[0].value[3] == 0,
            "Unused native value lanes are initialized");
    rejects([](auto& x) { x.scene.bytes.resize(12); }, "Truncated root rejected");
    rejects([](auto& x) { x.word(x.animations[0] + 12, 2); }, "Unsupported multiple samplers rejected");
    rejects([](auto& x) { x.word(x.animations[0] + 20, 0); }, "Absent channel rejected");
    rejects([](auto& x) { x.word(x.animations[0] + 28, 1); }, "Nested source animation not silently flattened");
    rejects([](auto& x) { x.word(x.channels[0] + 8, 999); }, "Unsupported full-library property not silently omitted");
    rejects([](auto& x) { x.word(x.samplers[0] + 4, 3); }, "Input source index checked");
    rejects([](auto& x) { x.word(x.samplers[0] + 8, 3); }, "Output source index checked");
    rejects([](auto& x) { x.word(x.sources[0] + 24, 6); }, "Signed-time source type checked");
    rejects([](auto& x) { x.word(x.sources[0], 4); }, "Float-value source type checked");
    rejects([](auto& x) { x.word(x.sources[0] + 4, 8); }, "Value count checked against component width");
    rejects([](auto& x) { x.word(x.sources[0] + 28, 0); x.word(x.sources[0] + 4, 0); }, "Empty track rejected");
    rejects([](auto& x) { x.word(x.times[0] + 4, uint32_t(-2)); }, "Decreasing time domain rejected");
    rejects([](auto& x) { x.word(x.values[0], 0x7f800000); }, "Nonfinite source rejected");
    rejects([](auto& x) { x.word(x.sources[0] + 8, uint32_t(x.scene.bytes.size())); }, "Truncated values rejected");
    // Exercise the existing clipped/particle route through the shared decoder.
    Fixture clipped; clipped.tracks({1}, {-100, 0, 50, 100, 200});
    auto node = CityNode{}; node.name = "target"; clipped.scene.nodes.push_back(node);
    const auto clip = clipped.allocate(12);
    clipped.word(clipped.scene.rootOffset + 28, 1); clipped.word(clipped.scene.rootOffset + 32, clip);
    clipped.word(clip, clipped.string("auto_sceneanims_range")); clipped.word(clip + 4, 0); clipped.word(clip + 8, 100);
    const auto particle = readCityParticleClip(clipped.scene);
    require(particle.name == "AUTO_SceneAnims_Range" && particle.durationMs == 100 &&
            particle.channels.size() == 1 && particle.channels[0].node == 0 &&
            particle.channels[0].keys.size() == 3 && particle.channels[0].keys[0].timeMs == 0 &&
            particle.channels[0].keys[2].timeMs == 100 &&
            readCityAnimationLibrary(clipped.scene).tracks[0].keys.size() == 5,
            "Legacy casefold/model binding/clipping remain separate from the full source");
}
void word(std::ostream& out, uint32_t x) {
    const char bytes[]{char(x), char(x >> 8), char(x >> 16), char(x >> 24)};
    out.write(bytes, 4);
}
void string(std::ostream& out, const std::string& text) { word(out, uint32_t(text.size())); out.write(text.data(), text.size()); }
void retail(const char* directory, const char* dump)
{
    ResourceFileSystem assets{std::filesystem::path(directory)};
    const auto full = loadCityAnimationLibrary(assets, "./anim_ingame.bdae");
    const auto catalog = loadCityAnimationCatalog(assets, "./anim_ingame.bdae");
    require(full.commonCacheMetadata == 5 && full.tracks.size() == 24 && catalog.size() == 213,
            "Actual plain animator global metadata and unchanged clipped catalogue");
    std::set<std::string> targets;
    size_t keys = 0, position = 0, z = 0, rotation = 0;
    for (size_t i = 0; i < full.tracks.size(); ++i) {
        const auto& track = full.tracks[i]; targets.insert(track.target); keys += track.keys.size();
        require(track.sourceIndex == i && !track.sourceUid.empty() && track.interpolationSelector == 1 &&
                track.keys.front().timeMs == 0 && track.keys.back().timeMs == 259066,
                "All source tracks retain original index/name/selector and complete endpoints");
        position += track.transform == CityAnimationTransform::translation;
        z += track.transform == CityAnimationTransform::translationZ;
        rotation += track.transform == CityAnimationTransform::rotation;
    }
    require(targets.size() == 23 && position == 1 && z == 1 && rotation == 22,
            "Actual 24-track/23-target property inventory");
    require(catalog[188].name == "move_run" && catalog[188].source->startMs == 241167 &&
            catalog[202].name == "move_walk" && catalog[202].source->endMs == 251332,
            "Original clipped source-index intervals preserved");
    if (dump) {
        std::ofstream out(dump, std::ios::binary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        word(out, 0x4c413630); word(out, full.commonCacheMetadata); word(out, uint32_t(full.tracks.size()));
        for (const auto& track : full.tracks) {
            word(out, track.sourceIndex); string(out, track.sourceUid); string(out, track.target);
            const auto kind = track.transform == CityAnimationTransform::translation ? 1u :
                track.transform == CityAnimationTransform::translationZ ? 4u :
                track.transform == CityAnimationTransform::rotation ? 5u : 10u;
            word(out, kind); word(out, track.interpolationSelector); word(out, track.inputSourceIndex);
            word(out, track.outputSourceIndex); word(out, uint32_t(track.keys.size()));
            for (const auto& key : track.keys) {
                word(out, uint32_t(key.timeMs)); for (float value : key.value) word(out, bits(value));
            }
        }
    }
    std::cout << full.tracks.size() << " full source tracks, " << keys << " keys, " << targets.size()
              << " targets; " << catalog.size() << " clipped catalogue entries preserved\n";
}
}
int main(int argc, char** argv)
{
    try {
        synthetic();
        if (argc > 1) retail(argv[1], argc > 2 ? argv[2] : nullptr);
        std::cout << "Unclipped animation library tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
