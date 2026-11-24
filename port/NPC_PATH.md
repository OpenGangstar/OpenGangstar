# Original NPC path ownership and waypoint transitions

This component ports Path's owned waypoint storage/access and NPC's path-selection
helpers. It does not implement NPC arrival detection, full NPC::Update, AI routing,
random-position generation, or a live NPC/world object-catalogue adapter. Required
host operations are explicit virtual functions; there is no successful empty host.

## Path loading and ownership

`Structs::Path::Read` at 0x315629 first reads GameObjectBasic, then a signed scalar
count and that many signed integers. `Path::Load` at 0x300bc5 calls
`CGameFactory::getInstance()` through GOT+0x1070 and `LevelObject::Load` at 0x316fec
before inspecting the scalar count. It ignores its incoming factory argument.
The size is signed division by three (0x300bec..0x300bf8), allocation is 12 bytes
per waypoint, and every XYZ integer is converted directly with `__aeabi_i2f`
(0x300c2c..0x300c62). The temporary scalar array is deleted at 0x300c64..0x300c70.
There is no addition of common-object position, quaternion rotation, scale, unit
conversion, spline interpolation, or reversal in this loop or the accessor.

`RetailNpcPathSource` owns decoded scalar storage and a fixed-width source index.
The index is a caller's catalogue reference, not a fabricated original numeric
path ID or native pointer. `RetailNpcPath` takes the source by value, requires
`RetailNpcPathLoadHost::loadLevelObject` first, converts complete triples, and
releases the temporary source on constructor exit. That required host must load
the associated original GameObjectBasic using the game's actual global factory.
The common record/scene implementation is outside this component; coordinates
alone do not substitute for that operation. This API contains no new GMAP parser.

The resulting point vector belongs exclusively to the path owner and cannot be
copied or moved. It is independent of subsequent changes to the caller's source
vector. Original Path's destructor at 0x300b99 likewise owns/deletes its converted
waypoint array. Native RAII cleans construction failures rather than leaking
partial allocations. No ARM struct is overlaid with native pointers.

`getSize` corresponds to 0x300b11 (+112). `getWaypoint` corresponds to 0x300b15:
an index at/past size returns false and preserves the output. A valid index copies
all three float words. The original has no negative-index guard and reads before
its allocation for such an index; native code throws explicitly instead. Negative
source counts, invalid pointers, allocator-size overflow and original out-of-bounds
state are outside the supported source domain. A vector cannot express a negative
count; scalar counts beyond signed 32-bit range are rejected. Extremely large
allocations are subject to native allocation failure; original overflowing 32-bit
allocation behavior is not reproduced. Valid positive counts with one/two trailing
scalars retain the original floor-division behavior, discarding those values.

## NPC path state and dispatch

Original offsets are evidence only; the native state is a separate typed record:

| Original field | Native field | Meaning |
| --- | --- | --- |
| NPC+332 | activePath | Borrowed owner last accepted by getNextPoint |
| NPC+336 | activeWaypoint | Index to fetch next |
| NPC+440 | pathToFollow | Currently selected owner |
| NPC+444 | currentWaypoint | Last index published by getNextPoint |
| NPC+356 | external flags356 reference | Shared live NPC flag word |

The original NPC constructor at 0x2fe3c9 writes activePath=null,
activeWaypoint=0, pathToFollow=null, currentWaypoint=1 and flags356=0. Only the
four path fields are initialized here; callers retain responsibility for the
real live flag word and other NPC initialization.

- `setRetailNpcPath` follows `setPathToFollow` 0x2fd539: current=-1, store selected
  owner, then clear only looping bit 0x00200000. It does not reset the active owner
  or cursor, even when setting the same owner or null. `loopRetailNpcPath` and its
  predicate correspond to 0x2fd511/0x2fd559 and preserve every other flag.
- `nextRetailNpcWaypoint` follows `getNextWaypoint` 0x2fdde9. Start with positive
  zero XYZ. If the active owner exists and cursor<size, postincrement the cursor
  before copying the old index. After copying, looping reduces the incremented
  cursor modulo size. Empty/exhausted paths return zero without wrapping. This
  direct helper does not publish currentWaypoint.
- `nextRetailNpcPoint` follows `getNextPoint` 0x2fdff1. Read selected owner once;
  reset active owner/cursor=0 if it is null or differs by identity. When nonnull,
  publish currentWaypoint before the exhaustion test, return false without
  changing output on exhaustion, otherwise fetch the next waypoint. Equal source
  names/indices or point contents do not imply equal runtime owner identity.
- With no selected path, active owner/cursor reset on every call, while
  currentWaypoint remains unchanged. Original 0x2fe058..0x2fe070 captures the live
  PhysicAttributes pointer before the virtual GetPosition call, then invokes that
  captured provider's GetNextRandomPos(0,0,position). The native host separates
  provider acquisition from position to preserve this ordering and live callback
  effects. It returns the provider's point and true without inventing a route or
  assuming an unbound path means idle.
- `goToNextRetailNpcPoint` follows `goToNextPoint` 0x2fe085. If repeat and cursor>0,
  decrement before getNextPoint, including before a changed-owner reset. Success
  sets 0x00400000 before required NPC goToLocation (0x2fd478); failure clears it
  before required Character goToIdle (0x2f1cfc). There are no state writes after
  either callback, so their live mutations survive. A wrapped looping cursor=0
  is not decremented; repeat then requests the first point, not the previous tail.

Path owners must outlive selected and active references, including the interval
after selection changes but before the next request resets activePath. The state
and shared flag word are passed by reference, and host callbacks may change live
state in the same places as the original. Serial access is required. This is not
an NPC scheduler and does not decide when arrival has occurred.

## Validation and evidence limits

Original binary SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Matched original NPC.cpp/Path.cpp helper bodies and literal ARM instructions were
used together; tests do not treat the existing cinematic path approximation as
an oracle.

The native test enumerates 5,000 combinations of direct fetch, next point, dispatch,
set-path and loop operations; distinct selected/active identities including equal
catalogue indices; empty/one/three-point paths; boundary cursors; loop/repeat; and
host mutation. An independent ignored Unicorn driver executes the actual original
ARM helpers and compares output float bits, both owner identities, both indices,
the full flag word and ordered callback state snapshots. It verifies captured
random provider identity even when GetPosition changes the live provider/selection,
and verifies callback mutations survive location/idle dispatch.

Independent raw supplied GMAP fixture enumeration found 84 type-14 paths across
10 gameplay files, totaling 1,364 waypoints. All 84 have nonzero common placement;
none are empty or have trailing scalar remainders. The ignored enumeration script
is reference tooling, not a second shipped reader. All records plus eight synthetic
conversion/empty/tail cases pass native loading and execution of original
Path::Load/getSize/getWaypoint: 92 fixtures, 1,371 points compared bit-for-bit.
Common placement is deliberately nonidentity in the original load probe, proving
that these point copies do not apply that placement again. Original factory lookup,
common-load-before-allocation, allocation and source deletion ordering is checked.

The transition probe intercepts only external position/random/location/idle
operations and signed division/remainder imports. It executes actual path access
and helper branching. The load probe intercepts common LevelObject loading/global
factory lookup, allocation/deallocation and AEABI conversion/division; it does not
execute the full common-object scene loader, allocator implementation or NPC AI.
Those mandatory host operations remain real production dependencies. Probes live
under ignored port/build; ARM/Python execution is not a native runtime dependency.

Release and Debug compilation, the native focused test, original-backed comparisons
and nearby regression gates are recorded in the submission report. No manual
gameplay, mission progression or original ARM source modification is involved.
