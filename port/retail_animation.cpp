#include "retail_animation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
int32_t integerCursor(float value)
{
    // Avoid native C++ float-to-int undefined behavior outside the evidenced
    // finite int32 clock domain; do not silently cap or wrap animation time.
    if (!std::isfinite(value) || double(value) < std::numeric_limits<int32_t>::min() ||
        double(value) > std::numeric_limits<int32_t>::max())
        throw std::runtime_error("Animation cursor exceeds the supported int32 timeline.");
    return static_cast<int32_t>(value);
}

int32_t clipLength(const RetailAnimationClock& clock)
{
    const int64_t length = int64_t(clock.endMs) - clock.startMs;
    if (length <= 0 || length > std::numeric_limits<int32_t>::max())
        throw std::runtime_error("Invalid original animation clip interval.");
    return static_cast<int32_t>(length);
}
}

bool retailAnimationCanBeInterrupted(int32_t animation)
{
    return !(animation >= 1 && animation <= 23) && animation != 86 && animation != 164 && animation != 175;
}

RetailAnimationSelectionResult selectRetailAnimation(RetailAnimationClock& clock,
    const RetailAnimationRequest& request, int32_t startMs, int32_t endMs,
    uint32_t& movementFlags, bool inCinematic, int32_t weaponType)
{
    using Selection = RetailAnimationSelection;
    if (movementFlags & 0x800u) return {Selection::rejected};
    if (request.animation == -1) {
        clock.animation = -1;
        clock.startMs = clock.endMs = 0;
        clock.cursor = clock.rate = 0;
        clock.loop = clock.rootMotion = clock.directionalMotion = false;
        return {Selection::cleared};
    }
    if (request.animation == clock.animation) return {Selection::repeated};
    if (!clock.loop && clock.animation != -1 && clock.cursor < float(clipLength(clock)) &&
        !retailAnimationCanBeInterrupted(clock.animation) && !inCinematic) {
        if (retailAnimationCanBeInterrupted(request.animation)) return {Selection::rejected};
        // The original base setNextAnimation ignores the other request fields.
        clock.nextAnimation = request.animation;
        return {Selection::deferred};
    }
    if (request.animation < 0 || !std::isfinite(request.speed) || request.speed < 0 || clock.speedMultiplier < 0)
        throw std::runtime_error("Unsupported negative or nonfinite animation request.");
    RetailAnimationClock accepted = clock;
    accepted.startMs = startMs;
    accepted.endMs = endMs;
    clipLength(accepted);
    accepted.rate = float(clock.speedMultiplier) * request.speed;
    if (!std::isfinite(accepted.rate)) throw std::runtime_error("Animation rate overflow.");
    const bool reload = clock.animation == 67 && weaponType == 7;
    accepted.animation = request.animation;
    accepted.nextAnimation = -1;
    accepted.cursor = 0;
    accepted.loop = request.loop;
    accepted.rootMotion = request.rootMotion;
    accepted.directionalMotion = request.directionalMotion;
    clock = accepted; // Root cache and retained height are intentionally unchanged.
    if (request.animation == 164) movementFlags |= 0x80000000u;
    return {Selection::accepted, reload};
}

bool retailAnimationLastFrame(const RetailAnimationClock& clock, float advance)
{
    const auto next = integerCursor(clock.cursor + advance);
    if (!clock.loop && int64_t(next) >= int64_t(clock.endMs) - clock.startMs) return true;
    if (clock.animation == -1) return false;
    const auto length = clipLength(clock);
    return integerCursor(clock.cursor) % length > next % length;
}

std::array<float, 3> sampleRetailAnimationRoot(const CityAnimationChannel& channel,
    int32_t relativeMs, RetailAnimationInterpolation interpolation)
{
    if (channel.transform != CityAnimationTransform::translation || channel.keys.empty())
        throw std::runtime_error("An original local-translation root channel is required.");
    if (interpolation != RetailAnimationInterpolation::step && interpolation != RetailAnimationInterpolation::linear)
        throw std::runtime_error("Unsupported original root interpolation selector.");
    for (size_t i = 0; i < channel.keys.size(); ++i) {
        const auto& key = channel.keys[i];
        if (key.timeMs > uint32_t(std::numeric_limits<int32_t>::max()) ||
            (i && key.timeMs <= channel.keys[i-1].timeMs))
            throw std::runtime_error("Root keys must retain the decoder's strictly increasing int32 times.");
        for (size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(key.value[axis])) throw std::runtime_error("Nonfinite root key value.");
    }
    const auto next = std::upper_bound(channel.keys.begin(), channel.keys.end(), relativeMs,
        [](int32_t ms, const CityAnimationKey& key) { return int64_t(ms) < key.timeMs; });
    const auto& first = next == channel.keys.begin() ? channel.keys.front() : *std::prev(next);
    std::array<float, 3> value{first.value[0], first.value[1], first.value[2]};
    // Original equality dispatches a raw key copy, not lerp at weight zero/one.
    if (int64_t(relativeMs) == first.timeMs || channel.keys.size() == 1 ||
        next == channel.keys.end() || interpolation == RetailAnimationInterpolation::step)
        return value;
    const auto& second = next == channel.keys.begin() ? channel.keys[1] : *next;
    const float amount = std::clamp(float(int64_t(relativeMs) - first.timeMs) /
        float(uint64_t(second.timeMs) - first.timeMs), 0.f, 1.f);
    for (size_t axis = 0; axis < value.size(); ++axis)
        value[axis] += (second.value[axis] - value[axis]) * amount;
    return value;
}

