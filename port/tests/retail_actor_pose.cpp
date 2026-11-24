#include "retail_actor_pose.h"
#include "filesystem.h"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
uint32_t bits(float x) { uint32_t value; std::memcpy(&value, &x, 4); return value; }
std::shared_ptr<RetailActorScene> scene()
{
    auto source = std::make_shared<RetailActorSceneSource>();
    source->model.path = "./pose.bdae";
    CityNode node{}; node.name = "Bip01-node"; node.parent = -1;
    node.rotation = {0, 0, 0, 1}; node.scale = {1, 1, 1}; node.translation = {7, 8, 9};
    source->model.nodes.push_back(node);
    node.name = "child"; node.parent = 0; source->model.nodes.push_back(node);
    source->model.visualScenes.push_back({"visual", "visual", 0, 2});
    return std::make_shared<RetailActorScene>(source);
}
CityAnimationLibraryTrack track(std::string target, CityAnimationTransform transform,
                                std::array<float, 4> first, std::array<float, 4> last)
{
    return {0, "source", std::move(target), transform, 1, 0, 1, {{0, first}, {100, last}}};
}
std::shared_ptr<CityAnimationLibrary> library(bool common = true)
{
    auto source = std::make_shared<CityAnimationLibrary>(); source->path = "./full.bdae"; source->commonCacheMetadata = common ? 5 : 0;
    source->tracks = {
        track("missing", CityAnimationTransform::translation, {}, {100, 0, 0, 0}),
        track("Bip01-node", CityAnimationTransform::translation, {}, {100, 0, 0, 0}),
        track("Bip01-node", CityAnimationTransform::rotation, {2, 0, 0, 0}, {0, 0, 0, -1}),
        track("child", CityAnimationTransform::translationZ, {3, 0, 0, 0}, {13, 0, 0, 0}),
        track("child", CityAnimationTransform::scale, {1, 2, 3, 0}, {3, 4, 5, 0})};
    source->tracks.back().keys.insert(source->tracks.back().keys.begin() + 1, {40, {9, 8, 7, 0}});
    for (size_t i = 0; i < source->tracks.size(); ++i) source->tracks[i].sourceIndex = uint32_t(i);
    return source;
}
void controls()
{
    for (bool common : {false, true}) {
        auto owner = scene(); auto pose = RetailActorPose::create(library(common)); pose->bind(owner);
        auto* root = owner->root().findUid("Bip01-node"); auto* child = owner->root().findUid("child");
        require(pose->bindingCount() == 5 && !pose->target(0) && pose->target(1) == root &&
                pose->target(2) == root && root->boundAnimatorCount() == 1 && child->boundAnimatorCount() == 1 &&
                root->animatorCount() == 0 && child->animatorCount() == 0 &&
                (root->transformFlags() & 7) == 6 && (child->transformFlags() & 7) == 5,
                "Source-order NULL slots, persistent typed masks and deduplicated bound-only retention");
        const auto stale = root->absolute(); pose->apply(50);
        require(root->position()[0] == 50 && child->position()[0] == 7 && child->position()[2] == 8 &&
                root->absolute() == stale && pose->keyIndex(common ? 0 : 4) == 1 && pose->keyIndex(common ? 4 : 0) == 0,
                "Typed lane writes leave absolute cache stale; common scratch includes NULL first slot");
        pose->setMode(1); pose->apply(50);
        require(root->position()[0] == 0 && child->position()[2] == 3, "Only original mode1 disables interpolation");
        pose->setMode(2); pose->apply(50); require(root->position()[0] == 50, "Other modes interpolate");
        const auto p = root->position();
        const auto q = root->rotation();
        const auto scale = child->scale();
        for (float weight : {0.f, .5f, 2.f, std::numeric_limits<float>::quiet_NaN()}) {
            pose->setWeight(weight); pose->apply(80);
            require(root->position() == p && root->rotation() == q && child->scale() == scale,
                    "Original nonunit weighted apply changes scratch without writing typed poses");
        }
        pose->setWeight(1); root->setPosition({9, 8, 7}); pose->animateNode(*root, 50);
        require(root->position()[0] == 50 && root->animatorCount() == 0,
                "Original animateNode forwards its time without inventing an ordinary-list attachment");
        pose->apply(100); require(bits(root->rotation()[0]) == bits(2.f) && root->position()[0] == 0,
                               "Library modulo wraps and exact quaternion key remains unnormalized");
        pose->apply(80);
        const auto retainedIndex = pose->keyIndex(common ? 0 : 4);
        root->removeBoundAnimator(*pose);
        require(!pose->target(1) && !pose->target(2) && pose->target(3) == child && pose->boundRoot() == &owner->root(),
                "Unbind clears all matching source UIDs without clearing root or unrelated slots");
        pose->bind(owner); require(!pose->target(1), "Same-root onBind is a no-op after unbind");
        pose->forceBind(); require(pose->target(1) == root && root->boundAnimatorCount() == 1, "Explicit forceBind repairs targets");
        require(pose->keyIndex(common ? 0 : 4) == retainedIndex, "Target clearing/rebinding never resets key scratch");
        auto next = scene(); pose->bind(next); auto* nextRoot = next->root().findUid("Bip01-node");
        require(pose->target(1) == nextRoot && root->boundAnimatorCount() == 1, "Rebind retains old node registration");
        root->removeBoundAnimator(*pose);
        require(!pose->target(1) && !pose->target(2) && pose->boundRoot() == &next->root(),
                "Old-root callback UID-clears matching new targets exactly as original");
        pose->forceBind(); owner.reset(); // Old child destruction clears matching new child slots.
        require(pose->target(1) == nextRoot && !pose->target(3), "Old-owner teardown preserves literal per-UID callback effects");
        pose->forceBind(); std::weak_ptr<RetailActorScene> weakOwner = next; next.reset();
        require(weakOwner.expired() && !pose->target(1) && !pose->target(3), "Weak context breaks ownership cycle and all target pointers invalidate");
        bool rejected = false;
        try { pose->evaluate(0, 50); } catch (const std::logic_error&) { rejected = true; }
        require(rejected, "Expired owner cannot be silently sampled or dereferenced");
        rejected = false;
        try { pose->bind(scene()); } catch (const std::logic_error&) { rejected = true; }
        require(rejected, "Expired root identity cannot alias recycled scene memory during rebind");
    }
    auto rounded = library(); rounded->tracks.resize(1); rounded->tracks[0].keys = {{16777217, {}}, {16777219, {1, 2, 3, 0}}};
    require(RetailActorPose::create(rounded)->periodMs() == 4, "Original float endpoint conversion precedes span subtraction");
    auto source = library(); auto& t = source->tracks[1]; t.keys = {{10, {1, 2, 3, 0}}, {20, {2, 2, 3, 0}}, {20, {3, 2, 3, 0}}, {30, {4, 2, 3, 0}}};
    int32_t index = -1;
    require(sampleRetailAnimationLibraryTrack(t, 20, RetailAnimationInterpolation::linear, &index)[0] == 3 && index == 2,
            "Equal source times choose the original rightmost key");
    require(sampleRetailAnimationLibraryTrack(t, std::numeric_limits<int32_t>::min(), RetailAnimationInterpolation::linear, &index)[0] == 2 && index == 0,
            "Original signed subtraction wraps before interpolation conversion");
    std::weak_ptr<RetailActorPose> retained;
    auto lifetimeOwner = scene();
    {
        auto pose = RetailActorPose::create(library()); pose->bind(lifetimeOwner); retained = pose;
    }
    require(!retained.expired(), "Actual target nodes keep the animator alive after its creator releases it");
    lifetimeOwner.reset();
    require(retained.expired(), "Scene destruction releases the last target retention without an ownership cycle");
    auto stepSource = library(); stepSource->tracks[1].interpolationSelector = 0;
    auto stepOwner = scene(); auto stepPose = RetailActorPose::create(stepSource); stepPose->bind(stepOwner);
    stepPose->apply(50);
    require(stepOwner->root().findUid("Bip01-node")->position()[0] == 0 &&
            stepOwner->root().findUid("child")->position()[2] == 8,
            "A zero track selector suppresses only that track's interpolation");
}
void clockComposition()
{
    auto source = library(); source->tracks.resize(2); auto owner = scene(); auto pose = RetailActorPose::create(source); pose->bind(owner);
    auto* root = owner->root().findUid("Bip01-node"); RetailAnimationClock clock; uint32_t flags = 0;
    selectRetailAnimation(clock, {202, 1, true, true, false}, 10, 90, flags, false);
    clock.cursor = 70; clock.cachedRoot = {80, 0, 0};
    CityAnimationChannel clipped{{}, CityAnimationTransform::translation, {{0, {}}, {80, {80, 0, 0, 0}}}, "Bip01-node"};
    constexpr std::array<float, 16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    auto step = advanceRetailAnimation(clock, 20, &clipped, identity, true, {}, RetailAnimationInterpolation::linear, pose.get());
    require(step.evaluated && step.lastFrame && step.absoluteMs == 20 && step.sampledRoot[0] == 20 &&
            clock.cursor == 90 && clock.cachedRoot[0] == 20 && root->position()[0] == 20 && step.bodyVelocity && step.wakeBody,
            "Clock tail/start/head callbacks use real full-pose samples before host body/wake/root strip");
    root->setPosition(step.visualRootTranslation);
    require(root->position() == std::array<float, 3>{} && pose->evaluate(0, 21)[0] == 21,
            "Host strips the live root after body intents without poisoning subsequent source sampling");
}
void samplerControls(bool dump)
{
    const std::array<CityAnimationTransform, 4> transforms{CityAnimationTransform::translation,
        CityAnimationTransform::translationZ, CityAnimationTransform::rotation, CityAnimationTransform::scale};
    const std::array<std::array<int32_t, 4>, 3> domains{{{{10, 20, 20, 30}}, {{-100, -50, 0, 100}},
        {{std::numeric_limits<int32_t>::min(), -1, 0, std::numeric_limits<int32_t>::max()}}}};
    const std::array<std::array<float, 4>, 4> values{{{{1, 2, 3, 4}}, {{-1, .5f, 2, 1}},
        {{0, 0, 0, -1}}, {{.3f, .4f, .5f, .6f}}}};
    for (size_t kind = 0; kind < transforms.size(); ++kind)
        for (size_t domain = 0; domain < domains.size(); ++domain) {
            CityAnimationLibraryTrack track{0, "sample", "Bip01-node", transforms[kind], 1, 0, 1, {}};
            const auto width = kind == 1 ? 1u : kind == 2 ? 4u : 3u;
            for (size_t i = 0; i < 4; ++i) {
                auto value = values[i]; for (size_t c = width; c < 4; ++c) value[c] = 0;
                track.keys.push_back({domains[domain][i], value});
            }
            for (int32_t time : {std::numeric_limits<int32_t>::min(), -101, -1, 0, 9, 10, 15, 20, 21, 30, 100,
                                 std::numeric_limits<int32_t>::max()})
                for (auto interpolation : {RetailAnimationInterpolation::step, RetailAnimationInterpolation::linear}) {
                    int32_t index = -1;
                    const auto value = sampleRetailAnimationLibraryTrack(track, time, interpolation, &index);
                    require(index >= 0 && index < 4, "Original sampler produces a valid signed scratch index");
                    if (dump) {
                        std::cout << "sampler " << kind << ' ' << domain << ' ' << time << ' '
                                  << (interpolation == RetailAnimationInterpolation::linear) << ' ' << index;
                        for (float v : value) std::cout << ' ' << bits(v);
                        std::cout << '\n';
                    }
                }
        }
}
void retail(const char* directory, bool dump)
{
    ResourceFileSystem assets{std::filesystem::path(directory)};
    const auto full = std::make_shared<CityAnimationLibrary>(loadCityAnimationLibrary(assets, "./anim_ingame.bdae"));
    struct Profile { uint32_t time; int32_t mode; float weight; };
    std::vector<Profile> profiles;
    for (uint32_t time : {0u, 1u, 100u, 800u, 1000u, 12345u, 259065u, 259066u, 259067u, 0xffffffffu})
        profiles.push_back({time, 0, 1});
    for (const auto& clip : loadCityAnimationCatalog(assets, "./anim_ingame.bdae")) {
        const auto& range = *clip.source;
        profiles.push_back({uint32_t(range.startMs), 0, 1});
        profiles.push_back({uint32_t(range.endMs - 1), 0, 1});
        profiles.push_back({uint32_t(range.startMs + (range.endMs - range.startMs) / 2), 0, 1});
    }
    profiles.push_back({12345, 1, 1}); profiles.push_back({12345, 2, 1});
    for (float weight : {0.f, .5f, 2.f, std::numeric_limits<float>::quiet_NaN()}) profiles.push_back({12345, 0, weight});
    for (const char* model : {"char_pthug.bdae", "char_pthug_jail.bdae"}) {
        const auto decoded = loadRetailActorSceneSource(assets, model);
        auto owner = std::make_shared<RetailActorScene>(decoded), other = std::make_shared<RetailActorScene>(decoded);
        auto pose = RetailActorPose::create(full), independent = RetailActorPose::create(full); pose->bind(owner); independent->bind(other);
        require(pose->periodMs() == 259066 && pose->bindingCount() == 24, "Actual global source period and slots");
        for (size_t i = 0; i < 24; ++i)
            require(pose->target(i) == owner->root().findUid(full->tracks[i].target) && pose->target(i) &&
                    pose->target(i)->boundAnimatorCount() == 1 && pose->target(i)->animatorCount() == 0,
                    "Every actual source target resolves into the real retained graph");
        size_t profileIndex = 0;
        for (const auto& profile : profiles) {
            const auto time = profile.time;
            pose->setMode(profile.mode); pose->setWeight(profile.weight);
            pose->apply(time);
            if (dump) {
                for (const auto& node : owner->nodes()) {
                    std::cout << "pose " << model << ' ' << profileIndex << ' ' << time << ' ' << profile.mode << ' '
                              << bits(profile.weight) << ' ' << node->index() << ' ' << node->transformFlags();
                    for (float v : node->position()) std::cout << ' ' << bits(v);
                    for (float v : node->rotation()) std::cout << ' ' << bits(v);
                    for (float v : node->scale()) std::cout << ' ' << bits(v);
                    std::cout << '\n';
                }
                std::cout << "scratch " << model << ' ' << profileIndex;
                for (size_t i = 0; i < 24; ++i) std::cout << ' ' << pose->keyIndex(i);
                std::cout << '\n';
            }
            for (size_t i = 0; i < decoded->model.nodes.size(); ++i) {
                auto* untouched = other->root().findUid(decoded->model.nodes[i].name);
                require(untouched->position() == decoded->model.nodes[i].translation,
                        "Shared source does not leak live pose between roots");
            }
            owner->root().findUid("Bip01-node")->setPosition({});
            ++profileIndex;
        }
        std::cout << model << ": all24 retained tracks, independent roots and" << profiles.size() << " complete pose profiles passed\n";
    }
}
}
int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == "--sampler") {
            samplerControls(true);
            return 0;
        }
        controls(); clockComposition();
        samplerControls(argc > 2 && std::string(argv[2]) == "--dump");
        if (argc > 1) retail(argv[1], argc > 2 && std::string(argv[2]) == "--dump");
        std::cout << "Retained actor pose tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
