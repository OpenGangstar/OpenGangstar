#include "retail_animation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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

size_t components(CityAnimationTransform transform)
{
    switch (transform) {
    case CityAnimationTransform::translationZ: return 1;
    case CityAnimationTransform::translation:
    case CityAnimationTransform::scale: return 3;
    case CityAnimationTransform::rotation: return 4;
    }
    throw std::runtime_error("Unsupported original animation channel transform.");
}

void validateChannel(const CityAnimationChannel& channel, RetailAnimationInterpolation interpolation)
{
    if (channel.keys.empty()) throw std::runtime_error("An original animation channel requires keys.");
    if (interpolation != RetailAnimationInterpolation::step && interpolation != RetailAnimationInterpolation::linear)
        throw std::runtime_error("Unsupported original interpolation selector.");
    const auto count = components(channel.transform);
    for (size_t i = 0; i < channel.keys.size(); ++i) {
        const auto& key = channel.keys[i];
        if (key.timeMs > uint32_t(std::numeric_limits<int32_t>::max()) ||
            (i && key.timeMs <= channel.keys[i-1].timeMs))
            throw std::runtime_error("Animation keys require strictly increasing int32 times.");
        for (size_t axis = 0; axis < count; ++axis)
            if (!std::isfinite(key.value[axis])) throw std::runtime_error("Nonfinite animation key value.");
    }
}

template<class Key> struct ChannelBracket {
    const Key& first;
    const Key* second;
    float amount;
};

template<class Keys> size_t keyIndex(const Keys& keys, int32_t time)
{
    const auto next = std::upper_bound(keys.begin(), keys.end(), time,
        [](int32_t ms, const auto& key) { return int64_t(ms) < int64_t(key.timeMs); });
    return next == keys.begin() ? 0 : size_t(std::prev(next) - keys.begin());
}

template<class Keys, class Difference>
auto bracketKeys(const Keys& keys, int32_t time, RetailAnimationInterpolation interpolation,
                 Difference difference)
{
    const auto index = keyIndex(keys, time);
    const auto& first = keys[index];
    using Key = typename Keys::value_type;
    if (int64_t(time) == int64_t(first.timeMs) || index + 1 == keys.size() ||
        interpolation == RetailAnimationInterpolation::step)
        return ChannelBracket<Key>{first, nullptr, 0};
    const auto& second = keys[index + 1];
    const float amount = std::clamp(float(difference(time, first.timeMs)) /
        float(difference(second.timeMs, first.timeMs)), 0.f, 1.f);
    return ChannelBracket<Key>{first, &second, amount};
}

auto bracket(const CityAnimationChannel& channel, int32_t relativeMs,
             RetailAnimationInterpolation interpolation)
{
    validateChannel(channel, interpolation);
    return bracketKeys(channel.keys, relativeMs, interpolation,
        [](auto a, auto b) { return int64_t(a) - int64_t(b); });
}

void validateLibraryTrack(const CityAnimationLibraryTrack& track)
{
    if (track.keys.empty() || track.keys.size() > size_t(std::numeric_limits<int32_t>::max()))
        throw std::runtime_error("An original library track requires int32-indexable keys.");
    const auto count = components(track.transform);
    for (size_t i = 0; i < track.keys.size(); ++i) {
        if (i && track.keys[i].timeMs < track.keys[i-1].timeMs)
            throw std::runtime_error("Original library keys require nondecreasing signed times.");
        for (size_t axis = 0; axis < count; ++axis)
            if (!std::isfinite(track.keys[i].value[axis]))
                throw std::runtime_error("Nonfinite original library key value.");
    }
}

template<class Bracket>
std::array<float, 4> sampleBracket(const Bracket& selected, CityAnimationTransform transform)
{
    auto value = selected.first.value;
    if (!selected.second) return value;
    if (transform == CityAnimationTransform::rotation)
        return retailAnimationSlerp(value, selected.second->value, selected.amount);
    for (size_t axis = 0; axis < components(transform); ++axis)
        value[axis] += (selected.second->value[axis] - value[axis]) * selected.amount;
    return value;
}

}

