# Box2D retail parity investigation

OG-016, native baseline eb7368c, 2026-11-25. This investigation classifies the
37 remaining rows from the earlier 387-row ARM source probe. It makes no native
algorithm or build changes: none of those rows proves a new retail numerical
correction. The existing dependency provenance, 1,024-proxy adaptation, native
alignment repairs, all 31 engine translation units and strict floating-point
settings remain as documented in [PHYSICS_DEPENDENCY.md](PHYSICS_DEPENDENCY.md).
The library is a native foundation, not a completed production actor simulation.

## Method and limits

The initial probe compiled the pinned upstream 2.0.1 release, with the already
verified 1,024-proxy capacity, using original GCC 4.2.1, ARMv5TE Thumb, XScale,
softfp, PIC, -O2, -DNDEBUG, -fno-short-enums, -fno-strict-aliasing, -fno-rtti,
-fno-exceptions and -finline-limit=40. Release-only standard-header declaration
shims were present; no physics algorithm was changed. Its 387 per-translation-unit
rows include 225 equal non-relocation byte sequences, 125 padding candidates,
32 size differences and five other differences. This is not 387 unique functions.

Research-only recompilation changed the inline limit to 50, 55 and 70 with the
same source and other options. The 55 run also paired local symbols with their
STT_FILE owner rather than treating a local C++ name as globally unique. Its
387 rows contain 237 non-relocation-equal, 141 padding candidates, five size
and four other differences. The 70 run changes which methods inline and introduces
other differences: there is no claim that one option is the original global
compiler setting. The old world-method evidence at limit 40 remains separate.
These experiments are evidence of compiler sensitivity, not native build flags.

The probe compares object symbol bytes against retail symbol bytes, excluding
four-byte ARM relocation sites. It does not resolve and validate every external
call, GOT target, linker relaxation, weak-symbol selection or linked byte stream.
It must not update the matching ARM project's progress or support a full byte
matching claim. A candidate alignment difference is not promoted to a match:
0x46c0 and 0x0000 at a candidate slot need independent control-flow/padding proof.
No retail binaries, disassemblies or copied upstream source are committed here.
All such research artifacts remain under ignored port/build/box2d-research.

## Classification of the original 37 rows

| Class | Original rows | Supported conclusion |
| --- | ---: | --- |
| W | 9 | Nine copies of one weak Normalize helper; inline Length at limit 55 reproduces its size and all compared bytes except one padding candidate. |
| I | 18 | Unchanged algorithms reproduce equal compared bytes (11 rows) or differ only at padding candidates (7 rows) after an inline-limit change. |
| L | 1 | Earlier name-only lookup compared unrelated local static initializers. Source-scoped pairing removes that false difference. |
| H | 1 | The probe uses finite(double) with float-to-double conversion; retail uses __isfinitef(float). No new native validity behavior is required. |
| C | 2 | Remaining argument moves accompany equivalent reversed comparison helpers; not evidence of changed collision predicates. |
| R | 6 | Five function bodies, including two constructor aliases, remain without a complete verified parity explanation. No source change is inferred. |

The rows contain 29 unique mangled names. Nine Normalize rows share the same
retail weak function, and polygon C1/C2 are aliases at one retail entry point.
A duplicated weak implementation is not nine independent engine divergences.

Addresses in this table retain the retail Thumb bit. Sizes are bytes; the old
size is from the limit-40 probe, and the new size from the limit-55 probe. L is
the intentionally retained old wrong pairing, corrected explicitly below.

