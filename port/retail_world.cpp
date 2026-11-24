#include "retail_world.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

RetailWorldUpdate::RetailWorldUpdate(b2World& world, RetailWorldStepState& state)
    : world(world), state(state) {}

void RetailWorldUpdate::advance(std::uint32_t elapsedMs, RetailWorldVehicles& vehicles) {
    // Valid state from the original constructor/preceding updates is finite and
    // below one step. Positive infinity would make the original loop diverge.
    if (!std::isfinite(state.accumulatorSeconds))
        throw std::invalid_argument("Nonfinite retail world physics accumulator.");
    std::int32_t signedElapsed;
    static_assert(sizeof signedElapsed == sizeof elapsedMs);
    std::memcpy(&signedElapsed, &elapsedMs, sizeof signedElapsed);
    const auto clamped = signedElapsed < 0 ? 0 : signedElapsed > 249 ? 250 : signedElapsed;
    state.accumulatorSeconds = state.accumulatorSeconds + static_cast<float>(clamped) / 1000.f;
    state.completedSteps = 0;
    for (std::size_t i = 0; i < vehicles.size(); ++i)
        vehicles.updateFrame(i, signedElapsed);
    while (state.accumulatorSeconds >= fixedStepSeconds) {
        for (std::size_t i = 0; i < vehicles.size(); ++i)
            vehicles.updatePhysics(i, fixedStepSeconds);
        world.Step(fixedStepSeconds, 10);
        for (std::size_t i = 0; i < vehicles.size(); ++i)
            vehicles.synchronizePhysics(i);
        const float before = state.accumulatorSeconds;
        state.accumulatorSeconds = before - fixedStepSeconds;
        if (!(state.accumulatorSeconds < before))
            throw std::runtime_error("Nonterminating retail world physics accumulator.");
        // Original application+100 uses i2f, fadd(1), f2iz, not elapsed time.
        const float count = static_cast<float>(state.completedSteps) + 1.f;
        state.completedSteps = static_cast<std::int32_t>(count);
    }
}
