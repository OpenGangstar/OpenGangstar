/*
 * Copyright (c) 1993 Martin Birgmeier
 * All rights reserved.
 *
 * You may redistribute unmodified or modified versions of this source
 * code provided that the above copyright notice and this and the
 * following conditions are retained.
 *
 * This software is provided ``as is'', and comes with no warranties
 * of any kind. I shall in no event be liable for anything that happens
 * to anyone/anything when using this software.
 */

#include "retail_random48.h"

// Native adaptation of AOSP Bionic libc/stdlib/{_rand48,lrand48,srand48}.c,
// commit5f53a18204ec991f5a77872806eeaa185936aa8c. Explicit unsigned arithmetic
// preserves the original 16-bit-word recurrence without ARM long/layout casts,
// signed integer overflow or dependence on the Windows CRT random generator.
void RetailRandom48::srand48(std::uint32_t seedBits) noexcept {
    seed = (std::uint64_t(seedBits) << 16) | 0x330eULL;
}

std::int32_t RetailRandom48::lrand48() noexcept {
    // Bionic's default multiplier/addend; srand48 restores these same constants.
    // No lcong48 customization exists in the observed game imports/callers.
    seed = (seed * 0x5deece66dULL + 0xbULL) & 0xffffffffffffULL;
    return static_cast<std::int32_t>(seed >> 17);
}
