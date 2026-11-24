# Original random-position search

This component implements PhysicAttributes::GetNextRandomPos `0x31cf75`, with
literal GetRandomDestination `0x31aa5d` and GetTriCenter `0x31a4cd` helpers. NPC's
no-path call is `(slot=0,mode=0,GetPosition())`; VehicleController uses mode1.
These are the two entries of the original material-cost table, separate from
LevelObject categories and mesh factory tags.

## Source and state

The original unstripped ARM library supplies all instruction bodies. Matching
source declarations are in PhysicAttributes.h; NPC.cpp and
VehicleController.cpp establish call arguments. GetNextRandomPos's initial
selected-index test precedes RNG, mode/table access and geometry: `0xffff`
returns the input point unchanged, regardless of the node pointer or mode.

The native `RetailHeightState::RandomEdge` is the slot's original persistent
cursor. Its field order is native typed storage, never the ARM memory layout:

| Field | Original evidence |
| --- | --- |
| edge | PhysicInfo+76, reset0xffff; a lane0/1/2 cursor |
| triangle.indexOffset | +78, reset0xffff; distinct from selected height triangle |
| triangle.buffer | +80, reset0xffff |
| laneOrdinal | +82, reset1; clamped to encoded lane count, or1 when count0 |
| intersectionSeen | +84, resetfalse; settrue by original road/intersection gate |

Defaults come from actual PhysicInfo::Reset `0x319cd5`. Existing real height
reset restores them through RetailHeightState{}. Explicit selected-triangle or
scene initialization preserves the cursor, as original Init does. The existing
GetHeightFrom future binding must copy only the original three +76/+78/+80
fields; it must not silently copy +82/+84 merely because native storage groups
them. Neither ordinary lane7 visit words nor height-query generations are
modified by random-position selection.

The native controller overload checks that its borrowed terrain is the same
context and mutates that actual controller slot. A typed state-leaf overload
supports independent genuine state bindings and isolated validation; a second
shadow PhysicInfo is not required for production use. Decoded mesh/navigation,
node topology and caller data remain read-only during a serial query. The
global heightmap's live translation is reread at coordinate construction sites.

## Exact search behavior

An existing cursor first crosses its stored edge. A fresh cursor initializes
from the selected height triangle and performs the original ordered 3D edge
midpoint comparison. This is not a nearest-triangle reseed. The actual original
LoadArrays cost-construction block `0x31a2be..0x31a3cc` was executed independently:
mode0 gives sidewalk/park1 and all other material entries999; mode1 gives
road/intersection1 and others999. The native table uses those same eight decoded
material identities and validates complete, distinct coverage.

One shared lrand48 result chooses the start edge modulo3. The three candidates
are visited cyclically, retaining the last permitted backtracking edge when
no preferred edge exists. The backtracking check compares triangle index offset
only, before the mode1 wrong-way tests; it deliberately does not compare buffer
identity. Wrong-way byte low bits0/1 and2/3 exclude edge ordinal+1 for vehicles.
Bits4/5 can trigger the original vehicle hop and reciprocal-edge adjustment.
If no reciprocal edge is found, the prior cursor edge remains intact. A vehicle
with no candidate uses original edge0 behavior; it does not take pedestrian
fallback. There is no search-attempt cap.

The road/intersection check sets the persistent marker and never clears it.
High bits6/7 encode lane count. Count0 uses width1/2 and maximum ordinal1;
nonzero count uses width1/(2*count) and that count as maximum. Ordinal0 remains0,
including the original extrapolation. The returned point is the authored edge
interpolation in XYZ plus global heightmap translation, with original float
association. Selected ordinary height triangle, its cache and owned cliff
bodies remain separate.

Mode0 with no permitted edge calls GetRandomDestination on the decoded sidewalk
material, consuming another lrand48 result and using min/max distance0. The
helper selects `(random % indexCount)/3`. Starts0/1 scan all triangles ascending;
starts>=2 scan cyclically but omit the predecessor. It returns the first point
inside inclusive squared-distance bounds, otherwise the strictly better
absolute distance-to-midpoint score, with original finite best-score1e9. This
asymmetry is preserved even when the omitted predecessor is exactly at the
requested point. GetTriCenter uses `(C+(A+B))*(1.f/3.f)+translation`, preserving
single-precision rounding. No generic nearest-triangle substitute is used.

## Random stream and errors

The original external call at PLT `0x1c4664` resolves through `.rel.plt` to libc
`lrand48`, not Irrlicht::Randomizer. `RetailRandomPositionSource::lrand48` is a
mandatory genuine shared-source binding returning its nonnegative31-bit value.
This component does not create a private per-NPC stream, choose a new seed,
replace global consumption or claim that the full application RNG is bound.
Its future world host must supply the shared seed/state and all other original
consumers. Tests supply deterministic recorded import results while executing
the full original search and helper bodies.

Native errors explicitly reject an invalid mode, malformed/unmapped material
tables, absent cached crossing, invalid cursor edge, negative lrand48 result,
empty fallback buffer or a fallback that never initializes its output triangle.
The last two are original undefined inputs, not an invented valid destination.
Original fallback's output point can remain zero while its triangle output is
uninitialized; native throws instead. Cursor/RNG mutations before an error are
not rolled back. A failed binding is not a resumable transaction: restore known
state before further queries. No error suppresses an original required host.

## Validation scope

Tests exercise state initialization/reset/preservation, actual native controller
slot ownership, shared-terrain identity, RNG order/count, candidate order,
backtracking across buffer identities, all wrong-way bit groups, material cost
gates, both vehicle special branches, lane counts/ordinals, live translation,
fallback scan asymmetry, exact-center early return and explicit undefined-input
errors. Original execution compares output float bits, all cursor fields and
exact random consumption, including actual original cost construction.

The supplied height/navigation data are loaded by existing asset readers and
the verified original scene binding; no additional parser is introduced.
Release/Debug gates and exact corpus counts are recorded in the frozen task
report: full game build/link and 21/21 CTests pass in each configuration. Each
configuration matches actual original execution for all 5,888 permitted supplied
pedestrian/vehicle triangle queries plus157 synthetic cases (6,045 total), with
zero point-component bit, cursor-field or random-consumption differences. All
781 nonpadding instruction addresses in GetNextRandomPos execute in that corpus;
this is exercised instruction coverage, not a claim about every possible input
or every external controller's lifecycle. The two native asset dumps are
identical. The global RNG/actual NPC and vehicle controller host integration remains
separate. This leaf does not implement AI arrival, NPC Update, traffic or full
world startup. No gameplay progression is used for validation.
