#pragma once

#include "city_assets.h"

#include <array>
#include <cstdint>
#include <optional>

enum class RetailAnimationInterpolation { step, linear };

using RetailAnimationQuaternion = std::array<float, 4>;
// Original quaternion normalization and slerp: no implicit normalization of
// authored keys, shortest-path sign change applies to FIRST, and only the
// near-linear slerp branch normalizes its result.
RetailAnimationQuaternion retailAnimationNormalizeQuaternion(RetailAnimationQuaternion);
RetailAnimationQuaternion retailAnimationSlerp(RetailAnimationQuaternion first,
    RetailAnimationQuaternion second, float amount);

struct RetailAnimationPoseTransform {
    std::array<float, 3> translation{};
    RetailAnimationQuaternion rotation{0, 0, 0, 1};
    std::array<float, 3> scale{1, 1, 1};
};
// Times are relative to the decoded channel's origin. The caller must retain
// actual bracketing source keys and supply the original interpolation selector.
// A rotation's nonzero interpolation is original quaternion slerp.
std::array<float, 4> sampleRetailAnimationChannel(const CityAnimationChannel&,
    int32_t relativeMs, RetailAnimationInterpolation = RetailAnimationInterpolation::linear);
void applyRetailAnimationChannel(RetailAnimationPoseTransform&, const CityAnimationChannel&,
    int32_t relativeMs, RetailAnimationInterpolation = RetailAnimationInterpolation::linear);

// Genuine live-pose operations, supplied by an owning actor runtime. evaluate
// applies the whole bound animator at the selected time and reads its live root.
// The caller applies body/wake intents before the final visualRootTranslation
// setter, as in the existing controller. No clock or body is owned here.
class RetailAnimationPoseEvaluator {
  public:
    virtual ~RetailAnimationPoseEvaluator() = default;
    virtual std::array<float, 3> evaluate(int32_t relativeMs, int32_t absoluteMs) = 0;
};

// Native state, independent of the original ARM object layout. Times are integer
// milliseconds in the original timeline; cursor/rate deliberately remain float.
struct RetailAnimationClock {
    int32_t animation = -1;
    int32_t startMs = 0, endMs = 0;
    float cursor = 0, rate = 0;
    int32_t speedMultiplier = 1;
    bool loop = false, rootMotion = false, directionalMotion = false;
    int32_t nextAnimation = -1;
    std::array<float, 3> cachedRoot{};
    float rootHeight = 0;
};

struct RetailAnimationRequest {
    int32_t animation;
    float speed;
    bool loop, rootMotion, directionalMotion;
};

enum class RetailAnimationSelection { rejected, repeated, cleared, deferred, accepted };
struct RetailAnimationSelectionResult {
    RetailAnimationSelection selection;
    // Character calls Weapon::reloadRocket on this accepted transition. The
    // controller must consume this intent; this clock does not own a weapon.
    bool reloadRocket = false;
};

bool retailAnimationCanBeInterrupted(int32_t animation);
// Original guards only. Clear/defer apply their original state mutations;
// accepted leaves clock/flags untouched and does no interval/rate validation.
// A host must perform required live reload before accepted-transition commit.
RetailAnimationSelection guardRetailAnimationSelection(RetailAnimationClock&,
    const RetailAnimationRequest&, uint32_t& movementFlags, bool inCinematic);
RetailAnimationSelectionResult selectRetailAnimation(RetailAnimationClock& clock,
    const RetailAnimationRequest& request, int32_t startMs, int32_t endMs,
    uint32_t& movementFlags, bool inCinematic, int32_t weaponType = -1);
// Accepted branch only, after the host has evaluated guards and performed any
// required live reload callback. Does not rerun guards or reload. Reads the
// CURRENT speed multiplier/cache/flags so callback effects survive the commit.
void commitAcceptedRetailAnimation(RetailAnimationClock& clock,
    const RetailAnimationRequest& request, int32_t startMs, int32_t endMs,
    uint32_t& movementFlags);
bool retailAnimationLastFrame(const RetailAnimationClock& clock, float advance);

// The original Character root is a local translation, not a sampled skeleton.
// The supplied channel must retain the keys bracketing the requested interval.
// As in the native BRES decoder, duplicate/nonincreasing times are rejected.
std::array<float, 3> sampleRetailAnimationRoot(const CityAnimationChannel& channel,
    int32_t relativeMs, RetailAnimationInterpolation interpolation);

struct RetailAnimationStep {
    bool lastFrame = true;
    bool evaluated = false;
    int32_t relativeMs = 0, absoluteMs = 0;
    std::array<float, 3> sampledRoot{};
    // Original 0.001f scaling is applied to each sampled segment before summing.
    std::array<float, 3> scaledDisplacement{};
    std::array<float, 3> visualRootTranslation{};
    std::optional<std::array<float, 2>> bodyVelocity;
    bool wakeBody = false;
};

// absoluteMatrix is Irrlicht's column-major actor absolute matrix. Its
// translation is deliberately excluded from displacement transformation.
// No bodyVelocity means preserve existing body velocity, not assign zero.
RetailAnimationStep advanceRetailAnimation(RetailAnimationClock& clock, float elapsedMs,
    const CityAnimationChannel* root, const std::array<float, 16>& absoluteMatrix,
    bool bodyPresent, std::optional<std::array<float, 2>> direction = {},
    RetailAnimationInterpolation interpolation = RetailAnimationInterpolation::linear,
    RetailAnimationPoseEvaluator* pose = nullptr);
