#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct CityParticleEmitter;
struct CityScene;

std::string cityParticleTexture(const CityScene& scene);
std::array<float, 4> sampleCityDiffuseColor(const CityScene& scene, float timeMs);

struct CityParticle {
    std::array<float, 3> position, velocity;
    float age, lifetime, targetSize, size;
    float animationOffset, animationScale;
};

std::array<float, 4> cityParticleColor(const CityScene& scene, const CityParticle& particle, float emitterTimeMs);

struct CityParticleFrame {
    std::array<float, 3> origin{};
    std::array<std::array<float, 3>, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
};

class CityParticleSimulation {
public:
    void advance(const CityParticleEmitter& emitter, float seconds, float animationMs,
                 const CityParticleFrame& frame, bool emit = true);
    const std::vector<CityParticle>& particles() const { return live; }

private:
    float random();
    uint32_t seed = 12345;
    float birthRemainder = 0;
    std::vector<CityParticle> live;
};