RetailAnimationStep advanceRetailAnimation(RetailAnimationClock& clock, float elapsedMs,
    const CityAnimationChannel* root, const std::array<float, 16>& absoluteMatrix,
    bool bodyPresent, std::optional<std::array<float, 2>> direction,
    RetailAnimationInterpolation interpolation)
{
    RetailAnimationStep result;
    if (!root || clock.animation == -1) return result;
    if (!std::isfinite(elapsedMs) || elapsedMs < 0 || !std::isfinite(clock.rate) || clock.rate < 0 || clock.cursor < 0)
        throw std::runtime_error("Unsupported negative or nonfinite animation clock.");
    const auto length = clipLength(clock);
    if (root->keys.empty() || root->keys.front().timeMs != 0 || root->keys.back().timeMs < uint32_t(length - 1))
        throw std::runtime_error("Root track does not retain the clip boundary samples.");
    const float advance = elapsedMs * clock.rate;
    const bool writeVelocity = advance > 0 && clock.rootMotion && bodyPresent;
    const float denominator = writeVelocity ? elapsedMs * 0.0001f : 0;
    if (writeVelocity && (denominator == 0 || !std::isfinite(denominator)))
        throw std::runtime_error("Unsupported animation velocity denominator.");
    if (advance > 0 && clock.rootMotion && bodyPresent && clock.directionalMotion && !direction)
        throw std::runtime_error("Directional root motion requires the original stored direction.");
    result.lastFrame = retailAnimationLastFrame(clock, advance);
    const float cursor = clock.cursor + advance;
    const auto integer = integerCursor(cursor);
    const auto evaluate = [&](int32_t relative) {
        const int64_t absolute = int64_t(clock.startMs) + relative;
        if (absolute < std::numeric_limits<int32_t>::min() || absolute > std::numeric_limits<int32_t>::max())
            throw std::runtime_error("Animation sample exceeds the original timeline.");
        result.relativeMs = relative;
        result.absoluteMs = static_cast<int32_t>(absolute);
        result.sampledRoot = sampleRetailAnimationRoot(*root, relative, interpolation);
        result.evaluated = true;
    };
    const auto scaledDifference = [&] {
        std::array<float, 3> difference;
        for (size_t axis = 0; axis < clock.cachedRoot.size(); ++axis)
            difference[axis] = (result.sampledRoot[axis] - clock.cachedRoot[axis]) * 0.001f;
        return difference;
    };
    const auto accumulate = [&] {
        const auto difference = scaledDifference();
        for (size_t axis = 0; axis < difference.size(); ++axis)
            result.scaledDisplacement[axis] += difference[axis];
    };
    if (result.lastFrame) {
        evaluate(length - 1);
        // Original tail stores its first scaled difference directly. Adding it
        // to the ordinary path's initial +0 would discard a negative zero.
        result.scaledDisplacement = scaledDifference();
        if (clock.loop) {
            evaluate(0);
            clock.cachedRoot = result.sampledRoot;
            evaluate(integer % length);
            accumulate();
        }
    } else {
        evaluate(integer % length);
        accumulate();
    }
    if (writeVelocity) {
        std::array<float, 2> velocity{};
        if (clock.directionalMotion) {
            const auto& d = result.scaledDisplacement;
            const float magnitude = static_cast<float>(std::sqrt(double(d[0]*d[0] + d[1]*d[1] + d[2]*d[2])));
            const float inverse = 1.f / denominator;
            for (size_t axis = 0; axis < velocity.size(); ++axis)
                velocity[axis] = magnitude * direction->at(axis) * inverse;
        } else {
            const auto& d = result.scaledDisplacement;
            const float inverse = 1.f / denominator;
            result.wakeBody = d[0] * inverse != 0 || d[1] * inverse != 0;
            for (size_t axis = 0; axis < velocity.size(); ++axis)
                velocity[axis] = (d[0]*absoluteMatrix[axis] + d[1]*absoluteMatrix[4 + axis] +
                                  d[2]*absoluteMatrix[8 + axis]) / denominator;
        }
        result.bodyVelocity = velocity;
        if (clock.directionalMotion) result.wakeBody = velocity[0] != 0 || velocity[1] != 0;
    }
    clock.cursor = cursor;
    clock.cachedRoot = result.sampledRoot;
    clock.rootHeight = result.sampledRoot[2];
    // Original updateAnimation sets the animated root's local XYZ to zero even
    // when root-motion velocity is disabled. The caller must apply this result.
    return result;
}
