# Original GetHeightFrom transfer

`RetailHeightController::heightFrom(source,point,target)` implements
PhysicAttributes::GetHeightFrom `0x320041`, followed by the existing detailed
height wrapper. This is a partial transfer between actual slots of the same
terrain/world/controller; it does not clone a PhysicInfo or body collection.

## Source contract

The complete matching source is PhysicAttributes.cpp::GetHeightFrom. The
original instruction body at `0x320040..0x3200f4` confirms its ordered stores:

| Condition or store | Original ARM field/address evidence |
| --- | --- |
| Conditional source node | source+4 tested at0x320052..58 |
| Selected buffer/node/index | +0/+4/+8,0x32005a..72 |
| A/B/C vertices | +12/+24/+36,0x320074..a8 |
| Random edge/index/buffer | +76/+78/+80,0x3200aa..d2 |
| Cached point | +64,0x3200d4..e4 |
| GetHeight(point,target,true) |0x3200e6..ee |

The native conditional tests the actual selectedNode pointer. Its copied
nonnull identity sets the equivalent native hasHeightmap bookkeeping true.
Original ARM offsets are evidence only; native storage uses typed arrays,
references and scene-node pointers, never ARM-layout casts.

It does not copy normal(+48), changed(+60), laneOrdinal(+82),
intersectionSeen(+84), ground flags(+136/+137) or local body ownership. The
target's ordinary GetHeight query remains responsible for its own changed flag,
cache, generation, normal/neighbor/reseed changes, jump composition and cliff
updates. No eager updateEdges or source-body transfer is added. In particular a
target ground transition can destroy old cliff bodies after the copied geometry
changes its eligible strips; stable ground retains those existing bodies.

If the source node is null, all copy stores are skipped and the target still
runs its ordinary detailed query. An absent target may therefore remain a
cache-only query. Same-slot source/target is valid. Bounds use the existing
native checked slot storage; invalid original indices are not turned into a
fallback height.

## Reference alias timing

The point remains the caller's live const reference throughout the transfer
and delegated query. It may refer to a target cache or A/B/C vector overwritten
by the partial copy. Taking a point value snapshot before those stores changes
original behavior. All copy stores precede the query's first point read.

For example source A=(0,0,10), source cache=(25,25,77), and old target
A=(200,0,100), cache=(225,25,188): a reference to target A queries the new
(0,0,10), and a reference to target cache queries the new(25,25,77). The original
plane returns10 and25 respectively. A copied external point with the old
coordinates would produce a different result.

Reference liveness also continues inside the shared height routine. A focused
clockwise-seed/neighbor fixture points the argument at target A. After the
initial transfer, a genuine neighbor transition overwrites A again and the
query returns100. The identical starting external point remains fixed and
returns the old cached77. Both paths execute in the actual original binary.
This preserves the existing core's live aliases rather than hiding them behind
a transfer-local temporary.

## Validation and limits

Synthetic fixtures cover null/global/distinct source node identities,
null/non-null targets, same/distinct slots, live cache/A/B/C aliases, complete
partial-copy field preservation, detailed failure, cache sentinel behavior and
the neighbor-transition alias pair. Tests arrange differing state in actual
nonconst native controller objects through their read-only view; this is
test-only setup and introduces no mutable production accessor. A real Box2D
cliff-body fixture checks retained ownership and actual delegated destruction.

The retail test uses existing asset readers and the verified actual scene
binding. It enumerates all14,576 supplied triangles and queries every12,998
positive-area interior triangle, the same centroid-query domain as existing
height tests. Other winding/degenerate records are counted, not misreported as
successful interior queries. The full record/copy operation is independently
tested with adversarial geometry too.

Original execution compares result component bits, every semantic field in
both slots, selected node identity, shared visitation changes and generation.
Retail visitation export uses exact sparse old/new-word changes carried over
the same shared context, avoiding repeated copies of the entire navigation
array in every log record. Full native game builds/link and26/26 CTests pass in
each configuration. Both Release and Debug match actual original execution for
64 synthetic plus12,998 eligible retail records (13,062 total), with zero
result-bit, slot-state, visitation or generation differences. All90 instruction
addresses in GetHeightFrom execute in that corpus; that is exercised coverage,
not a claim about every possible caller input. Native Release/Debug dumps are
identical. No native runtime depends on emulation.

This method does not create missing actor/AI/controller bindings, alter the
existing ordinary/jump height algorithms or claim full world initialization.
Existing query errors and their partial state effects remain in force. No
mission progression or manual gameplay is used for validation.