| Translation unit | Function | Retail entry | Old / retail size | Limit-55 size | Classification |
| --- | --- | --- | ---: | ---: | --- |
| b2BroadPhase.cpp | `b2BroadPhase::ComputeBounds(unsigned short*, unsigned short*, b2AABB const&)` | 0x3f1719 | 336 / 320 | 336 | R: unresolved residual |
| b2BroadPhase.cpp | `b2Vec2::IsValid() const` | 0x3f1bd9 | 46 / 28 | 46 | H: finite header ABI |
| b2CollideCircle.cpp | `b2Vec2::Normalize()` | 0x3f29f1 | 84 / 100 | 100 | W: weak/inlining; padding candidate |
| b2CollideCircle.cpp | `b2CollidePolygonAndCircle(b2Manifold*, b2PolygonShape const*, b2XForm const&, b2CircleShape const*, b2XForm const&)` | 0x3f2c35 | 1376 / 1376 | 1376 | I: non-relocation equal |
| b2CollidePoly.cpp | `b2Vec2::Normalize()` | 0x3f29f1 | 84 / 100 | 100 | W: weak/inlining; padding candidate |
| b2Collision.cpp | `b2Vec2::Normalize()` | 0x3f29f1 | 84 / 100 | 100 | W: weak/inlining; padding candidate |
| b2Distance.cpp | `InPoints(b2Vec2 const&, b2Vec2 const*, int)` | 0x3f5d85 | 376 / 376 | 376 | C: equivalent comparison ordering |
| b2Distance.cpp | `b2Vec2::Normalize()` | 0x3f29f1 | 84 / 100 | 100 | W: weak/inlining; padding candidate |
| b2TimeOfImpact.cpp | `b2Vec2::Normalize()` | 0x3f29f1 | 84 / 100 | 100 | W: weak/inlining; padding candidate |
| b2Math.cpp | `__static_initialization_and_destruction_0(int, int)` | 0x400f79 | 96 / 72 | 96 | L: wrong local pairing; corrected equal |
| b2Island.cpp | `b2Vec2::Normalize()` | 0x3f29f1 | 84 / 100 | 100 | W: weak/inlining; padding candidate |
| b2Island.cpp | `b2Island::Solve(b2TimeStep const&, b2Vec2 const&, bool, bool)` | 0x3f82a9 | 1184 / 1184 | 1184 | R: unresolved residual |
| b2CircleShape.cpp | `b2CircleShape::UpdateSweepRadius(b2Vec2 const&)` | 0x3f26c9 | 84 / 76 | 76 | I: non-relocation equal |
| b2CircleShape.cpp | `b2Vec2::Normalize()` | 0x3f29f1 | 84 / 100 | 100 | W: weak/inlining; padding candidate |
| b2PolygonShape.cpp | `ComputeOBB(b2OBB*, b2Vec2 const*, int)` | 0x3f9f81 | 660 / 656 | 660 | R: unresolved residual |
| b2PolygonShape.cpp | `b2Vec2::Normalize()` | 0x3f29f1 | 84 / 100 | 100 | W: weak/inlining; padding candidate |
| b2PolygonShape.cpp | `b2PolygonShape::UpdateSweepRadius(b2Vec2 const&)` | 0x3faef5 | 156 / 144 | 144 | I: non-relocation equal |
| b2PolygonShape.cpp | `b2PolygonShape::ComputeMass(b2MassData*) const` | 0x3fa31d | 564 / 564 | 564 | I: padding candidate |
| b2PolygonShape.cpp | `b2PolygonShape::TestSegment(b2XForm const&, float*, b2Vec2*, b2Segment const&, float) const` | 0x3fa62d | 584 / 584 | 584 | R: unresolved residual |
| b2PolygonShape.cpp | `b2PolygonShape::ComputeAABB(b2AABB*, b2XForm const&) const` | 0x3fa875 | 276 / 500 | 500 | I: padding candidate |
| b2PolygonShape.cpp | `b2PolygonShape::b2PolygonShape(b2ShapeDef const*)` | 0x3faadd | 536 / 604 | 608 | R: unresolved residual |
| b2PolygonShape.cpp | `b2PolygonShape::b2PolygonShape(b2ShapeDef const*)` | 0x3faadd | 536 / 604 | 608 | R: unresolved residual |
| b2Shape.cpp | `b2Shape::Synchronize(b2BroadPhase*, b2XForm const&, b2XForm const&)` | 0x3ff471 | 96 / 240 | 240 | I: padding candidate |
| b2Shape.cpp | `b2Shape::CreateProxy(b2BroadPhase*, b2XForm const&)` | 0x3ff27d | 68 / 184 | 184 | C: equivalent comparison ordering |
| b2Shape.cpp | `b2Shape::RefilterProxy(b2BroadPhase*, b2XForm const&)` | 0x3ff375 | 100 / 252 | 252 | I: padding candidate |
| b2DistanceJoint.cpp | `b2Vec2::Normalize()` | 0x3f29f1 | 84 / 100 | 100 | W: weak/inlining; padding candidate |
| b2DistanceJoint.cpp | `b2DistanceJoint::InitVelocityConstraints(b2TimeStep const&)` | 0x3f718d | 908 / 912 | 912 | I: non-relocation equal |
| b2DistanceJoint.cpp | `b2DistanceJointDef::Initialize(b2Body*, b2Body*, b2Vec2 const&, b2Vec2 const&)` | 0x3f751d | 260 / 266 | 266 | I: non-relocation equal |
| b2MouseJoint.cpp | `b2MouseJoint::InitVelocityConstraints(b2TimeStep const&)` | 0x3f8bb5 | 436 / 484 | 484 | I: padding candidate |
| b2MouseJoint.cpp | `b2MouseJoint::SolveVelocityConstraints(b2TimeStep const&)` | 0x3f8d99 | 568 / 572 | 572 | I: non-relocation equal |
| b2PulleyJoint.cpp | `b2PulleyJoint::InitVelocityConstraints(b2TimeStep const&)` | 0x3fdc41 | 1200 / 1224 | 1224 | I: non-relocation equal |
| b2PulleyJoint.cpp | `b2PulleyJoint::SolvePositionConstraints()` | 0x3fd469 | 1932 / 2008 | 2008 | I: non-relocation equal |
| b2PulleyJoint.cpp | `b2PulleyJoint::GetLength2() const` | 0x3fd2e1 | 188 / 196 | 196 | I: non-relocation equal |
| b2PulleyJoint.cpp | `b2PulleyJoint::GetLength1() const` | 0x3fd3a5 | 188 / 196 | 196 | I: non-relocation equal |
| b2PulleyJoint.cpp | `b2PulleyJointDef::Initialize(b2Body*, b2Body*, b2Vec2 const&, b2Vec2 const&, b2Vec2 const&, b2Vec2 const&, float)` | 0x3fd141 | 384 / 416 | 416 | I: padding candidate |
| b2RevoluteJoint.cpp | `b2RevoluteJoint::SolvePositionConstraints()` | 0x3fe699 | 1204 / 1308 | 1308 | I: padding candidate |
| b2RevoluteJoint.cpp | `b2RevoluteJoint::InitVelocityConstraints(b2TimeStep const&)` | 0x3febb5 | 984 / 1016 | 1016 | I: non-relocation equal |

