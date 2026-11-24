#include "filesystem.h"
#include "retail_animation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
uint32_t bits(float x) {
    uint32_t value;
    std::memcpy(&value, &x, 4);
    return value;
}
void near(float x, float expected, const char *message) {
    require(std::isfinite(x) && std::abs(x - expected) < 0.000003f, message);
}
template <class F> void rejects(F f, const char *message) {
    bool failed = false;
    try {
        f();
    } catch (const std::runtime_error &) {
        failed = true;
    }
    require(failed, message);
}
void words(const std::array<float, 4> &value) {
    std::cout << '[';
    for (size_t i = 0; i < value.size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << bits(value[i]);
    }
    std::cout << ']';
}
void quaternionCases(bool dump) {
    using Q = RetailAnimationQuaternion;
    const Q identity{0, 0, 0, 1}, halfTurn{0, 0, 1, 0};
    const auto quarter = retailAnimationSlerp(identity, halfTurn, .25f);
    near(quarter[2], 0.3826834323650898f, "Original spherical quarter-angle Z");
    near(quarter[3], 0.9238795325112867f, "Original spherical quarter-angle W");
    const auto negative = retailAnimationSlerp({1, 0, 0, 0}, {-1, 0, 0, 0}, 0);
    require(negative[0] == -1, "Original shortest path negates FIRST quaternion");
    const auto nonunit = retailAnimationSlerp({0, 0, 0, 2}, {0, 0, 0, 3}, .25f);
    require(nonunit[3] == 1, "Near-linear branch normalizes weighted quaternion");
    const auto small = retailAnimationSlerp({.2f, 0, 0, .1f}, {0, .2f, 0, .1f}, .5f);
    const auto length =
        ((small[0] * small[0] + small[1] * small[1]) + small[2] * small[2]) + small[3] * small[3];
    require(length < .2f, "Large-angle branch must not normalize nonunit authored inputs");
    const auto zero = retailAnimationSlerp({0, 0, 0, 0}, {0, 0, 0, 0}, .5f);
    require(zero == Q{}, "Zero inputs retain original spherical arithmetic without identity fallback");
    const auto unit = retailAnimationNormalizeQuaternion({-0.f, 1, -0.f, 0});
    require(bits(unit[0]) == 0x80000000u && bits(unit[2]) == 0x80000000u,
            "Exact-unit normalizer preserves authored signed zeros");
    const float infinity = std::numeric_limits<float>::infinity();
    rejects([&] { retailAnimationSlerp(identity, {infinity, 0, 0, 1}, .5f); },
            "Reject nonfinite quaternion source");
    rejects([&] { retailAnimationSlerp(identity, halfTurn, infinity); },
            "Reject nonfinite quaternion factor");
    struct Pair {
        Q a, b;
    };
    const std::vector<Pair> pairs{{identity, halfTurn},
                                  {{1, 0, 0, 0}, {-1, 0, 0, 0}},
                                  {{0, 0, 0, 2}, {0, 0, 0, 3}},
                                  {{.2f, 0, 0, .1f}, {0, .2f, 0, .1f}},
                                  {{-0.f, 0, -0.f, 1}, {0, -0.f, 0, 1}},
                                  {{.6f, 0, 0, .8f}, {-.6f, 0, 0, -.8f}},
                                  {{0, 0, 0, 0}, {0, 0, 0, 0}},
                                  {identity, {.3122499f, 0, 0, .95f}},
                                  {identity, {.3122499f, 0, 0, std::nextafter(.95f, 0.f)}},
                                  {identity, {.3122499f, 0, 0, std::nextafter(.95f, 1.f)}},
                                  {{.125f, -.75f, .375f, .5f}, {-.25f, .125f, .75f, -.125f}},
                                  {{-1, 2, -3, 4}, {2, -3, 4, -5}},
                                  {{1.e30f, 1.e30f, 0, 0}, {1.e30f, -1.e30f, 0, 0}}};
    for (const auto &pair : pairs)
        for (float t : {-0.f, 0.f, .125f, .5f, .875f, 1.f, 1.25f}) {
            const auto result = retailAnimationSlerp(pair.a, pair.b, t);
            if (dump) {
                std::cout << "quaternion {\"first\":";
                words(pair.a);
                std::cout << ",\"second\":";
                words(pair.b);
                std::cout << ",\"amount\":" << bits(t) << ",\"result\":";
                words(result);
                std::cout << "}\n";
            }
        }
}
void sampleRow(const CityAnimationChannel &channel, int32_t time, RetailAnimationInterpolation interpolation,
               int32_t clip = -1) {
    const auto result = sampleRetailAnimationChannel(channel, time, interpolation);
    std::cout << "channel {\"clip\":" << clip << ",\"target\":" << std::quoted(channel.target)
              << ",\"type\":" << int(channel.transform) << ",\"time\":" << time
              << ",\"interpolation\":" << int(interpolation) << ",\"keys\":[";
    for (size_t i = 0; i < channel.keys.size(); ++i) {
        if (i)
            std::cout << ',';
        std::cout << "[" << channel.keys[i].timeMs << ',';
        words(channel.keys[i].value);
        std::cout << ']';
    }
    std::cout << "],\"result\":";
    words(result);
    std::cout << "}\n";
}
void typedCases(bool dump) {
    CityAnimationChannel channel{{},
                                 CityAnimationTransform::rotation,
                                 {{10, {-0.f, 0, 0, 2}}, {30, {0, 0, 0, 3}}, {60, {0, 0, 1, 0}}},
                                 "fixture-node"};
    const auto exact = sampleRetailAnimationChannel(channel, 10);
    require(exact[3] == 2 && bits(exact[0]) == 0x80000000u,
            "Exact key copies nonunit/signed-zero quaternion");
    require(sampleRetailAnimationChannel(channel, 9)[3] == 1,
            "Before-first linear quaternion follows slerp at clamped-zero weight");
    require(sampleRetailAnimationChannel(channel, 9, RetailAnimationInterpolation::step)[3] == 2,
            "Before-first step quaternion copies raw first key");
    require(sampleRetailAnimationChannel(channel, 100) == channel.keys.back().value,
            "After-last quaternion holds raw last key");
    auto one = channel;
    one.keys.resize(1);
    require(sampleRetailAnimationChannel(one, -5) == one.keys.front().value,
            "Single quaternion key is copied");
    RetailAnimationPoseTransform pose{{7, 8, 9}, {.1f, .2f, .3f, .4f}, {2, 3, 4}};
    CityAnimationChannel z{{},
                           CityAnimationTransform::translationZ,
                           {{10, {-0.f, 0, 0, 0}}, {30, {10, 0, 0, 0}}},
                           "fixture-node"};
    applyRetailAnimationChannel(pose, z, 10);
    require(pose.translation[0] == 7 && pose.translation[1] == 8 && bits(pose.translation[2]) == 0x80000000u,
            "Z track changes only local Z and copies exact signed zero");
    require(pose.rotation == RetailAnimationQuaternion{.1f, .2f, .3f, .4f} &&
                pose.scale == std::array<float, 3>{2, 3, 4},
            "Z application preserves other typed transform fields");
    CityAnimationChannel xyz{
        {}, CityAnimationTransform::translation, {{10, {1, 3, 5, 0}}, {30, {5, 7, 9, 0}}}, "fixture-node"};
    applyRetailAnimationChannel(pose, xyz, 20);
    require(pose.translation == std::array<float, 3>{3, 5, 7},
            "Translation uses original first+(second-first)*weight");
    auto scale = xyz;
    scale.transform = CityAnimationTransform::scale;
    applyRetailAnimationChannel(pose, scale, 20);
    require(pose.scale == std::array<float, 3>{3, 5, 7}, "Typed scale application");
    applyRetailAnimationChannel(pose, channel, 10);
    require(pose.rotation == channel.keys.front().value, "Typed quaternion application retains raw key");
    auto empty = channel;
    empty.keys.clear();
    rejects([&] { sampleRetailAnimationChannel(empty, 0); }, "Reject empty channel");
    auto invalid = channel;
    invalid.keys[1].timeMs = 10;
    rejects([&] { sampleRetailAnimationChannel(invalid, 10); }, "Reject duplicate key times");
    invalid = channel;
    invalid.keys[0].value[2] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { sampleRetailAnimationChannel(invalid, 10); }, "Reject nonfinite source quaternion key");
    rejects([&] { sampleRetailAnimationChannel(channel, 10, static_cast<RetailAnimationInterpolation>(99)); },
            "Reject unknown native selector");
    invalid = channel;
    invalid.transform = static_cast<CityAnimationTransform>(99);
    rejects([&] { sampleRetailAnimationChannel(invalid, 10); }, "Reject unknown native transform");
    if (dump)
        for (const auto &track : {channel, z, xyz, scale})
            for (auto interpolation :
                 {RetailAnimationInterpolation::step, RetailAnimationInterpolation::linear})
                for (int32_t time : {-100, 9, 10, 11, 20, 29, 30, 45, 60, 100})
                    sampleRow(track, time, interpolation);
}
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
void clockSeam() {
    RetailAnimationClock clock;
    uint32_t flags = 0;
    selectRetailAnimation(clock, {202, 1, true, false, false}, 500, 600, flags, false);
    clock.cursor = 90;
    clock.cachedRoot = {8, 1, 2};
    clock.rootHeight = 2;
    CityAnimationChannel root{{},
                              CityAnimationTransform::translation,
                              {{0, {0, 0, 0, 0}}, {100, {100, 100, 100, 0}}},
                              "Bip01-node"};
    struct Pose final : RetailAnimationPoseEvaluator {
        RetailAnimationClock &clock;
        std::vector<int32_t> calls;
        std::array<float, 3> local{};
        bool failHead = false;
        explicit Pose(RetailAnimationClock &c) : clock(c) {}
        std::array<float, 3> evaluate(int32_t relative, int32_t absolute) override {
            require(clock.cursor == 110, "Live clock advances before every whole-pose sample");
            require(absolute == 500 + relative, "Original selected absolute sample time");
            calls.push_back(relative);
            if (relative == 99) {
                require(clock.cachedRoot == std::array<float, 3>{8, 1, 2}, "Tail sees previous cache");
                local = {20, 3, 4};
            } else if (relative == 0) {
                require(clock.cachedRoot == std::array<float, 3>{8, 1, 2}, "Start still sees previous cache");
                local = {4, 5, 6};
            } else {
                require(relative == 10 && clock.cachedRoot == std::array<float, 3>{4, 5, 6},
                        "Head sees newly stored start cache");
                if (failHead)
                    throw std::runtime_error("Fixture head failure");
                local = {9, 10, 11};
            }
            return local;
        }

    } pose(clock);
    const auto step = advanceRetailAnimation(clock, 20, &root, identity, false, {},
                                             RetailAnimationInterpolation::linear, &pose);
    require(pose.calls == std::vector<int32_t>{99, 0, 10} && pose.local == std::array<float, 3>{9, 10, 11},
            "Whole-pose evaluator preserves selected calls and leaves root setter after body intents");
    require(step.sampledRoot == std::array<float, 3>{9, 10, 11} && clock.cachedRoot == step.sampledRoot,
            "Clock reads actual live root instead of independently replacing it from source track");
    near(step.scaledDisplacement[0], .017f, "Live root tail/head displacement");
    require(!step.bodyVelocity && !clock.rootMotion,
            "Disabled body motion still evaluates live pose and returns root setter intent");
    require(clock.rootHeight == pose.local[2] && clock.cachedRoot == pose.local,
            "Sample cache/height committed before caller final root setter");
    pose.local = step.visualRootTranslation;
    require(pose.local == std::array<float, 3>{}, "Caller consumes final zero-root setter intent");
    pose.calls.clear();
    advanceRetailAnimation(clock, 0, nullptr, identity, false, {}, RetailAnimationInterpolation::linear,
                           &pose);
    require(pose.calls.empty(), "Unbound root performs no pose callbacks");
    clock.cursor = 90;
    clock.cachedRoot = {8, 1, 2};
    clock.rootHeight = 2;
    pose.failHead = true;
    rejects(
        [&] {
            advanceRetailAnimation(clock, 20, &root, identity, false, {},
                                   RetailAnimationInterpolation::linear, &pose);
        },
        "Pose failure propagates without fabricated fallback");
    require(clock.cursor == 110 && clock.cachedRoot == std::array<float, 3>{4, 5, 6} && clock.rootHeight == 2,
            "Failed live evaluation is explicitly partial, not a resumable transaction");
    struct Single final : RetailAnimationPoseEvaluator {
        RetailAnimationClock &clock;
        float expectedCursor;
        int32_t expectedRelative;
        int calls = 0;
        Single(RetailAnimationClock &c, float cursor, int32_t relative)
            : clock(c), expectedCursor(cursor), expectedRelative(relative) {}
        std::array<float, 3> evaluate(int32_t relative, int32_t absolute) override {
            require(clock.cursor == expectedCursor && relative == expectedRelative &&
                        absolute == 500 + relative,
                    "Ordinary/nonloop/zero-advance pose sees original live time");
            ++calls;
            return {7, 8, 9};
        }
    };
    for (const auto mode : {0, 1, 2}) {
        clock = {};
        selectRetailAnimation(clock, {202, 1, mode != 0, true, false}, 500, 600, flags, false);
        clock.cursor = mode == 0 ? 90.f : 10.f;
        Single single(clock,
                      mode == 0   ? 110.f
                      : mode == 1 ? 10.f
                                  : 15.f,
                      mode == 0   ? 99
                      : mode == 1 ? 10
                                  : 15);
        const auto result =
            advanceRetailAnimation(clock,
                                   mode == 0   ? 20.f
                                   : mode == 1 ? 0.f
                                               : 5.f,
                                   &root, identity, true, {}, RetailAnimationInterpolation::linear, &single);
        require(single.calls == 1 && clock.cachedRoot == std::array<float, 3>{7, 8, 9} &&
                    clock.rootHeight == 9,
                "One whole-pose evaluation for nonloop tail, ordinary, and zero advance");
        require(bool(result.bodyVelocity) == (mode != 1),
                "Zero advance samples pose without writing body velocity");
    }
}
void retail(const std::filesystem::path &path, bool dump) {
    ResourceFileSystem files(path);
    const auto clips = loadCityAnimationCatalog(files, "./anim_ingame.bdae");
    require(clips.size() == 213, "Actual source catalogue identity");
    size_t pairs = 0, samples = 0;
    for (const auto &clip : clips) {
        require(clip.channels.size() == 24, "All actual original tracks retained in every clip");
        for (const auto &channel : clip.channels) {
            require(channel.keys.front().timeMs == 0 && channel.keys.back().timeMs >= clip.durationMs - 1,
                    "Actual clipped source channel retains original boundary brackets");
            for (int32_t t : {0, 1, int32_t(clip.durationMs / 2), int32_t(clip.durationMs - 1)}) {
                const auto value = sampleRetailAnimationChannel(channel, t);
                for (size_t c = 0; c < (channel.transform == CityAnimationTransform::rotation       ? 4u
                                        : channel.transform == CityAnimationTransform::translationZ ? 1u
                                                                                                    : 3u);
                     ++c)
                    require(std::isfinite(value[c]), "Actual retail typed pose samples are finite");
                ++samples;
            }
            ++pairs;
            if (dump && clip.source->index == 202)
                for (int32_t t : {0, 1, 100, int32_t(clip.durationMs / 2), int32_t(clip.durationMs - 1),
                                  int32_t(clip.durationMs)})
                    sampleRow(channel, t, RetailAnimationInterpolation::linear, 202);
        }
    }
    require(pairs == 5112, "Every supplied source clip/channel pair enumerated");
    std::cout << pairs << " original clip/channel pairs, " << samples
              << " typed samples validated;24 original source tracks.\n";
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        const bool syntheticDump = argc == 2 && std::wstring(argv[1]) == L"--oracle-dump";
        quaternionCases(syntheticDump);
        typedCases(syntheticDump);
        clockSeam();
        if (argc > 1 && !syntheticDump)
            retail(argv[1], argc > 2 && std::wstring(argv[2]) == L"--oracle-dump");
        std::cout << "Original quaternion slerp, typed channel and whole-pose clock seam checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
