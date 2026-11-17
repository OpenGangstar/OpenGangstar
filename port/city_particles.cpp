#include "city_particles.h"
#include "city_assets.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
float sample(const CityParticleEmitter& emitter, CityParticleParameter parameter, float ms, float fallback)
{
    const auto track = emitter.tracks.find(parameter);
    if (track == emitter.tracks.end() || track->second.empty()) return fallback;
    const auto& keys = track->second;
    const auto next = std::upper_bound(keys.begin(), keys.end(), ms,
        [](float time, const CityParticleKey& key) { return time < key.timeMs; });
    if (next == keys.begin()) return next->value;
    const auto& previous = *(next - 1);
    if (next == keys.end()) return previous.value;
    const auto fraction = (ms - previous.timeMs) / (next->timeMs - previous.timeMs);
    return previous.value + fraction * (next->value - previous.value);
}
}

std::string cityParticleTexture(const CityScene& scene)
{
    if (scene.materials.size() == 1 && !scene.materials.begin()->second.texture.empty())
        return scene.materials.begin()->second.texture;
    // The retail explosion effect animates diffuse color and lists one image separately.
    if (scene.images.size() == 1) return scene.images.front();
    return {};
}

std::array<float, 4> sampleCityDiffuseColor(const CityScene& scene, float ms)
{
    const auto& keys = scene.diffuseColors;
    if (keys.empty()) return {1, 1, 1, 1};
    const auto next = std::upper_bound(keys.begin(), keys.end(), ms,
        [](float time, const CityDiffuseColorKey& key) { return time < key.timeMs; });
    if (next == keys.begin()) return next->rgba;
    const auto& previous = *(next - 1);
    if (next == keys.end()) return previous.rgba;
    const auto fraction = (ms - previous.timeMs) / (next->timeMs - previous.timeMs);
    auto rgba = previous.rgba;
    for (size_t c = 0; c < rgba.size(); ++c) rgba[c] += fraction * (next->rgba[c] - rgba[c]);
    return rgba;
}

std::array<float, 4> cityParticleColor(const CityScene& scene, const CityParticle& particle, float emitterTimeMs)
{
    if (scene.diffuseColors.empty() || scene.particleEmitters.empty()) return {1, 1, 1, 1};
    const auto& emitter = scene.particleEmitters.front();
    auto time = particle.age;
    if (emitter.animationMapping == 1) {
        const auto length = sample(emitter, CityParticleParameter::animationLength,
                                   emitterTimeMs, emitter.animationLength);
        time = particle.age / particle.lifetime * particle.animationScale * length;
    }
    return sampleCityDiffuseColor(scene, (time + particle.animationOffset) * 1000);
}

float CityParticleSimulation::random()
{
    // PSRandom::Rand uses the 48271 multiplicative generator.
    seed = static_cast<uint32_t>(uint64_t(seed) * 48271 % 2147483647);
    return static_cast<float>(double(seed) / 2147483647);
}