## Evidence behind the classifications

Normalize at 0x003f29f1 computes sqrtf(x*x+y*y), returns zero without modifying
the vector when length < 0x34000000 (float epsilon), otherwise computes
1.0f/length and multiplies each coordinate. The initial limit-40 probe emits
Length separately and calls its GOT entry. At limit 55 it inlines that same
calculation; all nine weak copies then have retail's size 100, with only the
candidate padding halfword at relative offset 86 different. This supports an
inlining explanation, not replacing sqrtf or introducing approximate inverse
square root. Normalize remains ordinary historical source in the native build.

The local initializer `_Z41__static_initialization_and_destruction_0ii` occurs
many times in the retail symbol table. Its b2Math.cpp instance is at
0x003f888d, size 96. The old global name dictionary instead selected the
b2WorldCallbacks.cpp instance at 0x00400f79, size 72. Pairing by STT_FILE plus
local name yields size 96 and equal compared non-relocation bytes for the
b2Math instance. The old address in the inventory is retained to expose the
measurement error rather than silently rewrite the input evidence.

IsValid at 0x003f1bd9 calls __isfinitef on the two float coordinates, short
circuiting after an invalid first coordinate. The old-header probe converts
each float to double and calls finite(double). These have the same finite-value
classification, including infinities, NaNs, zeros and subnormals; the differing
ABI adds instructions, not a different tolerance or physical validity rule.
The native MSVC path already uses _finite. No Android header ABI is imposed on
the native library merely to imitate its ARM instruction count.

Two remaining comparison differences have explicit operand/helper explanations:

- InPoints at 0x003f5d85: relative offsets 120/122 swap the two argument moves.
  The probe calls __aeabi_fcmpgt(r7,r6); retail calls __aeabi_fcmplt(r6,r7).
  The following branch and selected value are unchanged. The reversed relation
  is equivalent, including unordered comparisons, not a new distance threshold.
