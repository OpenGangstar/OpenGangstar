# Native physics dependency

## Status and provenance

The native build now provides a complete Box2D 2.0.1 static library compiled for
Windows x86-64. It builds all 31 engine translation units, including collision,
broad phase, contacts, continuous collision, and every original upstream joint.
Independent Release and Debug subsystem tests exercise the library. The game's
production actor/vehicle simulation is not yet bound to this library, and full
equivalence to Gameloft's engine build has not been established.

The pinned source is the historical release from the original project's
[Google Code archive](https://code.google.com/archive/p/box2d/). Its
[download metadata](https://storage.googleapis.com/google-code-archive/v2/code.google.com/box2d/downloads-page-2.json)
identifies `Box2D_v2.0.1.zip` as the 866,019-byte Box2D Release 2.0.1 archive,
with SHA-1 `27a1a0bd08c81bbf661fa008645ee5c538bb2767`. The downloaded archive
matches that checksum. CMake downloads the
[original release](https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/box2d/Box2D_v2.0.1.zip)
and pins its independently computed SHA-256:

```text
62857048aa089b558561074154430883cee491eedd71247f75f488cba859e21f
```

This release uses Erin Catto's historical zlib-style license. Its license and
source notices are retained; do not substitute the current project's license.
The generated native source tree includes `License.txt`, and every modified file
is marked with an OpenGangstar adaptation comment. The fetched release and the
matching ARM project remain unchanged.

## Retail evidence and configuration

The retail ELF's `b2_version` object at 0x004b6068 contains three 32-bit integers:
2, 0, 1. Version alone is insufficient evidence of unchanged source, so the
selection was also checked against original APIs, object fields, tuning, and
compiled engine functions.

| Original evidence | Native dependency decision |
| --- | --- |
| `b2World::b2World(b2AABB const&, b2Vec2 const&, bool)` at 0x00400d35 | Retain the shape-based 2.0.1 API and bounded-world constructor. |
| `b2World::Step(float, int)` at 0x00400c7d | Retain its single iteration-count API; current Box2D APIs are incompatible. |
| `b2BodyDef::massData`, `b2PolygonDef`, shape filters, Add/Persist/Remove/Result callbacks | Use the historical source rather than adapting modern fixture/event APIs. |
| `b2World::GetProxyCount()` at 0x003ff941, `GetPairCount()` at 0x003ff955, and constructor allocation size | Set `b2_maxProxies` to 1,024, rather than upstream's 512; `b2_maxPairs` consequently remains eight times that capacity, 8,192. |
| `PhysicAttributes::InitWorld()` at 0x0031a951 | The game creates bounds from -2,000 to +2,000 m on both axes, zero gravity, sleep enabled, position correction enabled, continuous physics disabled, and custom game contact listener/filter objects. |
| `GS3DStuff::update(StateMachine*)` at 0x002e078d, Step call at 0x002e095a | The game uses 1/60 s steps and 10 solver iterations, with a capped timer accumulator. This is game-loop configuration, not a changed engine default. |
| `Character::initPhysic()` and `Character::updatePosition()` | Original game coordinates convert centimetres to physics metres and back. |

No native game-world singleton or replacement actor flags are created merely to
make these configuration values appear implemented. Tests use the verified game
configuration where relevant. Continuous collision also has an explicitly enabled
test so that the retail game's disabled setting does not hide broken or removed
engine functionality.

## Source comparison and remaining parity work

An ignored research build compiled the 31 historical source files with the
available original GCC 4.2.1 ARM Thumb/PIC toolchain, release assertions disabled,
`-finline-limit=40`, and `-fno-short-enums`. The only engine tuning adjustment in
that comparison was the independently derived 1,024-proxy capacity. Legacy header
declaration shims were used for missing standard header wrappers; no physics
algorithm was replaced for this probe.

All 36 emitted `b2World` methods that correspond to retail symbols had the same
sizes and the same non-relocation bytes, apart from alignment-NOP candidates.
This includes Step, Solve, Query, the constructor, and SolveTOI at 0x00400029.
An initial SolveTOI size discrepancy disappeared when the original inline limit
was applied; it is not evidence of a known CCD fork change.

Across the complete source probe, 387 per-translation-unit symbol comparisons
included duplicated weak/inline symbols. Of these, 225 had equal non-relocation
bytes, 125 differed only at candidate alignment padding, and 37 had other byte or
size differences. These are not full linked byte-match results. External call/GOT
relocations were excluded, and padding candidates were not credited as matches.
The original matching project's progress and symbol tables were not changed.

Further comparison is required for weak vector normalization/validity helpers,
broad-phase bound calculation, polygon AABB/proxy synchronization and construction,
island solving, and several joint methods. Differences may arise from compiler or
header choices as well as source changes; do not label them all confirmed fork
changes. Do not claim that version identity proves full behavioral parity. The
research commands, disassembly, source archive, object files, and comparison JSON
remain local build artifacts and are not distributable retail binaries.

## Native adaptations

`cmake/Box2D.cmake` stages copies in `port/build/x64/box2d-native`. It checks each
expected source fragment before adapting it and fails if the pinned source layout
does not match. The adaptations are:

- Restore the verified retail broad-phase capacity of 1,024 proxies.
- Expose a nonvirtual `b2World::InRange(const b2AABB&) const` forwarding method
  to the existing broad-phase predicate. This changes no class fields, object
  layout, solver, or predicate. The terrain backend needs the real world's
  bounds to reject unsupported cliff geometry before `CreateShape`: historical
  Debug asserts for an outside AABB, while Release silently leaves a null proxy.
  Strict overlap is accepted; touching alone is outside. The world remains the
  single authority for its constructor-supplied bounds. Tests cover contained,
  outside, boundary-touching, and nextafter-overlapping AABBs in both configs.
- Include the standard declarations needed by a modern native compiler.
- Replace the four-byte `b2Alloc` bookkeeping prefix with a prefix aligned to
  `std::max_align_t`. Returning `malloc` plus four bytes would misalign native
  pointer-bearing objects. The original byte-count bookkeeping and matching free
  operation are retained with the new header size.
- Align the local stack arena and round each stack allocation to the same native
  alignment. Mixed allocation sizes must not misalign subsequent native pointers.
  Allocation/free pairs and the heap fallback keep consistent padded sizes.
- Replace the Debug pool-check's narrowed pointer subtraction with `offsetof` for
  the member offset it actually computes. Assertions and Debug memory checks stay
  enabled.

Float and integer scalar widths remain 32 bits. Native pointers retain their
full width, and the compiler lays out native classes normally. MSVC uses strict
floating-point evaluation and treats pointer-truncation diagnostics as errors.
No ARM object layout, asset record, or save record is resized or reinterpreted by
these adaptations.

Native code must include the dependency's `Box2D.h` and use its real types and
accessors. The matching project's `src/game/core/Physics.h` contains partial ARM
call/layout views and must not define native physics objects or be mixed with
the real native class definitions. Game-body user-data records require their own
native adapter; an upstream opaque `void*` does not establish the full original
game record layout.

## Validation and next integration work

Build and run the independent subsystem test with:

```powershell
cmake --build port/build/x64 --config Release --target box2d_test --parallel 6
ctest --test-dir port/build/x64 -C Release -R '^box2d$' --output-on-failure
cmake --build port/build/x64 --config Debug --target box2d_test --parallel 6
ctest --test-dir port/build/x64 -C Debug -R '^box2d$' --output-on-failure
```

The test checks native allocation alignment, pool growth and stack/heap fallback,
allocation cleanup, fixed-step velocity integration, collision response, all four
contact callback categories, collision refiltering, retention of user-data
pointers, 800 simultaneous proxies, continuous collision against a thin wall, and
a distance joint. The CCD polygon respects the original 0.04 m core-shrink
constraint; assertions are not disabled to accept invalid test geometry.

Validation on 2026-11-24 passed the full x64 Release build, all six Release
CTests, the Debug physics test with assertions active, and the automated Vulkan
opening smoke. Release and Debug physics test executables and the native game
were verified as AMD64 PE32+ files. No compiler warnings were emitted by the new
physics targets in either configuration. These checks validate the dependency
and preserve existing startup behavior; they do not validate a production physics
binding that has not been implemented.

Still unresolved are original custom contact filtering/listener behavior, vertical
collision extents and terrain interactions, native actor/body ownership, root
motion to body velocity, the original fixed-step game-loop binding, and complete
engine parity. Upstream default callbacks do not implement Gameloft's custom
callbacks. Successful compilation, these subsystem tests, and static linking to
the game target do not establish that the production game uses Box2D yet.
