# Original shared lrand48 stream

`RetailRandom48` is the concrete native implementation of
`RetailRandomPositionSource`. One instance belongs to the application context;
navigation, actors and other original consumers borrow that same serial stream.
Copying is disabled to prevent accidental forks. This class has no Android
runtime dependency, global singleton or per-NPC seed policy.

## Original startup and consumers

The original ELF imports `lrand48` at PLT `0x1c4664`/GOT `0x4b029c` and
`srand48` at PLT `0x1c4580`/GOT `0x4b0250`. The inspected static direct-call
inventory has58 lrand48 sites and one srand48 site. It includes navigation,
NPC combat, character reactions, spawn selection, camera shake and bullet
traces. Other rand48-family functions are not imported by this game library.
They must not be invented as independent replacement streams for those callers.

Application::Init `0x2d5591`, instructions `0x2d56da..0x2d56ea`, obtains the
actual device timer, calls getRealTime, stores its uint32 result at application
state+92 and passes that unchanged register value to srand48. Actual original
CIrrDeviceStub::getTimer `0x1dea31` returns the retained timer. CTimer::getRealTime
`0x1deb81` calls os::Timer::getRealTime `0x1e0911`, whose gettimeofday-based result
is `(seconds*1000 + microseconds/1000) modulo2^32`. This seed is not an inferred
frame counter, physics delta or arbitrary host epoch.

The native `srand48(uint32_t seedBits)` accepts the exact32-bit ARM long bit
pattern, including negative original long values. Production startup must pass
its genuine equivalent real-time timer result at the original boundary. This
task implements the stream and proves the original seed argument; it does not
modify or claim production Application::Init/startup integration.

## Pinned historical implementation

Primary source is AOSP Bionic commit
[`5f53a18204ec991f5a77872806eeaa185936aa8c`](https://android.googlesource.com/platform/bionic/+/5f53a18204ec991f5a77872806eeaa185936aa8c)
(eclair-release):

| Original source | SHA256 |
| --- | --- |
| [libc/stdlib/_rand48.c](https://android.googlesource.com/platform/bionic/+/5f53a18204ec991f5a77872806eeaa185936aa8c/libc/stdlib/_rand48.c) | `8a0b643cc344d5b3780358bb45211f9446cec34d6f62d31474aaaeceedb03501` |
| [libc/stdlib/lrand48.c](https://android.googlesource.com/platform/bionic/+/5f53a18204ec991f5a77872806eeaa185936aa8c/libc/stdlib/lrand48.c) | `78cbca1067855665d4a278ccf25032cbac43d4de38546dc00f569b2f0a511108` |
| [libc/stdlib/srand48.c](https://android.googlesource.com/platform/bionic/+/5f53a18204ec991f5a77872806eeaa185936aa8c/libc/stdlib/srand48.c) | `1e7480dad65a4180468295d0bf614965e0803a13be8214c6cea284e8277f9ae7` |
| [libc/private/rand48.h](https://android.googlesource.com/platform/bionic/+/5f53a18204ec991f5a77872806eeaa185936aa8c/libc/private/rand48.h) | `cb9f6ef5c3435cf0fba98a23ba10d7c64cfa7ee66d416bf0ac8cd30d289830de` |

The source files are also byte-identical in inspected historical Donut and
Froyo release trees. This proves compatible historical source provenance; the
game's ELF imports external libc and does not identify a particular device's
libc bytes. It is not a claim of linked Android system-library byte matching.

These trees also contain `libc/bionic/_rand48.c` with single-underscore `_rand48_*`
symbols. That family belongs to a distinct older drand48 path. The game's
lrand48/srand48 functions use double-underscore `__rand48_*` and the private
header above. The two similarly named implementations must not be mixed.
The original Android.mk includes the actual stdlib family in libc_common.

## Scalar semantics and native adaptation

Original low/middle/high seed words are `{0x330e,0xabcd,0x1234}`, giving process
default state `0x1234abcd330e`. Construction preserves that pre-srand48 state.
`srand48(bits)` replaces state with `(uint64(bits)<<16)|0x330e`, restoring the
low word regardless of prior consumption. In particular seed0 yields
`0x00000000330e`, not the process default. Negative original seed-1 yields
`0xffffffff330e`; seed INT32_MIN yields `0x80000000330e`.

Each lrand48 call advances the48-bit recurrence with multiplier `0x5deece66d`
and addend `0xb`, then returns bits47..17 as a nonnegative31-bit integer. The
native unsigned64-bit multiplication is well-defined modulo2^64; masking its
low48 bits preserves the original three16-bit-word recurrence. Extraction
fits int32 exactly. It avoids signed-overflow arithmetic, C++ long-width
assumptions, original ARM pointers and Windows CRT random functions.

Original srand48 also restores the default multiplier/addend. They remain fixed
constants here because no observed game import/caller exposes lcong48 mutation.
Reseeding after consumption resets the complete supported stream. Creating a
fresh application context restores process default; an actor query never resets
or constructs a stream. The original shared API is not thread-safe: callers
serialize consumption in the world/application update order.

## Validation and current boundary

Fixed vectors are independently generated by compiling the unmodified pinned
historical stdlib C files with the actual private header,32-bit long and16-bit
seed-word ABI assertions. Release and Debug comparisons inspect both31-bit
outputs and complete48-bit states, including lower bits hidden from lrand48's
return value. The corpus includes default, seed0/1/-1, signed boundaries, split
word boundaries and deterministic additional seeds; final counts/gates are in
the task report: full native game builds/link and25/25 CTests pass in both
Release and Debug. Each native configuration matches75 starts (process default
plus74 reseeds),1,024 draws each:76,800 outputs and complete states, with zero
differences against its separately built historical C configuration. The native
Release/Debug dumps are identical. Original startup slice/getters/timer execute independently for
24 second/millisecond wrap cases and verify the stored/passed seed ordering.

Actual native navigation composition uses two distinct real height controllers
in one terrain/world, two random-position consumers borrowing one concrete
stream, an interleaved direct draw, and the real two-draw destination fallback.
The expected five complete states come from the unchanged historical C oracle.

Startup binding and migration of every original consumer remain required. The
existing camera-local presentation stream is not silently replaced here, and
this class does not claim complete NPC/traffic/AI or global replay equivalence
before those actual consumers share the application-owned instance. No gameplay
progression is used for validation.

## Retained source license

Copyright (c) 1993 Martin Birgmeier
All rights reserved.

You may redistribute unmodified or modified versions of this source
code provided that the above copyright notice and this and the
following conditions are retained.

This software is provided ``as is'', and comes with no warranties
of any kind. I shall in no event be liable for anything that happens
to anyone/anything when using this software.

The complete original notice is also retained in retail_random48.cpp. Preserve
it when redistributing the adapted source.