- CreateProxy at 0x003ff27d after inlining: relative offsets 100/102 swap the
  arguments to the maximum-of-two-components test. Probe uses
  __aeabi_fcmplt(r5,r4), retail __aeabi_fcmpgt(r4,r5), with the same branch and
  selected value. The remaining difference is padding candidate 162/163.

These helper identities were checked against .rel.plt/.dynsym rather than
guessed from objdump's absent dynamic labels: retail PLT 0x001c419c is
__aeabi_fcmpgt, 0x001c479c is __aeabi_fcmplt, 0x001c4184 is sqrtf and
0x001c45e0 is __isfinitef. That checks these particular sites; it does not
validate all calls excluded by the comparison mask.

## Residual numerical work: five bodies, six rows

| Function | Remaining observation | Required parity evidence before changing native source |
| --- | --- | --- |
| BroadPhase::ComputeBounds, 0x003f1719 | Probe 336 versus retail 320. Inspected code uses bounded float coordinates, quantization, even lower and odd upper values; instruction selection differs for clearing the low bit. | Complete clamp/quantization data-flow and conversion equivalence, including world edges and ordering of equal quantized bounds. Do not infer changed broad-phase capacity from this body. |
| Island::Solve, 0x003f82a9 | Both size 1184 at limit 55. Early differences are branch offsets; the sleep-time minimum block has argument/register scheduling differences and shifts later code by two bytes. | Resolve every changed helper and sleep/control-flow path, then check finite arithmetic order and thresholds. No changed velocity cap or sleep constant has been proved. |
| ComputeOBB, 0x003f9f81 | Probe 660 versus retail 656, with register/literal scheduling and comparison differences. | Compare oriented-bounds edge selection, area comparison/ties and centroid/extent arithmetic over the full body; a four-byte size difference does not justify changing geometry. |
| PolygonShape::TestSegment, 0x003fa62d | Both size 584. Observed differences include register initialization scheduling, reversed comparison argument moves and one padding candidate. | Verify all clipping predicates, reversed helper identities, endpoint/tie behavior and output ownership, rather than credit identical function size as proof. |
| PolygonShape constructors C1/C2, 0x003faadd | Two mangled names alias one entry. Probe grows from 536 to 608 with changed inlining; retail is 604. | Attribute remaining differences across centroid/OBB construction, edge-normal normalization and core-vertex offset calculation; compare degenerate and near-collinear finite inputs before any numerical adaptation. |

This is the bounded residual inventory, not five confirmed fork algorithms.
Compiler/header choices remain plausible causes; the investigation does not
assert full equivalence for these bodies. Do not patch them merely to make a
research compiler produce a desired size. If a behavioral difference is proved,
reconstruct it in the native staged source with an independent numerical test
that fails against unmodified upstream; preserve provenance and ARM source.

The only established retail engine setting change remains the already applied
1,024-proxy capacity (and consequent 8,192 pairs). The game's disabled CCD,
1/60 s steps and ten iterations are observed host configuration, not reasons to
remove upstream CCD or alter solver defaults. No additional setting or algorithm
adaptation is supported by this assignment. Native allocator alignment and
strict floating point must remain intact.

## Validation and reproducibility

The research reran all 31 upstream translation units for each tested inline
limit, with no compilation failure. The exact commands, generated assembly,
objects, comparison JSON and an inventory JSON are ignored artifacts under
port/build/box2d-research/parity. Local-symbol pairing uses preceding STT_FILE
ownership; global/weak names use their retail symbol entries. Relocations are
excluded only within the corresponding object section and function range.

| Evidence input | SHA-256 |
| --- | --- |
| Pinned Box2D_v2.0.1.zip | 62857048aa089b558561074154430883cee491eedd71247f75f488cba859e21f |
| Original arm-comparison.json | 07bb5567d91612ce57287d95aea9e04334d040a84dcfac88f8bc9a8089537816 |
| Source-scoped limit-55 comparison.json | 59bfb2c9c5468692d9f87873428d21d654f88bf5e036b03b4f6d0bfc5ad7b6b9 |

The existing native box2d and retail_physics CTests passed in both Release and
Debug with assertions active in Debug. No runtime, CMake or test source changed,
so no new behavior test or game smoke test is claimed for this documentation
patch. Previous production binding limitations remain: full original solver
parity, live actor/world ownership, contact-listener effects and terrain/jump
state integration are distinct incomplete work. No manual gameplay was used.