RetailAnimationQuaternion retailAnimationNormalizeQuaternion(RetailAnimationQuaternion q)
{
    const float length = ((q[0] * q[0] + q[1] * q[1]) + q[2] * q[2]) + q[3] * q[3];
    if (length != 1.f) {
        const float inverse = 1.f / std::sqrt(length);
        for (float& value : q) value *= inverse;
    }
    return q;
}

RetailAnimationQuaternion retailAnimationSlerp(RetailAnimationQuaternion first,
    RetailAnimationQuaternion second, float amount)
{
    for (size_t i = 0; i < first.size(); ++i)
        if (!std::isfinite(first[i]) || !std::isfinite(second[i]))
            throw std::runtime_error("Nonfinite original quaternion input.");
    if (!std::isfinite(amount)) throw std::runtime_error("Nonfinite quaternion interpolation amount.");
    float angle = ((second[0] * first[0] + second[1] * first[1]) + second[2] * first[2]) +
                  second[3] * first[3];
    if (angle < 0.f) {
        for (float& value : first) value = -value;
        angle = -angle;
    }
    float firstWeight, secondWeight;
    bool normalize = false;
    if (angle + 1.f > 0.05f) {
        if (1.f - angle >= 0.05f) {
            const float theta = std::acos(angle);
            const float inverse = 1.f / std::sin(theta);
            firstWeight = inverse * std::sin(theta * (1.f - amount));
            secondWeight = inverse * std::sin(theta * amount);
        } else {
            firstWeight = 1.f - amount;
            secondWeight = amount;
            normalize = true;
        }
    } else {
        // Literal original opposite branch, reachable for unordered intermediate
        // dot products as well. Do not manufacture an identity quaternion.
        second = {-first[1], first[0], -first[3], first[2]};
        firstWeight = std::sin((0.5f - amount) * 3.1415927410125732421875f);
        secondWeight = std::sin(amount * 3.1415927410125732421875f);
    }
    RetailAnimationQuaternion result;
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = firstWeight * first[i] + secondWeight * second[i];
    return normalize ? retailAnimationNormalizeQuaternion(result) : result;
}

std::array<float, 4> sampleRetailAnimationChannel(const CityAnimationChannel& channel,
    int32_t relativeMs, RetailAnimationInterpolation interpolation)
{
    return sampleBracket(bracket(channel, relativeMs, interpolation), channel.transform);
}

int32_t selectRetailAnimationLibraryKey(const CityAnimationLibraryTrack& track, int32_t time)
{
    validateLibraryTrack(track);
    return static_cast<int32_t>(keyIndex(track.keys, time));
}

std::array<float, 4> sampleRetailAnimationLibraryTrack(const CityAnimationLibraryTrack& track,
    int32_t time, RetailAnimationInterpolation interpolation, int32_t* selectedIndex)
{
    validateLibraryTrack(track);
    if (interpolation != RetailAnimationInterpolation::step && interpolation != RetailAnimationInterpolation::linear)
        throw std::runtime_error("Unsupported original interpolation selector.");
    if (selectedIndex) *selectedIndex = static_cast<int32_t>(keyIndex(track.keys, time));
    const auto difference = [](auto a, auto b) {
        const uint32_t bits = uint32_t(a) - uint32_t(b);
        int32_t signedValue;
        std::memcpy(&signedValue, &bits, 4);
        return signedValue;
    };
    return sampleBracket(bracketKeys(track.keys, time, interpolation, difference), track.transform);
}

void applyRetailAnimationChannel(RetailAnimationPoseTransform& pose, const CityAnimationChannel& channel,
    int32_t relativeMs, RetailAnimationInterpolation interpolation)
{
    const auto value = sampleRetailAnimationChannel(channel, relativeMs, interpolation);
    switch (channel.transform) {
    case CityAnimationTransform::translation: pose.translation = {value[0], value[1], value[2]}; break;
    case CityAnimationTransform::translationZ: pose.translation[2] = value[0]; break;
    case CityAnimationTransform::rotation: pose.rotation = value; break;
    case CityAnimationTransform::scale: pose.scale = {value[0], value[1], value[2]}; break;
    }
}

bool retailAnimationCanBeInterrupted(int32_t animation)
{
    return !(animation >= 1 && animation <= 23) && animation != 86 && animation != 164 && animation != 175;
}

