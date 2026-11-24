#pragma once

#include "retail_random_position.h"

// One instance belongs to the application's shared serial random stream.
// NPC/navigation/other consumers borrow it; copying would fork consumption.
// Construction represents the original libc process default before srand48.
class RetailRandom48 final : public RetailRandomPositionSource {
  public:
    RetailRandom48() = default;
    RetailRandom48(const RetailRandom48&) = delete;
    RetailRandom48& operator=(const RetailRandom48&) = delete;
    // Exact 32-bit ARM long bit pattern, including negative original seed values.
    // Application::Init passes ITimer::getRealTime() unchanged through this ABI.
    void srand48(std::uint32_t seedBits) noexcept;
    std::int32_t lrand48() noexcept override;
    std::uint64_t state() const noexcept { return seed; }

  private:
    std::uint64_t seed = 0x1234abcd330eULL;
};
