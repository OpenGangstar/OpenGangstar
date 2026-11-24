#pragma once

#include <cstdint>

// ARM SUBS preserves the signed32 bit pattern even across overflow. Avoid native
// signed overflow while retaining the original value before float conversion.
inline std::int32_t retailSubtractInt32(std::int32_t a, std::int32_t b)
{
    const auto bits = std::uint32_t(a) - std::uint32_t(b);
    const auto value = bits <= std::uint32_t(INT32_MAX) ? std::int64_t(bits) :
        std::int64_t(bits) - 0x100000000LL;
    return static_cast<std::int32_t>(value);
}