RetailAnimationSelection guardRetailAnimationSelection(RetailAnimationClock& clock,
    const RetailAnimationRequest& request, uint32_t& movementFlags, bool inCinematic)
{
    using Selection = RetailAnimationSelection;
    if (movementFlags & 0x800u) return Selection::rejected;
    if (request.animation == -1) {
        clock.animation = -1;
        clock.startMs = clock.endMs = 0;
        clock.cursor = clock.rate = 0;
        clock.loop = clock.rootMotion = clock.directionalMotion = false;
        return Selection::cleared;
    }
    if (request.animation == clock.animation) return Selection::repeated;
    if (!clock.loop && clock.animation != -1 && clock.cursor < float(clipLength(clock)) &&
        !retailAnimationCanBeInterrupted(clock.animation) && !inCinematic) {
        if (retailAnimationCanBeInterrupted(request.animation)) return Selection::rejected;
        // The original base setNextAnimation ignores the other request fields.
        clock.nextAnimation = request.animation;
        return Selection::deferred;
    }
    return Selection::accepted;
}

RetailAnimationSelectionResult selectRetailAnimation(RetailAnimationClock& clock,
    const RetailAnimationRequest& request, int32_t startMs, int32_t endMs,
    uint32_t& movementFlags, bool inCinematic, int32_t weaponType)
{
    const auto selection = guardRetailAnimationSelection(clock, request, movementFlags, inCinematic);
    if (selection != RetailAnimationSelection::accepted) return {selection};
    const bool reload = clock.animation == 67 && weaponType == 7;
    commitAcceptedRetailAnimation(clock, request, startMs, endMs, movementFlags);
    return {RetailAnimationSelection::accepted, reload};
}

void commitAcceptedRetailAnimation(RetailAnimationClock& clock,
    const RetailAnimationRequest& request, int32_t startMs, int32_t endMs,
    uint32_t& movementFlags)
{
    if (request.animation < 0 || !std::isfinite(request.speed) || request.speed < 0 || clock.speedMultiplier < 0)
        throw std::runtime_error("Unsupported negative or nonfinite animation request.");
    RetailAnimationClock accepted = clock;
    accepted.startMs = startMs;
    accepted.endMs = endMs;
    clipLength(accepted);
    accepted.rate = float(clock.speedMultiplier) * request.speed;
    if (!std::isfinite(accepted.rate)) throw std::runtime_error("Animation rate overflow.");
    accepted.animation = request.animation;
    accepted.nextAnimation = -1;
    accepted.cursor = 0;
    accepted.loop = request.loop;
    accepted.rootMotion = request.rootMotion;
    accepted.directionalMotion = request.directionalMotion;
    clock = accepted; // Root cache and retained height are intentionally unchanged.
    if (request.animation == 164) movementFlags |= 0x80000000u;
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
    if (channel.transform != CityAnimationTransform::translation)
        throw std::runtime_error("An original local-translation root channel is required.");
    const auto value = sampleRetailAnimationChannel(channel, relativeMs, interpolation);
    return {value[0], value[1], value[2]};
}

RetailAnimationStep advanceRetailAnimation(RetailAnimationClock& clock, float elapsedMs,
    const CityAnimationChannel* root, const std::array<float, 16>& absoluteMatrix,
    bool bodyPresent, std::optional<std::array<float, 2>> direction,
    RetailAnimationInterpolation interpolation, RetailAnimationPoseEvaluator* pose)
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
    if (root->transform != CityAnimationTransform::translation)
        throw std::runtime_error("An original local-translation root channel is required.");
    validateChannel(*root, interpolation);
    // Original0x2f34ce advances the live clock before animator callbacks.
    if (pose) clock.cursor = cursor;
    const auto evaluate = [&](int32_t relative) {
        const int64_t absolute = int64_t(clock.startMs) + relative;
        if (absolute < std::numeric_limits<int32_t>::min() || absolute > std::numeric_limits<int32_t>::max())
            throw std::runtime_error("Animation sample exceeds the original timeline.");
        result.relativeMs = relative;
        result.absoluteMs = static_cast<int32_t>(absolute);
        result.sampledRoot = pose ? pose->evaluate(relative, result.absoluteMs) :
                                   sampleRetailAnimationRoot(*root, relative, interpolation);
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