void CityParticleSimulation::advance(const CityParticleEmitter& emitter, float seconds, float animationMs,
    const CityParticleFrame& frame, bool emit)
{
    if (seconds <= 0) return;
    if (emitter.type > 2) throw std::runtime_error("Unsupported particle emission shape.");

    const auto birthRate = emit ? sample(emitter, CityParticleParameter::birthRate, animationMs, emitter.birthRate) : 0;
    // PGenerationModel accumulates the product and remainder as separate floats.
    const float birthsThisStep = std::max(0.f, birthRate) * seconds;
    const float total = birthRemainder + birthsThisStep;
    const auto maximum = emitter.maxParticles ? emitter.maxParticles : live.max_size();
    const auto available = maximum - std::min(maximum, live.size());
    const float whole = std::floor(total);
    birthRemainder = total - whole;
    const auto births = whole >= static_cast<double>(available) ? available : static_cast<size_t>(whole);
    const auto targetSize = sample(emitter, CityParticleParameter::targetSize, animationMs, emitter.targetSize);
    const auto sizeVariation = sample(emitter, CityParticleParameter::sizeVariation, animationMs, emitter.sizeVariation);
    const auto speedVariation = sample(emitter, CityParticleParameter::speedVariation, animationMs, emitter.speedVariation);

    for (size_t i = 0; i < births; ++i) {
        CityParticle particle{};
        particle.position = frame.origin;
        std::array<float, 3> offset{};
        if (emitter.type == 0) {
            // PDBox takes width, height, then length from the emitter parameters.
            for (size_t axis = 0; axis < 3; ++axis)
                offset[axis] = (random() - 0.5f) * emitter.dimensions[(axis + 1) % 3];
        } else if (emitter.type == 1) {
            // PDSphere samples a direction from a unit box and draws the radius independently.
            float squaredLength;
            do {
                for (auto& value : offset) value = random() - 0.5f;
                squaredLength = offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
            } while (squaredLength > 0.25f || squaredLength == 0);
            const auto scale = emitter.radius * random() / std::sqrt(squaredLength);
            for (auto& value : offset) value *= scale;
        } else {
            // PDCylinder samples height, angle, then radius.
            const auto height = (random() - 0.5f) * emitter.height;
            const auto angle = 6.283185307179586f * random();
            const auto radius = emitter.radius * random();
            offset = {std::sin(angle) * radius, height, std::cos(angle) * radius};
        }
        for (size_t axis = 0; axis < 3; ++axis) {
            for (size_t component = 0; component < 3; ++component)
                particle.position[component] += frame.axes[axis][component] * offset[axis];
        }
        const auto speed = emitter.speed * (1 + speedVariation * (random() - 0.5f));
        auto direction = emitter.direction;
        if (std::all_of(direction.begin(), direction.end(), [](float value) { return std::abs(value) <= 0.000001f; })) {
            for (auto& value : direction) value = random() - 0.5f;
        } else if (emitter.directionVariation > 0) {
            // PMotionModel rotates in XY, YZ, then XZ, with a range of 180 degrees
            // times DirectionVariation, centered on the authored direction.
            const auto rotate = [&](size_t first, size_t second) {
                const auto angle = 3.141592653589793 * emitter.directionVariation * (random() - 0.5f);
                const auto sine = std::sin(angle), cosine = std::cos(angle);
                const auto x = direction[first], y = direction[second];
                direction[first] = static_cast<float>(x * cosine - y * sine);
                direction[second] = static_cast<float>(x * sine + y * cosine);
            };
            rotate(0, 1);
            rotate(1, 2);
            rotate(0, 2);
        }
        std::array<float, 3> worldDirection{};
        for (size_t axis = 0; axis < 3; ++axis)
            for (size_t component = 0; component < 3; ++component)
                worldDirection[component] += frame.axes[axis][component] * direction[axis];
        const auto length = std::hypot(worldDirection[0], worldDirection[1], worldDirection[2]);
        if (length > 0)
            for (size_t axis = 0; axis < 3; ++axis) particle.velocity[axis] = speed * (worldDirection[axis] / length);
        particle.lifetime = emitter.life + emitter.lifeVariation * (random() - 0.5f);
        particle.targetSize = targetSize * (1 + sizeVariation * (random() - 0.5f));
        particle.animationOffset = emitter.animationOffset;
        if (emitter.animationOffsetVariation) particle.animationOffset += emitter.animationOffsetVariation * (random() - 0.5f);
        particle.animationScale = emitter.animationScale;
        if (emitter.animationScaleVariation) particle.animationScale += emitter.animationScaleVariation * (random() - 0.5f);
        live.push_back(particle);
    }
    for (auto& particle : live) {
        particle.age += seconds;
        for (size_t axis = 0; axis < 3; ++axis) particle.position[axis] += particle.velocity[axis] * seconds;
        const auto remaining = particle.lifetime - particle.age;
        particle.size = particle.targetSize;
        if (particle.age < emitter.sizeGrowthTime)
            particle.size = particle.targetSize * particle.age / emitter.sizeGrowthTime;
        if (remaining < emitter.sizeFadeTime && emitter.sizeFadeTime > 0)
            particle.size = particle.targetSize * std::max(0.f, remaining) / emitter.sizeFadeTime;
    }
    live.erase(std::remove_if(live.begin(), live.end(),
        [](const CityParticle& particle) { return particle.age >= particle.lifetime; }), live.end());
}
